// SPDX-License-Identifier: MPL-2.0
//
// Pure helpers (layout maths, URLs, small parsing) shared by the UI and the
// network code. No ESPHome / LVGL includes: covered by the host tests.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace esphome::socialhome_display {

/// Port the Social Home dashboard (and so the display API) listens on by default.
static constexpr int DEFAULT_PORT = 8100;

/// Turn what a person typed into a base URL: "192.168.1.5" →
/// "http://192.168.1.5:8100". Keeps an explicit scheme / port, drops a
/// trailing slash and any path. Empty when nothing usable is left.
std::string normalize_server_url(const std::string &typed);

/// Seconds from a Retry-After header (delta-seconds only), clamped to
/// [1, 3600]; `fallback` when missing or unparsable.
uint32_t parse_retry_after(const std::string &value, uint32_t fallback);

/// Upper-case for the small caps labels ("NEXT", "MESSAGES"): ASCII and the
/// Latin-1 letters (é → É, ä → Ä) the five dashboard languages use.
std::string upper(const std::string &text);

/// "14:32" → true: a 24 h HH:MM the panel can keep ticking itself.
bool is_hhmm(const std::string &text);

/// HH:MM for `epoch` (UTC seconds) shifted by `offset_minutes`.
std::string format_hhmm(int64_t epoch, int offset_minutes);

/// The text without its emoji: the panel fonts carry no emoji glyphs, and
/// a missing glyph would draw as a box. Spaces left behind are collapsed.
std::string strip_emoji(const std::string &text);

/// The emoji in `text` as Twemoji codes ("1f370", "1f468-200d-1f373"), in
/// order, at most `max` — drawn as pictures next to the stripped text.
std::vector<std::string> emoji_codes(const std::string &text, size_t max);

/// One piece of a text laid out with its emoji in place.
struct TextRun {
  enum Kind : uint8_t {
    TEXT,   ///< words (no emoji), drawn with the font
    EMOJI,  ///< one emoji as its Twemoji code, drawn as a picture
    BREAK,  ///< a line break in the text
  };
  Kind kind{TEXT};
  std::string value;
  /// EMOJI only: a space followed it (the gap is drawn, not typed).
  bool space_after{false};

  bool operator==(const TextRun &other) const {
    return kind == other.kind && value == other.value && space_after == other.space_after;
  }
};

/// Split `text` into text and emoji runs in reading order, so the emoji stay
/// where they were typed ("Shoes on! 👟" → "Shoes on! ", 👟). Spaces are
/// collapsed as in strip_emoji(). With `words`, every TEXT run ends after
/// its space ("Shoes ", "on! "), so a wrapping row can break between words.
/// At most `max_emoji` emoji are kept; the rest are dropped.
std::vector<TextRun> text_runs(const std::string &text, bool words, size_t max_emoji = 16);

/// The firmware's image sizes the server offers (avatars / emoji).
int pick_avatar_size(int px);
int pick_emoji_size(int px);

/// When to (re)load the map picture (GET /map.png). Pure bookkeeping: the
/// caller does the request and reports how it went.
struct MapFetch {
  std::string held_path;   ///< path (with ?w=&h=&scheme=) of the picture held
  std::string held_etag;   ///< its ETag, sent back as If-None-Match
  std::string seen_board;  ///< the board's image.etag when it was last checked
  uint32_t retry_at{0};    ///< millis: no new attempt before this
  bool waiting{false};     ///< retry_at is set

  /// True when `path` should be requested now: another picture than the one
  /// held, or the board says the map changed — unless a retry is pending.
  bool due(const std::string &path, const std::string &board_etag, uint32_t now) const;
  /// The If-None-Match to send for `path` (empty: nothing to revalidate).
  std::string if_none_match(const std::string &path) const;
  /// 200: `path` is now held with `etag`.
  void loaded(const std::string &path, const std::string &etag, const std::string &board_etag);
  /// 304: the held picture is still current.
  void unchanged(const std::string &board_etag);
  /// No picture (503, 429, network): try again after `seconds`.
  void failed(uint32_t now, uint32_t seconds);
  void reset() { *this = MapFetch{}; }
};

/// Geometry of the 1280×800 browser dashboard, scaled to any panel.
///
/// `scale` is ui_scale × min(width / 1280, height / 800), so a bigger panel
/// keeps the same proportions and a 1280×800 panel matches the browser
/// pixel for pixel.
struct Metrics {
  int width{1280};
  int height{800};
  float scale{1.0f};

  static Metrics compute(int width, int height, float ui_scale);
  int px(float design_px) const;

  int rail_width() const { return px(120); }
  int content_width() const { return width - rail_width(); }
  int pad() const { return px(28); }
  int gap() const { return px(20); }
  int radius() const { return px(16); }
  int card_pad() const { return px(20); }
  int rail_avatar() const { return px(72); }
  int avatar_sm() const { return px(40); }
  int avatar_md() const { return px(56); }
  int emoji_sm() const { return px(24); }
  int emoji_md() const { return px(32); }
  int emoji_lg() const { return px(72); }
  int tab_height() const { return px(64); }
  /// The Map tab's picture area (the browser's map frame) and its list.
  int map_list_width() const { return px(336); }
  int map_height() const { return px(560); }
  int tap_min() const { return px(48); }

  /// Columns of at least `min_col` design px that fit in `avail` px.
  int columns(int avail, int min_col) const;
  /// A percentage (0..100) of `total` px.
  static int pct(int percent, int total);
};

/// An analog clock hand: end point for `deg` (0 = 12 o'clock, clockwise)
/// on a dial centred at (cx, cy) with hand length `len`.
void hand_end(float deg, int cx, int cy, int len, int *x, int *y);

/// LVGL arc angles (0 = 3 o'clock, clockwise, degrees) of a clock wedge that
/// starts at `minute_deg` and covers `minutes` minutes (60 = the whole dial,
/// returned as 0..360).
void wedge_angles(float minute_deg, int minutes, int *start, int *end);

}  // namespace esphome::socialhome_display
