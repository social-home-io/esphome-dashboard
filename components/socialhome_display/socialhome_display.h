// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

#include "esphome/core/component.h"
#include "esphome/components/font/font.h"
#include "esphome/components/lvgl/lvgl_esphome.h"

namespace esphome::socialhome_display {

/// Social Home wall dashboard.
///
/// Bootstrap stage: renders a boot screen with the product name, a
/// "not connected yet" status and the firmware version. Polling the Social
/// Home display API and the dashboard screens are added later.
class SocialHomeDisplay : public Component {
 public:
  void set_lvgl(lvgl::LvglComponent *lvgl) { this->lvgl_ = lvgl; }
  void set_server_url(const std::string &url) { this->server_url_ = url; }
  void set_poll_interval(uint32_t interval_ms) { this->poll_interval_ms_ = interval_ms; }
  void set_screen_size(int width, int height) {
    this->screen_width_ = width;
    this->screen_height_ = height;
  }
  void set_title_font(font::Font *font) { this->title_font_ = font; }
  void set_body_font(font::Font *font) { this->body_font_ = font; }
  void set_small_font(font::Font *font) { this->small_font_ = font; }

  void setup() override;
  void dump_config() override;
  // After LVGL (setup_priority::PROCESSOR) has created its display.
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  void build_boot_screen_();
  static void apply_font_(lv_obj_t *obj, font::Font *font);

  lvgl::LvglComponent *lvgl_{nullptr};
  std::string server_url_;
  uint32_t poll_interval_ms_{30000};
  int screen_width_{0};
  int screen_height_{0};
  font::Font *title_font_{nullptr};
  font::Font *body_font_{nullptr};
  font::Font *small_font_{nullptr};
  lv_obj_t *status_label_{nullptr};
};

}  // namespace esphome::socialhome_display
