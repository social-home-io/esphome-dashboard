// SPDX-License-Identifier: MPL-2.0
//
// Home › Overview: clock, who's home, next up, messages, today's timeline,
// school, chores, shopping and notes — the browser's _home.html.j2.
#include <algorithm>

#include "socialhome_display.h"

namespace esphome::socialhome_display {

static const char *const MESSAGES_PATH = "/api/display/v1/messages";
static const char *const NOTES_PATH = "/api/display/v1/notes";

namespace {
/// Width of the Overview's main column (content area minus padding).
int main_width(const Metrics &m, bool rail) { return m.width - (rail ? m.rail_width() : 0) - 2 * m.pad(); }
}  // namespace

void SocialHomeDisplay::build_overview_(lv_obj_t *parent) {
  const Palette &p = this->palette_();
  const Overview &o = this->board_->overview;
  const int width = main_width(this->m_, this->board_->header.rail.shown);
  const int gap = this->m_.gap();

  // ---- clock + who's home | next + messages
  lv_obj_t *top = this->row_(parent, gap);
  lv_obj_set_width(top, LV_PCT(100));
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_t *left = this->column_(top, this->m_.px(8));
  lv_obj_set_width(left, (width - gap) * 47 / 100);
  lv_obj_t *clock = this->clock_label_(left, FontRole::CLOCK, p.text);
  lv_obj_set_style_text_line_space(clock, 0, LV_PART_MAIN);
  this->text_(left, this->board_->header.clock.date, FontRole::LARGE, p.muted);
  if (o.presence.has_value()) {
    lv_obj_t *who = this->column_(left, this->m_.px(8));
    lv_obj_set_style_pad_top(who, this->m_.px(16), LV_PART_MAIN);
    const int face = this->m_.px(44);
    // Live data: offline the faces and icons fade, the zones stay muted.
    auto group = [&](const PresenceGroup &g) {
      lv_obj_t *line = this->row_(who, this->m_.px(8));
      this->live_picture_(this->emoji_(line, g.icon, this->m_.emoji_md()));
      for (size_t k = 0; k < g.people.size(); k++) {
        this->live_picture_(this->avatar_(line, g.people[k], face));
        if (k < g.zones.size() && !g.zones[k].empty())
          this->text_(line, g.zones[k], FontRole::BODY, p.muted);
      }
    };
    if (o.presence->home.has_value() && !o.presence->home->people.empty())
      group(*o.presence->home);
    if (o.presence->away.has_value() && !o.presence->away->people.empty())
      group(*o.presence->away);
  }

  lv_obj_t *right = this->column_(top, this->m_.px(12));
  lv_obj_set_flex_grow(right, 1);
  if (o.next.has_value()) {
    const NextUp &n = *o.next;
    lv_obj_t *card = this->card_(right);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, this->m_.px(8), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(n.color.or_else(p.accent)), LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, this->m_.px(6), LV_PART_MAIN);
    this->caps_(card, n.label, p.muted);
    lv_obj_t *title = this->row_(card, this->m_.px(10));
    if (!n.icon.empty())
      this->emoji_(title, n.icon, this->m_.emoji_md());
    lv_obj_t *t = this->text_(title, n.time.empty() ? n.title : n.time + " " + n.title, FontRole::TITLE, p.text);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_width(t, (width - gap) * 50 / 100, LV_PART_MAIN);
    lv_obj_t *meta = this->row_(card, this->m_.px(10));
    for (const auto &id : n.people)
      this->avatar_(meta, id, this->m_.px(32));
    // "in 34 min" is no longer true offline (the browser's data-rel).
    if (!n.rel.empty() && !this->offline_)
      this->text_(meta, n.rel, FontRole::BODY, p.muted);
  }
  if (o.messages.has_value() && !o.messages->items.empty()) {
    lv_obj_t *card = this->card_(right);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_t *head = this->row_(card, 0);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    this->caps_(head, o.messages->label, p.muted);
    if (o.messages->see_all && !this->board_->header.is_night) {
      lv_obj_t *see = this->text_(head, this->str_("see_all") + " \xE2\x80\xBA", FontRole::LABEL, p.accent);
      lv_obj_add_flag(see, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_ext_click_area(see, this->m_.px(12));
      lv_obj_set_user_data(see, this);
      lv_obj_add_event_cb(see, SocialHomeDisplay::on_navigate_, LV_EVENT_CLICKED, this->payload_(MESSAGES_PATH));
    }
    for (const auto &msg : o.messages->items)
      this->message_row_(card, msg, true, FontRole::BODY);
  }

  // ---- today
  if (o.today.has_value()) {
    const Today &t = *o.today;
    lv_obj_t *card = this->section_card_(parent, t.label);
    lv_obj_set_width(card, LV_PCT(100));
    if (t.mode == "timeline" && t.timeline.has_value()) {
      this->build_timeline_(card, *t.timeline);
    } else if (t.mode == "list") {
      for (const auto &row : t.list)
        this->agenda_row_(card, row, true);
    } else {
      lv_obj_t *empty = this->row_(card, this->m_.px(12));
      if (!t.empty_icon.empty())
        this->emoji_(empty, t.empty_icon, this->m_.emoji_md());
      this->text_(empty, t.empty.empty() ? this->str_("empty_day") : t.empty, FontRole::BODY, p.muted);
    }
    if (!t.at_school.empty()) {
      lv_obj_t *line = this->row_(card, this->m_.px(14));
      lv_obj_set_width(line, LV_PCT(100));
      lv_obj_set_style_pad_top(line, this->m_.px(10), LV_PART_MAIN);
      lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW_WRAP);
      this->caps_(line, t.at_school_label, p.muted);
      for (const auto &item : t.at_school) {
        lv_obj_t *it = this->row_(line, this->m_.px(6));
        for (const auto &id : item.people)
          this->avatar_(it, id, this->m_.px(32));
        if (!item.icon.empty())
          this->emoji_(it, item.icon, this->m_.emoji_sm());
        this->text_(it, item.title, FontRole::BODY, p.text);
        this->text_(it, item.until, FontRole::BODY, p.muted);
      }
    }
    if (!t.changes.empty()) {
      lv_obj_t *block = this->column_(card, this->m_.px(8));
      lv_obj_set_width(block, LV_PCT(100));
      lv_obj_set_style_pad_top(block, this->m_.px(6), LV_PART_MAIN);
      this->caps_(block, t.changes_label, p.muted);
      lv_obj_t *line = this->row_(block, this->m_.px(24));
      lv_obj_set_width(line, LV_PCT(100));
      lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW_WRAP);
      lv_obj_set_style_pad_row(line, this->m_.px(8), LV_PART_MAIN);
      for (const auto &change : t.changes) {
        lv_obj_t *it = this->row_(line, this->m_.px(6));
        bool cancelled = change.status == Status::CANCELLED;
        this->emoji_(it, cancelled ? Emoji{"\xE2\x9A\xA0", "26a0"} : Emoji{"\xF0\x9F\x94\x81", "1f501"},
                     this->m_.emoji_sm());
        if (!change.who.empty())
          this->text_(it, change.who + ":", FontRole::BODY, p.text);
        lv_obj_t *title = this->text_(it, change.title, FontRole::BODY, p.text);
        if (cancelled)
          lv_obj_set_style_text_decor(title, LV_TEXT_DECOR_STRIKETHROUGH, LV_PART_MAIN);
        uint32_t fg = cancelled ? p.error_text : p.text;
        lv_obj_t *pill = this->pill_(it, upper(change.status_label), p.surface, fg, FontRole::LABEL);
        lv_obj_set_style_border_color(pill, lv_color_hex(fg), LV_PART_MAIN);
        lv_obj_set_style_border_width(pill, 1, LV_PART_MAIN);
        if (!change.meta.empty())
          this->text_(it, change.meta, FontRole::BODY, p.muted);
      }
    }
  }

  // ---- chores · shopping · notes
  lv_obj_t *bottom = this->row_(parent, gap);
  lv_obj_set_width(bottom, LV_PCT(100));
  lv_obj_set_flex_align(bottom, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  int cards = (o.chores.has_value() ? 1 : 0) + (o.shopping.has_value() ? 1 : 0) + (o.notes.has_value() ? 1 : 0);
  if (cards == 0) {
    lv_obj_delete(bottom);
    return;
  }
  const int card_w = (width - gap * (cards - 1)) / cards;
  if (o.chores.has_value()) {
    lv_obj_t *card = this->section_card_(bottom, o.chores->label);
    lv_obj_set_width(card, card_w);
    for (const auto &chore : o.chores->items)
      this->chore_row_(card, chore, true);
  }
  if (o.shopping.has_value()) {
    lv_obj_t *card = this->section_card_(bottom, o.shopping->label);
    lv_obj_set_width(card, card_w);
    for (const auto &item : o.shopping->items) {
      lv_obj_t *line = this->row_(card, this->m_.px(12));
      lv_obj_t *dot = this->box_(line);
      int d = this->m_.px(7);
      lv_obj_set_size(dot, d, d);
      lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
      lv_obj_set_style_bg_color(dot, lv_color_hex(p.muted), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
      this->text_(line, item, FontRole::BODY, p.text);
    }
    if (!o.shopping->more.empty())
      this->text_(card, o.shopping->more, FontRole::BODY, p.muted);
  }
  if (o.notes.has_value()) {
    lv_obj_t *card = this->card_(bottom);
    lv_obj_set_width(card, card_w);
    lv_obj_t *head = this->row_(card, 0);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    this->caps_(head, o.notes->label, p.muted);
    if (o.notes->see_all && !this->board_->header.is_night) {
      lv_obj_t *see = this->text_(head, this->str_("see_all") + " \xE2\x80\xBA", FontRole::LABEL, p.accent);
      lv_obj_add_flag(see, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_ext_click_area(see, this->m_.px(12));
      lv_obj_set_user_data(see, this);
      lv_obj_add_event_cb(see, SocialHomeDisplay::on_navigate_, LV_EVENT_CLICKED, this->payload_(NOTES_PATH));
    }
    for (const auto &note : o.notes->items) {
      lv_obj_t *sticky = this->box_(card);
      lv_obj_set_width(sticky, LV_PCT(100));
      lv_obj_set_style_pad_all(sticky, this->m_.px(14), LV_PART_MAIN);
      lv_obj_set_style_radius(sticky, this->m_.px(4), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(sticky, LV_OPA_COVER, LV_PART_MAIN);
      uint32_t ink = p.text;
      if (note.light.has_value()) {
        lv_obj_set_style_bg_color(sticky, lv_color_hex(note.light->bg.or_else(p.bg)), LV_PART_MAIN);
        ink = note.light->ink.or_else(p.text);
      } else {
        lv_obj_set_style_bg_color(sticky, lv_color_hex(p.bg), LV_PART_MAIN);
      }
      this->rich_(sticky, note.text, FontRole::BODY, ink);
    }
  }
}

void SocialHomeDisplay::build_timeline_(lv_obj_t *parent, const Timeline &line) {
  const Palette &p = this->palette_();
  // The "now" mark and line are live data: grey (the line faded) offline.
  const LiveStyle live = this->live_();
  const int width = main_width(this->m_, this->board_->header.rail.shown);
  const int inner = width - 2 * this->m_.card_pad() - 2;
  const int name_w = this->m_.px(170);
  const int track_w = inner - name_w;
  const int lane_h = this->m_.px(56);
  const int block_h = this->m_.px(42);

  // Hour ticks and the "now" marker.
  lv_obj_t *head = this->box_(parent);
  lv_obj_set_size(head, inner, this->m_.px(34));
  lv_obj_add_flag(head, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  for (const auto &tick : line.ticks) {
    lv_obj_t *label = this->text_(head, tick.label, FontRole::SMALL, p.muted);
    lv_obj_update_layout(label);
    int x = name_w + Metrics::pct(tick.x, track_w) - lv_obj_get_width(label) / 2;
    x = std::min(x, inner - static_cast<int>(lv_obj_get_width(label)));
    lv_obj_set_pos(label, x, this->m_.px(6));
  }
  if (line.now_x.has_value()) {
    lv_obj_t *now = this->pill_(head, line.now_label, live.accent, live.accent_ink, FontRole::LABEL);
    lv_obj_update_layout(now);
    lv_obj_set_pos(now, name_w + Metrics::pct(*line.now_x, track_w) - lv_obj_get_width(now) / 2, this->m_.px(4));
  }

  for (const auto &lane : line.lanes) {
    const int height = std::max(1, lane.rows) * lane_h;
    lv_obj_t *row = this->box_(parent);
    lv_obj_set_size(row, inner, height);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(p.border), LV_PART_MAIN);

    lv_obj_t *who = this->row_(row, this->m_.px(8));
    lv_obj_set_size(who, name_w - this->m_.px(8), height);
    this->avatar_(who, lane.person, this->m_.avatar_sm());
    lv_obj_t *names = this->column_(who, 0);
    lv_obj_set_flex_grow(names, 1);
    const Person *person = this->board_->header.find_person(lane.person);
    lv_obj_t *name = this->text_(names, person != nullptr ? person->name : "", FontRole::BODY_BOLD, p.text);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(name, LV_PCT(100));
    for (const auto &all_day : lane.all_day) {
      lv_obj_t *t = this->text_(names, all_day, FontRole::SMALL, p.muted);
      lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
      lv_obj_set_width(t, LV_PCT(100));
    }

    lv_obj_t *track = this->box_(row);
    lv_obj_set_size(track, track_w, height);
    lv_obj_set_pos(track, name_w, 0);
    const uint32_t color = lane.color.or_else(p.accent);
    for (const auto &block : lane.blocks) {
      lv_obj_t *b = this->row_(track, this->m_.px(6));
      int x = Metrics::pct(block.x, track_w);
      int w = std::max(this->m_.px(18), Metrics::pct(block.w, track_w));
      lv_obj_set_size(b, std::min(w, track_w - x), block_h);
      lv_obj_set_pos(b, x, block.row * lane_h + (lane_h - block_h) / 2);
      // Faded bars (school, past) are composited here, so the label is
      // picked against the colour actually drawn — never faded itself.
      const BarStyle style =
          bar_style(color, lane.ink.valid, lane.ink.rgb, p.surface, block.kind == "school", block.past);
      lv_obj_set_style_bg_color(b, lv_color_hex(style.fill), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_radius(b, this->m_.px(6), LV_PART_MAIN);
      lv_obj_set_style_pad_hor(b, this->m_.px(8), LV_PART_MAIN);
      lv_obj_set_flex_align(b, block.narrow ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
      if (!block.icon.empty()) {
        lv_obj_t *icon = this->emoji_(b, block.icon, this->m_.emoji_sm());
        if (block.past)
          lv_obj_set_style_opa(icon, LV_OPA_50, LV_PART_MAIN);
      }
      if (!block.narrow) {
        lv_obj_t *t = this->text_(b, block.title, FontRole::BODY_BOLD, style.ink);
        lv_obj_set_flex_grow(t, 1);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_CLIP);
      }
    }
    if (line.now_x.has_value()) {
      lv_obj_t *now = this->box_(track);
      lv_obj_set_size(now, std::max(2, this->m_.px(3)), height);
      lv_obj_set_pos(now, Metrics::pct(*line.now_x, track_w), 0);
      lv_obj_set_style_bg_color(now, lv_color_hex(live.accent), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(now, live.picture_opa, LV_PART_MAIN);
    }
  }
}

}  // namespace esphome::socialhome_display
