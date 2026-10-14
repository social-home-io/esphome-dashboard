// SPDX-License-Identifier: MPL-2.0
#include "map_picture.h"

#include <cinttypes>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

// Not re-exported by lvgl.h in 9.5.
#include "src/misc/cache/instance/lv_image_cache.h"

namespace esphome::socialhome_display {

static const char *const TAG = "socialhome_display.map";
/// Retry delay after a network error.
static constexpr uint32_t RETRY_S = 60;

void MapPicture::want(const std::string &path, const std::string &board_etag, int width, int height,
                      uint32_t background) {
  if (!this->factory_ || this->request_id_ != 0 || !this->fetch_.due(path, board_etag, millis()))
    return;
  HttpRequest request = this->factory_(path);
  std::string etag = this->fetch_.if_none_match(path);
  if (!etag.empty() && this->pixels_ != nullptr)
    request.headers.emplace_back("If-None-Match", etag);
  request.id = this->next_id_++;
  request.stream_width = width;
  request.stream_height = height;
  request.background = background;
  if (!this->worker_.submit(std::move(request)))
    return;
  this->request_id_ = this->next_id_ - 1;
  this->pending_path_ = path;
  this->pending_board_ = board_etag;
  ESP_LOGD(TAG, "Loading %s", path.c_str());
}

bool MapPicture::loop() {
  HttpResponse response;
  bool changed = false;
  while (this->worker_.poll(response)) {
    if (response.id != this->request_id_)
      continue;
    this->request_id_ = 0;
    const uint32_t now = millis();
    if (response.status == 304) {
      this->fetch_.unchanged(this->pending_board_);
    } else if (response.status == 200 && response.pixels != nullptr) {
      if (this->pixels_ != nullptr)
        lv_image_cache_drop(&this->dsc_);
      this->pixels_ = std::move(response.pixels);
      this->dsc_ = lv_image_dsc_t{};
      this->dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
      this->dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
      this->dsc_.header.w = this->pixels_->width;
      this->dsc_.header.h = this->pixels_->height;
      this->dsc_.header.stride = this->pixels_->stride();
      this->dsc_.data_size = this->pixels_->size;
      this->dsc_.data = this->pixels_->data;
      this->fetch_.loaded(this->pending_path_, response.etag, this->pending_board_);
      ESP_LOGD(TAG, "Map picture %dx%d", this->pixels_->width, this->pixels_->height);
      changed = true;
    } else {
      // 503 TILES_UNAVAILABLE / 429 carry Retry-After; keep what is shown.
      uint32_t wait = parse_retry_after(response.retry_after, RETRY_S);
      ESP_LOGW(TAG, "No map picture (%d %s), again in %" PRIu32 " s", response.status, response.error.c_str(), wait);
      this->fetch_.failed(now, wait);
    }
  }
  return changed;
}

const lv_image_dsc_t *MapPicture::picture(const std::string &path) const {
  if (this->pixels_ == nullptr || this->fetch_.held_path != path)
    return nullptr;
  return &this->dsc_;
}

void MapPicture::clear() {
  if (this->pixels_ != nullptr)
    lv_image_cache_drop(&this->dsc_);
  this->pixels_.reset();
  this->dsc_ = lv_image_dsc_t{};
  this->fetch_.reset();
  this->request_id_ = 0;
  this->worker_.cancel_if([](const HttpRequest &) { return true; });
}

}  // namespace esphome::socialhome_display
