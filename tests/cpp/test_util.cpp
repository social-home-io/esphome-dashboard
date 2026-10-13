// SPDX-License-Identifier: MPL-2.0
// Layout maths and URL helpers.
#include "check.h"
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
