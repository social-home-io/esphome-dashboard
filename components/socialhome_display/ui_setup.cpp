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
  lv_obj_t *root = this->column_(screen, 0);
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(root, SocialHomeDisplay::on_night_tap_, LV_EVENT_CLICKED, this);
  // Offline, the note stays up at night too (the browser hides the @here
  // banners, not .dash-offline): on top, where the day board has it.
  if (this->offline_) {
    lv_obj_set_style_pad_hor(root, this->m_.pad(), LV_PART_MAIN);
    lv_obj_set_style_pad_top(root, this->m_.px(16), LV_PART_MAIN);
    this->build_offline_(root);
  }
  lv_obj_t *body = this->box_(root);
  lv_obj_set_width(body, LV_PCT(100));
  lv_obj_set_flex_grow(body, 1);
  lv_obj_add_flag(body, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t *col = this->column_(body, this->m_.px(12));
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
//
// The wall tablet's lock screen (lock.html.j2 + dashboard.css at 1280×800),
// element for element: the household name, the clock (160 px; 96 px with the
// PIN pad open), the date, and a "🔒 PIN" pill. The pad: the alert ("Wrong
// PIN" + "Tries left: n" / the last-try warning), "Enter PIN", the typed
// dots, a 3 × 4 keypad (100 × 64 keys, 10 px apart, ⌫ quiet, ✓ accent; both
// faded while they can't be used) and "Forgot the PIN?" under it — all
// 320 px wide. Night colours in the night window (scheme_()).

static constexpr int PIN_PAD_WIDTH = 320;
static constexpr int PIN_KEY_W = 100;
static constexpr int PIN_KEY_H = 64;
static constexpr int PIN_GAP = 10;

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
  if (*key == "ok") {
    // POST /unlock {"pin"}; the answer unlocks, or says why not.
    if (pin_ready(self->pin_))
      self->request_unlock_();
  } else {
    // Typing keeps the alert, as on the tablet (only its shake stops).
    self->pin_ = pin_after_key(self->pin_, *key);
  }
  self->update_pin_pad_();
}

void SocialHomeDisplay::update_pin_pad_() {
  const Palette &p = this->palette_();
  if (this->pin_dots_ != nullptr) {
    lv_obj_clean(this->pin_dots_);
    const int d = this->m_.px(18);
    for (size_t k = 0; k < this->pin_.size(); k++) {
      lv_obj_t *dot = this->box_(this->pin_dots_);
      lv_obj_set_size(dot, d, d);
      lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
      lv_obj_set_style_bg_color(dot, lv_color_hex(p.text), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    }
  }
  const bool sending = this->unlock_request_ != 0;
  auto enable = [](lv_obj_t *key, bool on) {
    if (key == nullptr)
      return;
    // `.dash-key:disabled { opacity: 0.4 }`
    lv_obj_set_style_opa(key, on ? LV_OPA_COVER : LV_OPA_40, LV_PART_MAIN);
    if (on)
      lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
    else
      lv_obj_remove_flag(key, LV_OBJ_FLAG_CLICKABLE);
  };
  enable(this->pin_del_, !sending && !this->pin_.empty());
  enable(this->pin_ok_, !sending && pin_ready(this->pin_));
}

void SocialHomeDisplay::close_pin_pad_() {
  this->pin_open_ = false;
  this->pin_.clear();
  this->pin_message_.clear();
  this->pin_detail_.clear();
  this->pin_error_ = false;
  this->dirty_ = true;
}

void SocialHomeDisplay::build_lock_(lv_obj_t *screen) {
  const Palette &p = this->palette_();
  const LockScreen &lock = this->lock_;
  const bool open = this->pin_open_;
  auto lock_str = [this](const std::string &value, const char *key) -> std::string {
    return value.empty() ? this->str_(key) : value;
  };
  lv_obj_t *col = this->column_(screen, 0);
  lv_obj_set_width(col, std::min(this->m_.px(640), this->screen_width_ - 2 * this->m_.px(16)));
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_center(col);

  // .dash-lock-household: 24 px muted (the panel's nearest: 22 px).
  lv_obj_t *household = this->text_(col, lock.household, FontRole::BODY, p.muted);
  lv_obj_set_style_text_align(household, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_max_width(household, LV_PCT(100), LV_PART_MAIN);
  lv_label_set_long_mode(household, LV_LABEL_LONG_MODE_WRAP);
  // .dash-lock-time: 160 px light; 96 px with the pad open.
  lv_obj_t *clock = this->clock_label_(col, open ? FontRole::LOCK : FontRole::NIGHT, p.text);
  // Roboto's line box is taller than the browser's `line-height: 1`.
  lv_obj_set_style_margin_top(clock, -this->m_.px(open ? 7 : 13), LV_PART_MAIN);
  // .dash-lock-date: 32 px (24 px open), full text colour.
  lv_obj_t *date = this->text_(col, lock.clock.date, open ? FontRole::BODY : FontRole::LARGE, p.text);
  lv_obj_set_style_pad_bottom(date, this->m_.px(open ? 27 : 48), LV_PART_MAIN);

  if (!open) {
    // .dash-lock-summary: a 64 px pill "🔒 PIN", surface + border.
    lv_obj_t *pill = this->row_(col, this->m_.px(10));
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(pill, this->m_.px(64));
    lv_obj_set_style_min_width(pill, this->m_.px(64), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(pill, this->m_.px(28), LV_PART_MAIN);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pill, lv_color_hex(p.surface), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pill, lv_color_hex(p.bg), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(pill, lv_color_hex(p.border), LV_PART_MAIN);
    lv_obj_set_style_border_width(pill, 1, LV_PART_MAIN);
    lv_obj_add_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(pill, this);
    lv_obj_add_event_cb(pill, SocialHomeDisplay::on_pin_key_, LV_EVENT_CLICKED, this->payload_("open"));
    this->emoji_(pill, Emoji{"\xF0\x9F\x94\x92", "1f512"}, this->m_.px(22));
    this->text_(pill, lock_str(lock.pin_button, "pin_button"), FontRole::TEXT_BOLD, p.text);
    return;
  }

  // .dash-pin-form: one 320 px column, 12 px apart (short screens).
  const int width = this->m_.px(PIN_PAD_WIDTH);
  lv_obj_t *form = this->column_(col, this->m_.px(12));
  lv_obj_set_width(form, width);
  lv_obj_set_flex_align(form, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (this->pin_error_ && !this->pin_message_.empty()) {
    // .dash-error: error colours, 12 px radius, centred; the count bold.
    lv_obj_t *alert = this->column_(form, this->m_.px(10));
    lv_obj_set_width(alert, LV_PCT(100));
    lv_obj_set_flex_align(alert, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(alert, this->m_.px(12), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(alert, this->m_.px(16), LV_PART_MAIN);
    lv_obj_set_style_radius(alert, this->m_.px(12), LV_PART_MAIN);
    lv_obj_set_style_bg_color(alert, lv_color_hex(p.error_bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(alert, LV_OPA_COVER, LV_PART_MAIN);
    for (int k = 0; k < 2; k++) {
      const std::string &line = k == 0 ? this->pin_message_ : this->pin_detail_;
      if (line.empty())
        continue;
      lv_obj_t *label = this->wrap_(alert, line, k == 0 ? FontRole::TEXT : FontRole::TEXT_BOLD, p.error_text);
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
  }
  // "Enter PIN" (.dash-field span: 600 weight).
  this->text_(form, lock_str(lock.enter_pin, "enter_pin"), FontRole::TEXT_BOLD, p.text);
  // .dash-pin-dots: 18 px dots, 14 px apart, at least 20 px tall.
  this->pin_dots_ = this->row_(form, this->m_.px(14));
  lv_obj_set_style_min_height(this->pin_dots_, this->m_.px(20), LV_PART_MAIN);
  // .dash-keypad
  lv_obj_t *pad = this->row_(form, this->m_.px(PIN_GAP));
  lv_obj_set_width(pad, width);
  lv_obj_set_flex_flow(pad, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(pad, this->m_.px(PIN_GAP), LV_PART_MAIN);
  static const char *const KEYS[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "del", "0", "ok"};
  this->pin_del_ = nullptr;
  this->pin_ok_ = nullptr;
  for (const char *k : KEYS) {
    const bool ok = strcmp(k, "ok") == 0;
    const bool del = strcmp(k, "del") == 0;
    lv_obj_t *key = this->box_(pad);
    lv_obj_set_size(key, (width - 2 * this->m_.px(PIN_GAP)) / 3, this->m_.px(PIN_KEY_H));
    lv_obj_set_style_radius(key, this->m_.px(16), LV_PART_MAIN);
    // .dash-key / .dash-key-quiet (⌫: no fill) / .dash-key-ok (accent).
    if (!del) {
      lv_obj_set_style_bg_color(key, lv_color_hex(ok ? p.accent : p.surface), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(key, LV_OPA_COVER, LV_PART_MAIN);
    }
    lv_obj_set_style_bg_color(key, lv_color_hex(p.bg), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(key, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(key, lv_color_hex(ok ? p.accent : p.border), LV_PART_MAIN);
    lv_obj_set_style_border_width(key, 1, LV_PART_MAIN);
    lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(key, this);
    lv_obj_add_event_cb(key, SocialHomeDisplay::on_pin_key_, LV_EVENT_CLICKED, this->payload_(k));
    if (del) {
      // A drawn ⌫ (not in Roboto): a 28 px tag outline with a cross.
      const lv_value_precise_t w = this->m_.px(30), h = this->m_.px(20);
      const int stroke = std::max(2, this->m_.px(2));
      lv_obj_t *tag = this->poly_(key, {{w * 30 / 100, 0}, {w, 0}, {w, h}, {w * 30 / 100, h}, {0, h / 2},
                                        {w * 30 / 100, 0}},
                                  stroke, p.text);
      lv_obj_set_size(tag, w + stroke, h + stroke);
      lv_obj_center(tag);
      const lv_value_precise_t cx = w * 64 / 100, cy = h / 2, r = h * 22 / 100;
      lv_obj_t *a = this->poly_(key, {{cx - r, cy - r}, {cx + r, cy + r}}, stroke, p.text);
      lv_obj_set_size(a, w + stroke, h + stroke);
      lv_obj_center(a);
      lv_obj_t *b = this->poly_(key, {{cx - r, cy + r}, {cx + r, cy - r}}, stroke, p.text);
      lv_obj_set_size(b, w + stroke, h + stroke);
      lv_obj_center(b);
      this->pin_del_ = key;
    } else if (ok) {
      lv_obj_t *check = this->check_mark_(key, this->m_.px(26), p.accent_text, std::max(2, this->m_.px(3)));
      lv_obj_center(check);
      this->pin_ok_ = key;
    } else {
      lv_obj_t *label = this->text_(key, k, FontRole::LARGE, p.text);
      lv_obj_center(label);
    }
  }
  // .dash-lock-forgot: 16 px muted, centred under the pad.
  lv_obj_t *hint = this->wrap_(col, lock_str(lock.forgot_pin, "pin_forgot"), FontRole::SMALL, p.muted);
  lv_obj_set_width(hint, width);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_pad_top(hint, this->m_.px(12), LV_PART_MAIN);
  lv_obj_set_style_text_line_space(hint, this->m_.px(4), LV_PART_MAIN);
  this->update_pin_pad_();
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
