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
