// SPDX-License-Identifier: MPL-2.0
//
// A person's page: the reader view (now / next, school, today, chores) and
// the picture view for children who can't read yet (emoji tiles and an
// analog clock with a time-timer wedge) — _person_reader / _person_picture.
#include <algorithm>

#include "socialhome_display.h"

namespace esphome::socialhome_display {

namespace {

int main_width(const Metrics &m, bool rail) { return m.width - (rail ? m.rail_width() : 0) - 2 * m.pad(); }

/// What the dial's draw callback paints (owned by the dial object).
struct Dial {
  float hour_deg{0};
  float minute_deg{0};
  bool hands{true};
  bool wedge{false};
  bool wedge_full{false};
  int wedge_minutes{0};
  uint32_t wedge_color{0};
  uint32_t ink{0};
  uint32_t tick{0};
};

void free_dial(lv_event_t *event) { delete static_cast<Dial *>(lv_event_get_user_data(event)); }

void draw_dial(lv_event_t *event) {
  auto *d = static_cast<Dial *>(lv_event_get_user_data(event));
  lv_obj_t *obj = lv_event_get_current_target_obj(event);
  lv_layer_t *layer = lv_event_get_layer(event);
  lv_area_t area;
  lv_obj_get_coords(obj, &area);
  const int r = lv_area_get_width(&area) / 2;
  const int cx = area.x1 + r;
  const int cy = area.y1 + r;
  const int border = lv_obj_get_style_border_width(obj, LV_PART_MAIN);
  if (d->wedge && (d->wedge_full || d->wedge_minutes > 0)) {
    int start, end;
    wedge_angles(d->minute_deg, d->wedge_full ? 60 : d->wedge_minutes, &start, &end);
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    const int wr = d->hands ? (r - border) * 88 / 100 : r - border;
    arc.center.x = cx;
    arc.center.y = cy;
    arc.radius = static_cast<uint16_t>(wr);
    arc.width = wr;
    arc.start_angle = start;
    arc.end_angle = end;
    arc.color = lv_color_hex(d->wedge_color);
    arc.opa = d->hands ? LV_OPA_70 : LV_OPA_COVER;
    lv_draw_arc(layer, &arc);
  }
  if (!d->hands)
    return;
  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  line.color = lv_color_hex(d->tick);
  line.round_start = 1;
  line.round_end = 1;
  const int outer = r - border - r / 14;
  for (int k = 0; k < 12; k++) {
    const bool major = k % 3 == 0;
    int x1, y1, x2, y2;
    hand_end(k * 30.0f, cx, cy, outer, &x1, &y1);
    hand_end(k * 30.0f, cx, cy, outer - (major ? r / 6 : r / 10), &x2, &y2);
    line.width = major ? std::max(3, r / 20) : std::max(2, r / 40);
    line.p1.x = x1;
    line.p1.y = y1;
    line.p2.x = x2;
    line.p2.y = y2;
    lv_draw_line(layer, &line);
  }
  line.color = lv_color_hex(d->ink);
  int x, y;
  hand_end(d->hour_deg, cx, cy, r * 50 / 100, &x, &y);
  line.width = std::max(4, r / 10);
  line.p1.x = cx;
  line.p1.y = cy;
  line.p2.x = x;
  line.p2.y = y;
  lv_draw_line(layer, &line);
  hand_end(d->minute_deg, cx, cy, r * 78 / 100, &x, &y);
  line.width = std::max(3, r / 16);
  line.p2.x = x;
  line.p2.y = y;
  lv_draw_line(layer, &line);
}

}  // namespace

void SocialHomeDisplay::build_analog_clock_(lv_obj_t *parent, const AnalogClock &clock, int size) {
  const Palette &p = this->palette_();
  lv_obj_t *dial = this->box_(parent);
  lv_obj_set_size(dial, size, size);
  lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(dial, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(dial, lv_color_hex(p.text), LV_PART_MAIN);
  lv_obj_set_style_border_width(dial, std::max(3, size / 40), LV_PART_MAIN);
  auto *d = new Dial();
  d->hour_deg = clock.hour_deg;
  d->minute_deg = clock.minute_deg;
  d->ink = 0x1F1D1A;
  d->tick = 0x5F5A52;
  if (clock.wedge.has_value()) {
    d->wedge = true;
    d->wedge_full = clock.wedge->full;
    d->wedge_minutes = clock.wedge->minutes;
    d->wedge_color = clock.wedge->color.or_else(p.accent);
  }
  lv_obj_add_event_cb(dial, draw_dial, LV_EVENT_DRAW_MAIN_END, d);
  lv_obj_add_event_cb(dial, free_dial, LV_EVENT_DELETE, d);
}

lv_obj_t *SocialHomeDisplay::build_tile_(lv_obj_t *parent, const Tile &tile, int size) {
  const Palette &p = this->palette_();
  lv_obj_t *t = this->column_(parent, this->m_.px(2));
  lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_min_width(t, size, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(t, this->m_.px(10), LV_PART_MAIN);
  lv_obj_set_style_pad_ver(t, this->m_.px(6), LV_PART_MAIN);
  lv_obj_set_style_bg_color(t, lv_color_hex(p.bg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(t, this->m_.px(12), LV_PART_MAIN);
  lv_obj_set_style_border_color(t, lv_color_hex(tile.current ? p.accent : p.border), LV_PART_MAIN);
  lv_obj_set_style_border_width(t, tile.current ? this->m_.px(3) : std::max(1, this->m_.px(2)), LV_PART_MAIN);
  // The colour strip along the bottom, as on the tablet.
  lv_obj_set_style_shadow_color(t, lv_color_hex(tile.color.or_else(p.border)), LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(t, this->m_.px(5), LV_PART_MAIN);
  lv_obj_set_style_shadow_width(t, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_spread(t, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(t, LV_OPA_COVER, LV_PART_MAIN);
  if (!tile.icon.empty())
    this->emoji_(t, tile.icon, size * 52 / 100);
  if (!tile.caption.empty()) {
    lv_obj_t *caption = this->text_(t, tile.caption, FontRole::SMALL, p.muted);
    lv_obj_set_style_max_width(caption, size * 2, LV_PART_MAIN);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
    if (tile.cancelled)
      lv_obj_set_style_text_decor(caption, LV_TEXT_DECOR_STRIKETHROUGH, LV_PART_MAIN);
  }
  if (tile.past || tile.cancelled)
    lv_obj_set_style_opa(t, LV_OPA_60, LV_PART_MAIN);
  return t;
}

void SocialHomeDisplay::build_person_(lv_obj_t *parent) {
  const Palette &p = this->palette_();
  const Board &b = *this->board_;
  const PersonHead &who = b.person;
  const bool picture = b.person_kind == "picture" && b.picture.has_value();

  lv_obj_t *head = this->row_(parent, this->m_.px(20));
  lv_obj_set_width(head, LV_PCT(100));
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, picture ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START);
  lv_obj_add_flag(head, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_t *who_row = this->row_(head, this->m_.px(20));
  lv_obj_add_flag(who_row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_flex_grow(who_row, 1);
  // Room for the avatar's ring: the scrolling column clips at its content box.
  lv_obj_set_style_pad_all(who_row, this->m_.px(10), LV_PART_MAIN);
  lv_obj_t *avatar = this->avatar_of_(who_row, who.initials, who.avatar, who.color, who.ink, this->m_.px(88));
  if (picture) {
    lv_obj_set_style_outline_color(avatar, lv_color_hex(p.accent), LV_PART_MAIN);
    lv_obj_set_style_outline_width(avatar, this->m_.px(5), LV_PART_MAIN);
    lv_obj_set_style_outline_pad(avatar, this->m_.px(3), LV_PART_MAIN);
  }
  lv_obj_t *name = this->row_(who_row, this->m_.px(12));
  lv_obj_set_flex_grow(name, 1);
  this->text_(name, who.name, FontRole::DISPLAY, p.text);
  if (!who.status_emoji.empty())
    this->emoji_(name, who.status_emoji, this->m_.emoji_md());
  if (!who.status_text.empty())
    this->text_(name, who.status_text, FontRole::BODY, p.muted);
  if (picture) {
    // The dial floats top-right, as on the tablet: the cards below run
    // alongside it instead of starting under it.
    lv_obj_t *clock = this->row_(parent, this->m_.px(16));
    lv_obj_add_flag(clock, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(clock, LV_ALIGN_TOP_RIGHT, 0, 0);
    this->build_analog_clock_(clock, b.picture->clock, this->m_.px(200));
    this->emoji_(clock, b.picture->part_now.icon, this->m_.px(64));
    this->long_press_settings_(clock);
  } else {
    if (who.where.has_value()) {
      lv_obj_t *where = this->row_(head, this->m_.px(6));
      this->emoji_(where, who.where->icon, this->m_.emoji_sm());
      this->text_(where, who.where->label, FontRole::LARGE, p.text);
    }
    this->clock_label_(head, FontRole::DISPLAY, p.text);
  }

  if (picture) {
    this->build_picture_(parent);
  } else {
    this->build_alerts_(parent, b.mentions);
    this->build_reader_(parent);
  }
}

void SocialHomeDisplay::build_reader_(lv_obj_t *parent) {
  const Palette &p = this->palette_();
  if (!this->board_->reader.has_value())
    return;
  const Reader &r = *this->board_->reader;
  const int width = main_width(this->m_, this->board_->header.rail.shown);
  const int gap = this->m_.gap();

  if (r.now.has_value() || r.next.has_value()) {
    lv_obj_t *row = this->row_(parent, gap);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    const int n = (r.now.has_value() ? 1 : 0) + (r.next.has_value() ? 1 : 0);
    const int w = (width - gap * (n - 1)) / n;
    auto shell = [&](Color color, bool now) {
      lv_obj_t *card = this->card_(row);
      lv_obj_set_width(card, w);
      lv_obj_set_style_pad_row(card, this->m_.px(6), LV_PART_MAIN);
      lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
      lv_obj_set_style_border_width(card, this->m_.px(8), LV_PART_MAIN);
      lv_obj_set_style_border_color(card, lv_color_hex(color.or_else(p.accent)), LV_PART_MAIN);
      if (now) {
        lv_obj_set_style_outline_color(card, lv_color_hex(color.or_else(p.accent)), LV_PART_MAIN);
        lv_obj_set_style_outline_width(card, this->m_.px(3), LV_PART_MAIN);
      }
      return card;
    };
    if (r.now.has_value()) {
      lv_obj_t *card = shell(r.now->color, true);
      this->caps_(card, r.now->label, p.muted);
      lv_obj_t *line = this->row_(card, this->m_.px(10));
      if (!r.now->icon.empty())
        this->emoji_(line, r.now->icon, this->m_.emoji_md());
      this->text_(line, r.now->title, FontRole::TITLE, p.text);
      if (!r.now->meta.empty())
        this->text_(line, "\xC2\xB7 " + r.now->meta, FontRole::BODY, p.muted);
      this->text_(card, r.now->until, FontRole::LARGE, p.muted);
    }
    if (r.next.has_value()) {
      lv_obj_t *card = shell(r.next->color, false);
      this->caps_(card, r.next->label, p.muted);
      lv_obj_t *line = this->row_(card, this->m_.px(10));
      if (!r.next->icon.empty())
        this->emoji_(line, r.next->icon, this->m_.emoji_md());
      this->text_(line, r.next->title, FontRole::TITLE, p.text);
      if (!r.next->meta.empty())
        this->text_(line, "\xC2\xB7 " + r.next->meta, FontRole::BODY, p.muted);
      this->text_(card, r.next->time + " \xC2\xB7 " + r.next->rel, FontRole::LARGE, p.muted);
    }
  }

  const int cols = (r.school.has_value() ? 1 : 0) + (r.day.has_value() ? 1 : 0) + (r.chores.has_value() ? 1 : 0);
  if (cols == 0)
    return;
  const int w = (width - gap * (cols - 1)) / cols;
  lv_obj_t *row = this->row_(parent, gap);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  if (r.school.has_value()) {
    lv_obj_t *card = this->section_card_(row, r.school->label);
    lv_obj_set_width(card, w);
    for (const auto &item : r.school->rows)
      this->agenda_row_(card, item, false);
  }
  if (r.day.has_value()) {
    lv_obj_t *card = this->section_card_(row, r.day->label);
    lv_obj_set_width(card, w);
    for (const auto &item : r.day->rows)
      this->agenda_row_(card, item, false);
    if (r.day->rows.empty() && !r.day->empty.empty()) {
      lv_obj_t *line = this->row_(card, this->m_.px(10));
      if (!r.day->empty_icon.empty())
        this->emoji_(line, r.day->empty_icon, this->m_.emoji_md());
      this->text_(line, r.day->empty, FontRole::BODY, p.muted);
    }
    if (r.day->tomorrow.has_value()) {
      lv_obj_t *label = this->caps_(card, r.day->tomorrow->label, p.muted);
      lv_obj_set_style_pad_top(label, this->m_.px(8), LV_PART_MAIN);
      for (const auto &item : r.day->tomorrow->rows)
        this->agenda_row_(card, item, false);
    }
  }
  if (r.chores.has_value()) {
    lv_obj_t *card = this->section_card_(row, r.chores->label);
    lv_obj_set_width(card, w);
    for (const auto &chore : r.chores->items)
      this->chore_row_(card, chore, false);
  }
}

void SocialHomeDisplay::build_picture_(lv_obj_t *parent) {
  const Palette &p = this->palette_();
  const Picture &pv = *this->board_->picture;
  const int gap = this->m_.gap();

  // Messages for this child: the sender's face, a speech bubble, "Got it".
  for (const auto &mention : this->board_->mentions) {
    if (this->dismissed_.count(mention.key))
      continue;
    lv_obj_t *card = this->row_(parent, this->m_.px(24));
    lv_obj_set_style_bg_color(card, lv_color_hex(p.surface), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(p.accent), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, this->m_.px(3), LV_PART_MAIN);
    lv_obj_set_style_radius(card, this->m_.px(20), LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, this->m_.px(14), LV_PART_MAIN);
    lv_obj_t *who = this->column_(card, this->m_.px(6));
    lv_obj_set_flex_align(who, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *face = this->avatar_(who, mention.person, this->m_.px(88));
    lv_obj_t *bubble = this->emoji_(face, Emoji{"\xF0\x9F\x92\xAC", "1f4ac"}, this->m_.px(40));
    lv_obj_align(bubble, LV_ALIGN_BOTTOM_RIGHT, this->m_.px(4), this->m_.px(4));
    lv_obj_t *caption = this->text_(who, mention.caption, FontRole::SMALL, p.muted);
    lv_obj_set_style_max_width(caption, this->m_.px(200), LV_PART_MAIN);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
    if (mention.ack.has_value()) {
      lv_obj_t *ok = this->box_(card);
      int d = this->m_.px(88);
      lv_obj_set_size(ok, d, d);
      lv_obj_set_style_radius(ok, LV_RADIUS_CIRCLE, LV_PART_MAIN);
      lv_obj_set_style_bg_color(ok, lv_color_hex(p.accent), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(ok, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_bg_color(ok, lv_color_hex(p.text), LV_STATE_PRESSED);
      lv_obj_add_flag(ok, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_user_data(ok, this);
      lv_obj_add_event_cb(ok, SocialHomeDisplay::on_ack_, LV_EVENT_CLICKED, this->payload_(mention.key));
      lv_obj_t *check = this->check_mark_(ok, d / 2, p.accent_text, std::max(4, this->m_.px(6)));
      lv_obj_center(check);
    }
  }

  // NOW → NEXT.
  if (pv.now.has_value() || pv.next.has_value()) {
    lv_obj_t *row = this->row_(parent, this->m_.px(28));
    auto big_tile = [&](const Tile &tile, const std::string &label, bool next) {
      lv_obj_t *t = this->column_(row, this->m_.px(4));
      lv_obj_set_size(t, this->m_.px(196), this->m_.px(170));
      lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_bg_color(t, lv_color_hex(p.surface), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_radius(t, this->m_.px(20), LV_PART_MAIN);
      lv_obj_set_style_border_width(t, next ? this->m_.px(3) : this->m_.px(9), LV_PART_MAIN);
      lv_obj_set_style_border_color(t, lv_color_hex(next ? tile.color.or_else(p.accent) : p.border), LV_PART_MAIN);
      this->caps_(t, label, p.muted);
      if (!tile.icon.empty())
        this->emoji_(t, tile.icon, this->m_.px(84));
      lv_obj_t *caption = this->text_(t, tile.caption, FontRole::BODY, p.muted);
      lv_obj_set_style_max_width(caption, this->m_.px(176), LV_PART_MAIN);
      lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
      if (next && pv.next_pie.has_value()) {
        lv_obj_t *pie = this->box_(t);
        int d = this->m_.px(42);
        lv_obj_set_size(pie, d, d);
        lv_obj_add_flag(pie, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_align(pie, LV_ALIGN_TOP_RIGHT, this->m_.px(4), -this->m_.px(4));
        lv_obj_set_style_radius(pie, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(pie, lv_color_hex(p.bg), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pie, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(pie, lv_color_hex(p.border), LV_PART_MAIN);
        lv_obj_set_style_border_width(pie, 1, LV_PART_MAIN);
        auto *d2 = new Dial();
        d2->hands = false;
        d2->wedge = true;
        d2->wedge_full = pv.next_pie->full;
        d2->wedge_minutes = pv.next_pie->minutes;
        d2->minute_deg = pv.clock.minute_deg;
        d2->wedge_color = tile.color.or_else(p.accent);
        lv_obj_add_event_cb(pie, draw_dial, LV_EVENT_DRAW_MAIN_END, d2);
        lv_obj_add_event_cb(pie, free_dial, LV_EVENT_DELETE, d2);
      }
    };
    if (pv.now.has_value())
      big_tile(*pv.now, pv.now_label, false);
    if (pv.now.has_value() && pv.next.has_value()) {
      // An arrow, drawn: the dashboard fonts carry no arrows.
      lv_obj_t *arrow = this->box_(row);
      int a = this->m_.px(40);
      lv_obj_set_size(arrow, a, a);
      const lv_value_precise_t u = a;
      const int w = std::max(3, this->m_.px(5));
      lv_obj_t *shaft = this->poly_(arrow, {{u * 5 / 100, u / 2}, {u * 85 / 100, u / 2}}, w, p.muted);
      lv_obj_set_size(shaft, a, a);
      lv_obj_t *tip = this->poly_(arrow, {{u * 55 / 100, u * 20 / 100}, {u * 90 / 100, u / 2}, {u * 55 / 100, u * 80 / 100}},
                                  w, p.muted);
      lv_obj_set_size(tip, a, a);
    }
    if (pv.next.has_value())
      big_tile(*pv.next, pv.next_label, true);
  }

  // The household chat as faces with a caption.
  if (!pv.messages.empty()) {
    lv_obj_t *row = this->row_(parent, this->m_.px(28));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    for (const auto &msg : pv.messages) {
      lv_obj_t *item = this->column_(row, this->m_.px(6));
      lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_t *face = this->avatar_(item, msg.person, this->m_.px(88));
      lv_obj_t *bubble = this->emoji_(face, Emoji{"\xF0\x9F\x92\xAC", "1f4ac"}, this->m_.px(40));
      lv_obj_align(bubble, LV_ALIGN_BOTTOM_RIGHT, this->m_.px(4), this->m_.px(4));
      lv_obj_t *caption = this->text_(item, msg.caption, FontRole::SMALL, p.muted);
      lv_obj_set_width(caption, this->m_.px(170));
      lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
      lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
  }

  // My day: morning / afternoon / evening / night, each a row of tiles.
  this->caps_(parent, pv.day.label, p.muted);
  if (pv.day.empty && !pv.day.empty_label.empty()) {
    lv_obj_t *line = this->row_(parent, this->m_.px(10));
    if (!pv.day.empty_icon.empty())
      this->emoji_(line, pv.day.empty_icon, this->m_.emoji_md());
    this->text_(line, pv.day.empty_label, FontRole::BODY, p.muted);
  }
  // The parts side by side, as on a 1280 px tablet; tiles wrap inside.
  const int tile = this->m_.px(84);
  const int width = main_width(this->m_, this->board_->header.rail.shown);
  const int parts = std::max<int>(1, static_cast<int>(pv.day.parts.size()));
  const int part_w = (width - gap * (parts - 1)) / parts;
  lv_obj_t *day = this->row_(parent, gap);
  lv_obj_set_width(day, LV_PCT(100));
  lv_obj_set_flex_align(day, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  for (const auto &part : pv.day.parts) {
    lv_obj_t *col = this->column_(day, this->m_.px(10));
    lv_obj_set_width(col, part_w);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_min_height(col, this->m_.px(180), LV_PART_MAIN);
    lv_obj_set_style_bg_color(col, lv_color_hex(p.surface), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(col, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(col, this->m_.px(16), LV_PART_MAIN);
    lv_obj_set_style_pad_all(col, this->m_.px(12), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(col, this->m_.px(18), LV_PART_MAIN);
    if (part.state == "now") {
      lv_obj_set_style_border_color(col, lv_color_hex(p.accent), LV_PART_MAIN);
      lv_obj_set_style_border_width(col, this->m_.px(3), LV_PART_MAIN);
    }
    this->emoji_(col, part.icon, this->m_.px(44));
    lv_obj_t *tiles = this->row_(col, this->m_.px(8));
    lv_obj_set_width(tiles, LV_PCT(100));
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tiles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(tiles, this->m_.px(14), LV_PART_MAIN);
    for (const auto &t : part.tiles)
      this->build_tile_(tiles, t, tile);
    if (part.state == "past")
      lv_obj_set_style_opa(col, LV_OPA_70, LV_PART_MAIN);
  }

  // Tomorrow · My jobs.
  lv_obj_t *row = this->row_(parent, gap);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(row, gap, LV_PART_MAIN);
  if (pv.tomorrow.has_value()) {
    lv_obj_t *card = this->section_card_(row, pv.tomorrow->label);
    lv_obj_t *tiles = this->row_(card, this->m_.px(12));
    lv_obj_set_style_pad_bottom(tiles, this->m_.px(6), LV_PART_MAIN);
    this->emoji_(tiles, Emoji{"\xF0\x9F\x8C\x99", "1f319"}, this->m_.px(40));
    for (const auto &t : pv.tomorrow->tiles)
      this->build_tile_(tiles, t, tile);
  }
  if (!pv.jobs.empty()) {
    lv_obj_t *card = this->section_card_(row, pv.jobs_label);
    lv_obj_t *tiles = this->row_(card, this->m_.px(12));
    lv_obj_set_style_pad_bottom(tiles, this->m_.px(6), LV_PART_MAIN);
    for (const auto &job : pv.jobs) {
      Tile t;
      t.icon = job.icon;
      t.caption = job.caption;
      lv_obj_t *jt = this->build_tile_(tiles, t, tile);
      lv_obj_add_flag(jt, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
      lv_obj_t *mark = this->box_(jt);
      int d = this->m_.px(32);
      lv_obj_set_size(mark, d, d);
      lv_obj_add_flag(mark, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_align(mark, LV_ALIGN_TOP_RIGHT, this->m_.px(14), -this->m_.px(12));
      lv_obj_set_style_radius(mark, job.done ? LV_RADIUS_CIRCLE : this->m_.px(4), LV_PART_MAIN);
      lv_obj_set_style_bg_color(mark, lv_color_hex(job.done ? p.accent : p.surface), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_color(mark, lv_color_hex(job.done ? p.surface : p.text), LV_PART_MAIN);
      lv_obj_set_style_border_width(mark, std::max(1, this->m_.px(2)), LV_PART_MAIN);
      if (job.done) {
        lv_obj_t *check = this->check_mark_(mark, d * 6 / 10, p.accent_text, std::max(2, this->m_.px(3)));
        lv_obj_center(check);
      }
    }
  }
  if (pv.where.has_value()) {
    lv_obj_t *card = this->section_card_(row, pv.where->label);
    lv_obj_t *line = this->row_(card, this->m_.px(10));
    auto group = [&](const PresenceGroup &g) {
      this->emoji_(line, g.icon, this->m_.px(40));
      for (const auto &id : g.people)
        this->avatar_(line, id, this->m_.px(56));
    };
    if (pv.where->home.has_value() && !pv.where->home->people.empty())
      group(*pv.where->home);
    if (pv.where->away.has_value() && !pv.where->away->people.empty())
      group(*pv.where->away);
  }
}

}  // namespace esphome::socialhome_display
