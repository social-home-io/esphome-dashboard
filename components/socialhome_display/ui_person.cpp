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
  uint8_t wedge_opa{LV_OPA_COVER};
  uint32_t ink{0};
  uint32_t tick_major{0};
  uint32_t tick_minor{0};
};

/// LVGL's flex lays a content-sized object's children out in the size its
/// children need, before a min width / height grows it: fix the grown size
/// so they centre in it, as the browser's flex does.
void fix_size(lv_obj_t *obj) {
  lv_obj_update_layout(obj);
  lv_obj_set_size(obj, lv_obj_get_width(obj), lv_obj_get_height(obj));
}

void free_dial(lv_event_t *event) { delete static_cast<Dial *>(lv_event_get_user_data(event)); }

/// LV_EVENT_REFR_EXT_DRAW_SIZE: the object draws (and lets its children
/// draw) this many px past its edges.
void overhang_ext(lv_event_t *event) {
  lv_event_set_ext_draw_size(event, static_cast<int32_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event))));
}

/// Let children of `obj` show up to `px` past its edges. LVGL 9 clips a
/// child to its parent even with LV_OBJ_FLAG_OVERFLOW_VISIBLE, which only
/// widens the clip by the parent's ext draw size (its shadow, outline …).
void allow_overhang(lv_obj_t *obj, int px) {
  lv_obj_add_flag(obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_event_cb(obj, overhang_ext, LV_EVENT_REFR_EXT_DRAW_SIZE, reinterpret_cast<void *>(static_cast<intptr_t>(px)));
  lv_obj_refresh_ext_draw_size(obj);
}

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
    arc.opa = d->hands ? d->wedge_opa : LV_OPA_COVER;
    lv_draw_arc(layer, &arc);
  }
  if (!d->hands)
    return;
  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  line.round_start = 1;
  line.round_end = 1;
  const int outer = r - border - r / 14;
  for (int k = 0; k < 12; k++) {
    const bool major = k % 3 == 0;
    line.color = lv_color_hex(major ? d->tick_major : d->tick_minor);
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
  // The hub (`.dash-pv-hub`, r 7 of the 96 dial).
  lv_draw_arc_dsc_t hub;
  lv_draw_arc_dsc_init(&hub);
  hub.center.x = cx;
  hub.center.y = cy;
  hub.radius = static_cast<uint16_t>(std::max(3, r * 7 / 96));
  hub.width = hub.radius;
  hub.start_angle = 0;
  hub.end_angle = 360;
  hub.color = lv_color_hex(d->ink);
  lv_draw_arc(layer, &hub);
}

}  // namespace

void SocialHomeDisplay::build_analog_clock_(lv_obj_t *parent, const AnalogClock &clock, int size) {
  const Palette &p = this->palette_();
  // The browser's dial is drawn from the scheme's palette: a dark face at
  // night and in dark mode (dial_look).
  const DialLook look = dial_look(p);
  lv_obj_t *dial = this->box_(parent);
  lv_obj_set_size(dial, size, size);
  lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(dial, lv_color_hex(look.face), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(dial, lv_color_hex(look.ring), LV_PART_MAIN);
  lv_obj_set_style_border_width(dial, std::max(3, size / 40), LV_PART_MAIN);
  auto *d = new Dial();
  d->hour_deg = clock.hour_deg;
  d->minute_deg = clock.minute_deg;
  d->ink = look.hands;
  d->tick_major = look.tick_major;
  d->tick_minor = look.tick_minor;
  d->wedge_opa = look.wedge_opa;
  // The time-timer wedge counts down to NEXT: no longer true offline (the
  // browser's data-rel), so the dial shows only the time.
  if (clock.wedge.has_value() && !this->offline_) {
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
  // Past and cancelled tiles fade only their picture (tile_style).
  const TileStyle style = tile_style(tile, p);
  // .dash-pv-tile: 96 px to 10rem wide, at least 72 px tall, 8 px / 4 px
  // padding (10 px either side) inside a 2 px border, the picture 4 px
  // over the caption.
  lv_obj_t *t = this->column_(parent, this->m_.px(4));
  lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_min_width(t, this->m_.px(96), LV_PART_MAIN);
  lv_obj_set_style_max_width(t, this->m_.px(160), LV_PART_MAIN);
  lv_obj_set_style_min_height(t, this->m_.px(72), LV_PART_MAIN);
  lv_obj_set_style_pad_hor(t, this->m_.px(10), LV_PART_MAIN);
  lv_obj_set_style_pad_top(t, this->m_.px(8), LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(t, this->m_.px(4), LV_PART_MAIN);
  lv_obj_set_style_bg_color(t, lv_color_hex(p.bg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(t, this->m_.px(18), LV_PART_MAIN);
  const int border = std::max(1, this->m_.px(2));
  lv_obj_set_style_border_color(t, lv_color_hex(p.border), LV_PART_MAIN);
  lv_obj_set_style_border_width(t, border, LV_PART_MAIN);
  if (tile.current) {
    // .dash-pv-current: a 6 px accent ring 3 px outside the tile — live
    // data, grey offline.
    lv_obj_set_style_outline_color(t, lv_color_hex(this->live_().accent), LV_PART_MAIN);
    lv_obj_set_style_outline_width(t, this->m_.px(6), LV_PART_MAIN);
    lv_obj_set_style_outline_pad(t, this->m_.px(3), LV_PART_MAIN);
  }
  // The colour strip along the bottom, as on the tablet: a hard shadow
  // under the tile. LVGL draws no shadow at all with a width of 0, so the
  // narrowest blur (1 px) keeps the edge sharp.
  lv_obj_set_style_shadow_color(t, lv_color_hex(style.strip), LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(t, this->m_.px(5), LV_PART_MAIN);
  lv_obj_set_style_shadow_width(t, 1, LV_PART_MAIN);
  lv_obj_set_style_shadow_spread(t, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(t, LV_OPA_COVER, LV_PART_MAIN);
  if (!tile.icon.empty()) {
    lv_obj_t *icon = this->emoji_(t, tile.icon, size);
    if (style.emoji_opa != OPA_FULL)
      lv_obj_set_style_opa(icon, style.emoji_opa, LV_PART_MAIN);
    if (tile.current)
      this->live_picture_(icon);
  }
  if (!tile.caption.empty()) {
    // .dash-pv-cap: 15 px, line-height 1.3, one line ending in "…".
    lv_obj_t *caption = this->inline_(t, tile.caption, FontRole::CAPTION, style.caption_ink,
                                      this->m_.px(160) - 2 * this->m_.px(10) - 2 * border);
    this->line_box_(caption, FontRole::CAPTION, 19.5f);
    if (style.strike) {
      for (uint32_t k = 0; k < lv_obj_get_child_count(caption); k++)
        lv_obj_set_style_text_decor(lv_obj_get_child(caption, k), LV_TEXT_DECOR_STRIKETHROUGH, LV_PART_MAIN);
    }
  }
  // Content-sized, but LVGL's flex centres the children in the size they
  // need before min-width / min-height grow the tile: size it here.
  int content_w = tile.icon.empty() ? 0 : size;
  int content_h = tile.icon.empty() ? 0 : size;
  if (!tile.caption.empty()) {
    lv_obj_t *caption = lv_obj_get_child(t, -1);
    lv_obj_update_layout(caption);
    content_w = std::max<int>(content_w, lv_obj_get_width(caption));
    content_h += (content_h > 0 ? this->m_.px(4) : 0) + this->m_.px(19.5f);
  }
  lv_obj_set_size(t,
                  std::clamp(content_w + 2 * this->m_.px(10) + 2 * border, this->m_.px(96), this->m_.px(160)),
                  std::max(this->m_.px(72), content_h + this->m_.px(8) + this->m_.px(4) + 2 * border));
  return t;
}

void SocialHomeDisplay::build_person_(lv_obj_t *parent) {
  const Palette &p = this->palette_();
  const Board &b = *this->board_;
  const PersonHead &who = b.person;
  const bool picture = b.person_kind == "picture" && b.picture.has_value();
  if (picture) {
    // The picture view is one block under the offline note (the tablet's
    // .dash-pv): the dial floats at its top end, not over the note.
    // .dash-pv-head's 16 px rows (the browser's .dash-pv gap is 20 px
    // below the head).
    lv_obj_t *pv = this->column_(parent, this->m_.px(16));
    lv_obj_set_width(pv, LV_PCT(100));
    allow_overhang(pv, this->m_.px(12));
    parent = pv;
  }

  lv_obj_t *head = this->row_(parent, this->m_.px(20));
  lv_obj_set_width(head, LV_PCT(100));
  // The face 24 px from the top, as the browser's content padding.
  lv_obj_set_style_pad_top(head, this->m_.px(8), LV_PART_MAIN);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, picture ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START);
  lv_obj_add_flag(head, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_t *who_row = this->row_(head, this->m_.px(20));
  lv_obj_add_flag(who_row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_flex_grow(who_row, 1);
  // The face sits at the column's start and the head ends under it, as
  // the browser's .dash-person-head / .dash-pv-who; the picture view's ring
  // overhangs (the rows let it out).
  lv_obj_t *avatar = this->avatar_of_(who_row, who.initials, who.avatar, who.color, who.ink, this->m_.px(88));
  if (picture) {
    // The ring (4 px gap + 5 px) overhangs the face's row and the head.
    allow_overhang(who_row, this->m_.px(10));
    allow_overhang(head, this->m_.px(10));
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
      // Where they are is live data: muted words, faded icon offline
      // (.dash-person-where: the board's 22 px, 8 px after the icon).
      lv_obj_t *where = this->row_(head, this->m_.px(8));
      this->live_picture_(this->emoji_(where, who.where->icon, this->emoji_px_(FontRole::BODY)));
      this->text_(where, who.where->label, FontRole::BODY, this->live_().text);
    }
    // .dash-clock-sm: 4rem light, letter-spacing -0.02em (1.28 px) — 2 px
    // here, as the hinted digits are each a little wider than Chromium's.
    lv_obj_t *clock = this->clock_label_(head, FontRole::CLOCK_SMALL, p.text);
    lv_obj_set_style_text_letter_space(clock, -std::max(1, this->m_.px(2)), LV_PART_MAIN);
  }

  if (picture) {
    this->build_picture_(parent);
  } else {
    // .dash-for-you: 20 px over and under the messages for this person.
    const uint32_t before = lv_obj_get_child_count(parent);
    this->build_alerts_(parent, b.mentions);
    if (lv_obj_get_child_count(parent) > before)
      lv_obj_set_style_margin_top(lv_obj_get_child(parent, static_cast<int32_t>(before)),
                                  this->m_.px(20) - lv_obj_get_style_pad_row(parent, LV_PART_MAIN), LV_PART_MAIN);
    this->build_reader_(parent);
  }
}

void SocialHomeDisplay::build_reader_(lv_obj_t *parent) {
  const Palette &p = this->palette_();
  if (!this->board_->reader.has_value())
    return;
  const Reader &r = *this->board_->reader;
  // NOW is live data: offline its frame greys, its words go muted and its
  // emoji fades (NEXT keeps its colour; only its "in 34 min" goes).
  const LiveStyle live = this->live_();
  const int width = main_width(this->m_, this->board_->header.rail.shown);
  const int gap = this->m_.gap();

  if (r.now.has_value() || r.next.has_value()) {
    // .dash-nownext: a grid of cards at least 16rem wide, 16 px apart.
    const int nn_gap = this->m_.px(16);
    lv_obj_t *row = this->row_(parent, nn_gap);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_style_margin_top(row, this->m_.px(20) - lv_obj_get_style_pad_row(parent, LV_PART_MAIN), LV_PART_MAIN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    const int n = (r.now.has_value() ? 1 : 0) + (r.next.has_value() ? 1 : 0);
    const int w = (width - nn_gap * (n - 1)) / n;
    // .dash-nn: 14 px / 20 px padding, a 1 px border with the colour as an
    // 8 px start bar; NOW gets a 3 px accent ring inside (box-shadow inset).
    auto shell = [&](Color color, bool now) {
      const uint32_t frame = now ? live.mark(color.or_else(p.accent)) : color.or_else(p.accent);
      lv_obj_t *card = this->card_(row);
      lv_obj_set_width(card, w);
      lv_obj_set_style_pad_row(card, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_ver(card, this->m_.px(14), LV_PART_MAIN);
      lv_obj_set_style_pad_hor(card, this->m_.px(20), LV_PART_MAIN);
      lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
      lv_obj_set_style_border_width(card, this->m_.px(8), LV_PART_MAIN);
      lv_obj_set_style_border_color(card, lv_color_hex(frame), LV_PART_MAIN);
      if (now) {
        lv_obj_set_style_outline_color(card, lv_color_hex(live.mark(p.accent)), LV_PART_MAIN);
        lv_obj_set_style_outline_width(card, this->m_.px(3), LV_PART_MAIN);
        lv_obj_set_style_outline_pad(card, -this->m_.px(3), LV_PART_MAIN);
      }
      return card;
    };
    // The NOW / NEXT label (.dash-label, 8 px under it), the title line
    // (.dash-nn-title: 28 px bold, line-height 1.25, with the 20 px room
    // after it on one baseline) and the time (.dash-nn-when: the board's
    // 22 px on its 1.35 line, 6 px under the title).
    const float title_line = 35.0f;
    auto fill = [&](lv_obj_t *card, const std::string &label, const Emoji &icon, const std::string &title,
                    const std::string &meta, uint32_t ink, bool live_icon) {
      lv_obj_t *head = this->caps_(card, label, p.muted);
      this->line_box_(head, FontRole::LABEL, 20.8f);
      lv_obj_set_style_margin_bottom(head, this->m_.px(8), LV_PART_MAIN);
      const int space = lv_font_get_glyph_width(this->font_(FontRole::HEADING), ' ', 0);
      lv_obj_t *line = this->row_(card, space);
      lv_obj_set_width(line, LV_PCT(100));
      lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW_WRAP);
      lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
      if (!icon.empty()) {
        const int size = this->emoji_px_(FontRole::HEADING);
        lv_obj_t *pic = this->emoji_(line, icon, size);
        lv_obj_set_style_margin_top(pic, (this->m_.px(title_line) - size) / 2, LV_PART_MAIN);
        // The browser's emoji glyph advances ~1.23 em (about 1.05 Roboto
        // lines), wider than the picture.
        const int advance = lv_font_get_line_height(this->font_(FontRole::HEADING)) * 105 / 100;
        lv_obj_set_style_margin_right(pic, std::max(0, advance - size), LV_PART_MAIN);
        if (live_icon)
          this->live_picture_(pic);
      }
      lv_obj_t *t = this->text_(line, title, FontRole::HEADING, ink);
      lv_obj_set_style_max_width(t, LV_PCT(100), LV_PART_MAIN);
      lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
      this->line_box_(t, FontRole::HEADING, title_line);
      if (!meta.empty()) {
        lv_obj_t *m = this->text_(line, "\xC2\xB7 " + meta, FontRole::MEDIUM, p.muted);
        lv_obj_set_style_margin_top(m, this->inline_top_(FontRole::HEADING, title_line, FontRole::MEDIUM),
                                    LV_PART_MAIN);
      }
    };
    auto when = [&](lv_obj_t *card, const std::string &text) {
      lv_obj_t *label = this->text_(card, text, FontRole::BODY, p.muted);
      this->line_margin_(label, FontRole::BODY, 29.7f);
      lv_obj_set_style_margin_top(label, this->m_.px(6), LV_PART_MAIN);
    };
    if (r.now.has_value()) {
      lv_obj_t *card = shell(r.now->color, true);
      fill(card, r.now->label, r.now->icon, r.now->title, r.now->meta, live.text, true);
      when(card, r.now->until);
    }
    if (r.next.has_value()) {
      lv_obj_t *card = shell(r.next->color, false);
      fill(card, r.next->label, r.next->icon, r.next->title, r.next->meta, p.text, false);
      // "· in 34 min" is no longer true offline (the browser's data-rel).
      const bool rel = !r.next->rel.empty() && !live.stale;
      when(card, rel ? r.next->time + " \xC2\xB7 " + r.next->rel : r.next->time);
    }
  }

  const int cols = (r.school.has_value() ? 1 : 0) + (r.day.has_value() ? 1 : 0) + (r.chores.has_value() ? 1 : 0);
  if (cols == 0)
    return;
  const int w = (width - gap * (cols - 1)) / cols;
  // .dash-columns: 20 px under NOW / NEXT.
  lv_obj_t *row = this->row_(parent, gap);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_margin_top(row, this->m_.px(20) - lv_obj_get_style_pad_row(parent, LV_PART_MAIN), LV_PART_MAIN);
  // .dash-col: its label (line-height 1.3) 8 px over the list, whose rows
  // are 6 px apart (.dash-agenda; the chores 10 px, .dash-chores).
  auto column = [&](const std::string &label, int list_gap) {
    lv_obj_t *card = this->section_card_(row, label);
    lv_obj_set_width(card, w);
    lv_obj_set_style_pad_row(card, list_gap, LV_PART_MAIN);
    if (!label.empty()) {
      lv_obj_t *head = lv_obj_get_child(card, 0);
      this->line_margin_(head, FontRole::LABEL, 20.8f);
      lv_obj_set_style_margin_bottom(head, lv_obj_get_style_margin_bottom(head, LV_PART_MAIN) + this->m_.px(8) - list_gap,
                                     LV_PART_MAIN);
    }
    return card;
  };
  if (r.school.has_value()) {
    lv_obj_t *card = column(r.school->label, this->m_.px(6));
    for (const auto &item : r.school->rows)
      this->agenda_row_(card, item, false);
  }
  if (r.day.has_value()) {
    lv_obj_t *card = column(r.day->label, this->m_.px(6));
    for (const auto &item : r.day->rows)
      this->agenda_row_(card, item, false);
    if (r.day->rows.empty() && !r.day->empty.empty()) {
      lv_obj_t *line = this->row_(card, this->m_.px(10));
      if (!r.day->empty_icon.empty())
        this->emoji_(line, r.day->empty_icon, this->m_.emoji_md());
      this->text_(line, r.day->empty, FontRole::BODY, p.muted);
    }
    if (r.day->tomorrow.has_value()) {
      // h3.dash-sublabel: 16 px under the list, 8 px over the next.
      lv_obj_t *label = this->caps_(card, r.day->tomorrow->label, p.muted);
      this->line_margin_(label, FontRole::LABEL, 20.8f);
      const int list_gap = lv_obj_get_style_pad_row(card, LV_PART_MAIN);
      lv_obj_set_style_margin_top(label, lv_obj_get_style_margin_top(label, LV_PART_MAIN) + this->m_.px(16) - list_gap,
                                  LV_PART_MAIN);
      lv_obj_set_style_margin_bottom(label,
                                     lv_obj_get_style_margin_bottom(label, LV_PART_MAIN) + this->m_.px(8) - list_gap,
                                     LV_PART_MAIN);
      for (const auto &item : r.day->tomorrow->rows)
        this->agenda_row_(card, item, false);
    }
  }
  if (r.chores.has_value()) {
    lv_obj_t *card = column(r.chores->label, this->m_.px(10));
    for (const auto &chore : r.chores->items)
      this->chore_row_(card, chore, false);
  }
}

void SocialHomeDisplay::build_picture_(lv_obj_t *parent) {
  const Palette &p = this->palette_();
  const Picture &pv = *this->board_->picture;
  const LiveStyle live = this->live_();
  const int gap = this->m_.gap();
  // The 💬 on a face (the Twemoji picture: a pale bubble with three dots),
  // on a round badge so it reads on any face; a new message's badge is
  // filled with the accent, as on the tablet.
  auto speech_badge = [&](lv_obj_t *face, bool fresh) {
    lv_obj_t *badge = this->box_(face);
    const int d = this->m_.px(fresh ? 46 : 40);
    lv_obj_set_size(badge, d, d);
    lv_obj_add_flag(badge, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge, lv_color_hex(fresh ? p.accent : p.surface), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(badge, lv_color_hex(p.surface), LV_PART_MAIN);
    lv_obj_set_style_border_width(badge, std::max(2, this->m_.px(3)), LV_PART_MAIN);
    lv_obj_align(badge, LV_ALIGN_BOTTOM_RIGHT, this->m_.px(fresh ? 12 : 8), this->m_.px(fresh ? 10 : 6));
    lv_obj_t *bubble = this->emoji_(badge, Emoji{"\xF0\x9F\x92\xAC", "1f4ac"}, d * 62 / 100);
    lv_obj_center(bubble);
  };

  // Messages for this child: the sender's face, a speech bubble, "Got it".
  for (const auto &mention : this->board_->mentions) {
    if (this->dismissed_.count(mention.key))
      continue;
    lv_obj_t *card = this->row_(parent, this->m_.px(24));
    lv_obj_set_style_bg_color(card, lv_color_hex(p.surface), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(p.accent), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, this->m_.px(3), LV_PART_MAIN);
    // .dash-pv-mention: 12 px / 10 px padding (16 px / 12 px either side)
    // in a 3 px accent ring, 32 px round.
    lv_obj_set_style_radius(card, this->m_.px(32), LV_PART_MAIN);
    lv_obj_set_style_pad_top(card, this->m_.px(12), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(card, this->m_.px(10), LV_PART_MAIN);
    lv_obj_set_style_pad_left(card, this->m_.px(16), LV_PART_MAIN);
    lv_obj_set_style_pad_right(card, this->m_.px(12), LV_PART_MAIN);
    // .dash-pv-msg: the face 10 px over its caption, at most 11rem wide.
    lv_obj_t *who = this->column_(card, this->m_.px(10));
    lv_obj_set_flex_align(who, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *face = this->avatar_(who, mention.person, this->m_.px(88));
    speech_badge(face, true);
    this->line_box_(this->inline_(who, mention.caption, FontRole::CAPTION, p.muted, this->m_.px(176)),
                    FontRole::CAPTION, 19.5f);
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
    // .dash-pv-nownext: the cards 24 px either side of the arrow.
    lv_obj_t *row = this->row_(parent, this->m_.px(24));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto big_tile = [&](const Tile &tile, const std::string &label, bool next) {
      // .dash-pv-card: 200 px wide, at least 172 px, 8 px / 10 px padding,
      // its label, picture (5rem) and caption 4 px apart, centred.
      lv_obj_t *t = this->column_(row, this->m_.px(4));
      lv_obj_set_width(t, this->m_.px(200));
      lv_obj_set_style_min_height(t, this->m_.px(172), LV_PART_MAIN);
      lv_obj_set_style_pad_top(t, this->m_.px(8), LV_PART_MAIN);
      lv_obj_set_style_pad_bottom(t, this->m_.px(10), LV_PART_MAIN);
      lv_obj_set_style_pad_hor(t, this->m_.px(10), LV_PART_MAIN);
      lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_bg_color(t, lv_color_hex(p.surface), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_radius(t, this->m_.px(24), LV_PART_MAIN);
      lv_obj_set_style_border_width(t, next ? this->m_.px(3) : this->m_.px(9), LV_PART_MAIN);
      // NOW is live data: offline its frame greys and its picture fades.
      lv_obj_set_style_border_color(t, lv_color_hex(next ? tile.color.or_else(p.accent) : live.mark(p.border)),
                                    LV_PART_MAIN);
      this->pv_sec_(t, label);
      if (!tile.icon.empty()) {
        lv_obj_t *icon = this->emoji_(t, tile.icon, this->m_.px(80));
        if (!next)
          this->live_picture_(icon);
      }
      // .dash-pv-card .dash-pv-cap: 16 px, line-height 1.3, centred, two
      // lines at most.
      if (!tile.caption.empty()) {
        lv_obj_t *cap = this->text_(t, tile.caption, FontRole::SMALL, p.muted);
        lv_obj_set_style_max_width(cap, LV_PCT(100), LV_PART_MAIN);
        lv_label_set_long_mode(cap, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_max_height(cap, this->m_.px(2 * 20.8f), LV_PART_MAIN);
        this->line_box_(cap, FontRole::SMALL, 20.8f);
      }
      fix_size(t);
      // The pie counts down to NEXT: hidden offline (the browser's data-rel).
      if (next && pv.next_pie.has_value() && !live.stale) {
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
    // .dash-pv-msg-list: 20 px apart.
    lv_obj_t *row = this->row_(parent, this->m_.px(20));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    for (const auto &msg : pv.messages) {
      lv_obj_t *item = this->column_(row, this->m_.px(10));
      lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_t *face = this->avatar_(item, msg.person, this->m_.px(88));
      speech_badge(face, msg.is_new);
      // .dash-pv-msg: at most 11rem wide, its caption one line with "…".
      this->line_box_(this->inline_(item, msg.caption, FontRole::CAPTION, p.muted, this->m_.px(176)),
                      FontRole::CAPTION, 19.5f);
    }
  }

  // My day: morning / afternoon / evening / night, each a row of tiles.
  // .dash-pv-day > .dash-pv-sec: 6 px in, 6 px over the parts.
  lv_obj_t *day_label = this->pv_sec_(parent, pv.day.label);
  lv_obj_set_style_margin_left(day_label, this->m_.px(6), LV_PART_MAIN);
  // .dash-pv: 20 px between the head and the day.
  lv_obj_set_style_margin_top(day_label, this->m_.px(20) - lv_obj_get_style_pad_row(parent, LV_PART_MAIN),
                              LV_PART_MAIN);
  lv_obj_set_style_margin_bottom(day_label, this->m_.px(6) - lv_obj_get_style_pad_row(parent, LV_PART_MAIN),
                                 LV_PART_MAIN);
  if (pv.day.empty && !pv.day.empty_label.empty()) {
    lv_obj_t *line = this->row_(parent, this->m_.px(10));
    if (!pv.day.empty_icon.empty())
      this->emoji_(line, pv.day.empty_icon, this->m_.emoji_md());
    this->text_(line, pv.day.empty_label, FontRole::BODY, p.muted);
  }
  // The parts side by side, as on a 1280 px tablet; tiles wrap inside.
  // .dash-pv-emoji: 2.75rem.
  const int tile = this->m_.px(44);
  const int width = main_width(this->m_, this->board_->header.rail.shown);
  // .dash-pv-parts: four equal columns, 12 px apart.
  const int part_gap = this->m_.px(12);
  const int parts = std::max<int>(1, static_cast<int>(pv.day.parts.size()));
  const int part_w = (width - part_gap * (parts - 1)) / parts;
  lv_obj_t *day = this->row_(parent, part_gap);
  lv_obj_set_width(day, LV_PCT(100));
  lv_obj_set_flex_align(day, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  for (const auto &part : pv.day.parts) {
    // .dash-pv-part: 12 px / 8 px padding inside a 3 px border (the
    // accent on the part of the day it is now), the icon 10 px over the
    // tiles.
    lv_obj_t *col = this->column_(day, this->m_.px(10));
    lv_obj_set_width(col, part_w);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(col, lv_color_hex(p.surface), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(col, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(col, this->m_.px(20), LV_PART_MAIN);
    lv_obj_set_style_pad_ver(col, this->m_.px(12), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(col, this->m_.px(8), LV_PART_MAIN);
    lv_obj_set_style_border_width(col, this->m_.px(3), LV_PART_MAIN);
    lv_obj_set_style_border_color(col, lv_color_hex(p.accent), LV_PART_MAIN);
    lv_obj_set_style_border_opa(col, part.state == "now" ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    // A past part fades only its icon; its tiles fade their own pictures.
    lv_obj_t *part_icon = this->emoji_(col, part.icon, this->m_.px(40));
    if (part.state == "past")
      lv_obj_set_style_opa(part_icon, OPA_PAST_PICTURE, LV_PART_MAIN);
    // .dash-pv-tiles: wrapping, centred, 10 px apart.
    lv_obj_t *tiles = this->row_(col, this->m_.px(10));
    lv_obj_set_width(tiles, LV_PCT(100));
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tiles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(tiles, this->m_.px(10), LV_PART_MAIN);
    // Room for the last row's colour strip, drawn under each tile.
    lv_obj_set_style_pad_bottom(tiles, this->m_.px(6), LV_PART_MAIN);
    for (const auto &t : part.tiles)
      this->build_tile_(tiles, t, tile);
  }
  // A grid row: every part as tall as the tallest.
  lv_obj_update_layout(day);
  int tallest = 0;
  for (uint32_t k = 0; k < lv_obj_get_child_count(day); k++)
    tallest = std::max<int>(tallest, lv_obj_get_height(lv_obj_get_child(day, k)));
  for (uint32_t k = 0; k < lv_obj_get_child_count(day); k++)
    lv_obj_set_height(lv_obj_get_child(day, k), tallest);

  // Tomorrow · My jobs · Who's home: .dash-pv-row cards with a small-caps
  // label.
  auto pv_card = [&](lv_obj_t *in, const std::string &label) {
    lv_obj_t *card = this->card_(in);
    if (!label.empty())
      this->pv_sec_(card, label);
    return card;
  };
  lv_obj_t *row = this->row_(parent, gap);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(row, gap, LV_PART_MAIN);
  if (pv.tomorrow.has_value()) {
    lv_obj_t *card = pv_card(row, pv.tomorrow->label);
    lv_obj_t *tiles = this->row_(card, this->m_.px(12));
    lv_obj_set_style_pad_bottom(tiles, this->m_.px(6), LV_PART_MAIN);
    this->emoji_(tiles, Emoji{"\xF0\x9F\x8C\x99", "1f319"}, this->m_.px(40));
    for (const auto &t : pv.tomorrow->tiles)
      this->build_tile_(tiles, t, tile);
  }
  if (!pv.jobs.empty()) {
    lv_obj_t *card = pv_card(row, pv.jobs_label);
    lv_obj_t *tiles = this->row_(card, this->m_.px(12));
    lv_obj_set_style_pad_bottom(tiles, this->m_.px(6), LV_PART_MAIN);
    // The ☐ / ✓ overhangs each tile's top corner, as the browser's
    // .dash-pv-check (34 px, 10 px over the padding box: 8 px past the 2 px
    // border). The tile and the row let it out into the card's padding —
    // without that LVGL cut its top and end at the row's edges.
    const int over = this->m_.px(8);
    const int tile_border = std::max(1, this->m_.px(2));
    allow_overhang(tiles, over);
    for (const auto &job : pv.jobs) {
      Tile t;
      t.icon = job.icon;
      t.caption = job.caption;
      lv_obj_t *jt = this->build_tile_(tiles, t, tile);
      allow_overhang(jt, over);
      // Open: a ☐ on the card colour in a border ring; done: a ✓ on accent.
      // Aligned in the tile's content box, so its padding and border count.
      lv_obj_t *mark = this->box_(jt);
      const int d = this->m_.px(34);
      lv_obj_set_size(mark, d, d);
      lv_obj_add_flag(mark, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_align(mark, LV_ALIGN_TOP_RIGHT, this->m_.px(10) + tile_border + over,
                   -(this->m_.px(6) + tile_border + over));
      lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, LV_PART_MAIN);
      lv_obj_set_style_bg_color(mark, lv_color_hex(job.done ? p.accent : p.surface), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_color(mark, lv_color_hex(job.done ? p.accent : p.border), LV_PART_MAIN);
      lv_obj_set_style_border_width(mark, std::max(1, this->m_.px(2)), LV_PART_MAIN);
      if (job.done) {
        lv_obj_t *check = this->check_mark_(mark, d * 6 / 10, p.accent_text, std::max(2, this->m_.px(3)));
        lv_obj_center(check);
      } else {
        lv_obj_t *box = this->box_(mark);
        const int b = d * 44 / 100;
        lv_obj_set_size(box, b, b);
        lv_obj_set_style_border_color(box, lv_color_hex(p.text), LV_PART_MAIN);
        lv_obj_set_style_border_width(box, std::max(1, this->m_.px(2)), LV_PART_MAIN);
        lv_obj_set_style_radius(box, this->m_.px(2), LV_PART_MAIN);
        lv_obj_center(box);
      }
    }
  }
  if (pv.where.has_value()) {
    lv_obj_t *card = pv_card(row, pv.where->label);
    lv_obj_t *line = this->row_(card, this->m_.px(10));
    // Live data: offline the faces and icons fade.
    auto group = [&](const PresenceGroup &g) {
      this->live_picture_(this->emoji_(line, g.icon, this->m_.px(40)));
      for (const auto &id : g.people)
        this->live_picture_(this->avatar_(line, id, this->m_.px(56)));
    };
    if (pv.where->home.has_value() && !pv.where->home->people.empty())
      group(*pv.where->home);
    if (pv.where->away.has_value() && !pv.where->away->people.empty())
      group(*pv.where->away);
  }
}

}  // namespace esphome::socialhome_display
