// SPDX-License-Identifier: MPL-2.0
#include "photo_frame.h"

#include <cinttypes>

#include "esphome/core/log.h"
#include "parser.h"

// Not re-exported by lvgl.h in 9.5.
#include "src/misc/cache/instance/lv_image_cache.h"

namespace esphome::socialhome_display {

static const char *const TAG = "socialhome_display.frame";
/// Wait before asking again after a network error.
static constexpr uint32_t RETRY_S = 30;

void PhotoFrame::begin() {
  this->end();
  this->active_ = true;
  this->request_round_();
}

void PhotoFrame::end() {
  this->drop_(this->shown_);
  this->drop_(this->previous_);
  this->drop_(this->waiting_);
  this->worker_.cancel_if([](const HttpRequest &) { return true; });
  this->active_ = false;
  this->pending_ = Pending::NONE;
  this->request_id_ = 0;
  this->want_photo_ = false;
  this->retry_at_ = 0;
  this->round_ = FrameRound{};
  this->have_round_ = false;
  this->cursor_ = FrameCursor{};
}

void PhotoFrame::request_round_() {
  if (!this->factory_)
    return;
  HttpRequest request = this->factory_(std::string(FRAME_PATH));
  if (this->have_round_ && !this->round_etag_.empty())
    request.headers.emplace_back("If-None-Match", this->round_etag_);
  request.id = this->next_id_++;
  if (!this->worker_.submit(std::move(request)))
    return;
  this->request_id_ = this->next_id_ - 1;
  this->pending_ = Pending::ROUND;
}

void PhotoFrame::request_photo_() {
  if (!this->factory_ || this->round_.photos.empty())
    return;
  const FramePhoto &photo = this->round_.photos[this->cursor_.index];
  HttpRequest request = this->factory_(frame_photo_path(photo.url, this->width_, this->height_));
  request.id = this->next_id_++;
  // The server fits the photo into the screen; anything bigger is refused.
  request.stream_width = this->width_;
  request.stream_height = this->height_;
  request.background = 0x000000;
  if (!this->worker_.submit(std::move(request)))
    return;
  this->request_id_ = this->next_id_ - 1;
  this->pending_ = Pending::PHOTO;
  this->want_photo_ = false;
  ESP_LOGD(TAG, "Loading photo %u of %u", static_cast<unsigned>(this->cursor_.index + 1),
           static_cast<unsigned>(this->round_.photos.size()));
}

PhotoFrame::Event PhotoFrame::loop(uint32_t now) {
  if (!this->active_)
    return Event::NONE;
  Event event = Event::NONE;
  HttpResponse response;
  while (this->worker_.poll(response)) {
    if (response.id != this->request_id_)
      continue;
    const Pending was = this->pending_;
    this->pending_ = Pending::NONE;
    this->request_id_ = 0;
    if (was == Pending::ROUND) {
      if (response.status == 200) {
        FrameRound round;
        if (parse_frame(response.body.data(), response.body.size(), round) != ParseResult::OK ||
            round.photos.empty()) {
          ESP_LOGD(TAG, "No photos to show");
          return Event::OVER;
        }
        this->round_ = std::move(round);
        this->round_etag_ = response.etag;
        this->have_round_ = true;
      } else if (response.status != 304 || !this->have_round_) {
        // 404 FRAME_OFF, 409 NIGHT, 423 locked, no answer.
        ESP_LOGD(TAG, "Screensaver round: %d %s", response.status, response.error.c_str());
        return Event::OVER;
      }
      this->cursor_.start(this->round_.photos.size());
      this->want_photo_ = true;
    } else if (was == Pending::PHOTO) {
      const FramePhoto &photo = this->round_.photos[this->cursor_.index];
      FrameStep step = this->cursor_.answered(response.status, response.pixels != nullptr);
      if (step == FrameStep::SHOW) {
        this->drop_(this->waiting_);
        const int slot = this->free_slot_();
        fill_(this->slots_[slot], std::move(response.pixels), photo);
        this->waiting_ = slot;
        ESP_LOGD(TAG, "Photo %dx%d ready", this->slots_[slot].pixels->width, this->slots_[slot].pixels->height);
        event = Event::READY;
      } else if (step == FrameStep::STOP) {
        ESP_LOGD(TAG, "Photo: %d %s — ending", response.status, response.error.c_str());
        return Event::OVER;
      } else if (step == FrameStep::WAIT) {
        uint32_t wait = parse_retry_after(response.retry_after, RETRY_S);
        ESP_LOGW(TAG, "Photo: %d %s, again in %" PRIu32 " s", response.status, response.error.c_str(), wait);
        this->retry_at_ = now + wait * 1000;
        this->want_photo_ = true;
      } else {
        this->want_photo_ = true;  // SKIP: the next one
      }
    }
  }
  // One photo ahead, and never while one still fades out (memory: the
  // shown, the fading and the waiting picture at most).
  if (this->pending_ == Pending::NONE && this->want_photo_ && this->waiting_ < 0 && this->previous_ < 0 &&
      static_cast<int32_t>(now - this->retry_at_) >= 0) {
    if (this->cursor_.round_over())
      this->request_round_();
    else
      this->request_photo_();
  }
  return event;
}

const lv_image_dsc_t *PhotoFrame::promote() {
  if (this->waiting_ < 0)
    return nullptr;
  this->drop_(this->previous_);
  this->previous_ = this->shown_;
  this->shown_ = this->waiting_;
  this->waiting_ = -1;
  return &this->slots_[this->shown_].dsc;
}

const FramePhoto *PhotoFrame::shown_photo() const {
  return this->shown_ < 0 ? nullptr : &this->slots_[this->shown_].photo;
}

void PhotoFrame::release_previous() {
  this->drop_(this->previous_);
  this->cursor_.advance();
  this->want_photo_ = true;
}

int PhotoFrame::free_slot_() const {
  for (int k = 0; k < 3; k++)
    if (k != this->shown_ && k != this->previous_ && k != this->waiting_)
      return k;
  return 0;  // unreachable: at most two are ever taken when one is asked for
}

void PhotoFrame::fill_(Slot &slot, std::shared_ptr<Pixels> pixels, const FramePhoto &photo) {
  slot.pixels = std::move(pixels);
  slot.photo = photo;
  slot.dsc = lv_image_dsc_t{};
  slot.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  slot.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  slot.dsc.header.w = slot.pixels->width;
  slot.dsc.header.h = slot.pixels->height;
  slot.dsc.header.stride = slot.pixels->stride();
  slot.dsc.data_size = slot.pixels->size;
  slot.dsc.data = slot.pixels->data;
}

void PhotoFrame::drop_(int &index) {
  if (index < 0)
    return;
  Slot &slot = this->slots_[index];
  if (slot.pixels != nullptr)
    lv_image_cache_drop(&slot.dsc);
  slot = Slot{};
  index = -1;
}

}  // namespace esphome::socialhome_display
