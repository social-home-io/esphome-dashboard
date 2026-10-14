// SPDX-License-Identifier: MPL-2.0
// Layout maths and URL helpers.
#include "check.h"
#include "parser.h"
#include "util.h"

using namespace esphome::socialhome_display;

TEST(server_url_defaults) {
  CHECK_EQ(normalize_server_url("192.168.1.5"), std::string("http://192.168.1.5:8100"));
  CHECK_EQ(normalize_server_url(" home.local "), std::string("http://home.local:8100"));
  CHECK_EQ(normalize_server_url("http://home.local:9000/"), std::string("http://home.local:9000"));
  CHECK_EQ(normalize_server_url("HTTP://home.local/dashboard"), std::string("http://home.local:8100"));
  CHECK_EQ(normalize_server_url("https://sh.example.org"), std::string("https://sh.example.org"));
  CHECK_EQ(normalize_server_url("[fe80::1]"), std::string("http://[fe80::1]:8100"));
  CHECK_EQ(normalize_server_url("[fe80::1]:81"), std::string("http://[fe80::1]:81"));
}

TEST(server_url_rejects) {
  CHECK_EQ(normalize_server_url(""), std::string(""));
  CHECK_EQ(normalize_server_url("http://"), std::string(""));
  CHECK_EQ(normalize_server_url("ftp://x"), std::string(""));
  CHECK_EQ(normalize_server_url("home:abc"), std::string(""));
  CHECK_EQ(normalize_server_url("home:70000"), std::string(""));
  CHECK_EQ(normalize_server_url(":8100"), std::string(""));
}

TEST(server_url_from_home_assistant) {
  // The connect action's url goes through the same normalisation as a typed
  // address.
  CHECK_EQ(normalize_server_url("http://192.168.1.20:8100"), std::string("http://192.168.1.20:8100"));
  CHECK_EQ(normalize_server_url("http://192.168.1.20:8100/"), std::string("http://192.168.1.20:8100"));
  CHECK_EQ(normalize_server_url("http://homeassistant.local"), std::string("http://homeassistant.local:8100"));
  CHECK_EQ(normalize_server_url("https://sh.example.org:8443/api/display/v1"),
           std::string("https://sh.example.org:8443"));
  CHECK_EQ(normalize_server_url("socialhome.local"), std::string("http://socialhome.local:8100"));
  CHECK_EQ(normalize_server_url("javascript:alert(1)"), std::string(""));
  CHECK_EQ(normalize_server_url("file:///etc/passwd"), std::string(""));
}

TEST(url_hosts) {
  CHECK_EQ(url_host("http://192.168.1.20:8100"), std::string("192.168.1.20"));
  CHECK_EQ(url_host("https://sh.example.org"), std::string("sh.example.org"));
  CHECK_EQ(url_host("http://[fe80::1]:81"), std::string("[fe80::1]"));
  CHECK_EQ(url_host("http://[fe80::1]"), std::string("[fe80::1]"));
  CHECK_EQ(url_host("home.local"), std::string("home.local"));
}

TEST(json_escaping) {
  CHECK_EQ(json_escape("plain"), std::string("plain"));
  CHECK_EQ(json_escape("a\"b\\c"), std::string("a\\\"b\\\\c"));
  CHECK_EQ(json_escape("l1\nl2\tx\r"), std::string("l1\\nl2\\tx\\r"));
  CHECK_EQ(json_escape(std::string("\x01", 1)), std::string("\\u0001"));
  CHECK_EQ(json_escape("caf\xC3\xA9"), std::string("caf\xC3\xA9"));
}

TEST(request_bodies) {
  CHECK_EQ(pair_request_body("guition-jc8012p4a1", "2026.10.13"),
           std::string(R"({"model": "guition-jc8012p4a1", "firmware": "2026.10.13"})"));
  CHECK_EQ(claim_request_body("Abc-_123", "guition-jc8012p4a1", "dev"),
           std::string(R"({"claim": "Abc-_123", "model": "guition-jc8012p4a1", "firmware": "dev"})"));
  // Whatever Home Assistant passes stays one JSON string.
  CHECK_EQ(claim_request_body("x\", \"model\": \"evil", "m", "f\\"),
           std::string(R"({"claim": "x\", \"model\": \"evil", "model": "m", "firmware": "f\\"})"));
}

TEST(ha_statuses) {
  CHECK_EQ(std::string(ha_status(false, false, false, false)), std::string("unpaired"));
  CHECK_EQ(std::string(ha_status(false, true, false, false)), std::string("pairing"));
  CHECK_EQ(std::string(ha_status(true, true, false, false)), std::string("pairing"));
  CHECK_EQ(std::string(ha_status(true, false, false, false)), std::string("paired"));
  CHECK_EQ(std::string(ha_status(true, false, true, false)), std::string("locked"));
  CHECK_EQ(std::string(ha_status(true, false, false, true)), std::string("unreachable"));
  CHECK_EQ(std::string(ha_status(false, false, false, true)), std::string("unpaired"));
}

TEST(retry_after) {
  CHECK_EQ(parse_retry_after("12", 30), 12u);
  CHECK_EQ(parse_retry_after("0", 30), 1u);
  CHECK_EQ(parse_retry_after("99999", 30), 3600u);
  CHECK_EQ(parse_retry_after("", 30), 30u);
  CHECK_EQ(parse_retry_after("Wed, 21 Oct 2026 07:28:00 GMT", 30), 30u);
}

TEST(image_sizes) {
  CHECK_EQ(pick_avatar_size(40), 48);
  CHECK_EQ(pick_avatar_size(72), 96);
  CHECK_EQ(pick_avatar_size(500), 192);
  CHECK_EQ(pick_emoji_size(24), 24);
  CHECK_EQ(pick_emoji_size(70), 72);
  CHECK_EQ(pick_emoji_size(1000), 128);
}

TEST(metrics_scale) {
  Metrics m = Metrics::compute(1280, 800, 1.0f);
  CHECK_EQ(m.scale, 1.0f);
  CHECK_EQ(m.rail_width(), 120);
  CHECK_EQ(m.content_width(), 1160);
  Metrics big = Metrics::compute(1920, 1200, 1.0f);
  CHECK_EQ(big.scale, 1.5f);
  CHECK_EQ(big.rail_width(), 180);
  // A wider-than-16:10 panel scales by height.
  Metrics wide = Metrics::compute(1920, 800, 1.0f);
  CHECK_EQ(wide.scale, 1.0f);
  Metrics zoomed = Metrics::compute(1280, 800, 1.25f);
  CHECK_EQ(zoomed.px(16), 20);
}

TEST(metrics_columns) {
  Metrics m = Metrics::compute(1280, 800, 1.0f);
  CHECK_EQ(m.columns(1104, 240), 4);
  CHECK_EQ(m.columns(500, 240), 2);
  CHECK_EQ(m.columns(499, 240), 1);
  CHECK_EQ(m.columns(10, 240), 1);
  CHECK_EQ(Metrics::pct(50, 1000), 500);
  CHECK_EQ(Metrics::pct(150, 1000), 1000);
}

TEST(clock_hands) {
  int x, y;
  hand_end(0, 100, 100, 50, &x, &y);
  CHECK_EQ(x, 100);
  CHECK_EQ(y, 50);
  hand_end(90, 100, 100, 50, &x, &y);
  CHECK_EQ(x, 150);
  CHECK_EQ(y, 100);
  hand_end(180, 100, 100, 50, &x, &y);
  CHECK_EQ(y, 150);
}

TEST(clock_wedge) {
  int s, e;
  wedge_angles(0, 15, &s, &e);  // from 12 o'clock to 3 o'clock
  CHECK_EQ(s, 270);
  CHECK_EQ(e, 0);
  wedge_angles(180, 10, &s, &e);
  CHECK_EQ(s, 90);
  CHECK_EQ(e, 150);
  wedge_angles(90, 60, &s, &e);
  CHECK_EQ(s, 0);
  CHECK_EQ(e, 360);
}

TEST(upper_case_labels) {
  CHECK_EQ(upper("Next"), std::string("NEXT"));
  CHECK_EQ(upper("Who's home"), std::string("WHO'S HOME"));
  CHECK_EQ(upper("Prochain \xC3\xA9v\xC3\xA9nement"), std::string("PROCHAIN \xC3\x89V\xC3\x89NEMENT"));
  CHECK_EQ(upper("Gr\xC3\xBC\xC3\x9F"), std::string("GR\xC3\x9C\xC3\x9F"));
  CHECK_EQ(upper("\xC3\xB7 \xE2\x80\xBA"), std::string("\xC3\xB7 \xE2\x80\xBA"));
}

TEST(local_clock) {
  CHECK(is_hhmm("14:32"));
  CHECK(!is_hhmm("2:32 PM"));
  CHECK(!is_hhmm("14.32"));
  // 1791808320 is 2026-10-12 12:32 UTC; Berlin is +120.
  CHECK_EQ(format_hhmm(1791808320, 120), std::string("14:32"));
  CHECK_EQ(format_hhmm(1791808320, -780), std::string("23:32"));
  CHECK_EQ(format_hhmm(1791808320 + 59, 0), std::string("12:32"));
}

TEST(emoji_text) {
  CHECK_EQ(strip_emoji("Leaving work now \xF0\x9F\x8D\xB0"), std::string("Leaving work now"));
  CHECK_EQ(strip_emoji("\xE2\x98\x80\xEF\xB8\x8F Sunny \xF0\x9F\x8E\xB5 day"), std::string("Sunny day"));
  CHECK_EQ(strip_emoji("Gr\xC3\xBC\xC3\x9F \xE2\x80\xBA ok"), std::string("Gr\xC3\xBC\xC3\x9F \xE2\x80\xBA ok"));
  auto codes = emoji_codes("a \xF0\x9F\x8D\xB0 b \xE2\x98\x80\xEF\xB8\x8F", 5);
  CHECK_EQ(codes.size(), size_t{2});
  CHECK_EQ(codes[0], std::string("1f370"));
  CHECK_EQ(codes[1], std::string("2600"));
  // ZWJ sequence, skin tone, flag.
  auto zwj = emoji_codes("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x8D\xB3", 5);
  CHECK_EQ(zwj.size(), size_t{1});
  CHECK_EQ(zwj[0], std::string("1f468-200d-1f373"));
  auto tone = emoji_codes("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", 5);
  CHECK_EQ(tone.size(), size_t{1});
  CHECK_EQ(tone[0], std::string("1f44d-1f3fd"));
  auto flag = emoji_codes("\xF0\x9F\x87\xAC\xF0\x9F\x87\xA7", 5);
  CHECK_EQ(flag.size(), size_t{1});
  CHECK_EQ(flag[0], std::string("1f1ec-1f1e7"));
  CHECK_EQ(emoji_codes("\xF0\x9F\x8D\xB0\xF0\x9F\x8D\xB0\xF0\x9F\x8D\xB0", 2).size(), size_t{2});
  CHECK_EQ(emoji_codes("no emoji", 3).size(), size_t{0});
}

namespace {
/// Runs as "T(Shoes )E(1f45f_)B" — `_` marks an emoji followed by a space.
std::string runs(const std::string &text, bool words, size_t max_emoji = 16) {
  std::string out;
  for (const auto &run : text_runs(text, words, max_emoji)) {
    switch (run.kind) {
      case TextRun::TEXT:
        out += "T(" + run.value + ")";
        break;
      case TextRun::EMOJI:
        out += "E(" + run.value + (run.space_after ? "_" : "") + ")";
        break;
      case TextRun::BREAK:
        out += "B";
        break;
    }
  }
  return out;
}
}  // namespace

TEST(text_runs_keep_emoji_in_place) {
  // A trailing emoji is kept (tile captions dropped it).
  CHECK_EQ(runs("@leo Shoes on! \xF0\x9F\x91\x9F", false), std::string("T(@leo Shoes on! )E(1f45f)"));
  // Inline emoji stay between their words.
  CHECK_EQ(runs("Cake \xF0\x9F\x8D\xB0 for Gran", false), std::string("T(Cake )E(1f370_)T(for Gran)"));
  CHECK_EQ(runs("\xF0\x9F\x8E\xA4 Can I go?", false), std::string("E(1f3a4_)T(Can I go?)"));
  CHECK_EQ(runs("no emoji here", false), std::string("T(no emoji here)"));
  CHECK_EQ(runs("", false), std::string(""));
  CHECK_EQ(runs("\xF0\x9F\x8E\x82\xF0\x9F\x8E\x89", false), std::string("E(1f382)E(1f389)"));
  // Text glued to an emoji keeps no gap.
  CHECK_EQ(runs("go\xF0\x9F\x8F\x83now", false), std::string("T(go)E(1f3c3)T(now)"));
}

TEST(text_runs_words_split_for_wrapping) {
  CHECK_EQ(runs("@leo Shoes on! \xF0\x9F\x91\x9F", true), std::string("T(@leo )T(Shoes )T(on! )E(1f45f)"));
  CHECK_EQ(runs("Cake \xF0\x9F\x8D\xB0 for Gran", true), std::string("T(Cake )E(1f370_)T(for )T(Gran)"));
  CHECK_EQ(runs("one", true), std::string("T(one)"));
}

TEST(text_runs_spaces_and_breaks) {
  // Spaces collapse, none lead or trail.
  CHECK_EQ(runs("  a   b  ", false), std::string("T(a b)"));
  CHECK_EQ(runs("\xF0\x9F\x8D\xB0   x", false), std::string("E(1f370_)T(x)"));
  CHECK_EQ(runs("x \xF0\x9F\x8D\xB0 ", false), std::string("T(x )E(1f370)"));
  // Line breaks survive (once), not at the edges.
  CHECK_EQ(runs("\nfirst \n\n second\n", false), std::string("T(first)BT(second)"));
  CHECK_EQ(runs("a\r\nb", false), std::string("T(a)BT(b)"));
  // Variation selectors and stray joiners vanish.
  CHECK_EQ(runs("\xE2\x98\x80\xEF\xB8\x8F Sunny", false), std::string("E(2600_)T(Sunny)"));
  CHECK_EQ(runs("a\xE2\x80\x8D" "b", false), std::string("T(ab)"));
}

TEST(text_runs_sequences) {
  // ZWJ family, skin tone and a flag are one picture each.
  CHECK_EQ(runs("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x8D\xB3 cooks", false), std::string("E(1f468-200d-1f373_)T(cooks)"));
  CHECK_EQ(runs("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", false), std::string("E(1f44d-1f3fd)"));
  CHECK_EQ(runs("\xF0\x9F\x87\xAC\xF0\x9F\x87\xA7\xF0\x9F\x87\xA8\xF0\x9F\x87\xAD", false),
           std::string("E(1f1ec-1f1e7)E(1f1e8-1f1ed)"));
  // Non-emoji symbols stay text; bad UTF-8 doesn't crash.
  CHECK_EQ(runs("Gr\xC3\xBC\xC3\x9F \xE2\x80\xBA ok", false), std::string("T(Gr\xC3\xBC\xC3\x9F \xE2\x80\xBA ok)"));
  CHECK_EQ(runs("a\xFF" "b", false), std::string("T(a\xFF" "b)"));
}

TEST(text_runs_emoji_limit) {
  CHECK_EQ(runs("\xF0\x9F\x8D\xB0 \xF0\x9F\x8D\xB0 \xF0\x9F\x8D\xB0 end", false, 2),
           std::string("E(1f370_)E(1f370_)T(end)"));
  // The text left is the same as strip_emoji's.
  std::string mixed = "Leaving \xF0\x9F\x9A\x97 work  now \xF0\x9F\x8D\xB0";
  std::string joined;
  for (const auto &run : text_runs(mixed, false))
    if (run.kind == TextRun::TEXT)
      joined += run.value;
  CHECK_EQ(joined, std::string("Leaving work now "));
  CHECK_EQ(strip_emoji(mixed), std::string("Leaving work now"));
}

TEST(map_fetch_policy) {
  MapFetch f;
  const std::string light = "/api/display/v1/map.png?w=748&h=560&scheme=light";
  const std::string dark = "/api/display/v1/map.png?w=748&h=560&scheme=dark";
  // Nothing held: fetch, nothing to revalidate.
  CHECK(f.due(light, "\"a\"", 0));
  CHECK_EQ(f.if_none_match(light), std::string(""));
  f.loaded(light, "\"p1\"", "\"a\"");
  // Same picture, same board ETag: no request at all.
  CHECK(!f.due(light, "\"a\"", 1000));
  // The board says the map changed: revalidate with the held ETag.
  CHECK(f.due(light, "\"b\"", 1000));
  CHECK_EQ(f.if_none_match(light), std::string("\"p1\""));
  f.unchanged("\"b\"");
  CHECK(!f.due(light, "\"b\"", 2000));
  // Another scheme is another picture: fetch, nothing to revalidate.
  CHECK(f.due(dark, "\"b\"", 2000));
  CHECK_EQ(f.if_none_match(dark), std::string(""));
  // 503 / 429: wait Retry-After, then try again even without a new ETag.
  f.failed(2000, 60);
  CHECK(!f.due(dark, "\"b\"", 30000));
  CHECK(!f.due(dark, "\"c\"", 61999));
  CHECK(f.due(dark, "\"b\"", 62000));
  f.loaded(dark, "\"p2\"", "\"b\"");
  CHECK(!f.due(dark, "\"b\"", 70000));
  f.reset();
  CHECK(f.due(dark, "\"b\"", 70000));
}

// ── Timeline bar colours ────────────────────────────────────────────────

namespace {
constexpr uint32_t DARK_SURFACE = 0x22201D;   // theme.json colors.dark.surface
constexpr uint32_t LIGHT_SURFACE = 0xFFFFFF;  // colors.light.surface
constexpr uint32_t NIGHT_SURFACE = 0x121110;  // colors.night.surface
}  // namespace

TEST(blend_rgb_composites) {
  CHECK_EQ(blend_rgb(0xFFFFFF, 0x000000, 255), 0xFFFFFFu);
  CHECK_EQ(blend_rgb(0xFFFFFF, 0x000000, 0), 0x000000u);
  CHECK_EQ(blend_rgb(0xFFFFFF, 0x000000, 127), 0x7F7F7Fu);
  CHECK_EQ(blend_rgb(0xC44D72, 0x22201D, 255), 0xC44D72u);
  CHECK_EQ(blend_rgb(0x123456, 0x123456, 99), 0x123456u);
}

TEST(ink_on_matches_social_home) {
  // contrast.ink_on: black on light fills, white on dark ones; the fixture
  // lanes' server-picked ink is the same rule.
  CHECK_EQ(ink_on(0xFFFFFF), 0x000000u);
  CHECK_EQ(ink_on(0x000000), 0xFFFFFFu);
  CHECK_EQ(ink_on(0xC44D72), 0x000000u);  // home.json u-anna
  CHECK_EQ(ink_on(0x3F7D57), 0xFFFFFFu);  // home.json u-mia
  CHECK_EQ(ink_on(0xAD780C), 0x000000u);  // home.json u-leo
  CHECK_EQ(ink_on(0x357BBB), 0x000000u);  // home.json u-pascal
  CHECK(contrast_ratio(0x000000, 0xFFFFFF) > 20.9);
  CHECK(contrast_ratio(0x777777, 0x777777) < 1.0001);
}

TEST(bar_upcoming_keeps_lane_ink) {
  BarStyle s = bar_style(0xC44D72, true, 0x000000, DARK_SURFACE, false, false);
  CHECK_EQ(s.fill, 0xC44D72u);
  CHECK_EQ(s.ink, 0x000000u);
  CHECK_EQ(static_cast<int>(s.alpha), 255);
  // The server's pick wins on an unfaded bar, even where ours would differ.
  CHECK_EQ(bar_style(0xC44D72, true, 0x1F1D1A, LIGHT_SURFACE, false, false).ink, 0x1F1D1Au);
  // No ink from the server: picked against the colour.
  CHECK_EQ(bar_style(0x3F7D57, false, 0, LIGHT_SURFACE, false, false).ink, 0xFFFFFFu);
}

TEST(bar_past_dark_gets_light_label) {
  // The bug: a past bar faded to half over the dark card kept the lane's
  // black ink (faded too), so "Work" read dark on dark.
  BarStyle s = bar_style(0xC44D72, true, 0x000000, DARK_SURFACE, false, true);
  CHECK_EQ(s.fill, blend_rgb(0xC44D72, DARK_SURFACE, 127));
  CHECK_EQ(s.ink, 0xFFFFFFu);
  CHECK(contrast_ratio(s.ink, s.fill) > contrast_ratio(0x000000, s.fill));
  CHECK(contrast_ratio(s.ink, s.fill) >= 4.5);
  CHECK_EQ(bar_style(0xAD780C, true, 0x000000, DARK_SURFACE, false, true).ink, 0xFFFFFFu);
  CHECK_EQ(bar_style(0x357BBB, true, 0x000000, NIGHT_SURFACE, false, true).ink, 0xFFFFFFu);
}

TEST(bar_past_light_gets_dark_label) {
  // Faded toward white the bar lightens: white ink (Mia) turns black.
  BarStyle s = bar_style(0x3F7D57, true, 0xFFFFFF, LIGHT_SURFACE, false, true);
  CHECK_EQ(s.fill, blend_rgb(0x3F7D57, LIGHT_SURFACE, 127));
  CHECK_EQ(s.ink, 0x000000u);
  CHECK_EQ(bar_style(0xC44D72, true, 0x000000, LIGHT_SURFACE, false, true).ink, 0x000000u);
}

TEST(bar_school_is_striped_not_faded) {
  // As .dash-block-school: the lane's own colour and ink, with white
  // stripes at 16 % on top — 8 % once past, over the half-faded fill.
  BarStyle s = bar_style(0xAD780C, true, 0x000000, DARK_SURFACE, true, false);
  CHECK_EQ(static_cast<int>(s.alpha), 255);
  CHECK_EQ(s.fill, 0xAD780Cu);
  CHECK_EQ(s.ink, 0x000000u);
  CHECK_EQ(static_cast<int>(s.stripe_opa), 41);
  BarStyle past = bar_style(0xAD780C, true, 0x000000, DARK_SURFACE, true, true);
  CHECK_EQ(past.fill, blend_rgb(0xAD780C, DARK_SURFACE, 127));
  CHECK_EQ(past.ink, 0xFFFFFFu);
  CHECK_EQ(static_cast<int>(past.stripe_opa), 20);
  // Anything else has no stripes.
  CHECK_EQ(static_cast<int>(bar_style(0xAD780C, true, 0x000000, DARK_SURFACE, false, false).stripe_opa), 0);
  CHECK_EQ(static_cast<int>(bar_style(0xAD780C, true, 0x000000, DARK_SURFACE, false, true).stripe_opa), 0);
}

TEST(stripe_tile_repeats_seamlessly) {
  // Eight periods of 16·√2 px across: 181 px at 1280 × 800.
  CHECK_EQ(stripe_tile_width(1.0f), 181);
  CHECK_EQ(stripe_tile_width(1.5f), 272);
  const int w = stripe_tile_width(1.0f), h = 42;
  std::vector<uint8_t> c = stripe_coverage(w, h);
  CHECK_EQ(static_cast<int>(c.size()), w * h);
  // The stripe starts in the top-left corner (the gradient's 0 %) and is
  // gone half a period on: x + y ≈ 3/4 of 22.6 px.
  CHECK_EQ(static_cast<int>(c[0]), 255);
  CHECK_EQ(static_cast<int>(c[16]), 0);
  // Constant along the 45° stripe direction (x + y fixed).
  CHECK_EQ(static_cast<int>(c[1 * w + 5]), static_cast<int>(c[3 * w + 3]));
  // Half the area is striped.
  long sum = 0;
  for (uint8_t v : c)
    sum += v;
  const double mean = static_cast<double>(sum) / c.size();
  CHECK(mean > 120 && mean < 135);
  // No seam where tiles meet: the last column at row y lies on the same
  // stripe as the first column one row up (x + y ≡ y - 1 either way).
  for (int y = 1; y < h; y++)
    CHECK_EQ(static_cast<int>(c[static_cast<size_t>(y) * w + (w - 1)]), static_cast<int>(c[static_cast<size_t>(y - 1) * w]));
  CHECK(stripe_coverage(0, 5).empty());
}

TEST(stripe_tile_survives_the_16_bit_screen) {
  CHECK_EQ(rgb565(0xFFFFFF), 0xFFFFu);
  CHECK_EQ(rgb565(0x000000), 0x0000u);
  CHECK_EQ(rgb565(0xF80000), 0xF800u);
  CHECK_EQ(rgb565(0x00FC00), 0x07E0u);
  CHECK_EQ(rgb565(0x0000F8), 0x001Fu);
  // Leo's past school bar on the light card: white at 8 % composited
  // before the cut still shows as a stripe.
  const uint32_t fill = bar_style(0xC99A3D, true, 0x000000, LIGHT_SURFACE, true, true).fill;
  std::vector<uint16_t> t = stripe_tile_rgb565(fill, 20, {0, 255, 128});
  CHECK_EQ(t.size(), 3u);
  CHECK_EQ(t[0], rgb565(fill));
  CHECK_EQ(t[1], rgb565(blend_rgb(0xFFFFFF, fill, 20)));
  CHECK(t[1] != t[0]);
  CHECK_EQ(t[2], rgb565(blend_rgb(0xFFFFFF, fill, 10)));
}

TEST(lane_geometry_matches_the_tablet) {
  // .dash-lane: 1 px border + 6 px + .dash-track (44 / 92 / 140) + 6 px.
  CHECK_EQ(LaneGeometry::track_height(1), 44);
  CHECK_EQ(LaneGeometry::track_height(2), 92);
  CHECK_EQ(LaneGeometry::track_height(3), 140);
  CHECK_EQ(LaneGeometry::track_height(0), 44);
  CHECK_EQ(LaneGeometry::lane_height(1), 57);
  CHECK_EQ(LaneGeometry::lane_height(2), 105);
}

TEST(map_height_matches_the_map_tab) {
  // .dash-mp-tab: calc(100dvh - 104px), never under 420 px.
  CHECK_EQ(Metrics::compute(1280, 800, 1.0f).map_height(), 696);
  CHECK_EQ(Metrics::compute(1920, 1200, 1.0f).map_height(), 1044);
}

TEST(bar_label_always_the_better_ink) {
  // Every state, every theme: the label is whichever of black / white reads
  // better on the bar actually drawn (or the lane's ink when unfaded).
  const uint32_t colors[] = {0xC44D72, 0x3F7D57, 0xAD780C, 0x357BBB, 0xF2C94C, 0x6B4E9B, 0xE07A3F, 0x2B2B2B};
  const uint32_t surfaces[] = {LIGHT_SURFACE, DARK_SURFACE, NIGHT_SURFACE};
  for (uint32_t color : colors) {
    for (uint32_t surface : surfaces) {
      for (int state = 0; state < 4; state++) {
        bool school = state & 1, past = state & 2;
        BarStyle s = bar_style(color, true, ink_on(color), surface, school, past);
        uint32_t other = s.ink == 0x000000u ? 0xFFFFFFu : 0x000000u;
        CHECK(contrast_ratio(s.ink, s.fill) >= contrast_ratio(other, s.fill));
      }
    }
  }
}

namespace {

/// The three palettes the panel draws with: the server's (theme.json) and
/// the firmware's built-in defaults, which must agree.
std::vector<Palette> palettes() {
  std::string json = check::fixture("theme.json");
  Theme served;
  CHECK(parse_theme(json.data(), json.size(), served) == ParseResult::OK);
  Theme builtin;
  return {served.light, served.dark, served.night, builtin.light, builtin.dark, builtin.night};
}

AgendaRow agenda_row(bool past, Status status, bool colored) {
  AgendaRow row;
  row.past = past;
  row.status = status;
  if (colored)
    row.color = Color::of(0xC44D72);
  return row;
}

}  // namespace

TEST(builtin_palettes_match_the_theme) {
  // Social Home #933 moved the night muted ink #7d786f -> #817c73.
  std::string json = check::fixture("theme.json");
  Theme served;
  CHECK(parse_theme(json.data(), json.size(), served) == ParseResult::OK);
  Theme builtin;
  CHECK_EQ(builtin.night.muted, served.night.muted);
  CHECK_EQ(builtin.night.muted, 0x817C73u);
  CHECK_EQ(builtin.dark.muted, served.dark.muted);
  CHECK_EQ(builtin.light.muted, served.light.muted);
}

TEST(past_row_words_stay_readable) {
  // Every word on a past or cancelled row is drawn at full strength in an
  // ink that keeps 4.5:1 on what is actually behind it, in every palette.
  for (const Palette &p : palettes()) {
    for (bool past : {false, true}) {
      for (Status status : {Status::NONE, Status::CANCELLED, Status::CHANGED}) {
        for (bool colored : {false, true}) {
          const RowStyle s = row_style(agenda_row(past, status, colored), p);
          const uint32_t ground = s.fill ? s.fill_color : p.surface;
          CHECK(contrast_ratio(s.time_ink, ground) >= 4.5);
          CHECK(contrast_ratio(s.title_ink, ground) >= 4.5);
          CHECK(contrast_ratio(s.meta_ink, ground) >= 4.5);
        }
      }
    }
  }
}

TEST(past_row_recedes_into_the_card) {
  // The browser's .dash-row.dash-past: no fill, strip at half over the
  // card (as a past timeline bar), emoji at 50 %, words in muted.
  for (const Palette &p : palettes()) {
    const RowStyle s = row_style(agenda_row(true, Status::NONE, true), p);
    CHECK(!s.fill);
    CHECK_EQ(s.strip, blend_rgb(0xC44D72, p.surface, 127));
    CHECK_EQ(s.strip, bar_style(0xC44D72, false, 0, p.surface, false, true).fill);
    CHECK_EQ(s.title_ink, p.muted);
    CHECK_EQ(s.time_ink, p.muted);
    CHECK_EQ(s.emoji_opa, OPA_PAST_ROW_EMOJI);
    CHECK(!s.strike);
    // No colour of its own: the plain border, as before.
    CHECK_EQ(row_style(agenda_row(true, Status::NONE, false), p).strip, p.border);
  }
}

TEST(upcoming_and_cancelled_rows_keep_their_strip) {
  for (const Palette &p : palettes()) {
    const RowStyle up = row_style(agenda_row(false, Status::NONE, true), p);
    CHECK(up.fill);
    CHECK_EQ(up.fill_color, p.bg);
    CHECK_EQ(up.strip, 0xC44D72u);
    CHECK_EQ(up.title_ink, p.text);
    CHECK_EQ(up.emoji_opa, OPA_FULL);
    // .dash-st-cancelled: struck through in muted, nothing faded.
    const RowStyle cancelled = row_style(agenda_row(false, Status::CANCELLED, true), p);
    CHECK(cancelled.fill);
    CHECK(cancelled.strike);
    CHECK_EQ(cancelled.title_ink, p.muted);
    CHECK_EQ(cancelled.strip, 0xC44D72u);
    CHECK_EQ(cancelled.emoji_opa, OPA_FULL);
    // The current row sits on the card colour.
    AgendaRow now = agenda_row(false, Status::NONE, true);
    now.now = true;
    CHECK_EQ(row_style(now, p).fill_color, p.surface);
  }
}

TEST(past_row_faded_whole_was_unreadable) {
  // Regression: the old LV_OPA_60 over the whole row (on the card) took the
  // time under 4.5:1 in the light and night palettes.
  std::string json = check::fixture("theme.json");
  Theme t;
  CHECK(parse_theme(json.data(), json.size(), t) == ParseResult::OK);
  for (const Palette *p : {&t.light, &t.night}) {
    const uint32_t ground = blend_rgb(p->bg, p->surface, 153);
    CHECK(contrast_ratio(blend_rgb(p->muted, p->surface, 153), ground) < 4.5);
  }
}

TEST(tiles_fade_only_their_picture) {
  // The browser's .dash-pv-past / .dash-pv-cancelled: the emoji fades, the
  // caption stays muted at full strength and the strip keeps its colour.
  for (const Palette &p : palettes()) {
    Tile tile;
    tile.color = Color::of(0x3F7D57);
    for (int state = 0; state < 4; state++) {
      tile.past = state & 1;
      tile.cancelled = state & 2;
      const TileStyle s = tile_style(tile, p);
      CHECK_EQ(s.caption_ink, p.muted);
      CHECK(contrast_ratio(s.caption_ink, p.bg) >= 4.5);
      CHECK_EQ(s.strip, 0x3F7D57u);
      CHECK_EQ(s.strike, tile.cancelled);
      const uint8_t want = tile.cancelled ? OPA_CANCELLED_PICTURE : tile.past ? OPA_PAST_PICTURE : OPA_FULL;
      CHECK_EQ(s.emoji_opa, want);
    }
    CHECK_EQ(tile_style(Tile{}, p).strip, p.border);
  }
}

TEST(grayscale_matches_the_browser) {
  // CSS grayscale(1): Rec. 709 luma of the sRGB values on every channel.
  CHECK_EQ(grayscale(0xFFFFFF), 0xFFFFFFu);
  CHECK_EQ(grayscale(0x000000), 0x000000u);
  CHECK_EQ(grayscale(0x2F6F5E), 0x606060u);  // the light accent
  CHECK_EQ(grayscale(0x6CC3A8), 0xAFAFAFu);  // the dark accent
  CHECK_EQ(grayscale(0x00FF00), 0xB6B6B6u);
}

TEST(live_data_unchanged_while_online) {
  for (const Palette &p : palettes()) {
    const LiveStyle s = live_style(p, false);
    CHECK(!s.stale);
    CHECK_EQ(s.text, p.text);
    CHECK_EQ(s.accent, p.accent);
    CHECK_EQ(s.accent_ink, p.accent_text);
    CHECK_EQ(s.picture_opa, OPA_FULL);
    CHECK_EQ(s.mark(0xC44D72), 0xC44D72u);
  }
}

TEST(offline_live_data_greys_but_stays_readable) {
  // The browser's .dash-stale [data-live]: the colour drains, words keep
  // 4.5:1 on the card and the page (never faded), only pictures fade.
  for (const Palette &p : palettes()) {
    const LiveStyle s = live_style(p, true);
    CHECK(s.stale);
    CHECK_EQ(s.text, p.muted);
    CHECK(contrast_ratio(s.text, p.surface) >= 4.5);
    CHECK(contrast_ratio(s.text, p.bg) >= 4.5);
    // The "now" pill: grey, its label picked against the grey.
    CHECK_EQ(s.accent, grayscale(p.accent));
    CHECK(contrast_ratio(s.accent_ink, s.accent) >= 4.5);
    CHECK_EQ(s.picture_opa, OPA_STALE_PICTURE);
    CHECK_EQ(s.mark(0xC44D72), grayscale(0xC44D72));
    const uint32_t g = s.mark(0xC44D72);
    CHECK_EQ(g >> 16, g & 0xFF);
    CHECK_EQ((g >> 8) & 0xFF, g & 0xFF);
  }
}

TEST(partial_grayscale_matches_the_browser) {
  // CSS grayscale(a): every channel moved a of the way to the luma.
  CHECK_EQ(grayscale(0xC44D72, 0), 0xC44D72u);
  CHECK_EQ(grayscale(0xC44D72, GREY_FULL), grayscale(0xC44D72));
  // 0.8 of the way: luma 105 (0.2126·196 + 0.7152·77 + 0.0722·114).
  const uint32_t luma = grayscale(0xC44D72) & 0xFF;
  CHECK_EQ(luma, 105u);
  CHECK_EQ(grayscale(0xC44D72, GREY_STALE_SPOT), 0x7B636Bu);
  CHECK_EQ(static_cast<int>(GREY_STALE_SPOT), 204);  // .dash-mp-stale: grayscale(0.8)
  CHECK_EQ(grayscale(0xFFFFFF, GREY_STALE_SPOT), 0xFFFFFFu);
}

TEST(notes_follow_the_scheme) {
  // Social Home softens each note toward the page, light and dark
  // (ColorBook); dark mode and night draw the dark pair, outlined, unshadowed.
  Note note;
  note.light = NoteColors{Color::of(0xFDEF9D), Color::of(0x1F1D1A)};
  note.dark = NoteColors{Color::of(0xBDB168), Color::of(0x1F1D1A)};
  const NoteLook day = note_look(note, false);
  CHECK_EQ(day.bg, 0xFDEF9Du);
  CHECK_EQ(day.ink, 0x1F1D1Au);
  CHECK(day.shadow);
  CHECK(!day.outline);
  const NoteLook dark = note_look(note, true);
  CHECK_EQ(dark.bg, 0xBDB168u);
  CHECK_EQ(dark.ink, 0x1F1D1Au);
  CHECK(!dark.shadow);
  CHECK(dark.outline);
  // White ink on a dark fill is the dark pair's own.
  note.dark = NoteColors{Color::of(0x3F2F7A), Color::of(0xFFFFFF)};
  CHECK_EQ(note_look(note, true).ink, 0xFFFFFFu);
  // A colourless note: the CSS fallback, #fff3a8 with the warm ink.
  const NoteLook plain = note_look(Note{}, true);
  CHECK_EQ(plain.bg, 0xFFF3A8u);
  CHECK_EQ(plain.ink, 0x1F1D1Au);
}

TEST(dial_follows_the_scheme) {
  // .dash-pv-dial: the card colour ringed in the text colour — a dark face
  // in dark mode and at night, never a white one.
  for (const Palette &p : palettes()) {
    const DialLook d = dial_look(p);
    CHECK_EQ(d.face, p.surface);
    CHECK_EQ(d.ring, p.text);
    CHECK_EQ(d.tick_major, p.text);
    CHECK_EQ(d.tick_minor, p.muted);
    CHECK_EQ(d.hands, p.text);
    CHECK_EQ(static_cast<int>(d.wedge_opa), 127);  // .dash-pv-wedge { opacity: 0.5 }
    CHECK(contrast_ratio(d.hands, d.face) >= 4.5);
  }
  Theme builtin;
  CHECK_EQ(dial_look(builtin.dark).face, 0x22201Du);
  CHECK_EQ(dial_look(builtin.night).face, 0x121110u);
}

TEST(stale_spot_says_last_seen) {
  MapSpot spot;
  spot.when = "1 h ago";
  spot.age_label = "last seen 1 h ago";
  spot.stale = true;
  SpotAge age = spot_age(spot, false);
  CHECK(age.shown);
  CHECK(age.clock);
  CHECK(age.bold);
  CHECK_EQ(age.text, std::string("last seen 1 h ago"));
  // Offline the age is hidden, as the browser's data-rel.
  CHECK(!spot_age(spot, true).shown);
  // Fresh: the bare "5 min ago", regular, no clock.
  spot.stale = false;
  spot.when = "5 min ago";
  spot.age_label = "updated 5 min ago";
  age = spot_age(spot, false);
  CHECK(!age.clock);
  CHECK(!age.bold);
  CHECK_EQ(age.text, std::string("5 min ago"));
  // A stale spot from a server without the label keeps the bare age.
  spot.stale = true;
  spot.age_label.clear();
  CHECK_EQ(spot_age(spot, false).text, std::string("5 min ago"));
}

TEST(offline_fades_pictures_like_the_browser) {
  CHECK_EQ(static_cast<int>(OPA_STALE_PICTURE), 115);  // opacity: 0.45
}

TEST(pin_pad_keys_like_the_tablet) {
  // Digits append up to six (the tablet's maxlength), ⌫ drops the last.
  std::string pin;
  for (const char *k : {"1", "2", "3", "4", "5", "6", "7"})
    pin = pin_after_key(pin, k);
  CHECK_EQ(pin, std::string("123456"));
  CHECK_EQ(pin_after_key(pin, "del"), std::string("12345"));
  CHECK_EQ(pin_after_key("", "del"), std::string(""));
  CHECK_EQ(pin_after_key("12", "x"), std::string("12"));
  CHECK_EQ(pin_after_key("12", "12"), std::string("12"));
  // ✓ needs four (minlength).
  CHECK(!pin_ready("123"));
  CHECK(pin_ready("1234"));
}

// The offline note sits where the tablet shows it: under the tab bar, first
// in the alert slot, ahead of the @here banners (_view.html.j2).
TEST(offline_note_under_the_tabs_above_the_banners) {
  using S = TopSlot;
  CHECK(top_slots(true, true, true) == (std::vector<S>{S::TABS, S::OFFLINE, S::ALERTS}));
  CHECK(top_slots(true, false, true) == (std::vector<S>{S::TABS, S::ALERTS}));
  CHECK(top_slots(true, true, false) == (std::vector<S>{S::TABS, S::OFFLINE}));
  // A person's page and Overview woken at night have no tabs: the note leads.
  CHECK(top_slots(false, true, false) == (std::vector<S>{S::OFFLINE}));
  CHECK(top_slots(false, false, false).empty());
}

TEST(warning_sign_is_a_triangle_with_a_bang) {
  for (int size : {16, 20, 30}) {
    const auto strokes = warning_sign(size);
    CHECK_EQ(strokes.size(), size_t{5});
    for (const auto &s : strokes) {
      CHECK(s.x1 >= 0 && s.x1 <= size && s.x2 >= 0 && s.x2 <= size);
      CHECK(s.y1 >= 0 && s.y1 <= size && s.y2 >= 0 && s.y2 <= size);
    }
    // Closed: each side ends where the next begins; apex centred on top.
    for (int k = 0; k < 3; k++) {
      CHECK_EQ(strokes[k].x2, strokes[(k + 1) % 3].x1);
      CHECK_EQ(strokes[k].y2, strokes[(k + 1) % 3].y1);
    }
    CHECK_EQ(strokes[0].x1, size / 2);
    CHECK(strokes[0].y1 < size / 4);
    // The bar sits above the dot, both on the centre line.
    CHECK_EQ(strokes[3].x1, size / 2);
    CHECK_EQ(strokes[4].x1, size / 2);
    CHECK(strokes[3].y2 < strokes[4].y1);
  }
}

TEST(refresh_arrow_turns_clockwise_open_at_the_top) {
  for (int size : {20, 24, 36}) {
    const RefreshArrow a = refresh_arrow(size);
    // The gap is the top: 240°..300° (270° = 12 o'clock) is not drawn.
    CHECK_EQ(a.start, 300);
    CHECK_EQ(a.end, 240);
    // The chevron starts at the arc's end, up and left of the centre …
    for (const auto &h : a.head) {
      CHECK(h.x1 < a.cx);
      CHECK(h.y1 < a.cy);
      CHECK(h.x2 >= 0 && h.x2 <= size && h.y2 >= 0 && h.y2 <= size);
    }
    const int dx = (a.head[0].x2 - a.head[0].x1) + (a.head[1].x2 - a.head[1].x1);
    // … and points the way it turns (to the right at the top): its arms
    // trail back to the left of the tip.
    CHECK(dx < 0);
  }
}
