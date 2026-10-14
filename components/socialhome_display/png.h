// SPDX-License-Identifier: MPL-2.0
//
// PNG → LVGL pixels with pngle (MIT). No ESPHome / LVGL includes: the host
// tests compile this file with g++ (and time the map picture's decode).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace esphome::socialhome_display {

/// A decoded picture, ready for LVGL.
struct Pixels {
  enum Format : uint8_t {
    ARGB8888,  ///< B, G, R, A bytes (avatars, emoji)
    RGB565,    ///< native-endian 16 bit, opaque (the map)
  };
  int width{0};
  int height{0};
  Format format{ARGB8888};
  uint8_t *data{nullptr};  ///< PSRAM when available; owned
  size_t size{0};

  Pixels() = default;
  Pixels(const Pixels &) = delete;
  Pixels &operator=(const Pixels &) = delete;
  ~Pixels();
  int stride() const { return this->width * (this->format == RGB565 ? 2 : 4); }
};

/// Rec. 709 luma of an sRGB colour (0.2126 R + 0.7152 G + 0.0722 B), in
/// 16-bit fixed point — what CSS `filter: grayscale()` puts on every
/// channel.
inline uint8_t luma709(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint8_t>((13933u * r + 46871u * g + 4732u * b + 32768u) >> 16);
}

/// One channel `amount` (0 … 255) of the way to `luma`: CSS
/// `grayscale(amount / 255)`.
inline uint8_t toward_grey(uint8_t c, uint8_t luma, uint8_t amount) {
  return static_cast<uint8_t>((c * (255u - amount) + luma * amount + 127u) / 255u);
}

/// Drain `amount` of the colour from `count` ARGB8888 pixels (B, G, R, A
/// bytes) in place; alpha is kept. LVGL has no grayscale filter, so a
/// greyed picture (offline live data, a stale map spot) is a grey copy.
void grey_argb8888(uint8_t *data, size_t count, uint8_t amount);

/// A copy of `src` with `amount` of its colour drained (grey_argb8888);
/// nullptr if `src` is no ARGB8888 picture or memory is short.
std::shared_ptr<Pixels> grey_copy(const Pixels &src, uint8_t amount);

/// malloc, from PSRAM when the chip has it.
uint8_t *big_alloc(size_t size);

/// Decode a PNG into ARGB8888 at `size`×`size` (box-filtered, aspect kept,
/// centred), optionally masked to a circle. nullptr when it is no PNG.
std::shared_ptr<Pixels> decode_png(const uint8_t *data, size_t len, int size, bool round);

/// Streaming PNG → RGB565 at the picture's own size, for a large opaque
/// picture (the map): bytes go in as they arrive from the network, so the
/// file is never held whole, and the only big buffer is the result
/// (width × height × 2 bytes). Transparent pixels are blended onto
/// `background` (0xRRGGBB).
class PngStream {
 public:
  PngStream(int max_width, int max_height, uint32_t background);
  ~PngStream();
  PngStream(const PngStream &) = delete;
  PngStream &operator=(const PngStream &) = delete;

  /// Feed the next bytes; false once the data is no (supported) PNG.
  bool feed(const uint8_t *data, size_t len);
  /// The picture, if the whole PNG arrived; nullptr otherwise.
  std::shared_ptr<Pixels> finish();
  const std::string &error() const { return this->error_; }

  // pngle callbacks (C linkage friendly).
  void on_init_(uint32_t w, uint32_t h);
  void on_draw_(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]);
  void on_done_() { this->done_ = true; }

 protected:
  void *pngle_{nullptr};
  int max_width_;
  int max_height_;
  uint8_t bg_[3];
  std::string carry_;  ///< bytes pngle didn't take yet (a split chunk header)
  std::shared_ptr<Pixels> out_;
  bool done_{false};
  bool failed_{false};
  std::string error_;
};

}  // namespace esphome::socialhome_display
