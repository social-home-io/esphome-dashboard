// SPDX-License-Identifier: MPL-2.0
// The parser against every pinned Social Home fixture.
#include <cstring>
#include <string>

#include "check.h"
#include "parser.h"

using namespace esphome::socialhome_display;

namespace {

Board board(const std::string &name, ParseResult expect = ParseResult::OK) {
  std::string json = check::fixture(name);
  Board out;
  ParseResult r = parse_board(json.data(), json.size(), out);
  if (r != expect)
    check::fail(__FILE__, __LINE__, name + ": " + parse_result_str(r));
  return out;
}

/// `json` with `"api_version": 1` swapped for `replacement`.
std::string with(const std::string &json, const std::string &needle, const std::string &replacement) {
  std::string out = json;
  size_t at = out.find(needle);
  if (at == std::string::npos) {
    check::fail(__FILE__, __LINE__, "needle not found: " + needle);
    return out;
  }
  out.replace(at, needle.size(), replacement);
  return out;
}

}  // namespace

TEST(overview_header) {
  Board b = board("home.json");
  CHECK(b.kind == BoardKind::OVERVIEW);
  const Header &h = b.header;
  CHECK_EQ(h.api_version, 1);
  CHECK_EQ(h.view, std::string("overview"));
  CHECK_EQ(h.view_key, std::string("b202181d92683fb8"));
  CHECK_EQ(h.household, std::string("Home"));
  CHECK(!h.rtl);
  CHECK_EQ(h.clock.time, std::string("14:32"));
  CHECK_EQ(h.clock.date, std::string("Monday, October 12"));
  CHECK_EQ(h.clock.epoch, int64_t{1791808320});
  CHECK_EQ(h.clock.utc_offset_minutes, 120);
  CHECK_EQ(h.settings.idle_return_minutes, 3);
  CHECK(h.settings.keep_screen_on);
  CHECK_EQ(h.settings.night_start, std::string("22:00"));
  CHECK(h.rail.shown);
  CHECK(h.rail.current.empty());
  CHECK(!h.is_night);
  CHECK(!h.night.has_value());
  CHECK(h.tab == TabKey::OVERVIEW);
}

TEST(overview_people) {
  Board b = board("home.json");
  const auto &people = b.header.people;
  CHECK_EQ(people.size(), size_t{4});
  CHECK_EQ(people[0].name, std::string("Pascal"));
  CHECK(people[0].color.valid);
  CHECK_EQ(people[0].color.rgb, 0x357bbbu);
  CHECK_EQ(people[0].ink.rgb, 0x000000u);
  CHECK(people[0].avatar.empty());
  CHECK_EQ(people[1].avatar, std::string("/api/display/v1/avatar/u-anna?v=pic1"));
  CHECK_EQ(people[1].where->icon.code, std::string("1f697"));
  CHECK_EQ(people[1].where->label, std::string("Away"));
  CHECK_EQ(people[2].badge, 1);
  CHECK_EQ(people[2].badge_label, std::string("New message for Mia"));
  CHECK(people[3].picture_view);
  const Person *leo = b.header.find_person("u-leo");
  CHECK(leo != nullptr && leo->name == "Leo");
  CHECK(b.header.find_person("nobody") == nullptr);
}

TEST(overview_tabs_and_alerts) {
  Board b = board("home.json");
  const auto &tabs = b.header.tabs;
  CHECK_EQ(tabs.size(), size_t{4});
  CHECK(tabs[0].key == TabKey::OVERVIEW && tabs[0].current);
  CHECK(tabs[1].key == TabKey::MESSAGES);
  CHECK_EQ(tabs[1].badge, 1);
  CHECK_EQ(tabs[1].path, std::string("/api/display/v1/messages"));
  CHECK(tabs[3].key == TabKey::MAP);
  CHECK_EQ(b.header.alerts.size(), size_t{1});
  const Mention &a = b.header.alerts[0];
  CHECK_EQ(a.label, std::string("Message for everyone"));
  CHECK_EQ(a.name, std::string("Anna"));
  // Text arrives raw: the panel must never interpret markup.
  CHECK_EQ(a.text, std::string("@here Dinner is <b>ready</b>!"));
  CHECK(a.ack.has_value());
  CHECK_EQ(a.ack->message_id, std::string("h1"));
  CHECK(a.ack->person_id.empty());
}

TEST(overview_sections) {
  Board b = board("home.json");
  const Overview &o = b.overview;
  CHECK(o.presence.has_value());
  CHECK_EQ(o.presence->label, std::string("Who's home"));
  CHECK_EQ(o.presence->home->people.size(), size_t{2});
  CHECK_EQ(o.presence->away->people[1], std::string("u-leo"));
  CHECK_EQ(o.presence->away->zones[1], std::string("School"));
  CHECK(o.presence->away->zones[0].empty());
  CHECK_EQ(o.next->title, std::string("Swimming"));
  CHECK_EQ(o.next->rel, std::string("in 28 min"));
  CHECK(o.next->icon.empty());
  CHECK_EQ(o.messages->items.size(), size_t{3});
  CHECK(o.messages->items[0].is_new);
  CHECK_EQ(o.messages->items[1].icon.code, std::string("1f4f7"));
  CHECK_EQ(o.chores->items.size(), size_t{3});
  CHECK(o.chores->items[1].state == ChoreState::OVERDUE);
  CHECK_EQ(o.chores->items[1].late, std::string("2 days late"));
  CHECK(o.chores->items[2].state == ChoreState::DONE);
  CHECK_EQ(o.shopping->items.size(), size_t{6});
  CHECK_EQ(o.shopping->more, std::string("+ 5 more"));
  CHECK(o.notes->see_all);
  CHECK_EQ(o.notes->items[0].light->bg.rgb, 0xfdef9du);
  CHECK(!o.notes->items[1].color.valid);
  CHECK(!o.notes->items[1].light.has_value());
}

TEST(overview_timeline) {
  Board b = board("home.json");
  const Today &t = *b.overview.today;
  CHECK_EQ(t.mode, std::string("timeline"));
  CHECK_EQ(t.list.size(), size_t{6});
  CHECK(t.list[2].now);
  CHECK(t.list[3].past);
  CHECK_EQ(t.list[0].time, std::string("All day"));
  const Timeline &line = *t.timeline;
  CHECK_EQ(line.start_hour, 8);
  CHECK_EQ(line.end_hour, 22);
  CHECK(line.now_x.has_value() && *line.now_x == 47);
  CHECK_EQ(line.ticks.size(), size_t{6});
  CHECK(line.ticks[1].minor);
  CHECK_EQ(line.lanes.size(), size_t{4});
  CHECK_EQ(line.lanes[2].all_day.size(), size_t{1});
  CHECK_EQ(line.lanes[2].blocks[0].kind, std::string("school"));
  CHECK_EQ(line.lanes[2].blocks[0].x, 2);
  CHECK_EQ(line.lanes[2].blocks[0].w, 54);
  CHECK(line.lanes[0].blocks[0].narrow);
  CHECK_EQ(t.at_school.size(), size_t{1});
  CHECK_EQ(t.at_school[0].until, std::string("until 15:00"));
  CHECK_EQ(t.changes.size(), size_t{3});
  CHECK(t.changes[0].status == Status::CANCELLED);
  CHECK(t.changes[1].status == Status::CHANGED);
  CHECK_EQ(t.changes[1].meta, std::string("Room B7"));
  CHECK(t.changes[2].status == Status::ADDED);
}

TEST(overview_night) {
  Board b = board("home-night.json");
  CHECK(b.header.is_night);
  CHECK(b.header.night.has_value());
  CHECK_EQ(b.header.night->wake_hint, std::string("Tap to show the dashboard"));
  CHECK(b.header.night->item.has_value());
  CHECK_EQ(b.header.night->item->when_label, std::string("Tomorrow"));
  CHECK_EQ(b.header.night->item->icon.code, std::string("1f392"));
  CHECK(!b.overview.messages.has_value());
  CHECK_EQ(b.header.tabs.size(), size_t{2});
}

TEST(messages_tab) {
  Board b = board("messages.json");
  CHECK(b.kind == BoardKind::MESSAGES);
  CHECK(b.header.tab == TabKey::MESSAGES);
  CHECK_EQ(b.messages.days.size(), size_t{1});
  CHECK_EQ(b.messages.days[0].label, std::string("Today"));
  CHECK_EQ(b.messages.days[0].rows.size(), size_t{3});
  CHECK_EQ(b.messages.days[0].rows[0].time, std::string("14:28"));
  CHECK(!b.messages.days[0].rows[0].rest);
  CHECK(!b.messages.empty.has_value());
}

TEST(notes_tab) {
  Board b = board("notes.json");
  CHECK(b.kind == BoardKind::NOTES);
  CHECK_EQ(b.notes.items.size(), size_t{2});
  CHECK_EQ(b.notes.more, std::string("+ 3 more"));
  CHECK_EQ(b.notes.items[0].dark->ink.rgb, 0x1f1d1au);
}

TEST(map_tab) {
  Board b = board("map.json");
  CHECK(b.kind == BoardKind::MAP);
  CHECK_EQ(b.map.title, std::string("Who's where"));
  CHECK_EQ(b.map.spots.size(), size_t{4});
  CHECK_EQ(b.map.spots[3].name, std::string("Anna"));
  CHECK_EQ(b.map.spots[3].place, std::string("Away"));
  CHECK_EQ(b.map.spots[3].age_label, std::string("updated 5 min ago"));
  CHECK(b.map.spots[3].zone.empty());
  CHECK_EQ(b.map.pins.size(), size_t{3});
  CHECK(b.map.pins[2].icon.code.empty());
  CHECK_EQ(b.map.pins[2].icon.text, std::string("W"));
  CHECK_EQ(b.map.attribution, std::string("\xC2\xA9 OpenStreetMap contributors"));
}

TEST(person_picture) {
  Board b = board("person-picture.json");
  CHECK(b.kind == BoardKind::PERSON);
  CHECK_EQ(b.person_kind, std::string("picture"));
  CHECK_EQ(b.person.name, std::string("Leo"));
  CHECK(b.picture.has_value());
  CHECK(!b.reader.has_value());
  const Picture &p = *b.picture;
  CHECK_EQ(p.clock.hour_deg, 315.0f);
  CHECK_EQ(p.clock.minute_deg, 180.0f);
  CHECK(p.clock.wedge.has_value() && p.clock.wedge->full);
  CHECK_EQ(p.part_now.key, std::string("morning"));
  CHECK(p.now.has_value() && p.now->current);
  CHECK_EQ(p.next->caption, std::string("Swimming \xC2\xB7 15:00"));
  CHECK(p.next_pie.has_value());
  CHECK_EQ(p.day.parts.size(), size_t{4});
  CHECK_EQ(p.day.parts[0].tiles.size(), size_t{4});
  CHECK(p.day.parts[0].tiles[2].cancelled);
  CHECK_EQ(p.day.parts[3].tiles.size(), size_t{0});
  CHECK_EQ(p.tomorrow->sleeps, 1);
  CHECK_EQ(p.jobs.size(), size_t{2});
  CHECK(p.jobs[1].done);
  CHECK_EQ(p.messages.size(), size_t{3});
  CHECK(p.where.has_value());
  CHECK_EQ(b.mentions.size(), size_t{1});
  CHECK_EQ(b.mentions[0].ack->person_id, std::string("u-leo"));
}

TEST(person_reader) {
  Board b = board("person-reader.json");
  CHECK_EQ(b.person_kind, std::string("reader"));
  CHECK_EQ(b.person.status_emoji.code, std::string("1f634"));
  CHECK(b.reader.has_value());
  CHECK_EQ(b.reader->now->until, std::string("until 17:00"));
  CHECK_EQ(b.reader->next->rel, std::string("in 3 h"));
  CHECK(!b.reader->school.has_value());
  CHECK_EQ(b.reader->day->rows.size(), size_t{2});
  CHECK_EQ(b.reader->day->tomorrow->rows.size(), size_t{1});
  CHECK_EQ(b.reader->chores->items.size(), size_t{1});
}

TEST(person_school) {
  Board b = board("person-school.json");
  CHECK(b.reader->school.has_value());
  const auto &rows = b.reader->school->rows;
  CHECK_EQ(rows.size(), size_t{5});
  CHECK(rows[0].now);
  CHECK(rows[1].is_break);
  CHECK(rows[2].status == Status::CANCELLED);
  CHECK_EQ(rows[4].icon.code, std::string("1f1ec-1f1e7"));
  CHECK_EQ(b.reader->now->meta, std::string("Room B12 \xC2\xB7 Keller"));
}

TEST(lock_screen) {
  std::string json = check::fixture("locked.json");
  LockScreen lock;
  CHECK(parse_lock(json.data(), json.size(), lock) == ParseResult::OK);
  CHECK_EQ(lock.household, std::string("Home"));
  CHECK_EQ(lock.clock.time, std::string("08:00"));
  CHECK_EQ(lock.locked, std::string("Screen locked"));
  CHECK_EQ(lock.enter_pin, std::string("Enter PIN"));
  CHECK_EQ(lock.del, std::string("Delete last digit"));
  CHECK(lock.unlock_path.empty());
  // A board is not a lock body.
  std::string home = check::fixture("home.json");
  CHECK(parse_lock(home.data(), home.size(), lock) == ParseResult::BAD_SHAPE);
}

TEST(theme) {
  std::string json = check::fixture("theme.json");
  Theme t;
  CHECK(parse_theme(json.data(), json.size(), t) == ParseResult::OK);
  CHECK_EQ(t.light.accent, 0x2f6f5eu);
  CHECK_EQ(t.light.bg, 0xf4f2eeu);
  CHECK_EQ(t.night.bg, 0x000000u);
  CHECK_EQ(t.dark.text, 0xf1eee9u);
  static const std::string FALLBACK = "fallback";
  CHECK_EQ(t.str("got_it", FALLBACK), std::string("Got it"));
  CHECK_EQ(t.str("pair_title", FALLBACK), std::string("Pair this panel"));
  CHECK_EQ(t.str("no_such_key", FALLBACK), FALLBACK);
  Theme defaults;
  CHECK_EQ(defaults.light.accent, 0x2f6f5eu);
  CHECK_EQ(defaults.str("got_it", FALLBACK), FALLBACK);
}

TEST(every_board_fixture_parses) {
  for (const char *name : {"home.json", "home-night.json", "messages.json", "notes.json", "map.json",
                           "person-picture.json", "person-reader.json", "person-school.json"})
    board(name);
}

TEST(unknown_fields_are_ignored) {
  std::string json = check::fixture("home.json");
  json = with(json, "\"api_version\": 1", "\"api_version\": 1, \"future\": {\"x\": [1, 2]}");
  json = with(json, "\"label\": \"Who's home\"", "\"label\": \"Who's home\", \"extra\": true");
  Board b;
  CHECK(parse_board(json.data(), json.size(), b) == ParseResult::OK);
  CHECK_EQ(b.overview.presence->label, std::string("Who's home"));
}

TEST(missing_optional_fields_default) {
  const char *json =
      R"({"api_version": 1, "view": "overview", "overview": {"today": {"mode": "empty", "empty": "Nothing today"}}})";
  Board b;
  CHECK(parse_board(json, strlen(json), b) == ParseResult::OK);
  CHECK(b.header.people.empty());
  CHECK(b.header.settings.keep_screen_on);
  CHECK(b.header.rail.shown);
  CHECK(!b.overview.presence.has_value());
  CHECK(!b.overview.today->timeline.has_value());
  CHECK_EQ(b.overview.today->empty, std::string("Nothing today"));
}

TEST(wrong_api_version_asks_for_an_update) {
  std::string json = with(check::fixture("home.json"), "\"api_version\": 1", "\"api_version\": 2");
  Board b;
  CHECK(parse_board(json.data(), json.size(), b) == ParseResult::WRONG_VERSION);
  const char *no_version = R"({"view": "overview", "overview": {}})";
  CHECK(parse_board(no_version, strlen(no_version), b) == ParseResult::WRONG_VERSION);
  LockScreen lock;
  std::string locked = with(check::fixture("locked.json"), "\"api_version\": 1", "\"api_version\": 3");
  CHECK(parse_lock(locked.data(), locked.size(), lock) == ParseResult::WRONG_VERSION);
}

TEST(bad_input) {
  Board b;
  CHECK(parse_board("", 0, b) == ParseResult::BAD_JSON);
  CHECK(parse_board("[1,2]", 5, b) == ParseResult::BAD_JSON);
  CHECK(parse_board("{\"api_version\": 1", 17, b) == ParseResult::BAD_JSON);
  const char *view = R"({"api_version": 1, "view": "space"})";
  CHECK(parse_board(view, strlen(view), b) == ParseResult::BAD_SHAPE);
  const char *missing = R"({"api_version": 1, "view": "notes"})";
  CHECK(parse_board(missing, strlen(missing), b) == ParseResult::BAD_SHAPE);
}

TEST(colors) {
  CHECK(parse_color("#2f6f5e").valid);
  CHECK_EQ(parse_color("#2F6F5E").rgb, 0x2f6f5eu);
  CHECK(!parse_color("2f6f5e").valid);
  CHECK(!parse_color("#2f6f5").valid);
  CHECK(!parse_color("#2f6f5g").valid);
  CHECK(!parse_color(nullptr).valid);
  CHECK_EQ(Color{}.or_else(0x123456), 0x123456u);
}

TEST(pairing) {
  const char *start = R"({"pair_id": "abc", "code": "K7Q2XM", "expires_in": 600})";
  PairStart ps;
  CHECK(parse_pair_start(start, strlen(start), ps) == ParseResult::OK);
  CHECK_EQ(ps.code, std::string("K7Q2XM"));
  CHECK_EQ(ps.expires_in, 600);
  CHECK(parse_pair_start("{}", 2, ps) == ParseResult::BAD_SHAPE);
  PairStatus st;
  const char *pending = R"({"status": "pending"})";
  CHECK(parse_pair_status(pending, strlen(pending), st) == ParseResult::OK);
  CHECK(st.state == PairState::PENDING);
  const char *paired = R"({"status": "paired", "token": "tok", "display_id": "d1"})";
  CHECK(parse_pair_status(paired, strlen(paired), st) == ParseResult::OK);
  CHECK(st.state == PairState::PAIRED);
  CHECK_EQ(st.token, std::string("tok"));
  CHECK_EQ(st.display_id, std::string("d1"));
  const char *no_token = R"({"status": "paired"})";
  CHECK(parse_pair_status(no_token, strlen(no_token), st) == ParseResult::BAD_SHAPE);
}

TEST(error_codes) {
  const char *night = R"({"error": {"code": "NIGHT", "detail": "Night mode shows Overview only."}})";
  CHECK_EQ(parse_error_code(night, strlen(night)), std::string("NIGHT"));
  CHECK_EQ(parse_error_code("oops", 4), std::string(""));
}
