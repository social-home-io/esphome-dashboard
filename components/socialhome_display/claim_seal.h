// SPDX-License-Identifier: MPL-2.0
//
// The sealed Connect-from-Home-Assistant claim (panel side).
//
// Home Assistant's recorder keeps every action's data, so Social Home seals
// the claim and the dashboard address to this panel's own X25519 key:
//
//   sealed = {"ct", "epk", "nonce", "suite"}   (compact JSON, bytes as
//                                               unpadded base64url)
//   shared = X25519(panel_private, epk)        (all zeros → rejected)
//   key    = HKDF-SHA256(shared, salt = epk || panel_pub,
//                        info = "socialhome-panel-claim-v1", L = 32)
//   plain  = AES-256-GCM-open(key, nonce, ct || tag,
//                             aad = suite || panel_pub)
//          = {"claim": "…", "url": "…"}
//
// Mirrors socialhome/dashboard/claim_seal.py; the shared vectors in
// docs/display-api/v1/fixtures/sealed-claim-vectors.json pin both sides.
// Platform-free (mbedTLS only) so the host tests compile it as is: Mbed TLS
// 3.6 from ESP-IDF on the panel, the system libmbedcrypto on the host build.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome::socialhome_display {

/// X25519 ECDH → HKDF-SHA256 → AES-256-GCM. A future post-quantum suite is
/// a sibling constant in SUPPORTED_PANEL_CLAIM_SUITES with the same shape.
inline constexpr const char *PANEL_CLAIM_SUITE_X25519_AESGCM = "x25519-hkdf-sha256-aes256gcm";
/// Every suite this build opens. No default: anything else is refused.
inline constexpr const char *SUPPORTED_PANEL_CLAIM_SUITES[] = {PANEL_CLAIM_SUITE_X25519_AESGCM};
/// HKDF info: domain-separates this key from any other X25519 use.
inline constexpr const char *PANEL_CLAIM_HKDF_INFO = "socialhome-panel-claim-v1";

inline constexpr size_t PANEL_KEY_BYTES = 32;
inline constexpr size_t PANEL_KEY_TEXT_LEN = 43;  ///< 32 bytes as unpadded base64url
inline constexpr size_t CLAIM_NONCE_BYTES = 12;
inline constexpr size_t CLAIM_TAG_BYTES = 16;
inline constexpr size_t CLAIM_MAX_CT_BYTES = 2048;

using PanelKey = std::array<uint8_t, PANEL_KEY_BYTES>;

/// The panel's keypair. The private half never leaves the panel (NVS only).
struct PanelKeyPair {
  PanelKey private_key{};
  PanelKey public_key{};
  ~PanelKeyPair();
  /// Zero both halves.
  void wipe();
};

/// What opening a sealed claim gave. Only UNSUPPORTED_SUITE is told apart
/// (it is checked before any key work); every other failure is INVALID,
/// with no detail that could act as an oracle.
enum class SealResult : uint8_t { OK, INVALID, UNSUPPORTED_SUITE };

/// The opened claim. Both strings are wiped when it goes away.
struct OpenedClaim {
  std::string claim;
  std::string url;
  ~OpenedClaim();
};

/// Overwrite `len` bytes at `data` (mbedtls_platform_zeroize: not optimised away).
void wipe_bytes(void *data, size_t len);
/// Wipe a string's bytes, then empty it.
void wipe_string(std::string &value);

/// Unpadded base64url.
std::string b64url_encode(const uint8_t *data, size_t len);
/// Strict unpadded base64url: only [A-Za-z0-9_-], no '=', no length ≡ 1
/// (mod 4) and no stray bits in the last character (the canonical form,
/// the one b64url_encode gives back). False on anything else.
bool b64url_decode(const std::string &text, std::vector<uint8_t> &out);

/// A "Social Home key" sensor state: exactly 43 characters of canonical
/// unpadded base64url → the 32 raw key bytes.
bool parse_panel_key(const std::string &text, PanelKey &out);
/// The 43-character "Social Home key" text of `key`.
std::string panel_key_text(const PanelKey &key);

/// X25519 public key of `private_key` (clamped as RFC 7748 says).
bool x25519_public_key(const PanelKey &private_key, PanelKey &public_key);
/// X25519(private_key, peer). False on failure or an all-zero result (a
/// small-order peer key). `shared` is zeroed on failure.
bool x25519_shared(const PanelKey &private_key, const PanelKey &peer, PanelKey &shared);
/// A fresh keypair from the platform's entropy source (the hardware RNG on
/// the panel, the OS on the host) through CTR-DRBG.
bool generate_panel_key(PanelKeyPair &out);

/// HKDF-SHA256(shared, salt = epk || panel_pub, info = PANEL_CLAIM_HKDF_INFO) → 32 bytes.
bool claim_key(const PanelKey &shared, const PanelKey &epk, const PanelKey &panel_pub, PanelKey &key);
/// The AEAD's associated data: the suite id (ASCII) then the panel key.
std::string claim_aad(const char *suite, const PanelKey &panel_pub);

/// Open `sealed` with the panel's keypair. `out` is filled on OK only.
SealResult open_sealed_claim(const std::string &sealed, const PanelKeyPair &panel, OpenedClaim &out);

}  // namespace esphome::socialhome_display
