// SPDX-License-Identifier: MPL-2.0
//
// The Map tab's picture (GET /api/display/v1/map.png): fetched on its own
// worker, streamed into RGB565 in PSRAM off the LVGL thread, revalidated
// with its ETag, and re-fetched only when the board's image.etag changes.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "net.h"
#include "util.h"

namespace esphome::socialhome_display {

class MapPicture {
 public:
  using RequestFactory = std::function<HttpRequest(const std::string &path)>;

  void set_factory(RequestFactory factory) { this->factory_ = std::move(factory); }

  /// The board shows the map: `path` is map.png with its ?w=&h=&scheme=,
  /// `board_etag` the board's image.etag. Starts a request when one is due.
  void want(const std::string &path, const std::string &board_etag, int width, int height, uint32_t background);
  /// Feed a finished download; true when a new picture is ready to show.
  bool loop();
  /// The picture for `path`, if it is the one held; nullptr otherwise.
  const lv_image_dsc_t *picture(const std::string &path) const;
  /// Forget the picture (server changed / unpaired). Only call while
  /// nothing on screen shows it.
  void clear();

 protected:
  RequestFactory factory_;
  // At the main loop's priority: a 1 MB PNG decode takes a while.
  HttpWorker worker_{"sh_map", 1, 1};
  MapFetch fetch_;
  uint32_t request_id_{0};
  uint32_t next_id_{1};
  std::string pending_path_;
  std::string pending_board_;
  std::shared_ptr<Pixels> pixels_;
  lv_image_dsc_t dsc_{};
};

}  // namespace esphome::socialhome_display
