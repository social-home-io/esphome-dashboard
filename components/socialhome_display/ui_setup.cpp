// SPDX-License-Identifier: MPL-2.0
//
// Full-screen states: Wi-Fi (Improv), server address, pairing code,
// "update the panel", the night screen, the privacy lock with its PIN pad,
// and the long-press settings overlay.
#include <algorithm>
#include <cstdint>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "socialhome_display.h"

namespace esphome::socialhome_display {

#ifdef ESPHOME_PROJECT_VERSION
static const char *const FIRMWARE_VERSION = ESPHOME_PROJECT_VERSION;
#else
static const char *const FIRMWARE_VERSION = "dev";
#endif

static const char *const TAG = "socialhome_display";

lv_obj_t *SocialHomeDisplay::setup_card_(lv_obj_t *screen, const std::string &title, const std::string &text) {
  const Palette &p = this->palette_();
  lv_obj_t *card = this->card_(screen);
  lv_obj_set_width(card, std::min(this->m_.px(780), this->screen_width_ - 2 * this->m_.pad()));
  lv_obj_set_style_pad_all(card, this->m_.px(36), LV_PART_MAIN);
  lv_obj_set_style_pad_row(card, this->m_.px(18), LV_PART_MAIN);
  lv_obj_set_style_radius(card, this->m_.px(24), LV_PART_MAIN);
  lv_obj_center(card);
  lv_obj_t *head = this->text_(card, title, FontRole::TITLE, p.text);
  this->long_press_settings_(head);
  if (!text.empty())
    this->wrap_(card, text, FontRole::BODY, p.muted);
  return card;
}

void SocialHomeDisplay::build_wifi_(lv_obj_t *screen) {
  const Palette &p = this->palette_();
  lv_obj_t *card = this->setup_card_(screen, this->str_("wifi_title"), this->str_("wifi_text"));
  lv_obj_t *line = this->row_(card, this->m_.px(12));
  lv_obj_t *spinner = this->box_(line);
  int d = this->m_.px(16);
  lv_obj_set_size(spinner, d, d);
  lv_obj_set_style_radius(spinner, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(spinner, lv_color_hex(p.accent), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(spinner, LV_OPA_COVER, LV_PART_MAIN);
  this->text_(line, this->str_("wifi_waiting"), FontRole::BODY_BOLD, p.text);
}

void SocialHomeDisplay::use_server_(const std::string &url) {
  ESP_LOGI(TAG, "Server %s", url.c_str());
  this->server_ = url;
  this->token_.clear();
  this->display_id_.clear();
  this->typed_.clear();
  this->save_state_();
  this->pair_note_.clear();
  this->set_mode_(Mode::PAIRING);
}

void SocialHomeDisplay::on_server_ready_(lv_event_t *event) {
  auto *self = static_cast<SocialHomeDisplay *>(lv_event_get_user_data(event));
  lv_event_code_t code = lv_event_get_code(event);
  if (code != LV_EVENT_READY && code != LV_EVENT_CLICKED)
    return;
  if (self->server_input_ == nullptr)
    return;
  std::string url = normalize_server_url(lv_textarea_get_text(self->server_input_));
  if (url.empty()) {
    self->typed_ = lv_textarea_get_text(self->server_input_);
    self->error_ = self->str_("server_invalid");
    self->dirty_ = true;
    return;
  }
  self->use_server_(url);
}

void SocialHomeDisplay::on_found_(lv_event_t *event) {
  lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
  auto *self = static_cast<SocialHomeDisplay *>(lv_obj_get_user_data(target));
  auto index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
  if (self == nullptr || index >= self->found_.size())
    return;
  // Copy: pairing rebuilds the screen (and the rows).
  std::string url = self->found_[index].url;
  self->use_server_(url);
}

void SocialHomeDisplay::on_server_tap_(lv_event_t *event) {
  auto *self = static_cast<SocialHomeDisplay *>(lv_event_get_user_data(event));
  self->show_keyboard_(true);
}

void SocialHomeDisplay::on_keyboard_cancel_(lv_event_t *event) {
  auto *self = static_cast<SocialHomeDisplay *>(lv_event_get_user_data(event));
  self->show_keyboard_(false);
}

void SocialHomeDisplay::show_keyboard_(bool show) {
  if (this->server_keyboard_ == nullptr || this->server_card_ == nullptr || this->server_input_ == nullptr)
    return;
  const int margin = this->m_.px(24);
  if (show) {
    lv_obj_remove_flag(this->server_keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(this->server_input_, LV_STATE_FOCUSED);
  } else {
    lv_obj_add_flag(this->server_keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_state(this->server_input_, LV_STATE_FOCUSED);
  }
  // The card keeps clear of the keyboard and scrolls when it can't.
  int room = this->screen_height_ - 2 * margin - (show ? lv_obj_get_height(this->server_keyboard_) : 0);
  lv_obj_set_style_max_height(this->server_card_, room, LV_PART_MAIN);
  lv_obj_update_layout(this->server_card_);
  // Typing: the field and Connect (the card's last row) stay in sight.
  if (show)
    lv_obj_scroll_to_view(lv_obj_get_child(this->server_card_, -1), LV_ANIM_OFF);
}

void SocialHomeDisplay::fill_found_() {
  if (this->found_list_ == nullptr)
    return;
  const Palette &p = this->palette_();
  lv_obj_clean(this->found_list_);
  if (this->found_.empty()) {
    this->wrap_(this->found_list_, this->str_(this->found_searched_ ? "found_none" : "found_looking"),
                FontRole::BODY, p.muted);
    return;
  }
  // Four rows fit above the address field; more households on one network
  // than that is rare, and the field is still there.
  const size_t shown = std::min<size_t>(this->found_.size(), 4);
  for (size_t k = 0; k < shown; k++) {
    const FoundServer &found = this->found_[k];
    lv_obj_t *row = this->row_(this->found_list_, this->m_.px(16));
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_style_min_height(row, this->m_.px(64), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, this->m_.px(20), LV_PART_MAIN);
    lv_obj_set_style_pad_ver(row, this->m_.px(10), LV_PART_MAIN);
    lv_obj_set_style_radius(row, this->m_.px(14), LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(p.bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(p.border), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(row, lv_color_hex(p.border), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, std::max(1, this->m_.px(2)), LV_PART_MAIN);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(row, this);
    lv_obj_add_event_cb(row, SocialHomeDisplay::on_found_, LV_EVENT_CLICKED, reinterpret_cast<void *>(k));
    lv_obj_t *texts = this->column_(row, this->m_.px(2));
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_t *name = this->text_(texts, found.name, FontRole::BODY_BOLD, p.text);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(name, LV_PCT(100));
    this->text_(texts, found.address, FontRole::SMALL, p.muted);
    this->text_(row, "\xE2\x80\xBA", FontRole::TITLE, p.accent);  // ›
  }
}

void SocialHomeDisplay::build_server_(lv_obj_t *screen) {
  const Palette &p = this->palette_();
  const bool browse = this->browser_.supported();
  lv_obj_t *card = this->setup_card_(screen, this->str_("server_title"),
                                     this->str_(browse ? "server_text_found" : "server_text"));
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, this->m_.px(24));
  lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  this->server_card_ = card;
  if (browse) {
    // Social Home announces itself over mDNS (_socialhome-dash._tcp); the
    // Home Assistant App (bridge network) can't, so the field stays.
    this->caps_(card, this->str_("found_title"), p.muted);
    this->found_list_ = this->column_(card, this->m_.px(10));
    lv_obj_set_width(this->found_list_, LV_PCT(100));
    this->fill_found_();
  }
  lv_obj_t *input = lv_textarea_create(card);
  lv_obj_set_width(input, LV_PCT(100));
  lv_textarea_set_one_line(input, true);
  lv_textarea_set_placeholder_text(input, this->str_("server_placeholder").c_str());
  const std::string &text = this->typed_.empty() ? this->server_ : this->typed_;
  if (!text.empty())
    lv_textarea_set_text(input, text.c_str());
  lv_obj_set_style_text_font(input, this->font_(FontRole::LARGE), LV_PART_MAIN);
  lv_obj_set_style_text_color(input, lv_color_hex(p.text), LV_PART_MAIN);
  lv_obj_set_style_bg_color(input, lv_color_hex(p.bg), LV_PART_MAIN);
  lv_obj_set_style_border_color(input, lv_color_hex(p.accent), LV_PART_MAIN);
  lv_obj_set_style_border_width(input, this->m_.px(2), LV_PART_MAIN);
  lv_obj_set_style_radius(input, this->m_.px(10), LV_PART_MAIN);
  lv_obj_set_style_pad_all(input, this->m_.px(14), LV_PART_MAIN);
  lv_obj_add_event_cb(input, SocialHomeDisplay::on_server_ready_, LV_EVENT_READY, this);
  lv_obj_add_event_cb(input, SocialHomeDisplay::on_server_tap_, LV_EVENT_CLICKED, this);
  this->server_input_ = input;
  if (!this->error_.empty())
    this->wrap_(card, this->error_, FontRole::BODY, p.error_text);
  lv_obj_t *actions = this->row_(card, this->m_.px(20));
  lv_obj_set_width(actions, LV_PCT(100));
  this->button_(actions, this->str_("connect"), true, SocialHomeDisplay::on_server_ready_, this);
  // The other way in: Social Home finds the panel through Home Assistant and
  // hands over its address (the connect_social_home action). Next to the
  // button, so an error line still leaves the card above the keyboard.
  lv_obj_t *ha = this->wrap_(actions, this->str_("server_ha"), FontRole::BODY, p.muted);
  lv_obj_set_width(ha, 1);
  lv_obj_set_flex_grow(ha, 1);

  lv_obj_t *keyboard = lv_keyboard_create(screen);
  lv_obj_set_size(keyboard, LV_PCT(100), this->screen_height_ * 42 / 100);
  lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(keyboard, input);
  this->keyboard_font_ = *this->font_(FontRole::BODY);
  this->keyboard_font_.fallback = &lv_font_montserrat_24;
  lv_obj_set_style_text_font(keyboard, &this->keyboard_font_, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(keyboard, lv_color_hex(p.border), LV_PART_MAIN);
  lv_obj_add_event_cb(keyboard, SocialHomeDisplay::on_keyboard_cancel_, LV_EVENT_CANCEL, this);
  this->server_keyboard_ = keyboard;
  lv_obj_update_layout(screen);
  // With Social Homes to tap, the keyboard waits for a tap on the field
  // (and makes room for the list); otherwise typing is the only way on.
  // After a wrong address it stays up for the correction.
  this->show_keyboard_(this->found_.empty() || !this->error_.empty());
}

void SocialHomeDisplay::on_change_server_(lv_event_t *event) {
  auto *self = static_cast<SocialHomeDisplay *>(lv_event_get_user_data(event));
  self->set_mode_(Mode::NO_SERVER);
}

void SocialHomeDisplay::on_retry_(lv_event_t *event) {
  auto *self = static_cast<SocialHomeDisplay *>(lv_event_get_user_data(event));
  self->next_pair_poll_ = 0;
  self->error_.clear();
  self->dirty_ = true;
}

void SocialHomeDisplay::build_pairing_(lv_obj_t *screen) {
  const Palette &p = this->palette_();
  lv_obj_t *card = this->setup_card_(screen, this->str_("pair_title"),
                                     this->pair_note_.empty() ? this->str_("pair_hint")
                                                              : this->pair_note_ + "\n" + this->str_("pair_hint"));
  lv_obj_t *code = this->row_(card, this->m_.px(12));
  lv_obj_set_width(code, LV_PCT(100));
  lv_obj_set_flex_align(code, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_ver(code, this->m_.px(12), LV_PART_MAIN);
  if (this->pairing_.code.empty()) {
    this->text_(code, this->error_.empty() ? this->str_("connecting") : this->error_, FontRole::LARGE,
                this->error_.empty() ? p.muted : p.error_text);
  } else {
    for (char ch : this->pairing_.code) {
      lv_obj_t *cell = this->box_(code);
      lv_obj_set_size(cell, this->m_.px(84), this->m_.px(104));
      lv_obj_set_style_bg_color(cell, lv_color_hex(p.bg), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_color(cell, lv_color_hex(p.border), LV_PART_MAIN);
      lv_obj_set_style_border_width(cell, this->m_.px(2), LV_PART_MAIN);
      lv_obj_set_style_radius(cell, this->m_.px(12), LV_PART_MAIN);
      lv_obj_t *label = this->text_(cell, std::string(1, ch), FontRole::DISPLAY, p.text);
      lv_obj_center(label);
    }
  }
  lv_obj_t *server = this->text_(card, this->server_, FontRole::SMALL, p.muted);
  lv_label_set_long_mode(server, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(server, LV_PCT(100));
  lv_obj_t *actions = this->row_(card, this->m_.px(12));
  this->button_(actions, this->str_("change_server"), false, SocialHomeDisplay::on_change_server_, this);
  if (!this->error_.empty())
    this->button_(actions, this->str_("retry"), true, SocialHomeDisplay::on_retry_, this);
}

void SocialHomeDisplay::on_claim_done_(lv_event_t *event) {
  auto *self = static_cast<SocialHomeDisplay *>(lv_event_get_user_data(event));
  if (self->mode_ == Mode::CLAIMING && self->claim_request_ == 0)
    self->finish_claim_();
}

void SocialHomeDisplay::build_claim_(lv_obj_t *screen) {
  const Palette &p = this->palette_();
  if (this->claim_error_.empty()) {
    lv_obj_t *card = this->setup_card_(screen, this->str_("claim_title"), this->str_("connecting"));
    lv_obj_t *server = this->text_(card, this->server_, FontRole::SMALL, p.muted);
    lv_label_set_long_mode(server, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(server, LV_PCT(100));
    return;
  }
  // Only a panel that was paired before lands here (an unpaired one goes
  // straight to the address screen): OK, or the timeout, goes back to it.
  lv_obj_t *card = this->setup_card_(screen, this->str_("claim_title"), "");
  this->wrap_(card, this->claim_error_, FontRole::BODY, p.error_text);
  lv_obj_t *actions = this->row_(card, this->m_.px(12));
  this->button_(actions, this->str_("ok"), true, SocialHomeDisplay::on_claim_done_, this);
}

void SocialHomeDisplay::build_update_(lv_obj_t *screen) {
  const Palette &p = this->palette_();
  lv_obj_t *card = this->setup_card_(screen, this->str_("update_title"), this->str_("update_text"));
  this->text_(card, std::string(this->str_("settings_firmware")) + " " + FIRMWARE_VERSION, FontRole::SMALL, p.muted);
}

// ---- night -----------------------------------------------------------------

void SocialHomeDisplay::on_night_tap_(lv_event_t *event) {
  auto *self = static_cast<SocialHomeDisplay *>(lv_event_get_user_data(event));
  ESP_LOGD(TAG, "Night: showing the dashboard for a minute");
  self->night_peek_ = true;
  self->dirty_ = true;
  lv_display_trigger_activity(self->lvgl_->get_disp());
}

void SocialHomeDisplay::build_night_(lv_obj_t *screen) {
  const Palette &p = this->palette_();
  const Header &h = this->board_->header;
  // One tap anywhere shows the dashboard for a minute.
  lv_obj_t *root = this->box_(screen);
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(root, SocialHomeDisplay::on_night_tap_, LV_EVENT_CLICKED, this);
  lv_obj_t *col = this->column_(root, this->m_.px(12));
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_center(col);
  // The clock (long-press for settings) passes a tap up to the root.
  lv_obj_add_flag(col, LV_OBJ_FLAG_EVENT_BUBBLE);
  this->clock_label_(col, FontRole::NIGHT, p.text);
  this->text_(col, h.night.has_value() ? h.night->date : h.clock.date, FontRole::LARGE, p.muted);
  if (h.night.has_value() && h.night->item.has_value()) {
    // "Tomorrow 08:00 🎒 School 👦" on one line, as on the tablet.
    const NightItem &item = *h.night->item;
    lv_obj_t *line = this->row_(col, this->m_.px(10));
    lv_obj_set_style_pad_top(line, this->m_.px(6), LV_PART_MAIN);
    this->text_(line, item.when_label, FontRole::BODY_BOLD, p.text);
    this->text_(line, item.time, FontRole::BODY, p.text);
    if (!item.icon.empty())
      this->emoji_(line, item.icon, this->m_.emoji_md());
    this->text_(line, item.title, FontRole::BODY, p.text);
    for (const auto &id : item.people)
      this->avatar_(line, id, this->m_.px(32));
  }
  lv_obj_t *hint = this->text_(col, h.night.has_value() && !h.night->wake_hint.empty() ? h.night->wake_hint
                                                                                      : this->str_("night_wake"),
                               FontRole::SMALL, p.muted);
  lv_obj_set_style_pad_top(hint, this->m_.px(20), LV_PART_MAIN);
}

// ---- lock ------------------------------------------------------------------

void SocialHomeDisplay::on_pin_key_(lv_event_t *event) {
  auto *key = static_cast<std::string *>(lv_event_get_user_data(event));
  lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
  auto *self = static_cast<SocialHomeDisplay *>(lv_obj_get_user_data(target));
  if (self == nullptr || key == nullptr)
    return;
  if (*key == "open") {
    self->pin_open_ = true;
    self->dirty_ = true;
    return;
  }
  if (self->unlock_request_ != 0)
    return;  // the last PIN is being checked
  if (*key == "del") {
    if (!self->pin_.empty())
      self->pin_.pop_back();
  } else if (*key == "ok") {
    // POST /unlock {"pin"}; the answer unlocks, or says why not.
    self->request_unlock_();
  } else if (self->pin_.size() < 8) {
    self->pin_ += *key;
    if (self->pin_error_ && self->pin_note_ != nullptr) {
      // Typing again clears "Wrong PIN".
      self->pin_error_ = false;
      self->pin_message_.clear();
      lv_label_set_text(self->pin_note_, " ");
    }
  }
  if (self->pin_dots_ != nullptr) {
    std::string dots;
    for (size_t k = 0; k < self->pin_.size(); k++)
      dots += "\xE2\x80\xA2 ";  // •
    lv_label_set_text(self->pin_dots_, dots.empty() ? " " : dots.c_str());
  }
}

void SocialHomeDisplay::build_lock_(lv_obj_t *screen) {
  const Palette &p = this->palette_();
  const LockScreen &lock = this->lock_;
  lv_obj_t *col = this->column_(screen, this->m_.px(10));
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_center(col);
  this->text_(col, lock.household, FontRole::LARGE, p.muted);
  this->clock_label_(col, this->pin_open_ ? FontRole::DISPLAY : FontRole::CLOCK, p.text);
  if (!this->pin_open_)
    this->text_(col, lock.clock.date, FontRole::LARGE, p.muted);
  if (!this->pin_open_) {
    // As on the tablet: a quiet "PIN" pill under the date (no 🔒 picture:
    // a locked panel gets no images either).
    lv_obj_t *open = this->button_(col, lock.pin_button.empty() ? this->str_("pin_button") : lock.pin_button, false,
                                   SocialHomeDisplay::on_pin_key_, this->payload_("open"));
    lv_obj_set_user_data(open, this);
    lv_obj_set_style_margin_top(open, this->m_.px(24), LV_PART_MAIN);
    return;
  }
  this->text_(col, lock.locked.empty() ? this->str_("locked") : lock.locked, FontRole::TITLE, p.text);
  this->text_(col, lock.enter_pin.empty() ? this->str_("enter_pin") : lock.enter_pin, FontRole::BODY, p.muted);
  std::string dots;
  for (size_t k = 0; k < this->pin_.size(); k++)
    dots += "\xE2\x80\xA2 ";  // •
  this->pin_dots_ = this->text_(col, dots.empty() ? " " : dots, FontRole::TITLE, p.text);
  lv_obj_t *pad = this->row_(col, this->m_.px(14));
  lv_obj_set_width(pad, this->m_.px(3 * 84 + 2 * 14));
  lv_obj_set_flex_flow(pad, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(pad, this->m_.px(12), LV_PART_MAIN);
  static const char *const KEYS[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "del", "0", "ok"};
  for (const char *k : KEYS) {
    lv_obj_t *key = this->box_(pad);
    int d = this->m_.px(84);
    lv_obj_set_size(key, d, this->m_.px(64));
    lv_obj_set_style_radius(key, this->m_.px(16), LV_PART_MAIN);
    bool ok = strcmp(k, "ok") == 0;
    lv_obj_set_style_bg_color(key, lv_color_hex(ok ? p.accent : p.surface), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(key, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(key, lv_color_hex(p.border), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(key, lv_color_hex(p.border), LV_PART_MAIN);
    lv_obj_set_style_border_width(key, 1, LV_PART_MAIN);
    lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(key, this);
    lv_obj_add_event_cb(key, SocialHomeDisplay::on_pin_key_, LV_EVENT_CLICKED, this->payload_(k));
    if (strcmp(k, "del") == 0) {
      // A drawn backspace: ⌫ isn't in the dashboard font.
      lv_value_precise_t u = this->m_.px(28);
      lv_obj_t *mark = this->poly_(key, {{u, u * 20 / 100}, {u * 35 / 100, u * 20 / 100}, {0, u / 2},
                                         {u * 35 / 100, u * 80 / 100}, {u, u * 80 / 100}, {u, u * 20 / 100}},
                                   std::max(2, this->m_.px(3)), p.text);
      lv_obj_set_size(mark, this->m_.px(30), this->m_.px(30));
      lv_obj_center(mark);
    } else if (ok) {
      lv_obj_t *check = this->check_mark_(key, this->m_.px(30), p.accent_text, std::max(3, this->m_.px(4)));
      lv_obj_center(check);
    } else {
      lv_obj_t *label = this->text_(key, k, FontRole::TITLE, p.text);
      lv_obj_center(label);
    }
  }
  this->pin_note_ = this->text_(col, this->pin_message_.empty() ? " " : this->pin_message_, FontRole::BODY_BOLD,
                                this->pin_error_ ? p.error_text : p.muted);
  lv_obj_set_style_max_width(this->pin_note_, this->m_.px(640), LV_PART_MAIN);
  lv_label_set_long_mode(this->pin_note_, LV_LABEL_LONG_MODE_WRAP);
}

// ---- settings overlay ------------------------------------------------------------

void SocialHomeDisplay::on_settings_close_(lv_event_t *event) {
  auto *self = static_cast<SocialHomeDisplay *>(lv_event_get_user_data(event));
  self->close_settings_();
}

void SocialHomeDisplay::on_forget_(lv_event_t *event) {
  auto *self = static_cast<SocialHomeDisplay *>(lv_event_get_user_data(event));
  if (self->confirm_ != nullptr)
    return;
  self->building_overlay_ = true;
  const Palette &p = self->palette_();
  lv_obj_t *dialog = self->card_(self->settings_);
  self->confirm_ = dialog;
  lv_obj_set_width(dialog, self->m_.px(620));
  lv_obj_set_style_pad_all(dialog, self->m_.px(32), LV_PART_MAIN);
  lv_obj_set_style_shadow_width(dialog, self->m_.px(40), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(dialog, LV_OPA_30, LV_PART_MAIN);
  lv_obj_add_flag(dialog, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_center(dialog);
  self->wrap_(dialog, self->str_("forget_confirm"), FontRole::LARGE, p.text);
  lv_obj_t *actions = self->row_(dialog, self->m_.px(12));
  self->button_(actions, self->str_("cancel"), false, SocialHomeDisplay::on_forget_confirm_, nullptr);
  lv_obj_t *yes = self->button_(actions, self->str_("forget"), true, SocialHomeDisplay::on_forget_confirm_, self);
  lv_obj_set_style_bg_color(yes, lv_color_hex(p.error_text), LV_PART_MAIN);
  // Cancel carries no user data; find self through the dialog.
  lv_obj_set_user_data(actions, self);
  self->building_overlay_ = false;
}

void SocialHomeDisplay::on_forget_confirm_(lv_event_t *event) {
  auto *self = static_cast<SocialHomeDisplay *>(lv_event_get_user_data(event));
  lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
  if (self == nullptr) {
    // Cancel.
    auto *owner = static_cast<SocialHomeDisplay *>(lv_obj_get_user_data(lv_obj_get_parent(target)));
    if (owner != nullptr && owner->confirm_ != nullptr) {
      lv_obj_delete_async(owner->confirm_);
      owner->confirm_ = nullptr;
    }
    return;
  }
  self->close_settings_();
  self->forget_server();
}

void SocialHomeDisplay::open_settings_() {
  if (this->settings_ != nullptr)
    return;
  this->building_overlay_ = true;
  const Palette &p = this->palette_();
  lv_obj_t *veil = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(veil);
  lv_obj_set_size(veil, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(veil, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(veil, LV_OPA_50, LV_PART_MAIN);
  lv_obj_add_flag(veil, LV_OBJ_FLAG_CLICKABLE);
  this->settings_ = veil;

  lv_obj_t *card = this->card_(veil);
  lv_obj_set_width(card, std::min(this->m_.px(820), this->screen_width_ - 2 * this->m_.pad()));
  lv_obj_set_style_pad_all(card, this->m_.px(32), LV_PART_MAIN);
  lv_obj_set_style_pad_row(card, this->m_.px(14), LV_PART_MAIN);
  lv_obj_set_style_radius(card, this->m_.px(24), LV_PART_MAIN);
  lv_obj_center(card);
  this->text_(card, this->str_("settings_title"), FontRole::TITLE, p.text);
  auto field = [&](const std::string &label, const std::string &value) {
    lv_obj_t *row = this->row_(card, this->m_.px(16));
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_t *l = this->text_(row, label, FontRole::BODY_BOLD, p.text);
    lv_obj_set_width(l, this->m_.px(200));
    lv_obj_t *v = this->text_(row, value.empty() ? "\xE2\x80\x94" : value, FontRole::BODY, p.muted);
    lv_obj_set_flex_grow(v, 1);
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
  };
  field(this->str_("settings_server"), this->server_);
  field(this->str_("settings_panel"), this->display_id_);
  field(this->str_("settings_firmware"), std::string(FIRMWARE_VERSION) + " \xC2\xB7 " + this->model_);
  this->wrap_(card, this->str_("credits"), FontRole::SMALL, p.muted);
  lv_obj_t *actions = this->row_(card, this->m_.px(12));
  lv_obj_set_style_pad_top(actions, this->m_.px(8), LV_PART_MAIN);
  if (!this->server_.empty()) {
    lv_obj_t *forget = this->button_(actions, this->str_("forget"), false, SocialHomeDisplay::on_forget_, this);
    lv_obj_t *label = lv_obj_get_child(forget, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(p.error_text), LV_PART_MAIN);
  }
  this->button_(actions, this->str_("close"), true, SocialHomeDisplay::on_settings_close_, this);
  this->building_overlay_ = false;
}

void SocialHomeDisplay::close_settings_() {
  if (this->settings_ == nullptr)
    return;
  lv_obj_delete_async(this->settings_);
  this->settings_ = nullptr;
  this->confirm_ = nullptr;
}

}  // namespace esphome::socialhome_display
