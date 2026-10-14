// SPDX-License-Identifier: MPL-2.0
//
// Pure helpers (layout maths, URLs, small parsing) shared by the UI and the
// network code. No ESPHome / LVGL includes: covered by the host tests.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "model.h"

namespace esphome::socialhome_display {

/// Port the Social Home dashboard (and so the display API) listens on by default.
static constexpr int DEFAULT_PORT = 8100;

/// The household PIN's length — the tablet keypad's minlength / maxlength.
static constexpr size_t PIN_MIN_DIGITS = 4;
static constexpr size_t PIN_MAX_DIGITS = 6;

/// `pin` after a PIN-pad key, as the tablet's keypad: a digit appends (up
/// to PIN_MAX_DIGITS), "del" drops the last one, anything else changes
/// nothing.
std::string pin_after_key(const std::string &pin, const std::string &key);

/// ✓ may send `pin`: it has at least PIN_MIN_DIGITS digits.
bool pin_ready(const std::string &pin);

/// Turn what a person typed into a base URL: "192.168.1.5" →
/// "http://192.168.1.5:8100". Keeps an explicit scheme / port, drops a
/// trailing slash and any path. Empty when nothing usable is left.
std::string normalize_server_url(const std::string &typed);

/// The host of a normalised base URL, for messages: "http://192.168.1.5:8100"
/// → "192.168.1.5", "http://[fe80::1]:81" → "[fe80::1]". The input as is when
/// it has no scheme.
std::string url_host(const std::string &url);

/// `text` as the inside of a JSON string: quotes, backslashes and control
/// characters escaped, UTF-8 passed through.
std::string json_escape(const std::string &text);

/// Body of POST /pair (code pairing): {"model": …, "firmware": …}.
std::string pair_request_body(const std::string &model, const std::string &firmware);

/// Body of POST /pair/claim (Connect from Home Assistant):
/// {"claim": …, "model": …, "firmware": …}.
std::string claim_request_body(const std::string &claim, const std::string &model, const std::string &firmware);

/// The "Social Home status" text sensor Home Assistant sees: "unpaired",
/// "pairing", "paired", "locked" or "unreachable".
/// `paired`: a token is stored; `pairing`: a code is shown or a connect
/// request is running; `locked`: the privacy lock (423); `unreachable`: the
/// paired server doesn't answer (or speaks an API this firmware can't).
const char *ha_status(bool paired, bool pairing, bool locked, bool unreachable);

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

/// `fg` drawn at `alpha` (0 = invisible, 255 = opaque) over `bg`, both
/// 0xRRGGBB: what LVGL's opacity (and CSS `opacity`) composites to.
uint32_t blend_rgb(uint32_t fg, uint32_t bg, uint8_t alpha);

/// WCAG contrast ratio of two 0xRRGGBB colours (1 … 21) — Social Home's
/// contrast.contrast_ratio.
double contrast_ratio(uint32_t a, uint32_t b);

/// Black or white, whichever reads better on `fill` — Social Home's
/// contrast.ink_on (the rule behind every lane's `ink`).
uint32_t ink_on(uint32_t fill);

/// How a bar on the Today timeline is painted.
struct BarStyle {
  uint32_t fill;   ///< the bar as drawn, already composited over the card
  uint32_t ink;    ///< its label colour, picked against that fill
  uint8_t alpha;   ///< how much of `color` is in `fill` (255: all of it)
};

/// A timeline bar in the lane's `color` on the card `surface`. School is
/// toned down (70 %, the panel's stand-in for the browser's stripes) and a
/// past bar is faded to half, as the browser's `.dash-past`. The fade goes
/// into the fill — not over the label — and a faded bar's label is picked
/// against the fill actually drawn (ink_on), so a past bar on a dark card
/// keeps a light label instead of the lane's dark ink faded into the dark.
/// An unfaded bar keeps the lane's own `ink` (when the server sent one).
BarStyle bar_style(uint32_t color, bool has_ink, uint32_t ink, uint32_t surface, bool school, bool past);

/// How much of a picture (emoji, face) is left when it recedes, as the
/// browser's `opacity` — which only ever sits on pictures, never on words.
static constexpr uint8_t OPA_PAST_ROW_EMOJI = 127;     ///< .dash-row.dash-past emoji, 50 %
static constexpr uint8_t OPA_PAST_PICTURE = 102;       ///< .dash-pv-past / past day part, 40 %
static constexpr uint8_t OPA_CANCELLED_PICTURE = 153;  ///< .dash-pv-cancelled, 60 %
static constexpr uint8_t OPA_STALE_FACE = 127;         ///< .dash-mp-stale face, 50 %
static constexpr uint8_t OPA_FULL = 255;

/// How a day-list row (the browser's `.dash-row`) is painted, on a card
/// whose background is `p.surface`.
struct RowStyle {
  bool fill;            ///< the row draws its own background (else the card shows)
  uint32_t fill_color;  ///< that background, when `fill`
  uint32_t strip;       ///< the colour strip on the start side, as drawn
  uint32_t time_ink;
  uint32_t title_ink;
  uint32_t meta_ink;
  uint8_t emoji_opa;    ///< the title's emoji
  bool strike;          ///< the title is struck through (cancelled)
};

/// A past row recedes into the card as on the tablet (Social Home #933): no
/// fill of its own, the colour strip faded to half over the card (the same
/// blend as a past timeline bar), the emoji faded — and every word in the
/// palette's muted ink at full strength, which keeps 4.5:1 on the card in
/// light, dark and night. A cancelled row is struck through in muted ink;
/// nothing on it fades. The current row sits on the card colour.
RowStyle row_style(const AgendaRow &row, const Palette &p);

/// How a picture-view tile (the browser's `.dash-pv-tile`) is painted.
struct TileStyle {
  uint32_t caption_ink;  ///< always muted, never faded
  uint32_t strip;        ///< the colour strip along the bottom
  uint8_t emoji_opa;     ///< only the picture fades (past 40 %, cancelled 60 %)
  bool strike;           ///< the caption is struck through (cancelled)
};

TileStyle tile_style(const Tile &tile, const Palette &p);

/// CSS `filter: grayscale(1)` of one 0xRRGGBB colour: the Rec. 709 luma of
/// the sRGB values on every channel, as the browser computes it.
uint32_t grayscale(uint32_t rgb);

/// How a picture of live data fades while the panel is offline: the
/// browser's `.dash-stale [data-live] [aria-hidden="true"]`, 45 %.
static constexpr uint8_t OPA_STALE_PICTURE = 115;

/// How "live" data is painted — what the browser marks `data-live`: where
/// people are (Overview's who's home, a reader's whereabouts, a picture
/// view's who's home), the timeline's "now" mark and line, the current row,
/// a reader's NOW card and a picture view's NOW card and current tile.
///
/// While the panel can't reach Social Home (`stale`, the browser's
/// `.dash-stale`) that data may no longer be true, so its colour drains:
/// words in the palette's muted ink at full strength (4.5:1 on the card and
/// the page in light, dark and night), accent marks and colour frames in
/// grey with the label picked against that grey, and only the pictures
/// (faces, emoji, the now line) fade. Not stale: everything as given.
struct LiveStyle {
  bool stale{false};
  uint32_t text{0};     ///< words otherwise drawn in `text`
  uint32_t accent{0};   ///< a mark otherwise drawn in `accent` (now pill / line, frames)
  uint32_t accent_ink{0};  ///< the label on such a mark
  uint8_t picture_opa{OPA_FULL};  ///< faces, emoji, the now line

  /// A frame or strip in `color` (a lane's or person's colour).
  uint32_t mark(uint32_t color) const { return stale ? grayscale(color) : color; }
};

LiveStyle live_style(const Palette &p, bool stale);

/// An analog clock hand: end point for `deg` (0 = 12 o'clock, clockwise)
/// on a dial centred at (cx, cy) with hand length `len`.
void hand_end(float deg, int cx, int cy, int len, int *x, int *y);

/// LVGL arc angles (0 = 3 o'clock, clockwise, degrees) of a clock wedge that
/// starts at `minute_deg` and covers `minutes` minutes (60 = the whole dial,
/// returned as 0..360).
void wedge_angles(float minute_deg, int minutes, int *start, int *end);

}  // namespace esphome::socialhome_display
