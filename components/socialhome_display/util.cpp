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
