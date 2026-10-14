// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/preferences.h"
#include "esphome/components/font/font.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#ifdef USE_LIGHT
#include "esphome/components/light/light_state.h"
#endif
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

#include "browser.h"
#include "claim_seal.h"
#include "images.h"
#include "map_picture.h"
#include "model.h"
#include "net.h"
#include "panel_strings.h"
#include "theme_cache.h"
#include "util.h"

namespace esphome::socialhome_display {

/// The fonts the browser dashboard's sizes map onto (see device fonts.yaml).
enum class FontRole : uint8_t { SMALL, LABEL, BODY, BODY_BOLD, LARGE, TITLE, DISPLAY, CLOCK, NIGHT, COUNT };

/// What the panel is doing.
enum class Mode : uint8_t {
  BOOT,
  NO_WIFI,        ///< waiting for Wi-Fi (Improv)
  NO_SERVER,      ///< asking for the Social Home address
  PAIRING,        ///< showing a pairing code
  RUNNING,        ///< the household dashboard
  LOCKED,         ///< privacy-locked (423)
  UPDATE_NEEDED,  ///< the server speaks a newer API
  CLAIMING,       ///< Connect from Home Assistant: claiming, or showing why it failed
};

/// One stored string (NVS on ESP32): survives reboots and firmware updates.
/// Kept under 256 bytes per preference, the limit some backends have.
struct StoredText {
  char value[160];
};

/// The panel's X25519 private key (NVS). The public half is derived at boot.
struct StoredKey {
  uint8_t private_key[PANEL_KEY_BYTES];
};

/// Social Home wall dashboard: pairs with a Social Home household, polls the
/// display API (/api/display/v1/) and draws the boards with LVGL.
class SocialHomeDisplay : public Component {
 public:
  void set_lvgl(lvgl::LvglComponent *lvgl) { this->lvgl_ = lvgl; }
  void set_server_url(const std::string &url) { this->default_server_ = url; }
  /// Host build only: a fixed list in place of the mDNS browse.
  void set_discovery_stub(const std::string &spec) { this->browser_.set_stub(spec); }
  void set_poll_interval(uint32_t interval_ms) { this->poll_interval_ms_ = interval_ms; }
  void set_screen_size(int width, int height) {
    this->screen_width_ = width;
    this->screen_height_ = height;
  }
  void set_ui_scale(float scale) { this->ui_scale_ = scale; }
  void set_font(FontRole role, font::Font *font) { this->fonts_[static_cast<size_t>(role)] = font; }
  void set_model(const std::string &model) { this->model_ = model; }
  void set_screen_off_minutes(uint32_t minutes) { this->screen_off_ms_ = minutes * 60000; }
  void set_night_brightness(float brightness) { this->night_brightness_ = brightness; }
#ifdef USE_LIGHT
  void set_backlight(light::LightState *light) { this->backlight_ = light; }
#endif
#ifdef USE_TIME
  void set_time(time::RealTimeClock *clock) { this->time_ = clock; }
#endif
#ifdef USE_TEXT_SENSOR
  /// "Social Home status" for Home Assistant (unpaired / pairing / paired / locked / unreachable).
  void set_status_sensor(text_sensor::TextSensor *sensor) { this->status_sensor_ = sensor; }
  /// "Social Home node": the ESPHome node name, so Social Home can name the connect action.
  void set_node_sensor(text_sensor::TextSensor *sensor) { this->node_sensor_ = sensor; }
  /// "Social Home key": the panel's X25519 public key (43-character
  /// base64url) Social Home seals the connect claim to.
  void set_key_sensor(text_sensor::TextSensor *sensor) { this->key_sensor_ = sensor; }
#endif

  void setup() override;
  void loop() override;
  void dump_config() override;
  // After LVGL (setup_priority::PROCESSOR) has created its display.
  float get_setup_priority() const override { return setup_priority::LATE; }

  /// Forget the server and token (settings screen, or an automation), and
  /// renew the panel key.
  void forget_server();
  /// The connect_social_home API action: Social Home (through Home Assistant)
  /// hands over its address and a one-time claim, sealed to this panel's key
  /// (claim_seal.h); the panel opens it and trades the claim for a token
  /// (POST /pair/claim). Replaces an existing pairing on success.
  void connect_from_home_assistant(const std::string &sealed);
  Mode get_mode() const { return this->mode_; }
  /// The board path shown now ("/api/display/v1/home", …).
  const std::string &get_path() const { return this->current_path_; }
  /// The household's string for `key` (panel_strings.h), else the panel's
  /// English: for LVGL widgets declared in YAML (common/improv.yaml).
  std::string get_string(const std::string &key) const;

 protected:
  // ---- state machine (socialhome_display.cpp)
  void set_mode_(Mode mode);
  void load_state_();
  void save_state_();
  void load_theme_cache_();
  /// Store the current theme when it differs from the cached one.
  void save_theme_cache_();
  bool network_up_() const;
  HttpRequest request_(const std::string &path, bool auth = true) const;
  void start_pairing_();
  void handle_api_(HttpResponse &response);
  void handle_pairing_(HttpResponse &response);
  /// Load the panel key, or make (and store) one when there is none.
  void load_panel_key_();
  /// A new panel key (Forget server): claims sealed to the old one no longer open.
  void renew_panel_key_();
  void publish_panel_key_();
  /// Trade an opened claim for a token at `url`.
  void claim_at_(const std::string &url, const std::string &claim);
  /// A connect request that didn't open: say so like a failed claim.
  void refuse_claim_();
  void handle_claim_(HttpResponse &response);
  void finish_claim_();
  void publish_status_();
  void handle_board_(HttpResponse &response);
  void handle_theme_(HttpResponse &response);
  void handle_lock_(HttpResponse &response);
  void handle_unlock_(HttpResponse &response);
  void handle_ack_(HttpResponse &response, const std::string &key);
  /// The server dropped this panel (401): forget the token, pair again.
  void unpair_();
  /// POST /lock, /unlock {pin}, /ack {message_id, person_id}.
  void request_lock_();
  void request_unlock_();
  void request_ack_(const std::string &key);
  const Mention *find_mention_(const std::string &key) const;
  /// The Map tab's picture: path with ?w=&h=&scheme= for this panel.
  std::string map_path_() const;
  void tick_map_();
  void request_board_();
  void request_theme_();
  void navigate_(const std::string &path);
  void tick_clock_();
  void tick_idle_();
  void apply_backlight_();
  void render_();
  std::string clock_text_() const;

  // ---- widgets (ui_common.cpp)
  const lv_font_t *font_(FontRole role) const;
  Scheme scheme_() const;
  const Palette &palette_() const;
  lv_obj_t *box_(lv_obj_t *parent);
  lv_obj_t *column_(lv_obj_t *parent, int gap);
  lv_obj_t *row_(lv_obj_t *parent, int gap);
  lv_obj_t *card_(lv_obj_t *parent);
  lv_obj_t *text_(lv_obj_t *parent, const std::string &text, FontRole font, uint32_t color);
  lv_obj_t *wrap_(lv_obj_t *parent, const std::string &text, FontRole font, uint32_t color);
  /// Text with its emoji in place, wrapping between words.
  lv_obj_t *rich_(lv_obj_t *parent, const std::string &text, FontRole font, uint32_t color);
  /// One line of text with its emoji in place, at most `max_width` wide
  /// (cut with "…").
  lv_obj_t *inline_(lv_obj_t *parent, const std::string &text, FontRole font, uint32_t color, int max_width);
  /// Emoji drawn inside text of `font`: about the line height.
  int emoji_px_(FontRole font) const;
  lv_obj_t *caps_(lv_obj_t *parent, const std::string &text, uint32_t color);
  lv_obj_t *emoji_(lv_obj_t *parent, const Emoji &emoji, int size);
  lv_obj_t *avatar_(lv_obj_t *parent, const std::string &person_id, int size);
  lv_obj_t *avatar_of_(lv_obj_t *parent, const std::string &initials, const std::string &avatar, Color color,
                       Color ink, int size);
  lv_obj_t *pill_(lv_obj_t *parent, const std::string &text, uint32_t bg, uint32_t fg, FontRole font);
  lv_obj_t *button_(lv_obj_t *parent, const std::string &text, bool primary, lv_event_cb_t cb, void *user_data);
  lv_obj_t *check_mark_(lv_obj_t *parent, int size, uint32_t color, int width);
  lv_obj_t *poly_(lv_obj_t *parent, std::initializer_list<lv_point_precise_t> points, int width, uint32_t color);
  lv_obj_t *clock_label_(lv_obj_t *parent, FontRole font, uint32_t color);
  void long_press_settings_(lv_obj_t *obj);
  void people_row_(lv_obj_t *parent, const std::vector<std::string> &people, int size);
  lv_obj_t *section_card_(lv_obj_t *parent, const std::string &label);
  lv_obj_t *agenda_row_(lv_obj_t *parent, const AgendaRow &row, bool show_people);
  lv_obj_t *chore_row_(lv_obj_t *parent, const Chore &chore, bool show_people);
  lv_obj_t *message_row_(lv_obj_t *parent, const Message &message, bool show_time, FontRole font);

  // ---- screens
  void build_frame_(lv_obj_t *screen);            // ui_common.cpp: rail + scrolling main column
  void build_rail_(lv_obj_t *parent);
  lv_obj_t *build_lock_button_(lv_obj_t *parent);
  /// The rail / corner lock button shows: the household has a PIN.
  bool can_lock_() const;
  void build_tabs_(lv_obj_t *parent);
  void build_alerts_(lv_obj_t *parent, const std::vector<Mention> &alerts);
  void build_offline_(lv_obj_t *parent);
  void build_overview_(lv_obj_t *parent);         // ui_overview.cpp
  void build_timeline_(lv_obj_t *parent, const Timeline &line);
  void build_messages_tab_(lv_obj_t *parent);     // ui_tabs.cpp
  void build_notes_tab_(lv_obj_t *parent);
  void build_map_tab_(lv_obj_t *parent);
  void build_person_(lv_obj_t *parent);           // ui_person.cpp
  void build_reader_(lv_obj_t *parent);
  void build_picture_(lv_obj_t *parent);
  void build_analog_clock_(lv_obj_t *parent, const AnalogClock &clock, int size);
  lv_obj_t *build_tile_(lv_obj_t *parent, const Tile &tile, int size);
  void build_night_(lv_obj_t *screen);            // ui_setup.cpp
  void build_lock_(lv_obj_t *screen);
  void build_wifi_(lv_obj_t *screen);
  void build_server_(lv_obj_t *screen);
  void build_pairing_(lv_obj_t *screen);
  void build_update_(lv_obj_t *screen);
  void build_claim_(lv_obj_t *screen);
  lv_obj_t *setup_card_(lv_obj_t *screen, const std::string &title, const std::string &text);
  /// The address screen's "Found on this network" rows, (re)filled in place
  /// so a browse never wipes what is being typed.
  void fill_found_();
  /// Show / hide the address screen's keyboard (the card shrinks to fit).
  void show_keyboard_(bool show);
  /// Use `url` as the server and go on to pairing.
  void use_server_(const std::string &url);
  void open_settings_();
  void close_settings_();

  // ---- event callbacks
  static void on_navigate_(lv_event_t *event);
  static void on_ack_(lv_event_t *event);
  static void on_lock_(lv_event_t *event);
  static void on_wake_(lv_event_t *event);
  static void on_night_tap_(lv_event_t *event);
  static void on_server_ready_(lv_event_t *event);
  static void on_change_server_(lv_event_t *event);
  static void on_found_(lv_event_t *event);
  static void on_server_tap_(lv_event_t *event);
  static void on_keyboard_cancel_(lv_event_t *event);
  static void on_retry_(lv_event_t *event);
  static void on_claim_done_(lv_event_t *event);
  static void on_pin_key_(lv_event_t *event);
  static void on_settings_press_(lv_event_t *event);
  static void on_settings_close_(lv_event_t *event);
  static void on_forget_(lv_event_t *event);
  static void on_forget_confirm_(lv_event_t *event);

  /// The server's string for `key`, else the panel's built-in English.
  const std::string &str_(const char *key) const;
  /// A string an LVGL callback can point at until the next rebuild.
  std::string *payload_(const std::string &value);

  // config
  lvgl::LvglComponent *lvgl_{nullptr};
  std::string default_server_;
  uint32_t poll_interval_ms_{30000};
  int screen_width_{1280};
  int screen_height_{800};
  float ui_scale_{1.0f};
  font::Font *fonts_[static_cast<size_t>(FontRole::COUNT)]{};
  std::string model_;
  uint32_t screen_off_ms_{10 * 60000};
  float night_brightness_{0.15f};
#ifdef USE_LIGHT
  light::LightState *backlight_{nullptr};
#endif
#ifdef USE_TIME
  time::RealTimeClock *time_{nullptr};
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *status_sensor_{nullptr};
  text_sensor::TextSensor *node_sensor_{nullptr};
  text_sensor::TextSensor *key_sensor_{nullptr};
#endif

  // persisted
  ESPPreferenceObject pref_server_;
  ESPPreferenceObject pref_token_;
  ESPPreferenceObject pref_display_;
  ESPPreferenceObject pref_key_;
  ESPPreferenceObject pref_theme_head_;
  ESPPreferenceObject pref_theme_[THEME_CACHE_CHUNKS];
  uint32_t theme_cached_{0};  ///< checksum of the cached theme (0: none)
  PanelKeyPair panel_key_;
  std::string panel_key_text_;  ///< empty when no key could be made
  std::string server_;
  std::string token_;
  std::string display_id_;

  // runtime
  Metrics m_;
  Mode mode_{Mode::BOOT};
  bool dirty_{true};
  bool clear_images_{false};
  uint32_t mode_since_{0};
  HttpWorker api_{"sh_api", 4};
  ImageCache images_;
  MapPicture map_;
  uint32_t next_id_{1};
  uint32_t board_request_{0};
  uint32_t theme_request_{0};
  uint32_t pair_request_{0};
  uint32_t lock_request_{0};
  uint32_t unlock_request_{0};
  std::map<uint32_t, std::string> ack_requests_;  ///< request id → mention key
  bool lock_refused_{false};  ///< 409 DASHBOARD_PIN_REQUIRED: no PIN after all
  uint32_t next_poll_{0};
  uint32_t next_theme_{0};
  uint32_t next_pair_poll_{0};
  uint32_t pair_expires_{0};
  /// Connect from Home Assistant: the request in flight, when a failure
  /// screen gives up, and the pairing to go back to if the claim fails.
  uint32_t claim_request_{0};
  uint32_t claim_until_{0};
  std::string claim_error_;
  std::string prev_server_;
  std::string prev_token_;
  std::string prev_display_id_;
  /// The MAC as ESPHome reports it to Home Assistant ("AA:BB:…"), sent as
  /// X-Display-Mac so Social Home can match the panel to its HA device.
  std::string mac_;
  const char *published_status_{nullptr};
  std::string current_path_{"/api/display/v1/home"};
  std::string requested_path_;
  std::map<std::string, std::string> etags_;
  std::unique_ptr<Board> board_;
  std::string board_path_;
  std::string theme_etag_;
  Theme theme_;
  LockScreen lock_;
  PairStart pairing_;
  std::string pair_note_;
  std::string error_;
  bool offline_{false};
  std::string offline_since_;
  std::set<std::string> dismissed_;
  bool night_peek_{false};
  bool screen_off_{false};
  float last_brightness_{-1.0f};
  std::string last_minute_;
  std::vector<lv_obj_t *> clock_labels_;
  lv_obj_t *main_scroll_{nullptr};
  int32_t keep_scroll_{0};
  std::string built_path_;
  lv_obj_t *settings_{nullptr};
  lv_obj_t *confirm_{nullptr};
  lv_obj_t *wake_layer_{nullptr};
  lv_obj_t *server_input_{nullptr};
  lv_obj_t *server_card_{nullptr};
  lv_obj_t *server_keyboard_{nullptr};
  lv_obj_t *found_list_{nullptr};
  std::string typed_;  ///< the address field's text across a rebuild
  ServerBrowser browser_;
  std::vector<FoundServer> found_;
  bool found_searched_{false};
  lv_font_t keyboard_font_{};
  std::string pin_;
  lv_obj_t *pin_dots_{nullptr};
  lv_obj_t *pin_note_{nullptr};
  bool pin_open_{false};
  std::string pin_message_;  ///< under the PIN pad ("Wrong PIN · 4 tries left")
  bool pin_error_{false};
  uint32_t pin_wait_until_{0};  ///< 429: no new try before this (millis)
  uint32_t press_started_{0};
  bool press_fired_{false};
  std::vector<std::unique_ptr<std::string>> payloads_;
};

}  // namespace esphome::socialhome_display
