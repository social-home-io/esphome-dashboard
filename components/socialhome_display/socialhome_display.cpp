// SPDX-License-Identifier: MPL-2.0
//
// The state machine: Wi-Fi → server address → pairing → the dashboard, the
// poll loop with ETags and every status code the display API documents,
// the clock, idle return, night dimming and the backlight timeout.
#include "socialhome_display.h"

#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#ifdef USE_NETWORK
#include "esphome/components/network/util.h"
#endif

#include "parser.h"

namespace esphome::socialhome_display {

static const char *const TAG = "socialhome_display";

#ifdef ESPHOME_PROJECT_VERSION
static const char *const FIRMWARE_VERSION = ESPHOME_PROJECT_VERSION;
#else
static const char *const FIRMWARE_VERSION = "dev";
#endif

static const char *const API = "/api/display/v1";
static const char *const HOME_PATH = "/api/display/v1/home";
/// Pairing status is asked this often while a code is shown.
static constexpr uint32_t PAIR_POLL_MS = 3000;
/// The theme (colours, strings) is revalidated this often.
static constexpr uint32_t THEME_MS = 15 * 60 * 1000;
/// Retry delay while the server can't be reached.
static constexpr uint32_t OFFLINE_RETRY_MS = 15000;
/// How long a tap shows the dashboard at night.
static constexpr uint32_t NIGHT_PEEK_MS = 60000;

// ---- strings -------------------------------------------------------------

const std::string &SocialHomeDisplay::str_(const char *key) const {
  // The panel's own English, for the setup screens (before the server's
  // strings exist) and for strings the v1 theme doesn't carry.
  static const std::map<std::string, std::string> BUILTIN = {
      {"wifi_title", "Connect the panel to Wi-Fi"},
      {"wifi_text",
       "Use Improv to give the panel your Wi-Fi: the web installer asks right after flashing, or a phone sends it "
       "over Bluetooth (Home Assistant finds the panel, or open improv-wifi.com). Tap Allow when the panel asks."},
      {"wifi_waiting", "Waiting for Wi-Fi\xE2\x80\xA6"},
      {"server_title", "Find Social Home"},
      {"server_text", "Type the address of your Social Home. The dashboard port 8100 is added for you."},
      {"server_placeholder", "192.168.1.20 or socialhome.local"},
      {"server_invalid", "That doesn't look like an address. Try 192.168.1.20 or socialhome.local."},
      {"connect", "Connect"},
      {"connecting", "Connecting to Social Home\xE2\x80\xA6"},
      {"pair_title", "Pair this panel"},
      {"pair_hint", "In Social Home, open Admin \xE2\x80\xBA Dashboard \xE2\x80\xBA Wall panels \xE2\x80\xBA Add "
                    "and enter this code:"},
      {"pair_expired", "The code expired \xE2\x80\x94 getting a new one\xE2\x80\xA6"},
      {"pair_unreachable", "Can't reach Social Home at this address."},
      {"pair_busy", "Too many pairing attempts. Trying again shortly\xE2\x80\xA6"},
      {"unpaired", "This panel isn't paired any more. Pair it again in Admin \xE2\x80\xBA Dashboard."},
      {"change_server", "Change address"},
      {"retry", "Try again"},
      {"update_title", "This panel needs an update"},
      {"update_text",
       "Your Social Home speaks a newer panel API. Update the panel's firmware from Home Assistant or the web "
       "installer."},
      {"offline", "Can't reach Social Home \xE2\x80\x94 showing info from {time}"},
      {"got_it", "Got it"},
      {"see_all", "See all"},
      {"new", "new"},
      {"nav_home", "Home"},
      {"map_title", "Who's where"},
      {"map_placeholder", "The map comes with a Social Home update."},
      {"settings_title", "Panel settings"},
      {"settings_server", "Social Home"},
      {"settings_panel", "Panel"},
      {"settings_firmware", "Firmware"},
      {"forget", "Forget server"},
      {"forget_confirm", "Forget this Social Home? The panel shows the setup again and needs a new pairing code."},
      {"cancel", "Cancel"},
      {"close", "Close"},
      {"credits",
       "Emoji: Twemoji \xC2\xA9 Twitter, Inc and other contributors, \xC2\xA9 jdecked and other contributors, "
       "CC-BY 4.0. Font: Roboto, Apache 2.0. Firmware: MPL-2.0, built on ESPHome (GPLv3) and LVGL (MIT)."},
      {"unlock_on_server", "Unlock this panel in Social Home: Admin \xE2\x80\xBA Dashboard \xE2\x80\xBA Wall panels."},
      {"locked", "Screen locked"},
      {"enter_pin", "Enter PIN"},
      {"unlock", "Unlock"},
      {"pin_button", "PIN"},
      {"wrong_pin", "Wrong PIN"},
      {"night_wake", "Tap to show the dashboard"},
  };
  static const std::string EMPTY;
  auto it = BUILTIN.find(key);
  return this->theme_.str(key, it == BUILTIN.end() ? EMPTY : it->second);
}

std::string *SocialHomeDisplay::payload_(const std::string &value) {
  this->payloads_.push_back(std::make_unique<std::string>(value));
  return this->payloads_.back().get();
}

// ---- lifecycle -------------------------------------------------------------

void SocialHomeDisplay::setup() {
  if (this->lvgl_ == nullptr) {
    ESP_LOGE(TAG, "No LVGL component");
    this->mark_failed();
    return;
  }
  this->m_ = Metrics::compute(this->screen_width_, this->screen_height_, this->ui_scale_);
  this->pref_server_ = global_preferences->make_preference<StoredText>(fnv1_hash("socialhome_display.server"), true);
  this->pref_token_ = global_preferences->make_preference<StoredText>(fnv1_hash("socialhome_display.token"), true);
  this->pref_display_ =
      global_preferences->make_preference<StoredText>(fnv1_hash("socialhome_display.display_id"), true);
  this->load_state_();
  this->images_.set_factory([this](const std::string &path) { return this->request_(path); });
  this->set_mode_(Mode::BOOT);
}

void SocialHomeDisplay::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Social Home display:\n"
                "  Firmware: %s\n"
                "  Model: %s\n"
                "  Screen: %dx%d (scale %.2f)\n"
                "  Server: %s\n"
                "  Paired: %s\n"
                "  Poll interval: %" PRIu32 " ms",
                FIRMWARE_VERSION, this->model_.c_str(), this->screen_width_, this->screen_height_, this->m_.scale,
                this->server_.empty() ? "(not set)" : this->server_.c_str(), YESNO(!this->token_.empty()),
                this->poll_interval_ms_);
}

namespace {
std::string load_text(ESPPreferenceObject &pref) {
  StoredText stored{};
  if (!pref.load(&stored))
    return {};
  stored.value[sizeof(stored.value) - 1] = 0;
  return stored.value;
}

void save_text(ESPPreferenceObject &pref, const std::string &value) {
  StoredText stored{};
  strncpy(stored.value, value.c_str(), sizeof(stored.value) - 1);
  pref.save(&stored);
}
}  // namespace

void SocialHomeDisplay::load_state_() {
  this->server_ = load_text(this->pref_server_);
  this->token_ = load_text(this->pref_token_);
  this->display_id_ = load_text(this->pref_display_);
  if (this->server_.empty() && !this->default_server_.empty())
    this->server_ = normalize_server_url(this->default_server_);
}

void SocialHomeDisplay::save_state_() {
  save_text(this->pref_server_, this->server_);
  save_text(this->pref_token_, this->token_);
  save_text(this->pref_display_, this->display_id_);
  global_preferences->sync();
}

void SocialHomeDisplay::forget_server() {
  ESP_LOGI(TAG, "Forgetting the server");
  this->server_.clear();
  this->token_.clear();
  this->display_id_.clear();
  this->save_state_();
  this->board_.reset();
  this->etags_.clear();
  this->theme_ = Theme{};
  this->theme_etag_.clear();
  this->pairing_ = PairStart{};
  this->clear_images_ = true;
  this->set_mode_(Mode::NO_SERVER);
}

bool SocialHomeDisplay::network_up_() const {
#ifdef USE_NETWORK
  return network::is_connected();
#else
  return true;
#endif
}

void SocialHomeDisplay::set_mode_(Mode mode) {
  if (mode != this->mode_)
    ESP_LOGD(TAG, "Mode %d -> %d", static_cast<int>(this->mode_), static_cast<int>(mode));
  this->mode_ = mode;
  this->mode_since_ = millis();
  this->dirty_ = true;
  this->pin_.clear();
  this->pin_open_ = false;
  switch (mode) {
    case Mode::PAIRING:
      this->pairing_ = PairStart{};
      this->pair_request_ = 0;
      this->next_pair_poll_ = 0;
      this->error_.clear();
      break;
    case Mode::RUNNING:
      this->next_poll_ = 0;
      this->next_theme_ = 0;
      break;
    case Mode::NO_SERVER:
      this->error_.clear();
      break;
    default:
      break;
  }
}

HttpRequest SocialHomeDisplay::request_(const std::string &path, bool auth) const {
  HttpRequest request;
  request.url = this->server_ + path;
  request.headers.emplace_back("X-Display-Firmware", FIRMWARE_VERSION);
  request.headers.emplace_back("X-Display-Model", this->model_);
  if (auth && !this->token_.empty())
    request.headers.emplace_back("Authorization", "Bearer " + this->token_);
  return request;
}

void SocialHomeDisplay::loop() {
  if (this->is_failed())
    return;
  const uint32_t now = millis();

  if (this->mode_ == Mode::BOOT || this->mode_ == Mode::NO_WIFI) {
    if (!this->network_up_()) {
      if (this->mode_ != Mode::NO_WIFI && now - this->mode_since_ > 3000)
        this->set_mode_(Mode::NO_WIFI);
    } else if (this->server_.empty()) {
      this->set_mode_(Mode::NO_SERVER);
    } else if (this->token_.empty()) {
      this->set_mode_(Mode::PAIRING);
    } else {
      this->set_mode_(Mode::RUNNING);
    }
  }

  HttpResponse response;
  while (this->api_.poll(response))
    this->handle_api_(response);
  this->images_.loop();

  if (this->mode_ == Mode::PAIRING && this->pair_request_ == 0 && now >= this->next_pair_poll_) {
    if (this->pairing_.pair_id.empty() || (this->pair_expires_ != 0 && now > this->pair_expires_)) {
      if (!this->pairing_.pair_id.empty())
        this->pair_note_ = this->str_("pair_expired");
      this->start_pairing_();
    } else {
      HttpRequest request = this->request_(std::string(API) + "/pair/" + this->pairing_.pair_id, false);
      request.id = this->pair_request_ = this->next_id_++;
      this->api_.submit(std::move(request));
    }
  }
  if (this->mode_ == Mode::RUNNING || this->mode_ == Mode::LOCKED) {
    if (this->board_request_ == 0 && now >= this->next_poll_)
      this->request_board_();
    if (this->mode_ == Mode::RUNNING && this->theme_request_ == 0 && now >= this->next_theme_)
      this->request_theme_();
  }

  this->tick_idle_();
  this->apply_backlight_();
  if (this->dirty_)
    this->render_();
  this->tick_clock_();
}

// ---- pairing -------------------------------------------------------------

void SocialHomeDisplay::start_pairing_() {
  HttpRequest request = this->request_(std::string(API) + "/pair", false);
  request.method = "POST";
  request.headers.emplace_back("Content-Type", "application/json");
  // model / firmware are short config strings without quotes.
  request.body = "{\"model\": \"" + this->model_ + "\", \"firmware\": \"" + FIRMWARE_VERSION + "\"}";
  request.id = this->pair_request_ = this->next_id_++;
  this->pairing_ = PairStart{};
  this->api_.submit(std::move(request));
}

void SocialHomeDisplay::handle_pairing_(HttpResponse &response) {
  this->pair_request_ = 0;
  const uint32_t now = millis();
  this->next_pair_poll_ = now + PAIR_POLL_MS;
  if (this->pairing_.pair_id.empty()) {
    // Answer to POST /pair.
    PairStart start;
    if (response.status == 201 && parse_pair_start(response.body.data(), response.body.size(), start) ==
                                      ParseResult::OK) {
      this->pairing_ = start;
      this->pair_expires_ = now + static_cast<uint32_t>(std::max(start.expires_in - 5, 10)) * 1000;
      this->error_.clear();
      ESP_LOGI(TAG, "Pairing code %s", start.code.c_str());
    } else if (response.status == 429) {
      this->error_ = this->str_("pair_busy");
      this->next_pair_poll_ = now + parse_retry_after(response.retry_after, 60) * 1000;
    } else {
      ESP_LOGW(TAG, "Pairing request: status %d %s", response.status, response.error.c_str());
      this->error_ = this->str_("pair_unreachable");
      this->next_pair_poll_ = now + OFFLINE_RETRY_MS;
    }
    this->dirty_ = true;
    return;
  }
  // Answer to GET /pair/{id}.
  if (response.status == 404) {
    this->pair_note_ = this->str_("pair_expired");
    this->pairing_ = PairStart{};
    this->next_pair_poll_ = now;
    this->dirty_ = true;
    return;
  }
  PairStatus status;
  if (response.status != 200 ||
      parse_pair_status(response.body.data(), response.body.size(), status) != ParseResult::OK)
    return;  // keep polling; the code stays on screen
  if (status.state == PairState::PAIRED) {
    ESP_LOGI(TAG, "Paired as display %s", status.display_id.c_str());
    this->token_ = status.token;
    this->display_id_ = status.display_id;
    this->save_state_();
    this->pair_note_.clear();
    this->board_.reset();
    this->etags_.clear();
    this->current_path_ = HOME_PATH;
    this->set_mode_(Mode::RUNNING);
  }
}

// ---- boards ----------------------------------------------------------------

void SocialHomeDisplay::request_board_() {
  HttpRequest request = this->request_(this->current_path_);
  auto etag = this->etags_.find(this->current_path_);
  if (etag != this->etags_.end() && this->board_ != nullptr && this->board_path_ == this->current_path_ &&
      this->mode_ == Mode::RUNNING)
    request.headers.emplace_back("If-None-Match", etag->second);
  request.id = this->board_request_ = this->next_id_++;
  this->requested_path_ = this->current_path_;
  this->api_.submit(std::move(request));
}

void SocialHomeDisplay::request_theme_() {
  HttpRequest request = this->request_(std::string(API) + "/theme");
  if (!this->theme_etag_.empty())
    request.headers.emplace_back("If-None-Match", this->theme_etag_);
  request.id = this->theme_request_ = this->next_id_++;
  this->api_.submit(std::move(request));
}

void SocialHomeDisplay::handle_api_(HttpResponse &response) {
  if (response.id == this->pair_request_ && this->pair_request_ != 0) {
    this->handle_pairing_(response);
  } else if (response.id == this->board_request_ && this->board_request_ != 0) {
    this->handle_board_(response);
  } else if (response.id == this->theme_request_ && this->theme_request_ != 0) {
    this->handle_theme_(response);
  }
}

void SocialHomeDisplay::handle_theme_(HttpResponse &response) {
  this->theme_request_ = 0;
  this->next_theme_ = millis() + THEME_MS;
  if (response.status == 200) {
    Theme theme;
    ParseResult r = parse_theme(response.body.data(), response.body.size(), theme);
    if (r == ParseResult::OK) {
      this->theme_ = std::move(theme);
      this->theme_etag_ = response.etag;
      this->dirty_ = true;
    } else if (r == ParseResult::WRONG_VERSION) {
      this->set_mode_(Mode::UPDATE_NEEDED);
    }
  } else if (response.status == 0 || response.status >= 500) {
    this->next_theme_ = millis() + OFFLINE_RETRY_MS;
  }
}

void SocialHomeDisplay::handle_board_(HttpResponse &response) {
  this->board_request_ = 0;
  const uint32_t now = millis();
  this->next_poll_ = now + this->poll_interval_ms_;
  const bool stale = this->requested_path_ != this->current_path_;
  switch (response.status) {
    case 200: {
      if (stale) {
        this->next_poll_ = now;
        return;
      }
      auto board = std::make_unique<Board>();
      ParseResult r = parse_board(response.body.data(), response.body.size(), *board);
      if (r == ParseResult::WRONG_VERSION) {
        this->set_mode_(Mode::UPDATE_NEEDED);
        return;
      }
      if (r != ParseResult::OK) {
        ESP_LOGW(TAG, "%s: %s", this->current_path_.c_str(), parse_result_str(r));
        return;
      }
      if (this->board_ != nullptr && this->board_->header.view_key != board->header.view_key)
        this->next_theme_ = 0;  // the theme colour may have changed
      this->board_ = std::move(board);
      this->board_path_ = this->current_path_;
      this->etags_[this->current_path_] = response.etag;
      this->offline_ = false;
      if (this->mode_ != Mode::RUNNING)
        this->set_mode_(Mode::RUNNING);
      // Forget dismissed banners the server no longer sends.
      std::set<std::string> live;
      for (const auto &a : this->board_->header.alerts)
        live.insert(a.key);
      for (const auto &a : this->board_->mentions)
        live.insert(a.key);
      for (auto it = this->dismissed_.begin(); it != this->dismissed_.end();)
        it = live.count(*it) ? std::next(it) : this->dismissed_.erase(it);
      this->dirty_ = true;
      return;
    }
    case 304:
      if (this->offline_) {
        this->offline_ = false;
        this->dirty_ = true;
      }
      return;
    case 401:
      ESP_LOGW(TAG, "The server doesn't know this panel any more; pairing again");
      this->token_.clear();
      this->display_id_.clear();
      this->save_state_();
      this->board_.reset();
      this->etags_.clear();
      this->clear_images_ = true;
      this->set_mode_(Mode::PAIRING);
      this->pair_note_ = this->str_("unpaired");
      return;
    case 404:
    case 409: {
      // TAB_OFF / NOT_FOUND / NIGHT: the wall goes back to Overview.
      std::string code = parse_error_code(response.body.data(), response.body.size());
      ESP_LOGD(TAG, "%s: %d %s", this->current_path_.c_str(), response.status, code.c_str());
      if (this->current_path_ != HOME_PATH) {
        this->navigate_(HOME_PATH);
      } else {
        this->next_poll_ = now + OFFLINE_RETRY_MS;
      }
      return;
    }
    case 423: {
      LockScreen lock;
      ParseResult r = parse_lock(response.body.data(), response.body.size(), lock);
      if (r == ParseResult::WRONG_VERSION) {
        this->set_mode_(Mode::UPDATE_NEEDED);
        return;
      }
      if (r == ParseResult::OK) {
        bool was_locked = this->mode_ == Mode::LOCKED;
        this->lock_ = std::move(lock);
        if (!was_locked) {
          this->current_path_ = HOME_PATH;
          this->set_mode_(Mode::LOCKED);
        } else {
          this->dirty_ = !this->pin_open_;  // don't wipe a PIN being typed
        }
      }
      return;
    }
    case 429:
      this->next_poll_ = now + parse_retry_after(response.retry_after, 30) * 1000;
      return;
    default:
      ESP_LOGW(TAG, "%s: status %d", this->current_path_.c_str(), response.status);
      if (!this->offline_) {
        this->offline_ = true;
        this->offline_since_ = this->board_ != nullptr ? this->board_->header.clock.time : "";
        this->dirty_ = true;
      }
      this->next_poll_ = now + OFFLINE_RETRY_MS;
      return;
  }
}

void SocialHomeDisplay::navigate_(const std::string &path) {
  if (path == this->current_path_ && this->board_path_ == path) {
    if (this->main_scroll_ != nullptr)
      lv_obj_scroll_to_y(this->main_scroll_, 0, LV_ANIM_ON);
    return;
  }
  ESP_LOGD(TAG, "Show %s", path.c_str());
  this->current_path_ = path;
  this->api_.cancel_if([](const HttpRequest &) { return true; });
  this->board_request_ = 0;
  this->theme_request_ = 0;
  this->request_board_();
}

// ---- clock, idle, backlight ----------------------------------------------------

std::string SocialHomeDisplay::clock_text_() const {
  const Clock *clock = nullptr;
  if (this->mode_ == Mode::LOCKED)
    clock = &this->lock_.clock;
  else if (this->board_ != nullptr)
    clock = &this->board_->header.clock;
  if (clock == nullptr)
    return "";
#ifdef USE_TIME
  if (this->time_ != nullptr && is_hhmm(clock->time)) {
    ESPTime utc = this->time_->utcnow();
    if (utc.is_valid())
      return format_hhmm(utc.timestamp, clock->utc_offset_minutes);
  }
#endif
  return clock->time;
}

void SocialHomeDisplay::tick_clock_() {
  if (this->clock_labels_.empty())
    return;
  std::string text = this->clock_text_();
  if (text == this->last_minute_)
    return;
  this->last_minute_ = text;
  for (lv_obj_t *label : this->clock_labels_)
    lv_label_set_text(label, text.c_str());
}

void SocialHomeDisplay::tick_idle_() {
  lv_display_t *disp = this->lvgl_->get_disp();
  if (disp == nullptr)
    return;
  const uint32_t inactive = lv_display_get_inactive_time(disp);
  if (this->mode_ != Mode::RUNNING || this->board_ == nullptr)
    return;
  const Settings &settings = this->board_->header.settings;
  if (settings.idle_return_minutes > 0 && this->current_path_ != HOME_PATH &&
      inactive > static_cast<uint32_t>(settings.idle_return_minutes) * 60000) {
    this->close_settings_();
    this->navigate_(HOME_PATH);
  }
  if (this->night_peek_ && inactive > NIGHT_PEEK_MS) {
    ESP_LOGD(TAG, "Night: back to the night screen");
    this->night_peek_ = false;
    this->dirty_ = true;
  }
  if (!settings.keep_screen_on && !this->screen_off_ && this->screen_off_ms_ > 0 && inactive > this->screen_off_ms_) {
    this->screen_off_ = true;
    // The first tap on a dark screen only wakes it.
    this->wake_layer_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(this->wake_layer_);
    lv_obj_set_size(this->wake_layer_, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(this->wake_layer_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(this->wake_layer_, SocialHomeDisplay::on_wake_, LV_EVENT_CLICKED, this);
  }
}

void SocialHomeDisplay::on_wake_(lv_event_t *event) {
  auto *self = static_cast<SocialHomeDisplay *>(lv_event_get_user_data(event));
  self->screen_off_ = false;
  if (self->wake_layer_ != nullptr) {
    lv_obj_delete_async(self->wake_layer_);
    self->wake_layer_ = nullptr;
  }
}

void SocialHomeDisplay::apply_backlight_() {
#ifdef USE_LIGHT
  if (this->backlight_ == nullptr)
    return;
  float target = 1.0f;
  if (this->screen_off_)
    target = 0.0f;
  else if (this->mode_ == Mode::RUNNING && this->board_ != nullptr && this->board_->header.is_night &&
           !this->night_peek_)
    target = this->night_brightness_;
  if (target == this->last_brightness_)
    return;
  this->last_brightness_ = target;
  auto call = this->backlight_->make_call();
  if (target <= 0.0f) {
    call.set_state(false);
  } else {
    call.set_state(true);
    call.set_brightness(target);
  }
  call.perform();
#endif
}

// ---- rendering -------------------------------------------------------------

void SocialHomeDisplay::render_() {
  this->dirty_ = false;
  lv_obj_t *screen = this->lvgl_->get_screen_active();
  // Same board again (a poll brought news): stay where the person scrolled.
  if (this->main_scroll_ != nullptr && this->built_path_ == this->board_path_)
    this->keep_scroll_ = lv_obj_get_scroll_y(this->main_scroll_);
  else
    this->keep_scroll_ = 0;
  this->built_path_ = this->board_path_;
  this->clock_labels_.clear();
  this->last_minute_.clear();
  this->main_scroll_ = nullptr;
  this->server_input_ = nullptr;
  this->pin_dots_ = nullptr;
  this->pin_note_ = nullptr;
  lv_obj_clean(screen);
  this->payloads_.clear();
  if (this->clear_images_) {
    // Only now: nothing on screen points at the cached pictures any more.
    this->images_.clear();
    this->clear_images_ = false;
  }

  const Palette &p = this->palette_();
  lv_obj_set_style_bg_color(screen, lv_color_hex(p.bg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  switch (this->mode_) {
    case Mode::BOOT:
      this->setup_card_(screen, "Social Home", this->str_("connecting"));
      break;
    case Mode::NO_WIFI:
      this->build_wifi_(screen);
      break;
    case Mode::NO_SERVER:
      this->build_server_(screen);
      break;
    case Mode::PAIRING:
      this->build_pairing_(screen);
      break;
    case Mode::LOCKED:
      this->build_lock_(screen);
      break;
    case Mode::UPDATE_NEEDED:
      this->build_update_(screen);
      break;
    case Mode::RUNNING:
      if (this->board_ == nullptr) {
        this->setup_card_(screen, "Social Home", this->str_("connecting"));
      } else if (this->board_->header.is_night && !this->night_peek_) {
        this->build_night_(screen);
      } else {
        this->build_frame_(screen);
      }
      break;
  }
  if (this->main_scroll_ != nullptr && this->keep_scroll_ > 0) {
    lv_obj_update_layout(this->main_scroll_);
    lv_obj_scroll_to_y(this->main_scroll_, this->keep_scroll_, LV_ANIM_OFF);
  }
  this->tick_clock_();
}

}  // namespace esphome::socialhome_display
