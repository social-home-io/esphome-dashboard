// SPDX-License-Identifier: MPL-2.0
#include "util.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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
constexpr uint8_t SCHOOL_ALPHA = 178;  // LV_OPA_70
constexpr uint8_t PAST_ALPHA = 127;    // LV_OPA_50

}  // namespace

double contrast_ratio(uint32_t a, uint32_t b) {
  double la = luminance(a), lb = luminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

uint32_t ink_on(uint32_t fill) { return contrast_ratio(BLACK, fill) >= contrast_ratio(WHITE, fill) ? BLACK : WHITE; }

BarStyle bar_style(uint32_t color, bool has_ink, uint32_t ink, uint32_t surface, bool school, bool past) {
  uint32_t alpha = 255;
  if (school)
    alpha = alpha * SCHOOL_ALPHA / 255;
  if (past)
    alpha = alpha * PAST_ALPHA / 255;
  BarStyle style;
  style.alpha = static_cast<uint8_t>(alpha);
  style.fill = blend_rgb(color, surface, style.alpha);
  style.ink = (alpha == 255 && has_ink) ? ink : ink_on(style.fill);
  return style;
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

uint32_t grayscale(uint32_t rgb) {
  const double r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  const auto y = static_cast<uint32_t>(std::lround(std::clamp(0.2126 * r + 0.7152 * g + 0.0722 * b, 0.0, 255.0)));
  return (y << 16) | (y << 8) | y;
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

}  // namespace esphome::socialhome_display
