// SPDX-License-Identifier: MPL-2.0
//
// The last household theme, kept across reboots: after a power cut, a Wi-Fi
// change or "Forget server" the setup and error screens still speak the
// household's language (and wear its colours) before any server answers.
//
// What is kept: the locale, text direction, colour mode, the three palettes
// and the strings the panel draws itself (panel_strings.h) — not the board
// strings the server sends with every board anyway. The blob is split into
// chunks under the 255 bytes one preference may hold on the host backend
// (ESP32 NVS blobs are fine with that size too), plus a small head with the
// length and a checksum, so a half-written cache is never used.
//
// No ESPHome / LVGL includes: covered by the host tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "model.h"

namespace esphome::socialhome_display {

/// Format of the blob; a panel ignores a cache written in another format.
static constexpr uint8_t THEME_CACHE_VERSION = 1;
/// Bytes per stored chunk (one preference each).
static constexpr size_t THEME_CHUNK_BYTES = 240;
/// Chunks at most: 5760 bytes, room for the panel strings in every
/// dashboard language. Strings that don't fit are left out (built-in
/// English for those).
static constexpr size_t THEME_CACHE_CHUNKS = 24;
static constexpr size_t THEME_CACHE_MAX = THEME_CHUNK_BYTES * THEME_CACHE_CHUNKS;

/// The head preference.
struct ThemeCacheHead {
  uint32_t checksum{0};  ///< FNV-1a of the blob
  uint16_t length{0};    ///< blob bytes
  uint8_t version{0};    ///< THEME_CACHE_VERSION
  uint8_t chunks{0};     ///< chunks written
};

/// One chunk preference.
struct ThemeCacheChunk {
  uint8_t data[THEME_CHUNK_BYTES];
};

/// The theme as a compact blob (at most THEME_CACHE_MAX bytes).
std::string encode_theme_cache(const Theme &theme);
/// A blob back into a theme; false (and `out` untouched) when it isn't one.
bool decode_theme_cache(const std::string &blob, Theme &out);

uint32_t theme_cache_checksum(const std::string &blob);
/// The head describing `blob`.
ThemeCacheHead theme_cache_head(const std::string &blob);
/// True when `head` is this format and fits the chunks it claims.
bool theme_cache_head_ok(const ThemeCacheHead &head);
/// `blob` split into chunks of THEME_CHUNK_BYTES (the last one zero-padded).
std::vector<ThemeCacheChunk> theme_cache_chunks(const std::string &blob);
/// Chunks read back into the blob `head` describes; empty when they don't
/// add up to it (wrong count, short, checksum mismatch).
std::string theme_cache_join(const ThemeCacheHead &head, const std::vector<ThemeCacheChunk> &chunks);

}  // namespace esphome::socialhome_display
