// SPDX-License-Identifier: MPL-2.0
#include "parser.h"

#include <cstring>
#include <cstdlib>
#include <utility>

// Same configuration as ESPHome's json component (json_util.h), so every
// translation unit sees one ArduinoJson.
#define ARDUINOJSON_ENABLE_STD_STRING 1  // NOLINT
#define ARDUINOJSON_USE_LONG_LONG 1      // NOLINT
#include <ArduinoJson.h>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace esphome::socialhome_display {

namespace {

using JV = ArduinoJson::JsonVariantConst;
using JA = ArduinoJson::JsonArrayConst;
using JO = ArduinoJson::JsonObjectConst;

#ifdef ESP_PLATFORM
/// Boards are 10–20 kB of JSON; keep the parsed tree in PSRAM.
class PsramAllocator : public ArduinoJson::Allocator {
 public:
  void *allocate(size_t size) override {
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p != nullptr ? p : malloc(size);
  }
  void deallocate(void *ptr) override { free(ptr); }
  void *reallocate(void *ptr, size_t size) override {
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p != nullptr ? p : realloc(ptr, size);
  }
  static ArduinoJson::Allocator *instance() {
    static PsramAllocator alloc;
    return &alloc;
  }
};
ArduinoJson::Allocator *allocator() { return PsramAllocator::instance(); }
#else
ArduinoJson::Allocator *allocator() { return ArduinoJson::detail::DefaultAllocator::instance(); }
#endif

ParseResult load(const char *data, size_t len, ArduinoJson::JsonDocument &doc) {
  auto err = ArduinoJson::deserializeJson(doc, data, len, ArduinoJson::DeserializationOption::NestingLimit(24));
  if (err || !doc.is<JO>())
    return ParseResult::BAD_JSON;
  return ParseResult::OK;
}

ParseResult check_version(JO root) {
  JV v = root["api_version"];
  if (!v.is<int>() || v.as<int>() != API_VERSION)
    return ParseResult::WRONG_VERSION;
  return ParseResult::OK;
}

std::string s(JV v) {
  if (v.is<const char *>())
    return v.as<const char *>();
  return {};
}

bool b(JV v, bool fallback = false) { return v.is<bool>() ? v.as<bool>() : fallback; }

int i(JV v, int fallback = 0) {
  if (v.is<int>())
    return v.as<int>();
  if (v.is<double>())
    return static_cast<int>(v.as<double>());
  return fallback;
}

double d(JV v) { return v.is<double>() ? v.as<double>() : 0.0; }

Color c(JV v) { return v.is<const char *>() ? parse_color(v.as<const char *>()) : Color{}; }

Emoji emoji(JV v) {
  Emoji e;
  if (v.is<JO>()) {
    e.text = s(v["text"]);
    e.code = s(v["code"]);
  }
  return e;
}

std::vector<std::string> strings(JV v) {
  std::vector<std::string> out;
  for (JV item : v.as<JA>())
    if (item.is<const char *>())
      out.emplace_back(item.as<const char *>());
  return out;
}

Status status(JV v) {
  std::string t = s(v);
  if (t == "cancelled")
    return Status::CANCELLED;
  if (t == "changed")
    return Status::CHANGED;
  if (t == "added")
    return Status::ADDED;
  return Status::NONE;
}

TabKey tab_key(const std::string &t) {
  if (t == "overview")
    return TabKey::OVERVIEW;
  if (t == "messages")
    return TabKey::MESSAGES;
  if (t == "notes")
    return TabKey::NOTES;
  if (t == "map")
    return TabKey::MAP;
  return TabKey::UNKNOWN;
}

Clock clock(JV v) {
  Clock out;
  out.time = s(v["time"]);
  out.date = s(v["date"]);
  out.day = s(v["day"]);
  out.iso = s(v["iso"]);
  if (v["unix"].is<int64_t>())
    out.epoch = v["unix"].as<int64_t>();
  out.tz = s(v["tz"]);
  out.utc_offset_minutes = i(v["utc_offset_minutes"]);
  return out;
}

std::optional<Where> where(JV v) {
  if (!v.is<JO>())
    return std::nullopt;
  return Where{emoji(v["icon"]), s(v["label"])};
}

Person person(JV v) {
  Person p;
  p.id = s(v["id"]);
  p.name = s(v["name"]);
  p.initials = s(v["initials"]);
  p.color = c(v["color"]);
  p.ink = c(v["ink"]);
  p.avatar = s(v["avatar"]);
  p.picture_hash = s(v["picture_hash"]);
  p.picture_view = b(v["picture_view"]);
  p.where = where(v["where"]);
  p.badge = i(v["badge"]);
  p.badge_label = s(v["badge_label"]);
  return p;
}

Mention mention(JV v) {
  Mention m;
  m.id = s(v["id"]);
  m.key = s(v["key"]);
  m.label = s(v["label"]);
  m.person = s(v["person"]);
  m.name = s(v["name"]);
  m.from_label = s(v["from_label"]);
  m.text = s(v["text"]);
  m.caption = s(v["caption"]);
  m.when = s(v["when"]);
  m.iso = s(v["iso"]);
  if (v["ack"].is<JO>())
    m.ack = Ack{s(v["ack"]["message_id"]), s(v["ack"]["person_id"])};
  return m;
}

std::vector<Mention> mentions(JV v) {
  std::vector<Mention> out;
  for (JV item : v.as<JA>())
    if (item.is<JO>())
      out.push_back(mention(item));
  return out;
}

Message message(JV v) {
  Message m;
  m.id = s(v["id"]);
  m.person = s(v["person"]);
  m.name = s(v["name"]);
  m.from_label = s(v["from_label"]);
  m.icon = emoji(v["icon"]);
  m.text = s(v["text"]);
  m.caption = s(v["caption"]);
  m.is_new = b(v["new"]);
  m.when = s(v["when"]);
  m.iso = s(v["iso"]);
  m.time = s(v["time"]);
  m.rest = b(v["rest"]);
  return m;
}

std::vector<Message> messages(JV v) {
  std::vector<Message> out;
  for (JV item : v.as<JA>())
    if (item.is<JO>())
      out.push_back(message(item));
  return out;
}

AgendaRow agenda_row(JV v) {
  AgendaRow r;
  r.kind = s(v["kind"]);
  r.is_break = b(v["is_break"]);
  r.time = s(v["time"]);
  r.title = s(v["title"]);
  r.icon = emoji(v["icon"]);
  r.status = status(v["status"]);
  r.status_label = s(v["status_label"]);
  r.now = b(v["now"]);
  r.past = b(v["past"]);
  r.meta = s(v["meta"]);
  r.color = c(v["color"]);
  r.people = strings(v["people"]);
  return r;
}

std::vector<AgendaRow> agenda_rows(JV v) {
  std::vector<AgendaRow> out;
  for (JV item : v.as<JA>())
    if (item.is<JO>())
      out.push_back(agenda_row(item));
  return out;
}

std::optional<ChoresCard> chores(JV v) {
  if (!v.is<JO>())
    return std::nullopt;
  ChoresCard card;
  card.label = s(v["label"]);
  for (JV item : v["items"].as<JA>()) {
    Chore ch;
    ch.title = s(item["title"]);
    std::string st = s(item["state"]);
    ch.state = st == "done" ? ChoreState::DONE : st == "overdue" ? ChoreState::OVERDUE : ChoreState::OPEN;
    ch.state_label = s(item["state_label"]);
    ch.late = s(item["late"]);
    ch.due = s(item["due"]);
    ch.people = strings(item["people"]);
    card.items.push_back(std::move(ch));
  }
  return card;
}

std::optional<NoteColors> note_colors(JV v) {
  if (!v.is<JO>())
    return std::nullopt;
  return NoteColors{c(v["bg"]), c(v["ink"])};
}

std::vector<Note> notes(JV v) {
  std::vector<Note> out;
  for (JV item : v.as<JA>()) {
    if (!item.is<JO>())
      continue;
    Note n;
    n.text = s(item["text"]);
    n.color = c(item["color"]);
    n.light = note_colors(item["light"]);
    n.dark = note_colors(item["dark"]);
    out.push_back(std::move(n));
  }
  return out;
}

std::optional<TabEmpty> tab_empty(JV v) {
  if (!v.is<JO>())
    return std::nullopt;
  return TabEmpty{emoji(v["icon"]), s(v["title"]), s(v["text"])};
}

std::optional<PresenceGroup> presence_group(JV v) {
  if (!v.is<JO>())
    return std::nullopt;
  PresenceGroup g;
  g.icon = emoji(v["icon"]);
  g.label = s(v["label"]);
  for (JV item : v["people"].as<JA>()) {
    if (item.is<const char *>()) {
      g.people.emplace_back(item.as<const char *>());
      g.zones.emplace_back();
    } else if (item.is<JO>()) {
      g.people.push_back(s(item["id"]));
      g.zones.push_back(s(item["zone"]));
    }
  }
  return g;
}

std::optional<Presence> presence(JV v) {
  if (!v.is<JO>())
    return std::nullopt;
  Presence p;
  p.label = s(v["label"]);
  p.home = presence_group(v["home"]);
  p.away = presence_group(v["away"]);
  return p;
}

void header(JO root, Header &h) {
  h.api_version = i(root["api_version"]);
  h.view = s(root["view"]);
  h.view_key = s(root["view_key"]);
  h.household = s(root["household"]);
  h.locale = s(root["locale"]);
  h.rtl = s(root["dir"]) == "rtl";
  h.clock = clock(root["clock"]);
  JV st = root["settings"];
  h.settings.idle_return_minutes = i(st["idle_return_minutes"]);
  h.settings.keep_screen_on = b(st["keep_screen_on"], true);
  h.settings.mention_sound = b(st["mention_sound"]);
  h.settings.night_start = s(st["night_start"]);
  h.settings.night_end = s(st["night_end"]);
  h.settings.pin_set = b(st["pin_set"]);
  JV rail = root["rail"];
  h.rail.shown = b(rail["shown"], true);
  h.rail.many = b(rail["many"]);
  h.rail.current = s(rail["current"]);
  for (JV item : root["people"].as<JA>())
    if (item.is<JO>())
      h.people.push_back(person(item));
  h.is_night = b(root["is_night"]);
  JV night = root["night"];
  if (night.is<JO>()) {
    Night n;
    n.time = s(night["time"]);
    n.date = s(night["date"]);
    n.wake_hint = s(night["wake_hint"]);
    JV item = night["item"];
    if (item.is<JO>()) {
      NightItem ni;
      ni.when = s(item["when"]);
      ni.when_label = s(item["when_label"]);
      ni.time = s(item["time"]);
      ni.title = s(item["title"]);
      ni.icon = emoji(item["icon"]);
      ni.people = strings(item["people"]);
      n.item = std::move(ni);
    }
    h.night = std::move(n);
  }
  h.tab = tab_key(s(root["tab"]));
  for (JV item : root["tabs"].as<JA>()) {
    Tab t;
    t.key = tab_key(s(item["key"]));
    t.label = s(item["label"]);
    t.path = s(item["path"]);
    t.current = b(item["current"]);
    t.badge = i(item["badge"]);
    t.badge_label = s(item["badge_label"]);
    if (t.key != TabKey::UNKNOWN)
      h.tabs.push_back(std::move(t));
  }
  h.alerts = mentions(root["alerts"]);
}

void overview(JO o, Overview &out) {
  out.presence = presence(o["presence"]);
  JV next = o["next"];
  if (next.is<JO>()) {
    NextUp n;
    n.kind = s(next["kind"]);
    n.label = s(next["label"]);
    n.time = s(next["time"]);
    n.icon = emoji(next["icon"]);
    n.title = s(next["title"]);
    n.rel = s(next["rel"]);
    n.people = strings(next["people"]);
    n.color = c(next["color"]);
    out.next = std::move(n);
  }
  JV msgs = o["messages"];
  if (msgs.is<JO>())
    out.messages = MessagesCard{s(msgs["label"]), b(msgs["see_all"]), messages(msgs["items"])};
  JV today = o["today"];
  if (today.is<JO>()) {
    Today t;
    t.label = s(today["label"]);
    t.mode = s(today["mode"]);
    JV tl = today["timeline"];
    if (tl.is<JO>()) {
      Timeline line;
      line.start_hour = i(tl["start_hour"]);
      line.end_hour = i(tl["end_hour"], 24);
      if (tl["now_x"].is<int>())
        line.now_x = tl["now_x"].as<int>();
      line.now_label = s(tl["now_label"]);
      line.label = s(tl["label"]);
      for (JV tick : tl["ticks"].as<JA>())
        line.ticks.push_back(TimelineTick{s(tick["label"]), i(tick["x"]), b(tick["minor"])});
      for (JV lane : tl["lanes"].as<JA>()) {
        TimelineLane l;
        l.person = s(lane["person"]);
        l.color = c(lane["color"]);
        l.ink = c(lane["ink"]);
        l.all_day = strings(lane["all_day"]);
        l.rows = i(lane["rows"], 1);
        if (l.rows < 1)
          l.rows = 1;
        for (JV bl : lane["blocks"].as<JA>()) {
          TimelineBlock blk;
          blk.kind = s(bl["kind"]);
          blk.title = s(bl["title"]);
          blk.label = s(bl["label"]);
          blk.x = i(bl["x"]);
          blk.w = i(bl["w"]);
          blk.row = i(bl["row"]);
          blk.past = b(bl["past"]);
          blk.narrow = b(bl["narrow"]);
          blk.icon = emoji(bl["icon"]);
          blk.more = i(bl["more"]);
          l.blocks.push_back(std::move(blk));
        }
        line.lanes.push_back(std::move(l));
      }
      t.timeline = std::move(line);
    }
    t.list = agenda_rows(today["list"]);
    t.empty = s(today["empty"]);
    t.empty_icon = emoji(today["empty_icon"]);
    JV school = today["at_school"];
    if (school.is<JO>()) {
      t.at_school_label = s(school["label"]);
      for (JV item : school["items"].as<JA>())
        t.at_school.push_back(
            AtSchoolItem{strings(item["people"]), emoji(item["icon"]), s(item["title"]), s(item["until"])});
    }
    JV changes = today["changes"];
    if (changes.is<JO>()) {
      t.changes_label = s(changes["label"]);
      for (JV item : changes["items"].as<JA>())
        t.changes.push_back(SchoolChange{status(item["status"]), s(item["status_label"]), s(item["who"]),
                                         s(item["title"]), s(item["meta"]), strings(item["people"])});
    }
    out.today = std::move(t);
  }
  out.chores = chores(o["chores"]);
  JV shop = o["shopping"];
  if (shop.is<JO>())
    out.shopping = ShoppingCard{s(shop["label"]), strings(shop["items"]), s(shop["more"])};
  JV nts = o["notes"];
  if (nts.is<JO>())
    out.notes = NotesCard{s(nts["label"]), b(nts["see_all"]), notes(nts["items"])};
}

Tile tile(JV v) {
  Tile t;
  t.icon = emoji(v["icon"]);
  t.label = s(v["label"]);
  t.caption = s(v["caption"]);
  t.color = c(v["color"]);
  t.past = b(v["past"]);
  t.current = b(v["current"]);
  t.cancelled = b(v["cancelled"]);
  return t;
}

std::vector<Tile> tiles(JV v) {
  std::vector<Tile> out;
  for (JV item : v.as<JA>())
    if (item.is<JO>())
      out.push_back(tile(item));
  return out;
}

std::optional<Wedge> wedge(JV v) {
  if (!v.is<JO>())
    return std::nullopt;
  return Wedge{i(v["minutes"]), b(v["full"]), c(v["color"])};
}

void reader(JO r, Reader &out) {
  JV now = r["now"];
  if (now.is<JO>())
    out.now = ReaderNow{s(now["label"]), emoji(now["icon"]), s(now["title"]),
                        s(now["meta"]),  s(now["until"]),    c(now["color"])};
  JV next = r["next"];
  if (next.is<JO>())
    out.next = ReaderNext{s(next["label"]), emoji(next["icon"]), s(next["title"]), s(next["meta"]),
                          s(next["time"]),  s(next["rel"]),      c(next["color"])};
  JV school = r["school"];
  if (school.is<JO>())
    out.school = AgendaSection{s(school["label"]), agenda_rows(school["rows"])};
  JV day = r["day"];
  if (day.is<JO>()) {
    ReaderDay rd;
    rd.label = s(day["label"]);
    rd.rows = agenda_rows(day["rows"]);
    rd.empty = s(day["empty"]);
    rd.empty_icon = emoji(day["empty_icon"]);
    JV tm = day["tomorrow"];
    if (tm.is<JO>())
      rd.tomorrow = AgendaSection{s(tm["label"]), agenda_rows(tm["rows"])};
    out.day = std::move(rd);
  }
  out.chores = chores(r["chores"]);
}

void picture(JO p, Picture &out) {
  JV ck = p["clock"];
  out.clock.hour_deg = static_cast<float>(d(ck["hour_deg"]));
  out.clock.minute_deg = static_cast<float>(d(ck["minute_deg"]));
  out.clock.label = s(ck["label"]);
  out.clock.wedge = wedge(ck["wedge"]);
  JV pn = p["part_now"];
  out.part_now.key = s(pn["key"]);
  out.part_now.icon = emoji(pn["icon"]);
  out.part_now.label = s(pn["label"]);
  out.now_next_label = s(p["now_next_label"]);
  out.now_label = s(p["now_label"]);
  out.next_label = s(p["next_label"]);
  if (p["now"].is<JO>())
    out.now = tile(p["now"]);
  if (p["next"].is<JO>())
    out.next = tile(p["next"]);
  out.next_pie = wedge(p["next_pie"]);
  JV day = p["day"];
  out.day.label = s(day["label"]);
  out.day.empty = b(day["empty"]);
  out.day.empty_label = s(day["empty_label"]);
  out.day.empty_icon = emoji(day["empty_icon"]);
  for (JV part : day["parts"].as<JA>()) {
    DayPart dp;
    dp.key = s(part["key"]);
    dp.icon = emoji(part["icon"]);
    dp.label = s(part["label"]);
    dp.state = s(part["state"]);
    dp.tiles = tiles(part["tiles"]);
    out.day.parts.push_back(std::move(dp));
  }
  JV tm = p["tomorrow"];
  if (tm.is<JO>())
    out.tomorrow = PictureTomorrow{s(tm["label"]), i(tm["sleeps"]), s(tm["sleeps_label"]), tiles(tm["tiles"])};
  JV jobs = p["jobs"];
  if (jobs.is<JO>()) {
    out.jobs_label = s(jobs["label"]);
    for (JV item : jobs["items"].as<JA>())
      out.jobs.push_back(Job{emoji(item["icon"]), s(item["label"]), s(item["caption"]), b(item["done"])});
  }
  out.where = presence(p["where"]);
  out.messages = messages(p["messages"]);
  JV night = p["night"];
  if (night.is<JO>())
    out.night = PictureNight{s(night["when"]), emoji(night["moon"]), emoji(night["icon"]), s(night["label"]),
                             s(night["caption"])};
}

}  // namespace

const char *parse_result_str(ParseResult result) {
  switch (result) {
    case ParseResult::OK:
      return "ok";
    case ParseResult::BAD_JSON:
      return "bad JSON";
    case ParseResult::WRONG_VERSION:
      return "unsupported api_version";
    case ParseResult::BAD_SHAPE:
      return "unexpected answer";
  }
  return "?";
}

Color parse_color(const char *text) {
  if (text == nullptr || text[0] != '#' || strlen(text) != 7)
    return {};
  uint32_t value = 0;
  for (int k = 1; k < 7; k++) {
    char ch = text[k];
    value <<= 4;
    if (ch >= '0' && ch <= '9')
      value |= ch - '0';
    else if (ch >= 'a' && ch <= 'f')
      value |= ch - 'a' + 10;
    else if (ch >= 'A' && ch <= 'F')
      value |= ch - 'A' + 10;
    else
      return {};
  }
  return Color::of(value);
}

ParseResult parse_board(const char *data, size_t len, Board &out) {
  ArduinoJson::JsonDocument doc(allocator());
  ParseResult r = load(data, len, doc);
  if (r != ParseResult::OK)
    return r;
  JO root = doc.as<JO>();
  if ((r = check_version(root)) != ParseResult::OK)
    return r;
  out = Board{};
  header(root, out.header);
  const std::string &view = out.header.view;
  if (view == "overview") {
    if (!root["overview"].is<JO>())
      return ParseResult::BAD_SHAPE;
    out.kind = BoardKind::OVERVIEW;
    overview(root["overview"].as<JO>(), out.overview);
  } else if (view == "messages") {
    JV m = root["messages"];
    if (!m.is<JO>())
      return ParseResult::BAD_SHAPE;
    out.kind = BoardKind::MESSAGES;
    for (JV day : m["days"].as<JA>())
      out.messages.days.push_back(MessageDay{s(day["key"]), s(day["label"]), messages(day["rows"])});
    out.messages.empty = tab_empty(m["empty"]);
  } else if (view == "notes") {
    JV n = root["notes"];
    if (!n.is<JO>())
      return ParseResult::BAD_SHAPE;
    out.kind = BoardKind::NOTES;
    out.notes.items = notes(n["items"]);
    out.notes.more = s(n["more"]);
    out.notes.empty = tab_empty(n["empty"]);
  } else if (view == "map") {
    JV m = root["map"];
    if (!m.is<JO>())
      return ParseResult::BAD_SHAPE;
    out.kind = BoardKind::MAP;
    out.map.title = s(m["title"]);
    out.map.label = s(m["label"]);
    out.map.attribution = s(m["attribution"]);
    out.map.image_field = !m["image"].isUnbound();
    if (m["image"].is<JO>()) {
      JV im = m["image"];
      out.map.image = MapImage{s(im["url"]), i(im["width"]), i(im["height"]), s(im["etag"])};
      if (out.map.image->url.empty())
        out.map.image.reset();
    }
    for (JV spot : m["spots"].as<JA>()) {
      MapSpot sp;
      sp.person = s(spot["person"]);
      sp.name = s(spot["name"]);
      sp.icon = emoji(spot["icon"]);
      sp.place = s(spot["place"]);
      sp.when = s(spot["when"]);
      sp.age_label = s(spot["age_label"]);
      sp.stale = b(spot["stale"]);
      sp.zone = s(spot["zone"]);
      out.map.spots.push_back(std::move(sp));
    }
    for (JV pin : m["pins"].as<JA>()) {
      MapPin mp;
      mp.id = s(pin["id"]);
      mp.name = s(pin["name"]);
      mp.icon = emoji(pin["icon"]);
      mp.lat = d(pin["lat"]);
      mp.lon = d(pin["lon"]);
      mp.radius_m = i(pin["radius_m"]);
      mp.color = c(pin["color"]);
      mp.is_home = b(pin["is_home"]);
      mp.occupied = b(pin["occupied"]);
      out.map.pins.push_back(std::move(mp));
    }
  } else if (view == "person") {
    JV p = root["person"];
    if (!p.is<JO>())
      return ParseResult::BAD_SHAPE;
    out.kind = BoardKind::PERSON;
    out.person.id = s(p["id"]);
    out.person.name = s(p["name"]);
    out.person.initials = s(p["initials"]);
    out.person.color = c(p["color"]);
    out.person.ink = c(p["ink"]);
    out.person.avatar = s(p["avatar"]);
    out.person.status_emoji = emoji(p["status_emoji"]);
    out.person.status_text = s(p["status_text"]);
    out.person.where = where(p["where"]);
    out.person_kind = s(root["kind"]);
    out.mentions = mentions(root["mentions"]);
    if (root["reader"].is<JO>()) {
      out.reader.emplace();
      reader(root["reader"].as<JO>(), *out.reader);
    }
    if (root["picture"].is<JO>()) {
      out.picture.emplace();
      picture(root["picture"].as<JO>(), *out.picture);
    }
    if (out.person_kind == "picture" && !out.picture)
      return ParseResult::BAD_SHAPE;
    if (out.person_kind != "picture" && !out.reader)
      return ParseResult::BAD_SHAPE;
  } else {
    return ParseResult::BAD_SHAPE;
  }
  return ParseResult::OK;
}

ParseResult parse_lock(const char *data, size_t len, LockScreen &out) {
  ArduinoJson::JsonDocument doc(allocator());
  ParseResult r = load(data, len, doc);
  if (r != ParseResult::OK)
    return r;
  JO root = doc.as<JO>();
  if ((r = check_version(root)) != ParseResult::OK)
    return r;
  if (!b(root["locked"]))
    return ParseResult::BAD_SHAPE;
  out = LockScreen{};
  out.api_version = API_VERSION;
  out.household = s(root["household"]);
  out.locale = s(root["locale"]);
  out.clock = clock(root["clock"]);
  JV st = root["strings"];
  out.locked = s(st["locked"]);
  out.pin_button = s(st["pin_button"]);
  out.enter_pin = s(st["enter_pin"]);
  out.unlock = s(st["unlock"]);
  out.del = s(st["delete"]);
  out.wrong_pin = s(st["wrong_pin"]);
  return ParseResult::OK;
}

namespace {
void palette(JV v, Palette &p) {
  if (!v.is<JO>())
    return;
  p.bg = c(v["bg"]).or_else(p.bg);
  p.surface = c(v["surface"]).or_else(p.surface);
  p.text = c(v["text"]).or_else(p.text);
  p.muted = c(v["muted"]).or_else(p.muted);
  p.accent = c(v["accent"]).or_else(p.accent);
  p.accent_text = c(v["accent_text"]).or_else(p.accent_text);
  p.border = c(v["border"]).or_else(p.border);
  p.error_bg = c(v["error_bg"]).or_else(p.error_bg);
  p.error_text = c(v["error_text"]).or_else(p.error_text);
  p.focus = c(v["focus"]).or_else(p.focus);
}
}  // namespace

Theme::Theme() {
  this->dark = Palette{0x151412, 0x22201D, 0xF1EEE9, 0xB3ADA3, 0x6CC3A8,
                       0x0F1A16, 0x3D3A35, 0x3A1A16, 0xFFB4A9, 0x8AB4FF};
  this->night = Palette{0x000000, 0x121110, 0xA8A298, 0x817C73, 0x6CC3A8,
                        0x0F1A16, 0x2A2825, 0x3A1A16, 0xFFB4A9, 0x8AB4FF};
}

const Palette &Theme::palette(Scheme scheme) const {
  switch (scheme) {
    case Scheme::DARK:
      return this->dark;
    case Scheme::NIGHT:
      return this->night;
    case Scheme::LIGHT:
    default:
      return this->light;
  }
}

Scheme scheme_for(const std::string &mode, bool night, bool night_board) {
  if (night_board)
    return Scheme::NIGHT;
  if (mode == "light")
    return Scheme::LIGHT;
  if (mode == "dark")
    return Scheme::DARK;
  return night ? Scheme::DARK : Scheme::LIGHT;
}

const std::string &Theme::str(const char *key, const std::string &fallback) const {
  for (const auto &pair : this->strings)
    if (pair.key == key)
      return pair.value;
  return fallback;
}

ParseResult parse_theme(const char *data, size_t len, Theme &out) {
  ArduinoJson::JsonDocument doc(allocator());
  ParseResult r = load(data, len, doc);
  if (r != ParseResult::OK)
    return r;
  JO root = doc.as<JO>();
  if ((r = check_version(root)) != ParseResult::OK)
    return r;
  Theme t;
  t.api_version = API_VERSION;
  t.locale = s(root["locale"]);
  t.rtl = s(root["dir"]) == "rtl";
  t.mode = s(root["mode"]);
  JV colors = root["colors"];
  palette(colors["light"], t.light);
  palette(colors["dark"], t.dark);
  palette(colors["night"], t.night);
  for (ArduinoJson::JsonPairConst kv : root["strings"].as<JO>())
    if (kv.value().is<const char *>())
      t.strings.push_back(StringPair{kv.key().c_str(), kv.value().as<const char *>()});
  out = std::move(t);
  return ParseResult::OK;
}

ParseResult parse_pair_start(const char *data, size_t len, PairStart &out) {
  ArduinoJson::JsonDocument doc(allocator());
  ParseResult r = load(data, len, doc);
  if (r != ParseResult::OK)
    return r;
  JO root = doc.as<JO>();
  out = PairStart{s(root["pair_id"]), s(root["code"]), i(root["expires_in"])};
  if (out.pair_id.empty() || out.code.empty())
    return ParseResult::BAD_SHAPE;
  return ParseResult::OK;
}

ParseResult parse_pair_status(const char *data, size_t len, PairStatus &out) {
  ArduinoJson::JsonDocument doc(allocator());
  ParseResult r = load(data, len, doc);
  if (r != ParseResult::OK)
    return r;
  JO root = doc.as<JO>();
  std::string st = s(root["status"]);
  out = PairStatus{};
  if (st == "pending") {
    out.state = PairState::PENDING;
  } else if (st == "paired") {
    out.state = PairState::PAIRED;
    out.token = s(root["token"]);
    out.display_id = s(root["display_id"]);
    if (out.token.empty())
      return ParseResult::BAD_SHAPE;
  } else {
    return ParseResult::BAD_SHAPE;
  }
  return ParseResult::OK;
}

ClaimResult parse_pair_claim(int status, const char *data, size_t len, PairStatus &out) {
  out = PairStatus{};
  switch (status) {
    case 200: {
      PairStatus parsed;
      if (parse_pair_status(data, len, parsed) != ParseResult::OK || parsed.state != PairState::PAIRED)
        return ClaimResult::FAILED;
      out = std::move(parsed);
      return ClaimResult::PAIRED;
    }
    case 404:
      return ClaimResult::EXPIRED;
    case 409:
      return parse_error_code(data, len) == "DASHBOARD_DISABLED" ? ClaimResult::DISABLED : ClaimResult::FAILED;
    case 429:
      return ClaimResult::BUSY;
    default:
      return ClaimResult::FAILED;
  }
}

std::string parse_error_code(const char *data, size_t len) {
  ArduinoJson::JsonDocument doc(allocator());
  if (load(data, len, doc) != ParseResult::OK)
    return {};
  return s(doc["error"]["code"]);
}

ApiError parse_error(const char *data, size_t len) {
  ApiError out;
  ArduinoJson::JsonDocument doc(allocator());
  if (load(data, len, doc) != ParseResult::OK)
    return out;
  JV e = doc["error"];
  out.code = s(e["code"]);
  out.detail = s(e["detail"]);
  out.remaining = i(e["params"]["remaining"], -1);
  out.retry_after = i(e["params"]["retry_after"], 0);
  return out;
}

ParseResult parse_unlocked(const char *data, size_t len) {
  ArduinoJson::JsonDocument doc(allocator());
  ParseResult r = load(data, len, doc);
  if (r != ParseResult::OK)
    return r;
  JO root = doc.as<JO>();
  if ((r = check_version(root)) != ParseResult::OK)
    return r;
  if (!root["locked"].is<bool>() || root["locked"].as<bool>())
    return ParseResult::BAD_SHAPE;
  return ParseResult::OK;
}

ParseResult parse_acknowledged(const char *data, size_t len) {
  ArduinoJson::JsonDocument doc(allocator());
  ParseResult r = load(data, len, doc);
  if (r != ParseResult::OK)
    return r;
  JO root = doc.as<JO>();
  if ((r = check_version(root)) != ParseResult::OK)
    return r;
  return b(root["acknowledged"]) ? ParseResult::OK : ParseResult::BAD_SHAPE;
}

std::string unlock_body(const std::string &pin) {
  ArduinoJson::JsonDocument doc(allocator());
  doc["pin"] = pin;
  std::string out;
  ArduinoJson::serializeJson(doc, out);
  return out;
}

std::string ack_body(const Ack &ack) {
  ArduinoJson::JsonDocument doc(allocator());
  doc["message_id"] = ack.message_id;
  if (ack.person_id.empty())
    doc["person_id"] = nullptr;
  else
    doc["person_id"] = ack.person_id;
  std::string out;
  ArduinoJson::serializeJson(doc, out);
  return out;
}

}  // namespace esphome::socialhome_display
