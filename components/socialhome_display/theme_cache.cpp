// SPDX-License-Identifier: MPL-2.0
#include "theme_cache.h"

#include <algorithm>
#include <cstring>

#include "panel_strings.h"

namespace esphome::socialhome_display {

// Blob, version 1 (all lengths little-endian):
//   u8 flags (bit 0: rtl)
//   u8 len, mode
//   u8 len, locale
//   3 × 10 × u24 RGB: light, dark, night palettes in Palette field order
//   repeated: u8 len, key; u16 len, value

namespace {
constexpr size_t SHORT_MAX = 255;
constexpr size_t PALETTE_BYTES = 10 * 3;

void put_u8(std::string &out, size_t v) { out.push_back(static_cast<char>(v & 0xFF)); }

void put_short(std::string &out, const std::string &text) {
  const size_t n = std::min(text.size(), SHORT_MAX);
  put_u8(out, n);
  out.append(text, 0, n);
}

void put_palette(std::string &out, const Palette &p) {
  for (uint32_t c : {p.bg, p.surface, p.text, p.muted, p.accent, p.accent_text, p.border, p.error_bg, p.error_text,
                     p.focus}) {
    put_u8(out, c >> 16);
    put_u8(out, c >> 8);
    put_u8(out, c);
  }
}

struct BlobReader {
  const std::string &blob;
  size_t at{0};

  bool u8(size_t &v) {
    if (at + 1 > blob.size())
      return false;
    v = static_cast<uint8_t>(blob[at++]);
    return true;
  }
  bool u16(size_t &v) {
    size_t lo, hi;
    if (!u8(lo) || !u8(hi))
      return false;
    v = lo | (hi << 8);
    return true;
  }
  bool bytes(size_t n, std::string &out) {
    if (at + n > blob.size())
      return false;
    out.assign(blob, at, n);
    at += n;
    return true;
  }
  bool color(uint32_t &c) {
    size_t r, g, b;
    if (!u8(r) || !u8(g) || !u8(b))
      return false;
    c = static_cast<uint32_t>((r << 16) | (g << 8) | b);
    return true;
  }
  bool palette(Palette &p) {
    return color(p.bg) && color(p.surface) && color(p.text) && color(p.muted) && color(p.accent) &&
           color(p.accent_text) && color(p.border) && color(p.error_bg) && color(p.error_text) && color(p.focus);
  }
};
}  // namespace

std::string encode_theme_cache(const Theme &theme) {
  std::string out;
  out.reserve(1024);
  put_u8(out, theme.rtl ? 1 : 0);
  put_short(out, theme.mode);
  put_short(out, theme.locale);
  put_palette(out, theme.light);
  put_palette(out, theme.dark);
  put_palette(out, theme.night);
  for (const auto &pair : theme.strings) {
    if (!is_panel_string(pair.key) || pair.key.size() > SHORT_MAX || pair.value.size() > 0xFFFF)
      continue;
    // A string that doesn't fit any more is left out: built-in English.
    if (out.size() + 1 + pair.key.size() + 2 + pair.value.size() > THEME_CACHE_MAX)
      continue;
    put_short(out, pair.key);
    put_u8(out, pair.value.size());
    put_u8(out, pair.value.size() >> 8);
    out += pair.value;
  }
  return out;
}

bool decode_theme_cache(const std::string &blob, Theme &out) {
  BlobReader r{blob};
  Theme t;
  size_t flags, n;
  if (!r.u8(flags) || !r.u8(n) || !r.bytes(n, t.mode) || !r.u8(n) || !r.bytes(n, t.locale))
    return false;
  t.rtl = (flags & 1) != 0;
  if (!r.palette(t.light) || !r.palette(t.dark) || !r.palette(t.night))
    return false;
  while (r.at < blob.size()) {
    StringPair pair;
    if (!r.u8(n) || n == 0 || !r.bytes(n, pair.key) || !r.u16(n) || !r.bytes(n, pair.value))
      return false;
    t.strings.push_back(std::move(pair));
  }
  // A cached theme is the API's version: it came from a v1 answer.
  t.api_version = 1;
  out = std::move(t);
  return true;
}

uint32_t theme_cache_checksum(const std::string &blob) {
  uint32_t hash = 2166136261UL;
  for (char c : blob) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 16777619UL;
  }
  return hash;
}

ThemeCacheHead theme_cache_head(const std::string &blob) {
  ThemeCacheHead head;
  head.checksum = theme_cache_checksum(blob);
  head.length = static_cast<uint16_t>(blob.size());
  head.version = THEME_CACHE_VERSION;
  head.chunks = static_cast<uint8_t>((blob.size() + THEME_CHUNK_BYTES - 1) / THEME_CHUNK_BYTES);
  return head;
}

bool theme_cache_head_ok(const ThemeCacheHead &head) {
  const size_t length = head.length;
  const size_t chunks = head.chunks;
  // Exactly as many chunks as the length needs.
  return head.version == THEME_CACHE_VERSION && chunks > 0 && chunks <= THEME_CACHE_CHUNKS &&
         length <= chunks * THEME_CHUNK_BYTES && length > (chunks - 1) * THEME_CHUNK_BYTES;
}

std::vector<ThemeCacheChunk> theme_cache_chunks(const std::string &blob) {
  std::vector<ThemeCacheChunk> chunks;
  for (size_t at = 0; at < blob.size(); at += THEME_CHUNK_BYTES) {
    ThemeCacheChunk chunk{};
    const size_t n = std::min(THEME_CHUNK_BYTES, blob.size() - at);
    memcpy(chunk.data, blob.data() + at, n);
    chunks.push_back(chunk);
  }
  return chunks;
}

std::string theme_cache_join(const ThemeCacheHead &head, const std::vector<ThemeCacheChunk> &chunks) {
  if (!theme_cache_head_ok(head) || chunks.size() != head.chunks)
    return {};
  std::string blob;
  blob.reserve(head.length);
  for (const auto &chunk : chunks) {
    const size_t n = std::min(THEME_CHUNK_BYTES, static_cast<size_t>(head.length) - blob.size());
    blob.append(reinterpret_cast<const char *>(chunk.data), n);
  }
  if (theme_cache_checksum(blob) != head.checksum)
    return {};
  return blob;
}

}  // namespace esphome::socialhome_display
