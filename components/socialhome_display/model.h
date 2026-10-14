// SPDX-License-Identifier: MPL-2.0
//
// The Social Home display API (v1) as plain C++ structs.
//
// Mirrors docs/display-api/v1/*.schema.json in the Social Home repository
// (pinned copy: tests/fixtures/display-api/v1/). Every string a person reads
// arrives pre-formatted from the server; the panel only lays it out.
// Unknown fields are ignored, missing optional fields keep the defaults below.
// No ESPHome / LVGL includes: the host tests compile this file with g++.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace esphome::socialhome_display {

/// The only API major version this firmware understands.
static constexpr int API_VERSION = 1;

/// A #rrggbb colour from the server; `valid == false` means "neutral look".
struct Color {
  bool valid{false};
  uint32_t rgb{0};

  static Color of(uint32_t rgb) { return Color{true, rgb}; }
  uint32_t or_else(uint32_t fallback) const { return this->valid ? this->rgb : fallback; }
};

/// An emoji drawn as a picture (GET /emoji/{code}); `code` empty means the
/// text is no emoji (e.g. a zone letter) and is drawn as text.
struct Emoji {
  std::string text;
  std::string code;
  bool empty() const { return this->text.empty() && this->code.empty(); }
};

struct Clock {
  std::string time;
  std::string date;
  std::string day;
  std::string iso;
  int64_t epoch{0};  ///< "unix" in the JSON
  std::string tz;
  int utc_offset_minutes{0};
};

struct Where {
  Emoji icon;
  std::string label;
};

struct Person {
  std::string id;
  std::string name;
  std::string initials;
  Color color;
  Color ink;
  std::string avatar;  ///< path below the server, empty when there is no picture
  std::string picture_hash;
  bool picture_view{false};
  std::optional<Where> where;
  int badge{0};
  std::string badge_label;
};

struct Settings {
  int idle_return_minutes{0};
  bool keep_screen_on{true};
  bool mention_sound{false};
  std::string night_start;
  std::string night_end;
  bool pin_set{false};
  /// The admin lets Home Assistant lift the privacy lock without the PIN
  /// (POST /unlock/ha). Absent (older servers): off.
  bool ha_unlock{false};
};

struct Rail {
  bool shown{true};
  bool many{false};
  std::string current;
};

struct NightItem {
  std::string when;
  std::string when_label;
  std::string time;
  std::string title;
  Emoji icon;
  std::vector<std::string> people;
};

struct Night {
  std::string time;
  std::string date;
  std::optional<NightItem> item;
  std::string wake_hint;
};

enum class TabKey : uint8_t { OVERVIEW, MESSAGES, NOTES, MAP, UNKNOWN };

struct Tab {
  TabKey key{TabKey::UNKNOWN};
  std::string label;
  std::string path;
  bool current{false};
  int badge{0};
  std::string badge_label;
};

struct Ack {
  std::string message_id;
  std::string person_id;
};

/// An @here / @person mention shown as a banner with "Got it".
struct Mention {
  std::string id;
  std::string key;
  std::string label;
  std::string person;
  std::string name;
  std::string from_label;
  std::string text;
  std::string caption;
  std::string when;
  std::string iso;
  std::optional<Ack> ack;
};

struct Message {
  std::string id;
  std::string person;
  std::string name;
  std::string from_label;
  Emoji icon;
  std::string text;
  std::string caption;
  bool is_new{false};
  std::string when;
  std::string iso;
  // Messages tab rows only.
  std::string time;
  bool rest{false};
};

enum class Status : uint8_t { NONE, CANCELLED, CHANGED, ADDED };

struct AgendaRow {
  std::string kind;
  bool is_break{false};
  std::string time;
  std::string title;
  Emoji icon;
  Status status{Status::NONE};
  std::string status_label;
  bool now{false};
  bool past{false};
  std::string meta;
  Color color;
  std::vector<std::string> people;
};

enum class ChoreState : uint8_t { OPEN, OVERDUE, DONE };

struct Chore {
  std::string title;
  ChoreState state{ChoreState::OPEN};
  std::string state_label;
  std::string late;
  std::string due;
  std::vector<std::string> people;
};

struct NoteColors {
  Color bg;
  Color ink;
};

struct Note {
  std::string text;
  Color color;
  std::optional<NoteColors> light;
  std::optional<NoteColors> dark;
};

struct TabEmpty {
  Emoji icon;
  std::string title;
  std::string text;
};

/// Shared by every board answer (overview, tabs, person).
struct Header {
  int api_version{0};
  std::string view;
  std::string view_key;
  std::string household;
  std::string locale;
  bool rtl{false};
  Clock clock;
  Settings settings;
  Rail rail;
  std::vector<Person> people;
  bool is_night{false};
  std::optional<Night> night;
  // Home boards only (overview / messages / notes / map).
  TabKey tab{TabKey::UNKNOWN};
  std::vector<Tab> tabs;
  std::vector<Mention> alerts;

  const Person *find_person(const std::string &id) const {
    for (const auto &p : this->people)
      if (p.id == id)
        return &p;
    return nullptr;
  }
};

// ---- Overview ---------------------------------------------------------

struct PresenceGroup {
  Emoji icon;
  std::string label;
  std::vector<std::string> people;
  std::vector<std::string> zones;  ///< parallel to people (away only; empty = no zone)
};

struct Presence {
  std::string label;
  std::optional<PresenceGroup> home;
  std::optional<PresenceGroup> away;
};

struct NextUp {
  std::string kind;
  std::string label;
  std::string time;
  Emoji icon;
  std::string title;
  std::string rel;
  std::vector<std::string> people;
  Color color;
};

struct MessagesCard {
  std::string label;
  bool see_all{false};
  std::vector<Message> items;
};

struct TimelineTick {
  std::string label;
  int x{0};
  bool minor{false};
};

struct TimelineBlock {
  std::string kind;
  std::string title;
  std::string label;
  int x{0};  ///< percent of the lane
  int w{0};  ///< percent of the lane
  int row{0};
  bool past{false};
  bool narrow{false};
  Emoji icon;
  int more{0};
};

struct TimelineLane {
  std::string person;
  Color color;
  Color ink;
  std::vector<std::string> all_day;
  int rows{1};
  std::vector<TimelineBlock> blocks;
};

struct Timeline {
  int start_hour{0};
  int end_hour{24};
  std::optional<int> now_x;
  std::string now_label;
  std::string label;
  std::vector<TimelineTick> ticks;
  std::vector<TimelineLane> lanes;
};

struct AtSchoolItem {
  std::vector<std::string> people;
  Emoji icon;
  std::string title;
  std::string until;
};

struct SchoolChange {
  Status status{Status::NONE};
  std::string status_label;
  std::string who;
  std::string title;
  std::string meta;
  std::vector<std::string> people;
};

struct Today {
  std::string label;
  std::string mode;  ///< timeline / list / empty
  std::optional<Timeline> timeline;
  std::vector<AgendaRow> list;
  std::string empty;
  Emoji empty_icon;
  std::string at_school_label;
  std::vector<AtSchoolItem> at_school;
  std::string changes_label;
  std::vector<SchoolChange> changes;
};

struct ChoresCard {
  std::string label;
  std::vector<Chore> items;
};

struct ShoppingCard {
  std::string label;
  std::vector<std::string> items;
  std::string more;
};

struct NotesCard {
  std::string label;
  bool see_all{false};
  std::vector<Note> items;
};

struct Overview {
  std::optional<Presence> presence;
  std::optional<NextUp> next;
  std::optional<MessagesCard> messages;
  std::optional<Today> today;
  std::optional<ChoresCard> chores;
  std::optional<ShoppingCard> shopping;
  std::optional<NotesCard> notes;
};

// ---- Tabs -------------------------------------------------------------

struct MessageDay {
  std::string key;
  std::string label;
  std::vector<Message> rows;
};

struct MessagesTab {
  std::vector<MessageDay> days;
  std::optional<TabEmpty> empty;
};

struct NotesTab {
  std::vector<Note> items;
  std::string more;
  std::optional<TabEmpty> empty;
};

struct MapSpot {
  std::string person;
  std::string name;
  Emoji icon;
  std::string place;
  std::string when;
  std::string age_label;
  bool stale{false};
  std::string zone;
};

struct MapPin {
  std::string id;
  std::string name;
  Emoji icon;
  double lat{0};
  double lon{0};
  int radius_m{0};
  Color color;
  bool is_home{false};
  bool occupied{false};
};

/// The map as one picture (GET {url}?w=&h=&scheme=light|dark).
struct MapImage {
  std::string url;
  int width{0};   ///< default size: the firmware's map area on 1280×800
  int height{0};
  std::string etag;  ///< ETag of the default (light) picture now
};

struct MapTab {
  std::string title;
  std::string label;
  std::string attribution;
  /// `image` in the JSON: absent from servers before the map picture,
  /// null when the server has no tile source.
  bool image_field{false};
  std::optional<MapImage> image;
  std::vector<MapSpot> spots;
  std::vector<MapPin> pins;
};

// ---- Person -----------------------------------------------------------

struct PersonHead {
  std::string id;
  std::string name;
  std::string initials;
  Color color;
  Color ink;
  std::string avatar;
  Emoji status_emoji;
  std::string status_text;
  std::optional<Where> where;
};

struct ReaderNow {
  std::string label;
  Emoji icon;
  std::string title;
  std::string meta;
  std::string until;
  Color color;
};

struct ReaderNext {
  std::string label;
  Emoji icon;
  std::string title;
  std::string meta;
  std::string time;
  std::string rel;
  Color color;
};

struct AgendaSection {
  std::string label;
  std::vector<AgendaRow> rows;
};

struct ReaderDay {
  std::string label;
  std::vector<AgendaRow> rows;
  std::string empty;
  Emoji empty_icon;
  std::optional<AgendaSection> tomorrow;
};

struct Reader {
  std::optional<ReaderNow> now;
  std::optional<ReaderNext> next;
  std::optional<AgendaSection> school;
  std::optional<ReaderDay> day;
  std::optional<ChoresCard> chores;
};

struct Tile {
  Emoji icon;
  std::string label;
  std::string caption;
  Color color;
  bool past{false};
  bool current{false};
  bool cancelled{false};
};

struct Wedge {
  int minutes{0};
  bool full{false};
  Color color;
};

struct AnalogClock {
  float hour_deg{0};
  float minute_deg{0};
  std::string label;
  std::optional<Wedge> wedge;
};

struct DayPart {
  std::string key;
  Emoji icon;
  std::string label;
  std::string state;  ///< past / now / later
  std::vector<Tile> tiles;
};

struct PictureDay {
  std::string label;
  bool empty{false};
  std::string empty_label;
  Emoji empty_icon;
  std::vector<DayPart> parts;
};

struct PictureTomorrow {
  std::string label;
  int sleeps{0};
  std::string sleeps_label;
  std::vector<Tile> tiles;
};

struct Job {
  Emoji icon;
  std::string label;
  std::string caption;
  bool done{false};
};

struct PictureNight {
  std::string when;
  Emoji moon;
  Emoji icon;
  std::string label;
  std::string caption;
};

struct Picture {
  AnalogClock clock;
  DayPart part_now;
  std::string now_next_label;
  std::string now_label;
  std::string next_label;
  std::optional<Tile> now;
  std::optional<Tile> next;
  std::optional<Wedge> next_pie;
  PictureDay day;
  std::optional<PictureTomorrow> tomorrow;
  std::string jobs_label;
  std::vector<Job> jobs;
  std::optional<Presence> where;
  std::vector<Message> messages;
  std::optional<PictureNight> night;
};

// ---- Answers ----------------------------------------------------------

enum class BoardKind : uint8_t { NONE, OVERVIEW, MESSAGES, NOTES, MAP, PERSON };

/// One parsed board answer. Only the section matching `kind` is filled.
struct Board {
  BoardKind kind{BoardKind::NONE};
  Header header;
  Overview overview;
  MessagesTab messages;
  NotesTab notes;
  MapTab map;
  PersonHead person;
  std::string person_kind;  ///< reader / picture
  std::vector<Mention> mentions;
  std::optional<Reader> reader;
  std::optional<Picture> picture;
};

/// The 423 body of a privacy-locked panel: what the wall tablet's lock
/// screen shows — household, clock, date, the PIN pad's words — never a
/// person.
struct LockScreen {
  int api_version{0};
  std::string household;
  std::string locale;
  Clock clock;
  /// The night window is on: night palette and a dimmed backlight, as the
  /// tablet's lock screen in `.dash-night`.
  bool is_night{false};
  std::string locked;
  std::string pin_button;
  std::string enter_pin;
  std::string unlock;
  std::string del;
  std::string wrong_pin;
  std::string tries_left;  ///< "Tries left: {n}"
  std::string last_try;    ///< with one try left, instead of tries_left
  std::string forgot_pin;  ///< under the PIN pad: who can change it
  /// Home Assistant may lift this lock (POST /unlock/ha), as the board's
  /// `settings.ha_unlock`. Not drawn. Absent (older servers): off.
  bool ha_unlock{false};
};

/// An error body ({"error": {"code", "detail", "params"}}).
struct ApiError {
  std::string code;
  std::string detail;
  int remaining{-1};    ///< DISPLAY_PIN_WRONG: wrong PINs left (-1: not sent)
  int retry_after{0};   ///< RATE_LIMITED from /unlock: seconds
};

struct Palette {
  uint32_t bg{0xF4F2EE};
  uint32_t surface{0xFFFFFF};
  uint32_t text{0x1F1D1A};
  uint32_t muted{0x5F5A52};
  uint32_t accent{0x2F6F5E};
  uint32_t accent_text{0xFFFFFF};
  uint32_t border{0xD6D1C8};
  uint32_t error_bg{0xFBE9E7};
  uint32_t error_text{0x8A1C12};
  uint32_t focus{0x1D5FD1};
};

struct StringPair {
  std::string key;
  std::string value;
};

/// Which of the theme's palettes the panel draws with.
enum class Scheme : uint8_t { LIGHT, DARK, NIGHT };

/// The palette for the household's colour `mode` ("light" / "dark" /
/// "auto"), as the browser dashboard picks it. `night`: the household's
/// night window is on — the server's `is_night` flag on the last board, the
/// same flag that brings up the night screen and dims the backlight, so the
/// panel and the browser agree on when night starts. `night_board`: the
/// board is up in the night window — the night clock, or the dashboard
/// someone woke from it.
///
/// The night board — and the lock screen while its 423 body says `is_night`
/// — always draws with the night palette, in every mode (the browser's
/// `.dash-night`). Everything else (settings, setup and error screens, the
/// lock screen by day): "light" and "dark" stay as they are,
/// day and night; "auto" — a panel has no OS setting to follow — is light by
/// day and dark in the night window. An unknown mode counts as "auto".
Scheme scheme_for(const std::string &mode, bool night, bool night_board);

struct Theme {
  int api_version{0};
  std::string locale{"en"};
  bool rtl{false};
  std::string mode{"auto"};
  Palette light;
  Palette dark;
  Palette night;
  std::vector<StringPair> strings;

  Theme();
  const Palette &palette(Scheme scheme) const;
  /// The server string `key`, else `fallback` (the panel's built-in English).
  const std::string &str(const char *key, const std::string &fallback) const;
};

struct PairStart {
  std::string pair_id;
  std::string code;
  int expires_in{0};
};

enum class PairState : uint8_t { PENDING, PAIRED, UNKNOWN };

struct PairStatus {
  PairState state{PairState::UNKNOWN};
  std::string token;
  std::string display_id;
};

}  // namespace esphome::socialhome_display
