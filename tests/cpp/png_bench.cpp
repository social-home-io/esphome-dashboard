// SPDX-License-Identifier: MPL-2.0
// Times the streamed map decode on the host: png_bench map.png [runs]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <pngle.h>

#include "png.h"

using namespace esphome::socialhome_display;

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s file.png [runs]\n", argv[0]);
    return 2;
  }
  std::ifstream in(argv[1], std::ios::binary);
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const int runs = argc > 2 ? atoi(argv[2]) : 10;
  double best = 1e9, total = 0;
  std::shared_ptr<Pixels> last;
  for (int r = 0; r < runs; r++) {
    auto start = std::chrono::steady_clock::now();
    PngStream png(1920, 1200, 0xFFFFFF);
    // 4 kB pieces, as esp_http_client_read hands them over.
    for (size_t at = 0; at < file.size(); at += 4096)
      png.feed(file.data() + at, std::min<size_t>(4096, file.size() - at));
    last = png.finish();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    best = std::min(best, ms);
    total += ms;
    if (last == nullptr) {
      fprintf(stderr, "decode failed: %s\n", png.error().c_str());
      return 1;
    }
  }
  printf("file %zu bytes, %dx%d -> RGB565 %zu bytes\n", file.size(), last->width, last->height, last->size);
  printf("decoder state (pngle_t, incl. 32 kB inflate window) %zu bytes, scanline buffer ~%d bytes\n",
         PNGLE_T_SIZE, last->width * 4 + 8);
  printf("decode best %.1f ms, mean %.1f ms over %d runs\n", best, total / runs, runs);
  return 0;
}
