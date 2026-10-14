// SPDX-License-Identifier: MPL-2.0
// PNG decoding: the streamed RGB565 map picture and the square avatars.
#include <cstring>
#include <string>

#include "check.h"
#include "png.h"

using namespace esphome::socialhome_display;

namespace {

// 3×2 RGBA, made with Python's zlib: red, green, blue / white, black at
// alpha 128, a fully transparent pixel.
const uint8_t RGBA_3X2[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 
0x44, 0x52, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x9d, 0x74, 0x66, 0x1a, 
0x00, 0x00, 0x00, 0x1c, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x0c, 0x19, 0xfe, 
0x03, 0x49, 0x20, 0x60, 0x60, 0x60, 0x68, 0xe0, 0x12, 0x91, 0x63, 0x00, 0x00, 0x9e, 0x9e, 0x0a, 0xb3, 0x51, 0xbf, 
0x2b, 0xe3, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

uint16_t at(const Pixels &p, int x, int y) { return reinterpret_cast<const uint16_t *>(p.data)[y * p.width + x]; }

}  // namespace

TEST(png_stream_rgb565) {
  PngStream png(1920, 1200, 0xFFFFFF);
  CHECK(png.feed(RGBA_3X2, sizeof(RGBA_3X2)));
  auto pixels = png.finish();
  CHECK(pixels != nullptr);
  if (pixels == nullptr)
    return;
  CHECK_EQ(pixels->width, 3);
  CHECK_EQ(pixels->height, 2);
  CHECK(pixels->format == Pixels::RGB565);
  CHECK_EQ(pixels->stride(), 6);
  CHECK_EQ(pixels->size, size_t{12});
  CHECK_EQ(at(*pixels, 0, 0), uint16_t{0xF800});
  CHECK_EQ(at(*pixels, 1, 0), uint16_t{0x07E0});
  CHECK_EQ(at(*pixels, 2, 0), uint16_t{0x001F});
  CHECK_EQ(at(*pixels, 0, 1), uint16_t{0xFFFF});
  // Half-transparent black on the white background → mid grey.
  CHECK_EQ(at(*pixels, 1, 1), uint16_t{0x7BEF});
  // Fully transparent → the background.
  CHECK_EQ(at(*pixels, 2, 1), uint16_t{0xFFFF});
}

TEST(png_stream_in_small_pieces) {
  // The network hands over arbitrary pieces, even a chunk header split in two.
  PngStream png(1920, 1200, 0x000000);
  for (size_t k = 0; k < sizeof(RGBA_3X2); k++)
    CHECK(png.feed(RGBA_3X2 + k, 1));
  auto pixels = png.finish();
  CHECK(pixels != nullptr);
  if (pixels != nullptr) {
    CHECK_EQ(at(*pixels, 0, 0), uint16_t{0xF800});
    CHECK_EQ(at(*pixels, 2, 1), uint16_t{0x0000});  // transparent on black
  }
}

TEST(png_stream_rejects) {
  {
    // Cut short: no picture.
    PngStream png(1920, 1200, 0xFFFFFF);
    CHECK(png.feed(RGBA_3X2, sizeof(RGBA_3X2) - 20));
    CHECK(png.finish() == nullptr);
  }
  {
    // Larger than allowed.
    PngStream png(2, 2, 0xFFFFFF);
    CHECK(!png.feed(RGBA_3X2, sizeof(RGBA_3X2)));
    CHECK(png.finish() == nullptr);
    CHECK_EQ(png.error(), std::string("picture too large"));
  }
  {
    // Not a PNG (an error body).
    PngStream png(1920, 1200, 0xFFFFFF);
    const char *json = R"({"error": {"code": "TILES_UNAVAILABLE"}})";
    CHECK(!png.feed(reinterpret_cast<const uint8_t *>(json), strlen(json)));
    CHECK(png.finish() == nullptr);
  }
}

TEST(png_square_argb) {
  auto pixels = decode_png(RGBA_3X2, sizeof(RGBA_3X2), 6, false);
  CHECK(pixels != nullptr);
  if (pixels == nullptr)
    return;
  CHECK(pixels->format == Pixels::ARGB8888);
  CHECK_EQ(pixels->width, 6);
  CHECK_EQ(pixels->stride(), 24);
  // 3×2 fits 6×4, centred: row 1 starts with the red source pixel (B, G, R, A).
  const uint8_t *red = pixels->data + (1 * 6 + 0) * 4;
  CHECK_EQ(int{red[2]}, 255);
  CHECK_EQ(int{red[3]}, 255);
  // Row 0 is the transparent margin.
  CHECK_EQ(int{pixels->data[3]}, 0);
  CHECK(decode_png(reinterpret_cast<const uint8_t *>("nope nope"), 9, 6, false) == nullptr);
}
