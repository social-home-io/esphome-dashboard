// SPDX-License-Identifier: MPL-2.0
#include "claim_seal.h"

#include <cstdlib>
#include <cstring>

// Same configuration as ESPHome's json component (json_util.h), so every
// translation unit sees one ArduinoJson.
#define ARDUINOJSON_ENABLE_STD_STRING 1  // NOLINT
#define ARDUINOJSON_USE_LONG_LONG 1      // NOLINT
#include <ArduinoJson.h>

#include <mbedtls/bignum.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>

namespace esphome::socialhome_display {

namespace {

// Mbed TLS 3.6 on the panel (ESP-IDF) and in the host tests. The host SDL
// build links the distro's libmbedcrypto (2.28 on Ubuntu CI runners), which
// only that build exercises: the code sticks to functions present in both
// lines and touches no struct fields. Small-order peer keys are refused by
// the all-zero shared-secret check whatever the library does about them.

constexpr const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int b64_value(char c) {
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '-')
    return 62;
  if (c == '_')
    return 63;
  return -1;
}

/// A CTR-DRBG seeded from the platform entropy source, for key generation
/// and the scalar multiplication's blinding.
class Drbg {
 public:
  Drbg() {
    mbedtls_entropy_init(&this->entropy_);
    mbedtls_ctr_drbg_init(&this->drbg_);
    static const unsigned char PERS[] = "socialhome-panel-key";
    this->ok_ = mbedtls_ctr_drbg_seed(&this->drbg_, mbedtls_entropy_func, &this->entropy_, PERS, sizeof(PERS) - 1) == 0;
  }
  ~Drbg() {
    mbedtls_ctr_drbg_free(&this->drbg_);
    mbedtls_entropy_free(&this->entropy_);
  }
  Drbg(const Drbg &) = delete;
  Drbg &operator=(const Drbg &) = delete;
  bool ok() const { return this->ok_; }
  void *ctx() { return &this->drbg_; }
  static int random(void *ctx, unsigned char *out, size_t len) {
    return mbedtls_ctr_drbg_random(ctx, out, len);
  }

 private:
  mbedtls_entropy_context entropy_;
  mbedtls_ctr_drbg_context drbg_;
  bool ok_{false};
};

/// X25519(scalar, u) with mbedTLS's Curve25519 (little-endian raw bytes).
bool x25519(const PanelKey &scalar, const PanelKey &u, PanelKey &out) {
  Drbg drbg;
  if (!drbg.ok())
    return false;
  PanelKey k = scalar;
  // RFC 7748 §5 clamping (mbedTLS insists on a clamped private scalar).
  k[0] &= 248;
  k[31] &= 127;
  k[31] |= 64;
  mbedtls_ecp_group grp;
  mbedtls_mpi d;
  mbedtls_ecp_point peer, result;
  mbedtls_ecp_group_init(&grp);
  mbedtls_mpi_init(&d);
  mbedtls_ecp_point_init(&peer);
  mbedtls_ecp_point_init(&result);
  size_t written = 0;
  // Reading the point drops the top bit (RFC 7748); mbedtls_ecp_mul also
  // refuses the known low-order points.
  bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) == 0 &&
            mbedtls_mpi_read_binary_le(&d, k.data(), k.size()) == 0 &&
            mbedtls_ecp_point_read_binary(&grp, &peer, u.data(), u.size()) == 0 &&
            mbedtls_ecp_mul(&grp, &result, &d, &peer, Drbg::random, drbg.ctx()) == 0 &&
            mbedtls_ecp_point_write_binary(&grp, &result, MBEDTLS_ECP_PF_UNCOMPRESSED, &written, out.data(),
                                           out.size()) == 0 &&
            written == out.size();
  wipe_bytes(k.data(), k.size());
  mbedtls_ecp_point_free(&result);
  mbedtls_ecp_point_free(&peer);
  mbedtls_mpi_free(&d);  // zeroizes the limbs
  mbedtls_ecp_group_free(&grp);
  if (!ok)
    wipe_bytes(out.data(), out.size());
  return ok;
}

bool hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t len, uint8_t out[32]) {
  const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return md != nullptr && mbedtls_md_hmac(md, key, key_len, data, len, out) == 0;
}

/// An ArduinoJson allocator that wipes every block it hands back: the
/// opened plaintext's strings (the claim) never linger in freed heap.
class WipingAllocator : public ArduinoJson::Allocator {
 public:
  void *allocate(size_t size) override {
    auto *block = static_cast<unsigned char *>(malloc(HEADER + size));
    if (block == nullptr)
      return nullptr;
    memcpy(block, &size, sizeof(size));
    return block + HEADER;
  }
  void deallocate(void *ptr) override {
    if (ptr == nullptr)
      return;
    unsigned char *block = static_cast<unsigned char *>(ptr) - HEADER;
    size_t size;
    memcpy(&size, block, sizeof(size));
    wipe_bytes(block, HEADER + size);
    free(block);
  }
  void *reallocate(void *ptr, size_t size) override {
    // Never realloc in place: the old block is wiped before it is freed.
    void *fresh = this->allocate(size);
    if (fresh == nullptr || ptr == nullptr)
      return fresh;
    size_t old;
    memcpy(&old, static_cast<unsigned char *>(ptr) - HEADER, sizeof(old));
    memcpy(fresh, ptr, old < size ? old : size);
    this->deallocate(ptr);
    return fresh;
  }
  static WipingAllocator *instance() {
    static WipingAllocator alloc;
    return &alloc;
  }

 private:
  static constexpr size_t HEADER = alignof(std::max_align_t) > sizeof(size_t) ? alignof(std::max_align_t)
                                                                               : sizeof(size_t);
};

/// One base64url field of the sealed object with exactly `want` bytes
/// (0: any length).
bool field(ArduinoJson::JsonObjectConst obj, const char *name, size_t want, std::vector<uint8_t> &out) {
  ArduinoJson::JsonVariantConst v = obj[name];
  if (!v.is<const char *>())
    return false;
  ArduinoJson::JsonString text = v.as<ArduinoJson::JsonString>();
  // Length before decoding: nothing longer than the largest allowed field.
  if (text.size() > (want != 0 ? (want * 4 + 2) / 3 : CLAIM_MAX_CT_TEXT_LEN))
    return false;
  if (!b64url_decode(std::string(text.c_str(), text.size()), out))
    return false;
  return want == 0 || out.size() == want;
}

bool supported_suite(ArduinoJson::JsonVariantConst v) {
  if (!v.is<const char *>())
    return false;
  ArduinoJson::JsonString suite = v.as<ArduinoJson::JsonString>();
  for (const char *known : SUPPORTED_PANEL_CLAIM_SUITES) {
    if (suite.size() == strlen(known) && memcmp(suite.c_str(), known, suite.size()) == 0)
      return true;
  }
  return false;
}

}  // namespace

PanelKeyPair::~PanelKeyPair() { this->wipe(); }

void PanelKeyPair::wipe() {
  wipe_bytes(this->private_key.data(), this->private_key.size());
  wipe_bytes(this->public_key.data(), this->public_key.size());
}

OpenedClaim::~OpenedClaim() {
  wipe_string(this->claim);
  wipe_string(this->url);
}

void wipe_bytes(void *data, size_t len) {
  if (data != nullptr && len > 0)
    mbedtls_platform_zeroize(data, len);
}

void wipe_string(std::string &value) {
  if (!value.empty())
    wipe_bytes(&value[0], value.size());
  value.clear();
}

std::string b64url_encode(const uint8_t *data, size_t len) {
  std::string out;
  out.reserve((len * 4 + 2) / 3);
  size_t i = 0;
  for (; i + 3 <= len; i += 3) {
    uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
    out += ALPHABET[(n >> 18) & 63];
    out += ALPHABET[(n >> 12) & 63];
    out += ALPHABET[(n >> 6) & 63];
    out += ALPHABET[n & 63];
  }
  if (len - i == 1) {
    uint32_t n = uint32_t(data[i]) << 16;
    out += ALPHABET[(n >> 18) & 63];
    out += ALPHABET[(n >> 12) & 63];
  } else if (len - i == 2) {
    uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
    out += ALPHABET[(n >> 18) & 63];
    out += ALPHABET[(n >> 12) & 63];
    out += ALPHABET[(n >> 6) & 63];
  }
  return out;
}

bool b64url_decode(const std::string &text, std::vector<uint8_t> &out) {
  out.clear();
  if (text.size() % 4 == 1)
    return false;
  out.reserve(text.size() * 3 / 4);
  uint32_t acc = 0;
  int bits = 0;
  for (char c : text) {
    int v = b64_value(c);
    if (v < 0) {
      out.clear();
      return false;
    }
    acc = (acc << 6) | uint32_t(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>(acc >> bits));
      acc &= (1u << bits) - 1;
    }
  }
  // Canonical: the leftover low bits of the last character must be zero.
  if (acc != 0) {
    out.clear();
    return false;
  }
  return true;
}

bool parse_panel_key(const std::string &text, PanelKey &out) {
  std::vector<uint8_t> raw;
  if (text.size() != PANEL_KEY_TEXT_LEN || !b64url_decode(text, raw) || raw.size() != PANEL_KEY_BYTES)
    return false;
  memcpy(out.data(), raw.data(), PANEL_KEY_BYTES);
  return true;
}

std::string panel_key_text(const PanelKey &key) { return b64url_encode(key.data(), key.size()); }

bool x25519_public_key(const PanelKey &private_key, PanelKey &public_key) {
  PanelKey base{};
  base[0] = 9;  // the Curve25519 base point, u = 9
  return x25519(private_key, base, public_key);
}

bool x25519_shared(const PanelKey &private_key, const PanelKey &peer, PanelKey &shared) {
  if (!x25519(private_key, peer, shared))
    return false;
  // A small-order peer gives all zeros; checked without an early exit.
  uint8_t any = 0;
  for (uint8_t b : shared)
    any |= b;
  if (any == 0) {
    wipe_bytes(shared.data(), shared.size());
    return false;
  }
  return true;
}

bool generate_panel_key(PanelKeyPair &out) {
  Drbg drbg;
  if (!drbg.ok() || Drbg::random(drbg.ctx(), out.private_key.data(), out.private_key.size()) != 0) {
    out.wipe();
    return false;
  }
  out.private_key[0] &= 248;
  out.private_key[31] &= 127;
  out.private_key[31] |= 64;
  if (!x25519_public_key(out.private_key, out.public_key)) {
    out.wipe();
    return false;
  }
  return true;
}

bool claim_key(const PanelKey &shared, const PanelKey &epk, const PanelKey &panel_pub, PanelKey &key) {
  // HKDF (RFC 5869) by hand over HMAC-SHA256: ESP-IDF builds Mbed TLS
  // without MBEDTLS_HKDF_C. One expand block covers L = 32.
  uint8_t salt[2 * PANEL_KEY_BYTES];
  memcpy(salt, epk.data(), PANEL_KEY_BYTES);
  memcpy(salt + PANEL_KEY_BYTES, panel_pub.data(), PANEL_KEY_BYTES);
  uint8_t prk[32];
  const size_t info_len = strlen(PANEL_CLAIM_HKDF_INFO);
  uint8_t info[64];
  memcpy(info, PANEL_CLAIM_HKDF_INFO, info_len);
  info[info_len] = 0x01;
  bool ok = hmac_sha256(salt, sizeof(salt), shared.data(), shared.size(), prk) &&
            hmac_sha256(prk, sizeof(prk), info, info_len + 1, key.data());
  wipe_bytes(prk, sizeof(prk));
  if (!ok)
    wipe_bytes(key.data(), key.size());
  return ok;
}

std::string claim_aad(const char *suite, const PanelKey &panel_pub) {
  std::string aad(suite);
  aad.append(reinterpret_cast<const char *>(panel_pub.data()), panel_pub.size());
  return aad;
}

SealResult open_sealed_claim(const std::string &sealed, const PanelKeyPair &panel, OpenedClaim &out) {
  if (sealed.size() > SEALED_MAX_LEN)
    return SealResult::INVALID;
  ArduinoJson::JsonDocument doc;
  if (ArduinoJson::deserializeJson(doc, sealed.data(), sealed.size()) || !doc.is<ArduinoJson::JsonObjectConst>())
    return SealResult::INVALID;
  ArduinoJson::JsonObjectConst obj = doc.as<ArduinoJson::JsonObjectConst>();
  // The suite first, before any key work; no default.
  if (!supported_suite(obj["suite"]))
    return SealResult::UNSUPPORTED_SUITE;
  std::vector<uint8_t> epk_raw, nonce, ct;
  if (!field(obj, "epk", PANEL_KEY_BYTES, epk_raw) || !field(obj, "nonce", CLAIM_NONCE_BYTES, nonce) ||
      !field(obj, "ct", 0, ct) || ct.size() <= CLAIM_TAG_BYTES || ct.size() > CLAIM_MAX_CT_BYTES)
    return SealResult::INVALID;
  PanelKey epk;
  memcpy(epk.data(), epk_raw.data(), PANEL_KEY_BYTES);

  PanelKey shared{}, key{};
  bool ok = x25519_shared(panel.private_key, epk, shared) && claim_key(shared, epk, panel.public_key, key);
  wipe_bytes(shared.data(), shared.size());

  const size_t plain_len = ct.size() - CLAIM_TAG_BYTES;
  std::vector<uint8_t> plain(plain_len);
  if (ok) {
    const std::string aad = claim_aad(PANEL_CLAIM_SUITE_X25519_AESGCM, panel.public_key);
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    // auth_decrypt checks the tag in constant time and wipes the output
    // when it doesn't match.
    ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), 256) == 0 &&
         mbedtls_gcm_auth_decrypt(&gcm, plain_len, nonce.data(), nonce.size(),
                                  reinterpret_cast<const unsigned char *>(aad.data()), aad.size(),
                                  ct.data() + plain_len, CLAIM_TAG_BYTES, ct.data(), plain.data()) == 0;
    mbedtls_gcm_free(&gcm);
  }
  wipe_bytes(key.data(), key.size());

  bool parsed = false;
  if (ok) {
    ArduinoJson::JsonDocument body(WipingAllocator::instance());
    if (!ArduinoJson::deserializeJson(body, reinterpret_cast<const char *>(plain.data()), plain.size()) &&
        body.is<ArduinoJson::JsonObjectConst>()) {
      ArduinoJson::JsonVariantConst claim = body["claim"];
      ArduinoJson::JsonVariantConst url = body["url"];
      if (claim.is<const char *>() && url.is<const char *>()) {
        ArduinoJson::JsonString c = claim.as<ArduinoJson::JsonString>();
        ArduinoJson::JsonString u = url.as<ArduinoJson::JsonString>();
        wipe_string(out.claim);
        wipe_string(out.url);
        out.claim.assign(c.c_str(), c.size());
        out.url.assign(u.c_str(), u.size());
        parsed = true;
      }
    }
    body.clear();
  }
  wipe_bytes(plain.data(), plain.size());
  return parsed ? SealResult::OK : SealResult::INVALID;
}

}  // namespace esphome::socialhome_display
