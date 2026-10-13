// SPDX-License-Identifier: MPL-2.0
#include "socialhome_display.h"

#include "esphome/core/defines.h"
#include "esphome/core/log.h"

namespace esphome::socialhome_display {

static const char *const TAG = "socialhome_display";

#ifdef ESPHOME_PROJECT_VERSION
static const char *const FIRMWARE_VERSION = ESPHOME_PROJECT_VERSION;
#else
static const char *const FIRMWARE_VERSION = "dev";
#endif

// Palette shared with the browser dashboard's light theme. The server's
// /theme endpoint takes over once the display API exists.
static const uint32_t COLOR_BACKGROUND = 0xF5F5F4;
static const uint32_t COLOR_TEXT = 0x1F2937;
static const uint32_t COLOR_MUTED = 0x6B7280;

void SocialHomeDisplay::setup() {
  if (this->lvgl_ == nullptr) {
    ESP_LOGE(TAG, "No LVGL component");
    this->mark_failed();
    return;
  }
  this->build_boot_screen_();
}

void SocialHomeDisplay::apply_font_(lv_obj_t *obj, font::Font *font) {
#ifdef USE_LVGL_FONT
  if (font != nullptr)
    lv_obj_set_style_text_font(obj, font->get_lv_font(), LV_PART_MAIN);
#endif
}

void SocialHomeDisplay::build_boot_screen_() {
  lv_obj_t *screen = this->lvgl_->get_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BACKGROUND), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t *column = lv_obj_create(screen);
  lv_obj_remove_style_all(column);
  lv_obj_set_size(column, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_center(column);
  lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(column, this->screen_height_ / 40, LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(column);
  lv_label_set_text(title, "Social Home");
  lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  apply_font_(title, this->title_font_);

  this->status_label_ = lv_label_create(column);
  lv_label_set_text(this->status_label_, "Not connected yet");
  lv_obj_set_style_text_color(this->status_label_, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
  apply_font_(this->status_label_, this->body_font_);

  lv_obj_t *version = lv_label_create(screen);
  lv_label_set_text_fmt(version, "Firmware %s", FIRMWARE_VERSION);
  lv_obj_set_style_text_color(version, lv_color_hex(COLOR_MUTED), LV_PART_MAIN);
  apply_font_(version, this->small_font_);
  lv_obj_align(version, LV_ALIGN_BOTTOM_MID, 0, -this->screen_height_ / 25);
}

void SocialHomeDisplay::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Social Home display:\n"
                "  Firmware: %s\n"
                "  Screen: %dx%d\n"
                "  Server URL: %s\n"
                "  Poll interval: %" PRIu32 " ms",
                FIRMWARE_VERSION, this->screen_width_, this->screen_height_,
                this->server_url_.empty() ? "(not set)" : this->server_url_.c_str(), this->poll_interval_ms_);
}

}  // namespace esphome::socialhome_display
