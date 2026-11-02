// SPDX-License-Identifier: MPL-2.0
//
// The photo screensaver, as the tablet's frame.js draws it: after
// settings.photo_frame_minutes without a touch (never at night, never while
// locked), photos from the household albums an admin picked, over
// everything on black — each one whole (fitted by the server), crossfading
// into the next over fade_ms, with "date · album" small in the bottom
// corner on a dark pill. A tap only wakes the board: the layer takes it.
#include <cinttypes>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "socialhome_display.h"

namespace esphome::socialhome_display {

static const char *const TAG = "socialhome_display.saver";
/// After a round that showed nothing (none to show, unreachable), ask again
/// at the earliest after this long.
static constexpr uint32_t SAVER_RETRY_MS = 5 * 60000;
/// The caption pill: rgb(0 0 0 / 55%) as on the tablet.
static constexpr lv_opa_t CAPTION_OPA = 140;

bool SocialHomeDisplay::screensaver_allowed_() const {
  return this->mode_ == Mode::RUNNING && this->board_ != nullptr && !this->board_->header.is_night &&
         this->board_->header.settings.photo_frame_minutes > 0 && !this->screen_off_ &&
         this->settings_ == nullptr;
}

void SocialHomeDisplay::tick_screensaver_() {
  lv_display_t *disp = this->lvgl_->get_disp();
  if (disp == nullptr)
    return;
  const uint32_t now = millis();
  const bool allowed = this->screensaver_allowed_();
  const int minutes = allowed ? this->board_->header.settings.photo_frame_minutes : 0;
  const bool due = frame_due(minutes, lv_display_get_inactive_time(disp), false);
  if (!this->saver_running_) {
    if (due && !waiting_until(now, this->saver_retry_at_)) {
      ESP_LOGD(TAG, "Idle for %d min: starting the screensaver", minutes);
      this->saver_running_ = true;
      this->photos_.begin();
    }
    return;
  }
  // Touched before the first photo arrived, or night / lock / turned off.
  if (!allowed || (this->saver_layer_ == nullptr && !due)) {
    this->stop_screensaver_();
    return;
  }
  if (this->photos_.loop(now) == PhotoFrame::Event::OVER) {
    this->stop_screensaver_();
    this->saver_retry_at_ = now + SAVER_RETRY_MS;
    return;
  }
  if (this->photos_.has_next() && !this->saver_fading_ &&
      (this->saver_layer_ == nullptr || now - this->saver_shown_at_ >= this->photos_.interval_ms()))
    this->show_photo_();
}

void SocialHomeDisplay::build_screensaver_() {
  lv_obj_t *layer = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(layer);
  lv_obj_set_size(layer, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(layer, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(layer, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(layer, SocialHomeDisplay::on_screensaver_tap_, LV_EVENT_CLICKED, this);
  for (lv_obj_t *&img : this->saver_img_) {
    img = lv_image_create(layer);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_opa(img, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_center(img);
  }
  // .dash-frame-caption: 24 px from the bottom and the start edge, 6 / 12 px
  // padding, 8 px corners, 18 px white text on 24 px lines, one line.
  const bool rtl = this->board_ != nullptr && this->board_->header.rtl;
  lv_obj_t *pill = lv_obj_create(layer);
  lv_obj_remove_style_all(pill);
  lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(pill, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(pill, CAPTION_OPA, LV_PART_MAIN);
  lv_obj_set_style_radius(pill, this->m_.px(8), LV_PART_MAIN);
  lv_obj_set_style_pad_hor(pill, this->m_.px(12), LV_PART_MAIN);
  lv_obj_set_style_pad_ver(pill, this->m_.px(6), LV_PART_MAIN);
  lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(pill, rtl ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_BOTTOM_LEFT, rtl ? -this->m_.px(24) : this->m_.px(24),
               -this->m_.px(24));
  lv_obj_add_flag(pill, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t *label = this->text_(pill, "", FontRole::TEXT, 0xFFFFFF);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_max_width(label, this->m_.width - 2 * this->m_.px(24) - 2 * this->m_.px(12), LV_PART_MAIN);
  this->line_box_(label, FontRole::TEXT, 24);
  this->saver_layer_ = layer;
  this->hide_board_(true);
  this->saver_caption_ = pill;
  this->saver_caption_label_ = label;
  this->saver_front_ = 0;
}

void SocialHomeDisplay::show_photo_() {
  const lv_image_dsc_t *picture = this->photos_.promote();
  const FramePhoto *photo = this->photos_.shown_photo();
  if (picture == nullptr || photo == nullptr)
    return;
  if (this->saver_layer_ == nullptr)
    this->build_screensaver_();
  const int back = 1 - this->saver_front_;
  lv_obj_t *img = this->saver_img_[back];
  lv_image_set_src(img, picture);
  lv_obj_center(img);
  lv_obj_set_style_opa(img, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_move_foreground(img);
  lv_obj_move_foreground(this->saver_caption_);
  const std::string caption = frame_caption(photo->date, photo->album);
  lv_label_set_text(this->saver_caption_label_, caption.c_str());
  if (caption.empty())
    lv_obj_add_flag(this->saver_caption_, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(this->saver_caption_, LV_OBJ_FLAG_HIDDEN);
  this->saver_front_ = back;
  this->saver_shown_at_ = millis();
  this->saver_fading_ = true;
  const uint32_t fade = this->photos_.fade_ms();
  if (fade == 0) {
    lv_obj_set_style_opa(img, LV_OPA_COVER, LV_PART_MAIN);
    this->fade_done_();
    return;
  }
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, img);
  lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&anim, fade);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&anim, [](void *obj, int32_t value) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(value), LV_PART_MAIN);
  });
  lv_anim_set_user_data(&anim, this);
  lv_anim_set_completed_cb(&anim, [](lv_anim_t *a) {
    static_cast<SocialHomeDisplay *>(lv_anim_get_user_data(a))->fade_done_();
  });
  lv_anim_start(&anim);
}

void SocialHomeDisplay::fade_done_() {
  if (!this->saver_fading_ || this->saver_layer_ == nullptr)
    return;
  this->saver_fading_ = false;
  // The photo underneath is fully covered now: let go of it.
  lv_obj_t *old = this->saver_img_[1 - this->saver_front_];
  lv_obj_set_style_opa(old, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_image_set_src(old, nullptr);
  this->photos_.release_previous();
}

void SocialHomeDisplay::hide_board_(bool hide) {
  // As the tablet's .dash-framed: the board under the photos isn't drawn
  // (nothing to repaint behind a full-screen picture).
  lv_obj_t *screen = this->lvgl_->get_screen_active();
  for (uint32_t k = 0; k < lv_obj_get_child_count(screen); k++) {
    lv_obj_t *child = lv_obj_get_child(screen, k);
    if (hide)
      lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(child, LV_OBJ_FLAG_HIDDEN);
  }
}

void SocialHomeDisplay::stop_screensaver_() {
  if (this->saver_layer_ != nullptr) {
    this->hide_board_(false);
    for (lv_obj_t *img : this->saver_img_) {
      lv_anim_delete(img, nullptr);
      // Nothing may point at the pictures once they are freed below.
      lv_image_set_src(img, nullptr);
    }
    lv_obj_delete_async(this->saver_layer_);
  }
  this->saver_layer_ = nullptr;
  this->saver_caption_ = nullptr;
  this->saver_caption_label_ = nullptr;
  this->saver_img_[0] = this->saver_img_[1] = nullptr;
  this->saver_fading_ = false;
  this->saver_running_ = false;
  this->photos_.end();
}

void SocialHomeDisplay::on_screensaver_tap_(lv_event_t *event) {
  auto *self = static_cast<SocialHomeDisplay *>(lv_event_get_user_data(event));
  ESP_LOGD(TAG, "Tap: back to the board");
  self->stop_screensaver_();
}

}  // namespace esphome::socialhome_display
