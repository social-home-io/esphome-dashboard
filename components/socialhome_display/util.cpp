// SPDX-License-Identifier: MPL-2.0
#include "util.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "png.h"

namespace esphome::socialhome_display {

std::string normalize_server_url(const std::string &typed) {
  std::string text;
  for (char ch : typed)
    if (!isspace(static_cast<unsigned char>(ch)))
      text.push_back(ch);
  if (text.empty())
    return {};
  std::string scheme = "http";
  size_t sep = text.find("://");
  if (sep != std::string::npos) {
    scheme = text.substr(0, sep);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char ch) { return tolower(ch); });
    text = text.substr(sep + 3);
    if (scheme != "http" && scheme != "https")
      return {};
  }
  size_t slash = text.find('/');
  if (slash != std::string::npos)
    text = text.substr(0, slash);
  if (text.empty())
    return {};
  // "[fe80::1]:8100" keeps its brackets; a bare host gets the default port.
  bool has_port;
  if (text[0] == '[') {
    size_t close = text.find(']');
    if (close == std::string::npos)
      return {};
    has_port = close + 1 < text.size() && text[close + 1] == ':';
  } else {
    has_port = text.find(':') != std::string::npos;
  }
  if (has_port) {
    size_t colon = text.rfind(':');
    std::string port = text.substr(colon + 1);
    if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), ::isdigit))
      return {};
    long value = strtol(port.c_str(), nullptr, 10);
    if (value < 1 || value > 65535)
      return {};
    if (colon == 0)
      return {};
  } else if (scheme == "http") {
    text += ":" + std::to_string(DEFAULT_PORT);
  }
  return scheme + "://" + text;
}

std::string url_host(const std::string &url) {
  size_t sep = url.find("://");
  if (sep == std::string::npos)
    return url;
  std::string host = url.substr(sep + 3);
  size_t slash = host.find('/');
  if (slash != std::string::npos)
    host = host.substr(0, slash);
  size_t colon = host.rfind(':');
  size_t close = host.rfind(']');
  if (colon != std::string::npos && (close == std::string::npos || colon > close))
    host = host.substr(0, colon);
  return host;
}

std::string json_escape(const std::string &text) {
  std::string out;
  out.reserve(text.size() + 2);
  for (char ch : text) {
    auto u = static_cast<unsigned char>(ch);
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (u < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", u);
          out += buf;
        } else {
          out.push_back(ch);
        }
    }
  }
  return out;
}

std::string pair_request_body(const std::string &model, const std::string &firmware) {
  return "{\"model\": \"" + json_escape(model) + "\", \"firmware\": \"" + json_escape(firmware) + "\"}";
}

std::string claim_request_body(const std::string &claim, const std::string &model, const std::string &firmware) {
  return "{\"claim\": \"" + json_escape(claim) + "\", \"model\": \"" + json_escape(model) + "\", \"firmware\": \"" +
         json_escape(firmware) + "\"}";
}

const char *ha_status(bool paired, bool pairing, bool locked, bool unreachable) {
  if (pairing)
    return "pairing";
  if (!paired)
    return "unpaired";
  if (locked)
    return "locked";
  if (unreachable)
    return "unreachable";
  return "paired";
}

uint32_t parse_retry_after(const std::string &value, uint32_t fallback) {
  if (value.empty() || !std::all_of(value.begin(), value.end(), ::isdigit) || value.size() > 9)
    return fallback;
  long seconds = strtol(value.c_str(), nullptr, 10);
  return static_cast<uint32_t>(std::clamp(seconds, 1L, 3600L));
}

std::string upper(const std::string &text) {
  std::string out = text;
  for (size_t k = 0; k < out.size(); k++) {
    auto ch = static_cast<unsigned char>(out[k]);
    if (ch >= 'a' && ch <= 'z') {
      out[k] = static_cast<char>(ch - 32);
    } else if (ch == 0xC3 && k + 1 < out.size()) {
      auto next = static_cast<unsigned char>(out[k + 1]);
      // U+00E0..U+00FE (à..þ) except U+00F7 (÷) → U+00C0..U+00DE.
      if (next >= 0xA0 && next <= 0xBE && next != 0xB7)
        out[k + 1] = static_cast<char>(next - 0x20);
      k++;
    }
  }
  return out;
}

bool is_hhmm(const std::string &text) {
  return text.size() == 5 && isdigit(static_cast<unsigned char>(text[0])) &&
         isdigit(static_cast<unsigned char>(text[1])) && text[2] == ':' &&
         isdigit(static_cast<unsigned char>(text[3])) && isdigit(static_cast<unsigned char>(text[4]));
}

std::string format_hhmm(int64_t epoch, int offset_minutes) {
  int64_t minutes = epoch / 60 + offset_minutes;
  int64_t day = ((minutes % 1440) + 1440) % 1440;
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", static_cast<int>(day / 60), static_cast<int>(day % 60));
  return buf;
}

namespace {

/// Decode one UTF-8 code point at `at`; advances `at`. Bad bytes → U+FFFD.
uint32_t next_cp(const std::string &text, size_t &at) {
  auto b0 = static_cast<unsigned char>(text[at]);
  int extra = b0 < 0x80 ? 0 : (b0 >> 5) == 0x6 ? 1 : (b0 >> 4) == 0xE ? 2 : (b0 >> 3) == 0x1E ? 3 : -1;
  if (extra < 0 || at + extra >= text.size() + (extra == 0 ? 1 : 0)) {
    at++;
    return 0xFFFD;
  }
  uint32_t cp = extra == 0 ? b0 : b0 & (0x3F >> extra);
  for (int k = 1; k <= extra; k++) {
    auto b = static_cast<unsigned char>(text[at + k]);
    if ((b & 0xC0) != 0x80) {
      at++;
      return 0xFFFD;
    }
    cp = (cp << 6) | (b & 0x3F);
  }
  at += extra + 1;
  return cp;
}

bool is_emoji(uint32_t cp) {
  return (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2300 && cp <= 0x23FF) ||
         (cp >= 0x2B00 && cp <= 0x2BFF) || (cp >= 0xE0020 && cp <= 0xE007F) || cp == 0x3030 || cp == 0x303D;
}

/// Joiners and modifiers that belong to the emoji before them.
bool is_emoji_glue(uint32_t cp) { return cp == 0xFE0F || cp == 0x200D || cp == 0x20E3 || cp == 0xFE0E; }

}  // namespace

std::string strip_emoji(const std::string &text) {
  std::string out;
  size_t at = 0;
  while (at < text.size()) {
    size_t start = at;
    uint32_t cp = next_cp(text, at);
    if (is_emoji(cp) || is_emoji_glue(cp))
      continue;
    if (cp == ' ' && (out.empty() || out.back() == ' '))
      continue;
    out.append(text, start, at - start);
  }
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  return out;
}

std::vector<std::string> emoji_codes(const std::string &text, size_t max) {
  std::vector<std::string> out;
  std::string current;
  bool joining = false;
  size_t at = 0;
  auto flush = [&]() {
    if (!current.empty() && out.size() < max)
      out.push_back(current);
    current.clear();
  };
  while (at < text.size()) {
    uint32_t cp = next_cp(text, at);
    if (cp == 0xFE0F || cp == 0xFE0E)
      continue;  // Twemoji names leave the variation selector out
    if (cp == 0x200D && !current.empty()) {
      current += "-200d";
      joining = true;
      continue;
    }
    bool regional = cp >= 0x1F1E6 && cp <= 0x1F1FF;
    bool skin = cp >= 0x1F3FB && cp <= 0x1F3FF;
    if (is_emoji(cp)) {
      char hex[12];
      snprintf(hex, sizeof(hex), "%x", static_cast<unsigned>(cp));
      bool pair = regional && !current.empty() && current.find('-') == std::string::npos &&
                  current.size() == 5 && current.rfind("1f1", 0) == 0;
      if (!joining && !skin && !pair)
        flush();
      current += current.empty() ? hex : std::string("-") + hex;
      joining = false;
    } else {
      flush();
      joining = false;
    }
  }
  flush();
  return out;
}

std::vector<TextRun> text_runs(const std::string &text, bool words, size_t max_emoji) {
  std::vector<TextRun> out;
  std::string txt;   // words collected since the last emoji / break
  std::string code;  // the emoji being collected ("1f468-200d-1f373")
  size_t emoji = 0;
  bool joining = false;
  auto flush_text = [&]() {
    if (txt.empty())
      return;
    if (!words) {
      out.push_back(TextRun{TextRun::TEXT, txt, false});
    } else {
      size_t start = 0;
      while (start < txt.size()) {
        size_t space = txt.find(' ', start);
        size_t end = space == std::string::npos ? txt.size() : space + 1;
        out.push_back(TextRun{TextRun::TEXT, txt.substr(start, end - start), false});
        start = end;
      }
    }
    txt.clear();
  };
  auto flush_emoji = [&]() {
    if (code.empty())
      return;
    if (emoji < max_emoji) {
      flush_text();
      out.push_back(TextRun{TextRun::EMOJI, code, false});
      emoji++;
    }
    code.clear();
  };
  size_t at = 0;
  while (at < text.size()) {
    const size_t start = at;
    uint32_t cp = next_cp(text, at);
    if (cp == 0xFE0F || cp == 0xFE0E)
      continue;  // Twemoji names leave the variation selector out
    if (cp == 0x200D && !code.empty()) {
      code += "-200d";
      joining = true;
      continue;
    }
    bool regional = cp >= 0x1F1E6 && cp <= 0x1F1FF;
    bool skin = cp >= 0x1F3FB && cp <= 0x1F3FF;
    if (is_emoji(cp)) {
      char hex[12];
      snprintf(hex, sizeof(hex), "%x", static_cast<unsigned>(cp));
      bool pair = regional && code.size() == 5 && code.rfind("1f1", 0) == 0;
      if (!joining && !skin && !pair)
        flush_emoji();
      code += code.empty() ? hex : std::string("-") + hex;
      joining = false;
      continue;
    }
    flush_emoji();
    joining = false;
    if (is_emoji_glue(cp) || cp == '\r')
      continue;
    if (cp == '\n') {
      while (!txt.empty() && txt.back() == ' ')
        txt.pop_back();
      flush_text();
      if (!out.empty() && out.back().kind != TextRun::BREAK) {
        out.back().space_after = false;
        out.push_back(TextRun{TextRun::BREAK, "", false});
      }
      continue;
    }
    if (cp == ' ' || cp == '\t') {
      if (!txt.empty()) {
        if (txt.back() != ' ')
          txt.push_back(' ');
      } else if (!out.empty() && out.back().kind == TextRun::EMOJI) {
        out.back().space_after = true;
      }
      continue;
    }
    txt.append(text, start, at - start);
  }
  flush_emoji();
  while (!txt.empty() && txt.back() == ' ')
    txt.pop_back();
  flush_text();
  while (!out.empty() && out.back().kind == TextRun::BREAK)
    out.pop_back();
  if (!out.empty() && out.back().kind == TextRun::EMOJI)
    out.back().space_after = false;
  if (!out.empty() && out.back().kind == TextRun::TEXT) {
    // A trailing space (split off by `words`) has nothing to separate.
    std::string &last = out.back().value;
    while (!last.empty() && last.back() == ' ')
      last.pop_back();
    if (last.empty())
      out.pop_back();
  }
  return out;
}

bool MapFetch::due(const std::string &path, const std::string &board_etag, uint32_t now) const {
  if (this->waiting && static_cast<int32_t>(now - this->retry_at) < 0)
    return false;
  return path != this->held_path || board_etag != this->seen_board || this->waiting;
}

std::string MapFetch::if_none_match(const std::string &path) const {
  return path == this->held_path ? this->held_etag : std::string();
}

void MapFetch::loaded(const std::string &path, const std::string &etag, const std::string &board_etag) {
  this->held_path = path;
  this->held_etag = etag;
  this->seen_board = board_etag;
  this->waiting = false;
}

void MapFetch::unchanged(const std::string &board_etag) {
  this->seen_board = board_etag;
  this->waiting = false;
}

void MapFetch::failed(uint32_t now, uint32_t seconds) {
  this->retry_at = now + seconds * 1000;
  this->waiting = true;
}

namespace {
int pick(int px, const int *sizes, int count) {
  for (int k = 0; k < count; k++)
    if (sizes[k] >= px)
      return sizes[k];
  return sizes[count - 1];
}
}  // namespace

int pick_avatar_size(int px) {
  static const int SIZES[] = {48, 64, 96, 128, 192};
  return pick(px, SIZES, 5);
}

int pick_emoji_size(int px) {
  static const int SIZES[] = {24, 32, 48, 64, 72, 96, 128};
  return pick(px, SIZES, 7);
}

Metrics Metrics::compute(int width, int height, float ui_scale) {
  Metrics m;
  m.width = width;
  m.height = height;
  float fit = std::min(width / 1280.0f, height / 800.0f);
  m.scale = std::max(0.25f, ui_scale * fit);
  return m;
}

int Metrics::px(float design_px) const { return static_cast<int>(std::lround(design_px * this->scale)); }

int Metrics::map_height() const { return std::max(this->px(420), this->height - this->px(104)); }

int Metrics::columns(int avail, int min_col) const {
  int col = std::max(1, this->px(min_col));
  int n = (avail + this->gap()) / (col + this->gap());
  return std::max(1, n);
}

int Metrics::pct(int percent, int total) {
  percent = std::clamp(percent, 0, 100);
  return static_cast<int>(std::lround(percent * total / 100.0));
}

uint32_t blend_rgb(uint32_t fg, uint32_t bg, uint8_t alpha) {
  uint32_t out = 0;
  for (int shift = 0; shift <= 16; shift += 8) {
    uint32_t f = (fg >> shift) & 0xFF;
    uint32_t b = (bg >> shift) & 0xFF;
    uint32_t c = (f * alpha + b * (255u - alpha) + 127u) / 255u;
    out |= c << shift;
  }
  return out;
}

namespace {

double channel(uint32_t value) {
  double c = value / 255.0;
  return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(uint32_t rgb) {
  return 0.2126 * channel((rgb >> 16) & 0xFF) + 0.7152 * channel((rgb >> 8) & 0xFF) + 0.0722 * channel(rgb & 0xFF);
}

constexpr uint32_t BLACK = 0x000000;
constexpr uint32_t WHITE = 0xFFFFFF;
constexpr uint8_t SCHOOL_STRIPE_OPA = 41;       // rgb(255 255 255 / 0.16)
constexpr uint8_t SCHOOL_PAST_STRIPE_OPA = 20;  // rgb(255 255 255 / 0.08)
constexpr uint8_t PAST_ALPHA = 127;    // LV_OPA_50

}  // namespace

double contrast_ratio(uint32_t a, uint32_t b) {
  double la = luminance(a), lb = luminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

uint32_t ink_on(uint32_t fill) { return contrast_ratio(BLACK, fill) >= contrast_ratio(WHITE, fill) ? BLACK : WHITE; }

uint32_t mix_srgb(uint32_t color, uint32_t toward, double weight) {
  uint32_t out = 0;
  for (int shift = 16; shift >= 0; shift -= 8) {
    const double a = (color >> shift) & 0xFF, b = (toward >> shift) & 0xFF;
    // Python's round(): halves go to the even neighbour (nearbyint's
    // default rounding mode).
    const auto v = static_cast<uint32_t>(std::clamp(std::nearbyint(a + (b - a) * weight), 0.0, 255.0));
    out |= v << shift;
  }
  return out;
}

uint32_t map_frame_color(uint32_t color, bool dark) {
  const uint32_t bubble = dark ? 0x3D3A35 : 0xFFFFFF;
  const uint32_t toward = dark ? WHITE : BLACK;
  for (int step = 0; step <= 20; step++) {
    const uint32_t frame = mix_srgb(color, toward, step / 20.0);
    if (contrast_ratio(frame, bubble) >= 3.0)
      return frame;
  }
  return toward;
}

BarStyle bar_style(uint32_t color, bool has_ink, uint32_t ink, uint32_t surface, bool school, bool past) {
  const uint32_t alpha = past ? PAST_ALPHA : 255;
  BarStyle style;
  style.alpha = static_cast<uint8_t>(alpha);
  style.stripe_opa = !school ? 0 : past ? SCHOOL_PAST_STRIPE_OPA : SCHOOL_STRIPE_OPA;
  style.fill = blend_rgb(color, surface, style.alpha);
  style.ink = (alpha == 255 && has_ink) ? ink : ink_on(style.fill);
  return style;
}

int stripe_tile_width(float scale) {
  return std::max(8, static_cast<int>(std::lround(8.0 * 16.0 * std::sqrt(2.0) * scale)));
}

std::vector<uint8_t> stripe_coverage(int w, int h) {
  std::vector<uint8_t> out(static_cast<size_t>(std::max(0, w)) * std::max(0, h), 0);
  if (w <= 0 || h <= 0)
    return out;
  // A point is striped when its distance from the top-left corner along the
  // gradient — (x + y) / √2 — falls in the first half of a period; across x
  // that is (x + y) mod period < period / 2. 4 × 4 samples per pixel.
  const double period = w / 8.0;
  constexpr int N = 4;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int hits = 0;
      for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
          const double t = std::fmod(x + (i + 0.5) / N + y + (j + 0.5) / N, period);
          if (t < period / 2)
            hits++;
        }
      }
      out[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(hits * 255 / (N * N));
    }
  }
  return out;
}

uint16_t rgb565(uint32_t rgb) {
  return static_cast<uint16_t>(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

std::vector<uint16_t> stripe_tile_rgb565(uint32_t fill, uint8_t opa, const std::vector<uint8_t> &coverage) {
  // One colour per coverage level (anti-aliased edges), then a lookup.
  uint16_t levels[256];
  for (int c = 0; c < 256; c++)
    levels[c] = rgb565(blend_rgb(0xFFFFFF, fill, static_cast<uint8_t>(c * opa / 255)));
  std::vector<uint16_t> out(coverage.size());
  for (size_t i = 0; i < coverage.size(); i++)
    out[i] = levels[coverage[i]];
  return out;
}

int LaneGeometry::track_height(int rows) { return BAR_HEIGHT + 2 + ROW_PITCH * (std::max(1, rows) - 1); }

int LaneGeometry::lane_height(int rows) { return BORDER + 2 * PAD + track_height(rows); }

int LaneGeometry::track_top(int all_day_h, const Metrics &m) {
  return m.px(PAD) + (all_day_h > 0 ? all_day_h + m.px(ALLDAY_GAP) : 0);
}

int LaneGeometry::lane_height(int rows, int all_day_h, const Metrics &m) {
  return m.px(BORDER) + track_top(all_day_h, m) + m.px(track_height(rows)) + m.px(PAD);
}

LineBox line_box(int font_line, int css_line) {
  const int leading = css_line - font_line;
  // Half above (rounded down, also when negative), the rest below.
  const int above = leading >= 0 ? leading / 2 : -((-leading + 1) / 2);
  return LineBox{above, leading - above, leading};
}

int inline_top(float css_line, int font_line, int font_base, float inner_css, int inner_line, int inner_base) {
  // Half the leading above each glyph run, the baseline under its ascent.
  const float baseline = (css_line - font_line) / 2 + (font_line - font_base);
  const float top = baseline - (inner_line - inner_base) - (inner_css - inner_line) / 2;
  return static_cast<int>(std::lround(top));
}

static constexpr float HINTING_SLACK = 0.0137f;  // of the font line, a glyph

int css_width(int panel_width, int glyphs, int font_line) {
  return panel_width + static_cast<int>(std::lround(glyphs * font_line * HINTING_SLACK));
}

int css_ch(float n, int zero_px, int font_line) {
  return static_cast<int>(std::lround(n * (zero_px + font_line * HINTING_SLACK)));
}

int utf8_glyphs(const std::string &text) {
  int n = 0;
  for (unsigned char c : text)
    n += (c & 0xC0) != 0x80;
  return n;
}

std::string ellipsize(const std::string &text, int max_w, const std::function<int(const std::string &)> &width) {
  if (width(text) <= max_w)
    return text;
  // Character boundaries (UTF-8 lead bytes); the widths of the prefixes
  // grow with their length, so the longest that fits is a binary search.
  std::vector<size_t> ends;
  for (size_t i = 1; i < text.size(); i++)
    if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
      ends.push_back(i);
  size_t lo = 0, hi = ends.size();  // the answer is ends[0 .. lo) fitting
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (width(text.substr(0, ends[mid]) + ELLIPSIS) <= max_w)
      lo = mid + 1;
    else
      hi = mid;
  }
  return (lo == 0 ? std::string() : text.substr(0, ends[lo - 1])) + ELLIPSIS;
}

bool bar_icon_only(bool narrow, int bar_w, int pad, int limit) { return narrow || bar_w - 2 * pad <= limit; }

StrikeLine strike_line(int line_height, int base_line, int underline_thickness) {
  return {(line_height - base_line) * 2 / 3 + underline_thickness / 2,
          underline_thickness > 0 ? underline_thickness : 1};
}

bool row_fits_beside(int room, int time_w, int gap, int main_min) { return time_w + gap + main_min <= room; }

bool rail_names_home(BoardKind kind, const std::string &person_kind) {
  return !(kind == BoardKind::PERSON && person_kind == "picture");
}

bool lock_screen_changed(const LockScreen &old, const LockScreen &now) {
  return old.household != now.household || old.locale != now.locale || old.clock.date != now.clock.date ||
         old.clock.day != now.clock.day || old.clock.tz != now.clock.tz || old.is_night != now.is_night ||
         old.locked != now.locked || old.pin_button != now.pin_button || old.enter_pin != now.enter_pin ||
         old.unlock != now.unlock || old.del != now.del || old.wrong_pin != now.wrong_pin ||
         old.tries_left != now.tries_left || old.last_try != now.last_try || old.forgot_pin != now.forgot_pin;
}

int top_side_width(int main_width, const Metrics &m) {
  return std::max(m.px(320), std::min(m.px(640), main_width - m.px(32) - m.px(320)));
}

RowStyle row_style(const AgendaRow &row, const Palette &p) {
  const bool cancelled = row.status == Status::CANCELLED;
  RowStyle style;
  style.fill = !row.past;
  style.fill_color = row.now ? p.surface : p.bg;
  style.strip = row.color.valid && row.past ? bar_style(row.color.rgb, false, 0, p.surface, false, true).fill
                                            : row.color.or_else(p.border);
  style.time_ink = p.muted;
  style.title_ink = row.past || cancelled || row.is_break ? p.muted : p.text;
  style.meta_ink = p.muted;
  style.emoji_opa = row.past ? OPA_PAST_ROW_EMOJI : OPA_FULL;
  style.strike = cancelled;
  return style;
}

TileStyle tile_style(const Tile &tile, const Palette &p) {
  TileStyle style;
  style.caption_ink = p.muted;
  style.strip = tile.color.or_else(p.border);
  // As in the browser's CSS, the cancelled look wins over the past one.
  style.emoji_opa = tile.cancelled ? OPA_CANCELLED_PICTURE : tile.past ? OPA_PAST_PICTURE : OPA_FULL;
  style.strike = tile.cancelled;
  return style;
}

uint32_t grayscale(uint32_t rgb) { return grayscale(rgb, GREY_FULL); }

uint32_t grayscale(uint32_t rgb, uint8_t amount) {
  const uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  const uint8_t luma = luma709(r, g, b);
  return (uint32_t{toward_grey(r, luma, amount)} << 16) | (uint32_t{toward_grey(g, luma, amount)} << 8) |
         toward_grey(b, luma, amount);
}

namespace {
// `.dash-sticky, .dash-note { background: var(--kb, #fff3a8); color: var(--kn, #1f1d1a) }`
constexpr uint32_t NOTE_PLAIN_BG = 0xFFF3A8;
constexpr uint32_t NOTE_PLAIN_INK = 0x1F1D1A;
constexpr uint8_t WEDGE_OPA = 127;  // .dash-pv-wedge { opacity: 0.5 }
}  // namespace

NoteLook note_look(const Note &note, bool dark) {
  NoteLook look{NOTE_PLAIN_BG, NOTE_PLAIN_INK, !dark, dark};
  const std::optional<NoteColors> &colors = dark ? note.dark : note.light;
  if (colors.has_value()) {
    look.bg = colors->bg.or_else(NOTE_PLAIN_BG);
    look.ink = colors->ink.or_else(NOTE_PLAIN_INK);
  }
  return look;
}

DialLook dial_look(const Palette &p) {
  return DialLook{p.surface, p.text, p.text, p.muted, p.text, WEDGE_OPA};
}

SpotAge spot_age(const MapSpot &spot, bool offline) {
  SpotAge age;
  age.shown = !offline;
  age.clock = spot.stale;
  age.bold = spot.stale;
  age.text = spot.stale && !spot.age_label.empty() ? spot.age_label : spot.when;
  return age;
}

LiveStyle live_style(const Palette &p, bool stale) {
  LiveStyle style;
  style.stale = stale;
  style.text = stale ? p.muted : p.text;
  style.accent = stale ? grayscale(p.accent) : p.accent;
  style.accent_ink = stale ? ink_on(style.accent) : p.accent_text;
  style.picture_opa = stale ? OPA_STALE_PICTURE : OPA_FULL;
  return style;
}

void hand_end(float deg, int cx, int cy, int len, int *x, int *y) {
  float rad = deg * static_cast<float>(M_PI) / 180.0f;
  *x = cx + static_cast<int>(std::lround(std::sin(rad) * len));
  *y = cy - static_cast<int>(std::lround(std::cos(rad) * len));
}

void wedge_angles(float minute_deg, int minutes, int *start, int *end) {
  minutes = std::clamp(minutes, 0, 60);
  // Clock degrees (0 = 12, clockwise) → LVGL degrees (0 = 3 o'clock, clockwise).
  int s = static_cast<int>(std::lround(minute_deg)) - 90;
  s = ((s % 360) + 360) % 360;
  *start = s;
  *end = (s + minutes * 6) % 360;
  if (minutes == 60) {
    *start = 0;  // the whole dial
    *end = 360;
  }
}

std::string pin_after_key(const std::string &pin, const std::string &key) {
  if (key == "del")
    return pin.empty() ? pin : pin.substr(0, pin.size() - 1);
  if (key.size() == 1 && key[0] >= '0' && key[0] <= '9' && pin.size() < PIN_MAX_DIGITS)
    return pin + key;
  return pin;
}

bool pin_ready(const std::string &pin) { return pin.size() >= PIN_MIN_DIGITS; }

std::vector<TopSlot> top_slots(bool tabs, bool offline, bool alerts) {
  std::vector<TopSlot> slots;
  if (tabs)
    slots.push_back(TopSlot::TABS);
  if (offline)
    slots.push_back(TopSlot::OFFLINE);
  if (alerts)
    slots.push_back(TopSlot::ALERTS);
  return slots;
}

std::vector<Stroke> warning_sign(int size) {
  auto at = [size](int pct) { return size * pct / 100; };
  const int mid = size / 2;
  return {
      {mid, at(8), at(96), at(92)},      // right side
      {at(96), at(92), at(4), at(92)},   // base
      {at(4), at(92), mid, at(8)},       // left side
      {mid, at(38), mid, at(62)},        // the "!" bar
      {mid, at(76), mid, at(78)},        // … and its dot
  };
}

RefreshArrow refresh_arrow(int size) {
  RefreshArrow a{};
  a.cx = size / 2;
  a.cy = size / 2;
  a.r = size * 36 / 100;
  // Open at the top (240°..300°), turning clockwise from 300° to 240°.
  a.start = 300;
  a.end = 240;
  const float rad = static_cast<float>(a.end) * static_cast<float>(M_PI) / 180.0f;
  const float ux = std::cos(rad), uy = std::sin(rad);  // outward, y down
  const float dx = -uy, dy = ux;                       // the way it turns
  const float len = static_cast<float>(a.r) * 0.45f;
  const int tx = a.cx + static_cast<int>(std::lround(ux * a.r));
  const int ty = a.cy + static_cast<int>(std::lround(uy * a.r));
  for (int k = 0; k < 2; k++) {
    const float side = k == 0 ? 1.0f : -1.0f;
    a.head[k] = {tx, ty, tx + static_cast<int>(std::lround(len * (-dx + side * ux))),
                 ty + static_cast<int>(std::lround(len * (-dy + side * uy)))};
  }
  return a;
}

}  // namespace esphome::socialhome_display
