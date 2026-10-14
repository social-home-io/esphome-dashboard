// SPDX-License-Identifier: MPL-2.0
//
// Pure helpers (layout maths, URLs, small parsing) shared by the UI and the
// network code. No ESPHome / LVGL includes: covered by the host tests.
#pragma once

#include <cstdint>
#include <functional>
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

/// What the panel is doing, as far as its privacy lock goes.
enum class PanelPhase : uint8_t {
  STARTING,  ///< booting: not yet known whether the server has it locked
  SETUP,     ///< no Wi-Fi, no server, pairing, connecting, update needed
  RUNNING,   ///< the dashboard
  LOCKED,    ///< the lock screen (423)
};

/// The "Privacy lock" entity's state for Home Assistant. Mirrors the
/// lock states ESPHome's lock component has (lock::LockState); UNCHANGED:
/// publish nothing (keep what Home Assistant has).
enum class PrivacyLock : uint8_t { UNCHANGED, LOCKED, UNLOCKED, JAMMED, LOCKING, UNLOCKING };

struct PrivacyLockInputs {
  PanelPhase phase{PanelPhase::STARTING};
  bool known{false};        ///< RUNNING: the server has answered since (else just started)
  bool offline{false};      ///< the last poll didn't reach the server
  bool locking{false};      ///< POST /lock in flight
  bool unlocking{false};    ///< POST /unlock (PIN) or /unlock/ha in flight
};

/// The privacy lock as Home Assistant should see it, derived from what
/// the panel is doing — so a lock or unlock from the PIN pad, by an admin
/// in Social Home (seen at the next poll) or from Home Assistant all show.
///
///   phase     in flight       → state
///   LOCKED    unlocking       → UNLOCKING
///   LOCKED    —               → LOCKED (offline too: the screen stays locked)
///   RUNNING   locking         → LOCKING
///   RUNNING   offline         → JAMMED
///   RUNNING   no answer yet   → UNCHANGED
///   RUNNING   —               → UNLOCKED
///   STARTING  —               → UNCHANGED
///   SETUP     —               → JAMMED
///
/// JAMMED is Home Assistant's "the lock can't reach the state asked for":
/// unpaired, pairing, an update needed or the server out of reach, a lock
/// from Home Assistant can't be carried out. Keeping the last state instead
/// would show a panel that can't lock as "unlocked" and lockable.
PrivacyLock privacy_lock_state(const PrivacyLockInputs &in);

/// Where a Connect from Home Assistant that didn't pair goes back to (a
/// refused sealed value, or a claim the server turned down): the address
/// screen when the panel wasn't paired (SETUP), else the old pairing —
/// its lock screen when it was locked (the lock is still the server's),
/// so a junk or failing connect can't lift the privacy lock, otherwise
/// its dashboard. A claim that pairs starts afresh on the new server.
PanelPhase phase_after_failed_claim(bool had_pairing, bool was_locked);

/// Wrap-safe "a wait set to end at `until` (millis) is still running";
/// 0 means no wait.
bool waiting_until(uint32_t now, uint32_t until);

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
  /// `.dash-mp-tab`: calc(100dvh - 104px), at least 420 px — taller than the
  /// screen under the tabs and banners, so the board scrolls as the
  /// browser's page does. The box's 1 px border is outside the picture.
  int map_height() const;
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

/// `color` moved `weight` (0 … 1) of the way to `toward`, channel by
/// channel in sRGB, rounded half to even — Social Home's contrast.mix_srgb.
uint32_t mix_srgb(uint32_t color, uint32_t toward, double weight);

/// A person's Map frame (the browser's `--kf`, Social Home's
/// contrast.frame_color): their colour nudged toward black on the light
/// bubble (#ffffff) or white on the dark one (#3d3a35) in 5 % steps until
/// it reaches 3:1 against that bubble. The display API sends only the
/// colour, so the panel derives the frame as the server does.
uint32_t map_frame_color(uint32_t color, bool dark);

/// How a bar on the Today timeline is painted.
struct BarStyle {
  uint32_t fill;       ///< the bar as drawn, already composited over the card
  uint32_t ink;        ///< its label colour, picked against that fill
  uint8_t alpha;       ///< how much of `color` is in `fill` (255: all of it)
  uint8_t stripe_opa;  ///< white school stripes over the fill (0: none)
};

/// A timeline bar in the lane's `color` on the card `surface`, as the
/// browser's `.dash-block`: a past bar is faded to half (`.dash-past`, the
/// server's --kp). The fade goes into the fill — not over the label — and a
/// faded bar's label is picked against the fill actually drawn (ink_on), so
/// a past bar on a dark card keeps a light label instead of the lane's dark
/// ink faded into the dark. An unfaded bar keeps the lane's own `ink` (when
/// the server sent one). School is hatched (`.dash-block-school`): white
/// stripes at 16 %, 8 % on a past bar — `stripe_opa`, drawn with
/// stripe_coverage.
BarStyle bar_style(uint32_t color, bool has_ink, uint32_t ink, uint32_t surface, bool school, bool past);

/// `.dash-block-school`'s stripes: repeating-linear-gradient(135deg, white
/// 0 8px, transparent 8px 16px), from the bar's top-left corner. Along x a
/// period is 16·√2 px, so a tile `stripe_tile_width(scale)` wide holds
/// eight of them (16.0 px each across the stripes) and repeats seamlessly.
int stripe_tile_width(float scale);

/// How much of each pixel (0 … 255, row-major) of a `w` × `h` tile the
/// stripes cover: anti-aliased, eight periods across `w`.
std::vector<uint8_t> stripe_coverage(int w, int h);

/// 0xRRGGBB as LVGL's RGB565 (the panel's colour depth), channels cut as
/// lv_color_to_u16 does.
uint16_t rgb565(uint32_t rgb);

/// A school bar's stripe tile, opaque: `fill` with white at `opa` ×
/// `coverage` over it, composited at full precision and then cut to RGB565.
/// Blending a translucent white tile on the 16-bit screen instead rounds
/// the 8 % stripes of a past bar away on light fills.
std::vector<uint16_t> stripe_tile_rgb565(uint32_t fill, uint8_t opa, const std::vector<uint8_t> &coverage);

/// Today's timeline lanes (`.dash-lane` / `.dash-track`), design px: a lane
/// is its 1 px top border, 6 px padding, the track and 6 px again; the
/// track is 44 px for one row, 48 px more per extra row (`.dash-rows-N`),
/// a bar 42 px high at the top of its row. A person's all-day items sit
/// above the track as chips (`.dash-allday`), 4 px over it; the name and
/// face stay centred on the lane.
struct LaneGeometry {
  static constexpr int BORDER = 1;
  static constexpr int PAD = 6;
  static constexpr int ROW_PITCH = 48;
  static constexpr int BAR_HEIGHT = 42;
  static constexpr int TICKS_HEIGHT = 28;  ///< `.dash-ticks`: 1.75rem
  static constexpr int ALLDAY_GAP = 4;     ///< `.dash-allday`: margin-block 0 4px
  static constexpr int CHIP_PAD = 10;      ///< `.dash-chip`: padding-inline 10px
  static constexpr int CHIP_GAP_X = 6;     ///< `.dash-allday`: gap 4px 6px
  static constexpr int CHIP_GAP_Y = 4;
  static constexpr float CHIP_LINE = 21.6f;  ///< `.dash-chip`: 16 px text, line-height 1.35
  static int track_height(int rows);
  static int lane_height(int rows);
  /// Screen px under the lane's top border: where the track starts, under
  /// `all_day_h` screen px of chips (0: none).
  static int track_top(int all_day_h, const Metrics &m);
  /// The lane's height in screen px, chips included.
  static int lane_height(int rows, int all_day_h, const Metrics &m);
};

/// A CSS line box on an LVGL label, whose line is the font's own height
/// (ascent + descent): the browser puts `css_line - font_line` of leading
/// between lines and half of it above the first and below the last — so
/// the glyphs land where the browser draws them. Negative when the CSS line
/// is tighter than the font (the clock's line-height 1): the box then
/// clips into the font's empty ascent and descent.
struct LineBox {
  int above;
  int below;
  int between;
};
LineBox line_box(int font_line, int css_line);

/// Where an inline box's top sits when it shares a baseline with a CSS line
/// (`align-items: baseline`, or an inline span in a block): the line is
/// `css_line` px of a font `font_line` px tall with `font_base` px below its
/// baseline (an LVGL font's `base_line`); the inner box is its own
/// `inner_css` px line of an `inner_line` / `inner_base` font. The CSS
/// lines stay fractional (29.7 px), as the browser lays them out, and the
/// result is rounded once. Negative when the inner box reaches above.
int inline_top(float css_line, int font_line, int font_base, float inner_css, int inner_line, int inner_base);

/// The browser's width of a text the panel measures `panel_width` px wide
/// (`glyphs` characters of a font whose LVGL line is `font_line` px). The
/// panel's fonts advance every glyph a whole, hinted pixel (ESPHome stores
/// integer advances); the browser lays Roboto out at fractional advances,
/// ~1.6 % of the font size (1.37 % of Roboto's 1.17 em line) wider a glyph:
/// at 22 px "08:15–09:00" is 120 px on the panel, 124 in Chromium. Where a layout decision hangs on a width — does a row's title
/// fit beside its time? — the browser's width decides it.
int css_width(int panel_width, int glyphs, int font_line);

/// CSS `n ch` for a font whose "0" is `zero_px` wide on the panel.
int css_ch(float n, int zero_px, int font_line);

/// Characters (UTF-8 code points) in `text`.
int utf8_glyphs(const std::string &text);

/// "…" (U+2026): what the browser's `text-overflow: ellipsis` ends a cut text with.
static constexpr const char *ELLIPSIS = "\xE2\x80\xA6";

/// `text` cut to `max_w` px as CSS `text-overflow: ellipsis` cuts it: the
/// longest prefix of whole characters that fits with "…" after it, or `text`
/// itself when it fits. `width` measures a string in the label's font (LVGL
/// fills its own dots mode with three full stops — never the browser's "…").
std::string ellipsize(const std::string &text, int max_w, const std::function<int(const std::string &)> &width);

/// The browser's Home timeline bar (.dash-block): just its icon, centred,
/// when the server marks it narrow (under 10 % of the day) or the bar's
/// content box — `bar_w` less `pad` either side — is `limit` px or less
/// (`@container dash-block (max-width: 72px)`); else its title (a school
/// bar's with 🎒 before it — and only a school bar's).
bool bar_icon_only(bool narrow, int bar_w, int pad, int limit);

/// Where LVGL draws a label's strikethrough (lv_draw_label.c): `y` from the
/// top of the line, `h` thick — for a stroke drawn beside the label, over
/// an emoji the browser's `line-through` crosses with the words.
struct StrikeLine {
  int y;
  int h;
};
StrikeLine strike_line(int line_height, int base_line, int underline_thickness);

/// `.dash-row` (flex-wrap): whether the time (`time_w`) and the main part
/// (at least `main_min`) share one line `room` px wide, `gap` apart.
bool row_fits_beside(int room, int time_w, int gap, int main_min);

/// Whether the rail names the Home tile under the house: always, except on
/// a picture-view person's page, where the tablet hides the word
/// (`_rail.html.j2`: `dash-visually-hidden` while `picture`) and the tile is
/// only the house — 88 px, not 120.
bool rail_names_home(BoardKind kind, const std::string &person_kind);

/// Whether a new lock-screen answer changes anything the lock screen draws.
/// The clock's time ticks by itself (tick_clock_), so a poll that only moves
/// the time must not rebuild the screen: a rebuild replaces the PIN button
/// under a finger that is pressing it, and that tap is lost.
bool lock_screen_changed(const LockScreen &old, const LockScreen &now);

/// `.dash-top-side` (NEXT and the messages): flex 1 1 20rem, at most
/// 40rem, at the end of the Overview's top row, 32 px past the clock
/// column, which keeps at least 20rem (the date). Screen px, for a
/// `main_width` px column.
int top_side_width(int main_width, const Metrics &m);

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

/// How much colour drains from a picture (0 = none, 255 = all), the
/// browser's `filter: grayscale(…)`: all of it for live data offline
/// (`.dash-stale [data-live]`), 80 % for a map spot over an hour old
/// (`.dash-mp-stale`).
static constexpr uint8_t GREY_FULL = 255;
static constexpr uint8_t GREY_STALE_SPOT = 204;

/// CSS `filter: grayscale(1)` of one 0xRRGGBB colour: the Rec. 709 luma of
/// the sRGB values on every channel, as the browser computes it.
uint32_t grayscale(uint32_t rgb);
/// CSS `filter: grayscale(amount / 255)`: each channel moved `amount` of
/// the way to the luma (grey_channel).
uint32_t grayscale(uint32_t rgb, uint8_t amount);

/// How a note card is painted — the browser's `.dash-sticky` (Overview)
/// and `.dash-note` (Notes tab): the fill and ink Social Home softened for
/// the scheme (`light` by day, `dark` in dark mode and at night, as its
/// ColorBook), a colourless note the CSS's own yellow; in dark mode and at
/// night no shadow but a 1 px outline in the border colour.
struct NoteLook {
  uint32_t bg;
  uint32_t ink;
  bool shadow;   ///< light: the drop shadow
  bool outline;  ///< dark / night: 1 px `p.border` outline instead
};
NoteLook note_look(const Note &note, bool dark);

/// How the picture view's analog clock is painted — the browser's
/// `.dash-pv-dial` and friends, all from the scheme's palette: the face in
/// the card colour ringed in the text colour, major ticks and both hands
/// and the hub in the text colour, minor ticks muted, and the time-timer
/// wedge at half opacity.
struct DialLook {
  uint32_t face;
  uint32_t ring;
  uint32_t tick_major;
  uint32_t tick_minor;
  uint32_t hands;  ///< both hands and the hub
  uint8_t wedge_opa;
};
DialLook dial_look(const Palette &p);

/// The age line under a "Who's where" row (`.dash-mp-row-age`).
struct SpotAge {
  bool shown;        ///< hidden offline ("5 min ago" no longer true, data-rel)
  bool clock;        ///< a ⏱ leads it (stale)
  bool bold;         ///< stale ages are bold
  std::string text;  ///< "5 min ago", or the server's "last seen 1 h ago"
};
SpotAge spot_age(const MapSpot &spot, bool offline);

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

/// What leads a board's main column, top to bottom — the browser's
/// _view.html.j2: the tab bar, then one alert slot (the offline note, then
/// Home's @here banners), then the board itself. Boards without tabs (a
/// person's page, Overview woken at night) start with the alert slot.
enum class TopSlot : uint8_t { TABS, OFFLINE, ALERTS };
std::vector<TopSlot> top_slots(bool tabs, bool offline, bool alerts);

/// One stroke of a drawn icon, in px inside its size×size box.
struct Stroke {
  int x1, y1, x2, y2;
};

/// The offline note's ⚠ (`.dash-offline`): an outlined triangle with a "!"
/// — Roboto carries no ⚠, so the panel draws it. Triangle sides, the bar,
/// then the dot.
std::vector<Stroke> warning_sign(int size);

/// The offline note's ↻ (the retry button): a clockwise arc open at the top
/// and a chevron at its end pointing the way it turns (Roboto has no ↻).
/// Arc angles are LVGL's (0 = 3 o'clock, clockwise, degrees).
struct RefreshArrow {
  int cx, cy, r;
  int start, end;
  Stroke head[2];  ///< both start at the arc's end point
};
RefreshArrow refresh_arrow(int size);

}  // namespace esphome::socialhome_display
