// SPDX-License-Identifier: MPL-2.0
// Which palette the panel draws with: the household's colour mode × the
// server's night flag × the night clock — and the cached theme choosing the
// same as the live one.
#include "check.h"
#include "parser.h"
#include "theme_cache.h"

using namespace esphome::socialhome_display;

TEST(scheme_auto_follows_the_night_window) {
  // A panel has no OS setting to follow: light by day, dark at night.
  CHECK(scheme_for("auto", false, false) == Scheme::LIGHT);
  CHECK(scheme_for("auto", true, false) == Scheme::DARK);
}

TEST(scheme_light_and_dark_stay_fixed) {
  CHECK(scheme_for("light", false, false) == Scheme::LIGHT);
  CHECK(scheme_for("light", true, false) == Scheme::LIGHT);
  CHECK(scheme_for("dark", false, false) == Scheme::DARK);
  CHECK(scheme_for("dark", true, false) == Scheme::DARK);
}

TEST(scheme_night_clock_is_always_night) {
  for (const char *mode : {"auto", "light", "dark", ""})
    CHECK(scheme_for(mode, true, true) == Scheme::NIGHT);
}

TEST(scheme_unknown_mode_counts_as_auto) {
  CHECK(scheme_for("", false, false) == Scheme::LIGHT);
  CHECK(scheme_for("", true, false) == Scheme::DARK);
  CHECK(scheme_for("sepia", false, false) == Scheme::LIGHT);
  CHECK(scheme_for("sepia", true, false) == Scheme::DARK);
}

TEST(scheme_picks_the_theme_palette) {
  std::string json = check::fixture("theme.json");
  Theme t;
  CHECK(parse_theme(json.data(), json.size(), t) == ParseResult::OK);
  CHECK_EQ(t.palette(Scheme::LIGHT).bg, 0xF4F2EEu);
  CHECK_EQ(t.palette(Scheme::DARK).bg, 0x151412u);
  CHECK_EQ(t.palette(Scheme::NIGHT).bg, 0x000000u);
}

TEST(scheme_cached_theme_chooses_the_same) {
  // After a reboot the cached theme (mode and palettes) draws the same
  // colours by day and in the night window as the live one did.
  std::string json = check::fixture("theme.json");
  Theme live;
  CHECK(parse_theme(json.data(), json.size(), live) == ParseResult::OK);
  live.mode = "auto";
  Theme cached;
  CHECK(decode_theme_cache(encode_theme_cache(live), cached));
  CHECK_EQ(cached.mode, std::string("auto"));
  for (bool night : {false, true}) {
    const Scheme s = scheme_for(cached.mode, night, false);
    CHECK(s == scheme_for(live.mode, night, false));
    CHECK_EQ(cached.palette(s).bg, live.palette(s).bg);
    CHECK_EQ(cached.palette(s).text, live.palette(s).text);
  }
}
