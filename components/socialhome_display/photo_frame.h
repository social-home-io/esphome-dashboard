// SPDX-License-Identifier: MPL-2.0
//
// The photo screensaver's pictures (GET /api/display/v1/frame and its
// photo/{id}.png): the round is fetched as JSON, each photo is streamed into
// RGB565 in PSRAM off the LVGL thread (like the map picture), one photo ahead
// of the one on screen. The component decides when the screensaver runs and
// draws it (ui_screensaver.cpp); this only fetches and holds the pictures.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "model.h"
#include "net.h"
#include "util.h"

namespace esphome::socialhome_display {

class PhotoFrame {
 public:
  using RequestFactory = std::function<HttpRequest(const std::string &path)>;

  enum class Event : uint8_t {
    NONE,
    READY,  ///< a photo is waiting (next_picture())
    OVER,   ///< nothing (more) to show: off, night, locked, unreachable
  };

  void set_factory(RequestFactory factory) { this->factory_ = std::move(factory); }
  /// The panel's screen: photos are asked fitted into it.
  void set_screen(int width, int height) {
    this->width_ = width;
    this->height_ = height;
  }

  /// Fetch a round, then its first photo.
  void begin();
  /// Stop and forget every picture. Only call while nothing on screen
  /// shows one of them.
  void end();
  bool active() const { return this->active_; }
  Event loop(uint32_t now);

  /// True once a photo is waiting to be shown (after READY).
  bool has_next() const { return this->waiting_ >= 0; }
  /// Show the waiting photo: it becomes the shown one (its picture is
  /// returned — the address stays valid until it is released), and the one
  /// shown before stays held until release_previous() (the crossfade draws
  /// both). nullptr when none is waiting.
  const lv_image_dsc_t *promote();
  /// The shown photo's caption data (nullptr before the first).
  const FramePhoto *shown_photo() const;
  /// The crossfade is over: free the photo shown before and fetch the next.
  void release_previous();
  uint32_t interval_ms() const { return static_cast<uint32_t>(this->round_.interval_s) * 1000; }
  uint32_t fade_ms() const { return static_cast<uint32_t>(this->round_.fade_ms); }

 protected:
  struct Slot {
    std::shared_ptr<Pixels> pixels;
    lv_image_dsc_t dsc{};
    FramePhoto photo;
  };
  void request_round_();
  void request_photo_();
  /// A slot neither shown, fading out nor waiting.
  int free_slot_() const;
  static void fill_(Slot &slot, std::shared_ptr<Pixels> pixels, const FramePhoto &photo);
  void drop_(int &index);

  RequestFactory factory_;
  // At the main loop's priority: a 1–2 MB photo PNG decodes for a while.
  HttpWorker worker_{"sh_frame", 1, 1};
  int width_{1280};
  int height_{800};
  bool active_{false};
  enum class Pending : uint8_t { NONE, ROUND, PHOTO } pending_{Pending::NONE};
  uint32_t request_id_{0};
  uint32_t next_id_{1};
  bool want_photo_{false};
  uint32_t retry_at_{0};
  FrameRound round_;
  std::string round_etag_;
  bool have_round_{false};
  FrameCursor cursor_;
  /// Fixed slots, so an lv_image keeps pointing at the same descriptor:
  /// the shown photo, the one fading out, the one waiting.
  Slot slots_[3];
  int shown_{-1};
  int previous_{-1};
  int waiting_{-1};
};

}  // namespace esphome::socialhome_display
