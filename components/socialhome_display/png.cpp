// SPDX-License-Identifier: MPL-2.0
#include "png.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <pngle.h>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace esphome::socialhome_display {

// ---- memory ------------------------------------------------------------

uint8_t *big_alloc(size_t size) {
#ifdef ESP_PLATFORM
  void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p != nullptr)
    return static_cast<uint8_t *>(p);
#endif
  return static_cast<uint8_t *>(malloc(size));
}

Pixels::~Pixels() { free(this->data); }

// ---- PNG ---------------------------------------------------------------

namespace {

struct PngTarget {
  uint32_t width{0};
  uint32_t height{0};
  uint8_t *rgba{nullptr};  // straight RGBA, source size
};

void png_init(pngle_t *pngle, uint32_t w, uint32_t h) {
  auto *t = static_cast<PngTarget *>(pngle_get_user_data(pngle));
  if (w == 0 || h == 0 || w > 1024 || h > 1024)
    return;
  t->width = w;
  t->height = h;
  t->rgba = big_alloc(static_cast<size_t>(w) * h * 4);
  if (t->rgba != nullptr)
    memset(t->rgba, 0, static_cast<size_t>(w) * h * 4);
}

void png_draw(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
  auto *t = static_cast<PngTarget *>(pngle_get_user_data(pngle));
  if (t->rgba == nullptr)
    return;
  for (uint32_t yy = y; yy < y + h && yy < t->height; yy++)
    for (uint32_t xx = x; xx < x + w && xx < t->width; xx++)
      memcpy(t->rgba + (static_cast<size_t>(yy) * t->width + xx) * 4, rgba, 4);
}

}  // namespace

std::shared_ptr<Pixels> decode_png(const uint8_t *data, size_t len, int size, bool round) {
  if (len < 8 || memcmp(data, "\x89PNG", 4) != 0 || size <= 0)
    return nullptr;
  PngTarget target;
  pngle_t *pngle = pngle_new();
  if (pngle == nullptr)
    return nullptr;
  pngle_set_user_data(pngle, &target);
  pngle_set_init_callback(pngle, png_init);
  pngle_set_draw_callback(pngle, png_draw);
  int fed = pngle_feed(pngle, data, len);
  pngle_destroy(pngle);
  if (fed < 0 || target.rgba == nullptr) {
    free(target.rgba);
    return nullptr;
  }
  auto out = std::make_shared<Pixels>();
  out->width = size;
  out->height = size;
  out->size = static_cast<size_t>(size) * size * 4;
  out->data = big_alloc(out->size);
  if (out->data == nullptr) {
    free(target.rgba);
    return nullptr;
  }
  memset(out->data, 0, out->size);
  // Fit the source into size×size, keeping its aspect, centred.
  float scale = std::min(size / static_cast<float>(target.width), size / static_cast<float>(target.height));
  int dw = std::max(1, static_cast<int>(std::lround(target.width * scale)));
  int dh = std::max(1, static_cast<int>(std::lround(target.height * scale)));
  int ox = (size - dw) / 2;
  int oy = (size - dh) / 2;
  float centre = (size - 1) / 2.0f;
  float radius = size / 2.0f;
  for (int y = 0; y < dh; y++) {
    uint32_t sy0 = static_cast<uint32_t>(y * target.height / dh);
    uint32_t sy1 = std::max(sy0 + 1, static_cast<uint32_t>((y + 1) * target.height / dh));
    for (int x = 0; x < dw; x++) {
      uint32_t sx0 = static_cast<uint32_t>(x * target.width / dw);
      uint32_t sx1 = std::max(sx0 + 1, static_cast<uint32_t>((x + 1) * target.width / dw));
      // Box filter, alpha-weighted so transparent edges don't darken.
      uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
      for (uint32_t sy = sy0; sy < sy1 && sy < target.height; sy++) {
        const uint8_t *row = target.rgba + static_cast<size_t>(sy) * target.width * 4;
        for (uint32_t sx = sx0; sx < sx1 && sx < target.width; sx++) {
          const uint8_t *p = row + sx * 4;
          r += p[0] * p[3];
          g += p[1] * p[3];
          b += p[2] * p[3];
          a += p[3];
          n++;
        }
      }
      uint8_t *o = out->data + (static_cast<size_t>(oy + y) * size + ox + x) * 4;
      if (a > 0) {
        o[0] = static_cast<uint8_t>(b / a);
        o[1] = static_cast<uint8_t>(g / a);
        o[2] = static_cast<uint8_t>(r / a);
      }
      uint32_t alpha = n > 0 ? a / n : 0;
      if (round) {
        float dx = ox + x - centre;
        float dy = oy + y - centre;
        float edge = radius - std::sqrt(dx * dx + dy * dy);  // >0 inside
        if (edge < 1.0f)
          alpha = edge <= 0.0f ? 0 : static_cast<uint32_t>(alpha * edge);
      }
      o[3] = static_cast<uint8_t>(alpha);
    }
  }
  free(target.rgba);
  return out;
}

// ---- streaming RGB565 -----------------------------------------------------

namespace {
PngStream *stream_of(pngle_t *pngle) { return static_cast<PngStream *>(pngle_get_user_data(pngle)); }
void stream_init(pngle_t *pngle, uint32_t w, uint32_t h) { stream_of(pngle)->on_init_(w, h); }
void stream_draw(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
  stream_of(pngle)->on_draw_(x, y, w, h, rgba);
}
void stream_done(pngle_t *pngle) { stream_of(pngle)->on_done_(); }
}  // namespace

PngStream::PngStream(int max_width, int max_height, uint32_t background)
    : max_width_(max_width), max_height_(max_height) {
  this->bg_[0] = (background >> 16) & 0xFF;
  this->bg_[1] = (background >> 8) & 0xFF;
  this->bg_[2] = background & 0xFF;
  pngle_t *pngle = pngle_new();
  this->pngle_ = pngle;
  if (pngle == nullptr) {
    this->failed_ = true;
    this->error_ = "out of memory";
    return;
  }
  pngle_set_user_data(pngle, this);
  pngle_set_init_callback(pngle, stream_init);
  pngle_set_draw_callback(pngle, stream_draw);
  pngle_set_done_callback(pngle, stream_done);
}

PngStream::~PngStream() {
  if (this->pngle_ != nullptr)
    pngle_destroy(static_cast<pngle_t *>(this->pngle_));
}

void PngStream::on_init_(uint32_t w, uint32_t h) {
  if (w == 0 || h == 0 || w > static_cast<uint32_t>(this->max_width_) ||
      h > static_cast<uint32_t>(this->max_height_)) {
    this->failed_ = true;
    this->error_ = "picture too large";
    return;
  }
  auto out = std::make_shared<Pixels>();
  out->width = static_cast<int>(w);
  out->height = static_cast<int>(h);
  out->format = Pixels::RGB565;
  out->size = static_cast<size_t>(w) * h * 2;
  out->data = big_alloc(out->size);
  if (out->data == nullptr) {
    this->failed_ = true;
    this->error_ = "out of memory";
    return;
  }
  memset(out->data, 0, out->size);
  this->out_ = std::move(out);
}

void PngStream::on_draw_(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
  Pixels *out = this->out_.get();
  if (out == nullptr)
    return;
  uint32_t r = rgba[0], g = rgba[1], b = rgba[2];
  const uint32_t a = rgba[3];
  if (a != 255) {
    r = (r * a + this->bg_[0] * (255 - a)) / 255;
    g = (g * a + this->bg_[1] * (255 - a)) / 255;
    b = (b * a + this->bg_[2] * (255 - a)) / 255;
  }
  const uint16_t px = static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  const uint32_t width = static_cast<uint32_t>(out->width);
  const uint32_t height = static_cast<uint32_t>(out->height);
  // w × h > 1 only for interlaced (Adam7) previews; normally one pixel.
  for (uint32_t yy = y; yy < y + h && yy < height; yy++) {
    auto *row = reinterpret_cast<uint16_t *>(out->data) + static_cast<size_t>(yy) * width;
    for (uint32_t xx = x; xx < x + w && xx < width; xx++)
      row[xx] = px;
  }
}

bool PngStream::feed(const uint8_t *data, size_t len) {
  if (this->failed_)
    return false;
  if (this->done_ || len == 0)
    return true;
  auto *pngle = static_cast<pngle_t *>(this->pngle_);
  const uint8_t *buf = data;
  size_t size = len;
  if (!this->carry_.empty()) {
    this->carry_.append(reinterpret_cast<const char *>(data), len);
    buf = reinterpret_cast<const uint8_t *>(this->carry_.data());
    size = this->carry_.size();
  }
  int fed = pngle_feed(pngle, buf, size);
  if (fed < 0 || this->failed_) {
    if (this->error_.empty())
      this->error_ = fed < 0 ? pngle_error(pngle) : "bad picture";
    this->failed_ = true;
    this->out_.reset();
    return false;
  }
  // pngle leaves a split chunk header / CRC for the next call.
  std::string rest(reinterpret_cast<const char *>(buf) + fed, size - static_cast<size_t>(fed));
  this->carry_ = std::move(rest);
  return true;
}

std::shared_ptr<Pixels> PngStream::finish() {
  if (this->failed_ || !this->done_)
    return nullptr;
  return std::move(this->out_);
}

}  // namespace esphome::socialhome_display
