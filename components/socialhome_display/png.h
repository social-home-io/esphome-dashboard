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
