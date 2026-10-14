// SPDX-License-Identifier: MPL-2.0
// The cached theme: blob round-trip, chunking under the preference limit and
// what a damaged or foreign cache does (nothing).
#include "check.h"
#include "panel_strings.h"
#include "parser.h"
#include "theme_cache.h"

using namespace esphome::socialhome_display;

namespace {
Theme fixture_theme() {
  std::string json = check::fixture("theme.json");
  Theme t;
  CHECK(parse_theme(json.data(), json.size(), t) == ParseResult::OK);
  return t;
}

bool same_palette(const Palette &a, const Palette &b) {
  return a.bg == b.bg && a.surface == b.surface && a.text == b.text && a.muted == b.muted && a.accent == b.accent &&
         a.accent_text == b.accent_text && a.border == b.border && a.error_bg == b.error_bg &&
         a.error_text == b.error_text && a.focus == b.focus;
}

Theme round_trip(const Theme &theme, bool *ok) {
  std::string blob = encode_theme_cache(theme);
  ThemeCacheHead head = theme_cache_head(blob);
  std::string back = theme_cache_join(head, theme_cache_chunks(blob));
  Theme out;
  *ok = back == blob && decode_theme_cache(back, out);
  return out;
}
}  // namespace

TEST(theme_cache_chunks_fit_a_preference) {
  // The host backend refuses preferences over 255 bytes.
  CHECK(sizeof(ThemeCacheChunk) <= 255);
  CHECK(sizeof(ThemeCacheHead) <= 255);
}

TEST(theme_cache_round_trip) {
  Theme t = fixture_theme();
  t.locale = "de";
  t.mode = "dark";
  t.rtl = true;
  t.light.accent = 0x123456;
  t.night.text = 0xABCDEF;
  // German, with umlauts and a placeholder.
  for (auto &pair : t.strings) {
    if (pair.key == "forget")
      pair.value = "Server vergessen";
    if (pair.key == "pin_remaining")
      pair.value = "Verbleibende Versuche: {n}";
  }
  bool ok = false;
  Theme back = round_trip(t, &ok);
  CHECK(ok);
  CHECK_EQ(back.locale, std::string("de"));
  CHECK_EQ(back.mode, std::string("dark"));
  CHECK(back.rtl);
  CHECK(same_palette(back.light, t.light));
  CHECK(same_palette(back.dark, t.dark));
  CHECK(same_palette(back.night, t.night));
  CHECK_EQ(panel_string(back, "forget"), std::string("Server vergessen"));
  CHECK_EQ(fill_placeholder(panel_string(back, "pin_remaining"), "n", "3"), std::string("Verbleibende Versuche: 3"));
  CHECK_EQ(panel_string(back, "credits"), panel_string(t, "credits"));
  // Every panel string the theme had is back, word for word.
  size_t kept = 0;
  for (const auto &pair : t.strings) {
    if (!is_panel_string(pair.key))
      continue;
    kept++;
    static const std::string MISSING = "\x01missing";
    CHECK_EQ(back.str(pair.key.c_str(), MISSING), pair.value);
  }
  CHECK_EQ(back.strings.size(), kept);
}

TEST(theme_cache_keeps_only_panel_strings) {
  // Board strings come with every board; only what the panel draws itself
  // is worth the flash.
  Theme t = fixture_theme();
  bool ok = false;
  Theme back = round_trip(t, &ok);
  CHECK(ok);
  static const std::string MISSING = "\x01missing";
  CHECK_EQ(back.str("tab_overview", MISSING), MISSING);
  CHECK_EQ(back.str("read_aloud", MISSING), MISSING);
  CHECK(back.str("wifi_title", MISSING) != MISSING);
  std::string blob = encode_theme_cache(t);
  CHECK(blob.size() <= THEME_CACHE_MAX);
  CHECK(theme_cache_head(blob).chunks <= THEME_CACHE_CHUNKS);
}

TEST(theme_cache_long_strings_capped) {
  // A theme far over the budget still caches what fits; the rest falls
  // back to English.
  Theme t;
  t.locale = "xx";
  for (const char *key : {"wifi_title", "wifi_text", "server_title", "server_text", "credits", "forget_confirm",
                          "update_text", "map_none"})
    t.strings.push_back({key, std::string(1000, 'a')});
  std::string blob = encode_theme_cache(t);
  CHECK(blob.size() <= THEME_CACHE_MAX);
  bool ok = false;
  Theme back = round_trip(t, &ok);
  CHECK(ok);
  CHECK(back.strings.size() >= 4);
  CHECK(back.strings.size() < 8);
  CHECK_EQ(panel_string(back, "map_none"), builtin_string("map_none"));
}

TEST(theme_cache_empty_theme) {
  Theme t;
  bool ok = false;
  Theme back = round_trip(t, &ok);
  CHECK(ok);
  CHECK_EQ(back.locale, std::string("en"));
  CHECK(back.strings.empty());
  CHECK_EQ(panel_string(back, "ok"), std::string("OK"));
}

TEST(theme_cache_damage_ignored) {
  Theme t = fixture_theme();
  std::string blob = encode_theme_cache(t);
  ThemeCacheHead head = theme_cache_head(blob);
  std::vector<ThemeCacheChunk> chunks = theme_cache_chunks(blob);
  CHECK(chunks.size() >= 2);
  CHECK(!theme_cache_join(head, chunks).empty());
  // A flipped byte (power cut mid-write): checksum mismatch.
  auto damaged = chunks;
  damaged[1].data[7] ^= 0x40;
  CHECK(theme_cache_join(head, damaged).empty());
  // A chunk missing.
  auto short_list = chunks;
  short_list.pop_back();
  CHECK(theme_cache_join(head, short_list).empty());
  // Another format, or a head that doesn't add up.
  ThemeCacheHead other = head;
  other.version = THEME_CACHE_VERSION + 1;
  CHECK(!theme_cache_head_ok(other));
  CHECK(theme_cache_join(other, chunks).empty());
  ThemeCacheHead too_long = head;
  too_long.length = static_cast<uint16_t>(head.chunks * THEME_CHUNK_BYTES + 1);
  CHECK(!theme_cache_head_ok(too_long));
  ThemeCacheHead none{};
  CHECK(!theme_cache_head_ok(none));
  ThemeCacheHead too_many = head;
  too_many.chunks = THEME_CACHE_CHUNKS + 1;
  CHECK(!theme_cache_head_ok(too_many));
  // Truncated blobs don't decode.
  Theme out;
  out.locale = "kept";
  CHECK(!decode_theme_cache(std::string(), out));
  CHECK(!decode_theme_cache(blob.substr(0, 20), out));
  CHECK(!decode_theme_cache(blob.substr(0, blob.size() - 1), out));
  CHECK_EQ(out.locale, std::string("kept"));
}
