// SPDX-License-Identifier: MPL-2.0
// The sealed Connect-from-Home-Assistant claim against the shared vectors
// Social Home pins (fixtures/sealed-claim-vectors.json).
#include <cstring>

#define ARDUINOJSON_ENABLE_STD_STRING 1  // NOLINT
#define ARDUINOJSON_USE_LONG_LONG 1      // NOLINT
#include <ArduinoJson.h>

#include "check.h"
#include "claim_seal.h"

using namespace esphome::socialhome_display;

namespace {

std::string hex(const uint8_t *data, size_t len) {
  static const char DIGITS[] = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < len; i++) {
    out += DIGITS[data[i] >> 4];
    out += DIGITS[data[i] & 15];
  }
  return out;
}

std::string hex(const PanelKey &key) { return hex(key.data(), key.size()); }

PanelKey key_of(const std::string &text) {
  PanelKey key{};
  CHECK(parse_panel_key(text, key));
  return key;
}

ArduinoJson::JsonDocument vectors() {
  ArduinoJson::JsonDocument doc;
  std::string text = check::fixture("sealed-claim-vectors.json");
  CHECK(!ArduinoJson::deserializeJson(doc, text));
  return doc;
}

PanelKeyPair panel_of(const ArduinoJson::JsonDocument &doc) {
  PanelKeyPair panel;
  panel.private_key = key_of(doc["panel_private"].as<std::string>());
  panel.public_key = key_of(doc["panel_public"].as<std::string>());
  return panel;
}

}  // namespace

TEST(claim_seal_suite_constants) {
  ArduinoJson::JsonDocument doc = vectors();
  CHECK_EQ(doc["suite"].as<std::string>(), std::string(PANEL_CLAIM_SUITE_X25519_AESGCM));
  CHECK_EQ(doc["hkdf_info"].as<std::string>(), std::string(PANEL_CLAIM_HKDF_INFO));
}

TEST(claim_seal_panel_public_key) {
  ArduinoJson::JsonDocument doc = vectors();
  PanelKey pub{};
  CHECK(x25519_public_key(key_of(doc["panel_private"].as<std::string>()), pub));
  CHECK_EQ(panel_key_text(pub), doc["panel_public"].as<std::string>());
}

TEST(claim_seal_valid_vectors) {
  ArduinoJson::JsonDocument doc = vectors();
  PanelKeyPair panel = panel_of(doc);
  int seen = 0;
  for (ArduinoJson::JsonObjectConst v : doc["valid"].as<ArduinoJson::JsonArrayConst>()) {
    seen++;
    std::string name = v["name"].as<std::string>();
    PanelKey eph_private = key_of(v["eph_private"].as<std::string>());
    PanelKey eph_public = key_of(v["eph_public"].as<std::string>());
    PanelKey derived{};
    CHECK(x25519_public_key(eph_private, derived));
    CHECK_EQ(hex(derived), hex(eph_public));

    // The panel's side and the sender's side agree on the secret.
    PanelKey shared{}, shared_sender{};
    CHECK(x25519_shared(panel.private_key, eph_public, shared));
    CHECK(x25519_shared(eph_private, panel.public_key, shared_sender));
    CHECK_EQ(hex(shared), v["shared_secret_hex"].as<std::string>());
    CHECK_EQ(hex(shared_sender), v["shared_secret_hex"].as<std::string>());

    PanelKey key{};
    CHECK(claim_key(shared, eph_public, panel.public_key, key));
    CHECK_EQ(hex(key), v["aead_key_hex"].as<std::string>());

    std::string aad = claim_aad(PANEL_CLAIM_SUITE_X25519_AESGCM, panel.public_key);
    CHECK_EQ(hex(reinterpret_cast<const uint8_t *>(aad.data()), aad.size()), v["aad_hex"].as<std::string>());

    OpenedClaim opened;
    SealResult result = open_sealed_claim(v["sealed"].as<std::string>(), panel, opened);
    if (result != SealResult::OK)
      check::fail(__FILE__, __LINE__, "valid vector did not open: " + name);
    CHECK_EQ(opened.claim, v["claim"].as<std::string>());
    CHECK_EQ(opened.url, v["url"].as<std::string>());
  }
  CHECK(seen > 0);
}

TEST(claim_seal_invalid_vectors) {
  ArduinoJson::JsonDocument doc = vectors();
  PanelKeyPair panel = panel_of(doc);
  int seen = 0;
  for (ArduinoJson::JsonObjectConst v : doc["invalid"].as<ArduinoJson::JsonArrayConst>()) {
    seen++;
    std::string name = v["name"].as<std::string>();
    SealResult want = v["unsupported_suite"].as<bool>() ? SealResult::UNSUPPORTED_SUITE : SealResult::INVALID;
    OpenedClaim opened;
    SealResult got = open_sealed_claim(v["sealed"].as<std::string>(), panel, opened);
    if (got != want)
      check::fail(__FILE__, __LINE__,
                  "invalid vector '" + name + "': got " + std::to_string(int(got)) + ", want " +
                      std::to_string(int(want)));
    CHECK(opened.claim.empty());
    CHECK(opened.url.empty());
  }
  CHECK(seen > 0);
}

TEST(claim_seal_wrong_panel_key) {
  // A valid sealed claim, opened by a panel that has since renewed its key
  // (Forget server): refused like any other failure.
  ArduinoJson::JsonDocument doc = vectors();
  PanelKeyPair other;
  CHECK(generate_panel_key(other));
  OpenedClaim opened;
  CHECK(open_sealed_claim(doc["valid"][0]["sealed"].as<std::string>(), other, opened) == SealResult::INVALID);
  CHECK(opened.claim.empty());
}

TEST(claim_seal_panel_keys) {
  ArduinoJson::JsonDocument doc = vectors();
  ArduinoJson::JsonArrayConst valid = doc["panel_keys"]["valid"];
  ArduinoJson::JsonArrayConst invalid = doc["panel_keys"]["invalid"];
  for (ArduinoJson::JsonVariantConst v : valid) {
    PanelKey key{};
    std::string text = v.as<std::string>();
    CHECK(parse_panel_key(text, key));
    CHECK_EQ(panel_key_text(key), text);
  }
  for (ArduinoJson::JsonVariantConst v : invalid) {
    PanelKey key{};
    std::string text = v.as<std::string>();
    if (parse_panel_key(text, key))
      check::fail(__FILE__, __LINE__, "accepted invalid panel key '" + text + "'");
  }
}

TEST(claim_seal_keygen_round_trip) {
  PanelKeyPair a, b;
  CHECK(generate_panel_key(a));
  CHECK(generate_panel_key(b));
  CHECK(hex(a.private_key) != hex(b.private_key));
  // The sensor text is exactly 43 characters and parses back.
  std::string text = panel_key_text(a.public_key);
  CHECK_EQ(text.size(), PANEL_KEY_TEXT_LEN);
  PanelKey parsed{};
  CHECK(parse_panel_key(text, parsed));
  CHECK_EQ(hex(parsed), hex(a.public_key));
  PanelKey pub{};
  CHECK(x25519_public_key(a.private_key, pub));
  CHECK_EQ(hex(pub), hex(a.public_key));
  // Both sides of an exchange between two fresh keys agree.
  PanelKey ab{}, ba{};
  CHECK(x25519_shared(a.private_key, b.public_key, ab));
  CHECK(x25519_shared(b.private_key, a.public_key, ba));
  CHECK_EQ(hex(ab), hex(ba));
  a.wipe();
  CHECK_EQ(hex(a.private_key), std::string(64, '0'));
}

TEST(claim_seal_small_order_peer) {
  PanelKeyPair a;
  CHECK(generate_panel_key(a));
  PanelKey zero{}, one{}, shared{};
  one[0] = 1;
  CHECK(!x25519_shared(a.private_key, zero, shared));
  CHECK(!x25519_shared(a.private_key, one, shared));
  CHECK_EQ(hex(shared), std::string(64, '0'));
}

TEST(b64url_round_trip) {
  for (size_t len = 0; len < 70; len++) {
    std::vector<uint8_t> data(len);
    for (size_t i = 0; i < len; i++)
      data[i] = static_cast<uint8_t>(i * 37 + 11);
    std::string text = b64url_encode(data.data(), data.size());
    CHECK(text.find('=') == std::string::npos);
    std::vector<uint8_t> back;
    CHECK(b64url_decode(text, back));
    CHECK(back == data);
  }
  CHECK_EQ(b64url_encode(reinterpret_cast<const uint8_t *>("\xfb\xff"), 2), std::string("-_8"));
}

TEST(b64url_canonical_only) {
  std::vector<uint8_t> out;
  CHECK(b64url_decode("", out));
  CHECK(out.empty());
  CHECK(b64url_decode("-_8", out));
  CHECK(!b64url_decode("-_9", out));   // stray low bits
  CHECK(b64url_decode("QQ", out));
  CHECK(!b64url_decode("QR", out));    // stray low bits
  CHECK(!b64url_decode("QQ==", out));  // padding
  CHECK(!b64url_decode("Q", out));     // length 1 mod 4
  CHECK(!b64url_decode("+/8", out));   // standard alphabet
  CHECK(!b64url_decode("QQ Q", out));  // whitespace
  CHECK(!b64url_decode(std::string("QQ\0Q", 4), out));
  CHECK(out.empty());
}
