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
  // .dash-home: the top row, Today and the columns, 20 px apart.
  parent = this->column_(parent, this->m_.px(20));
  lv_obj_set_width(parent, LV_PCT(100));

  // ---- clock + who's home | next + messages (.dash-top). Every line gets
  // the tablet's line box, so each block starts where it does there.
  lv_obj_t *top = this->row_(parent, 0);
  lv_obj_set_width(top, LV_PCT(100));
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  const int side_w = top_side_width(width, this->m_);
  lv_obj_t *left = this->column_(top, 0);
  lv_obj_set_width(left, width - side_w - this->m_.px(32));
  // .dash-clock: line-height 1.
  lv_obj_t *clock = this->clock_label_(left, FontRole::CLOCK, p.text);
  this->line_box_(clock, FontRole::CLOCK, 102.4f);
  // .dash-date: 22 px, 6 px under the clock.
  lv_obj_t *date = this->text_(left, this->board_->header.clock.date, FontRole::BODY, p.muted);
  this->line_box_(date, FontRole::BODY, 29.7f);
  lv_obj_set_style_margin_top(date, this->m_.px(6), LV_PART_MAIN);
  if (o.presence.has_value()) {
    // .dash-presence: 20 px under the date, a 44 px row per group, 8 apart.
    lv_obj_t *who = this->column_(left, this->m_.px(8));
    lv_obj_set_style_margin_top(who, this->m_.px(20), LV_PART_MAIN);
    const int face = this->m_.px(44);
    // Live data: offline the faces and icons fade, the zones stay muted.
    auto group = [&](const PresenceGroup &g) {
      lv_obj_t *line = this->row_(who, this->m_.px(8));
      this->live_picture_(this->emoji_(line, g.icon, this->m_.emoji_md()));
      for (size_t k = 0; k < g.people.size(); k++) {
        this->live_picture_(this->avatar_(line, g.people[k], face));
        if (k < g.zones.size() && !g.zones[k].empty())
          this->text_(line, g.zones[k], FontRole::TEXT, p.muted);  // .dash-zone: 18 px
      }
    };
    if (o.presence->home.has_value() && !o.presence->home->people.empty())
      group(*o.presence->home);
    if (o.presence->away.has_value() && !o.presence->away->people.empty())
      group(*o.presence->away);
  }

  // .dash-top-side: NEXT, 12 px, the messages.
  lv_obj_t *right = this->column_(top, this->m_.px(12));
  lv_obj_set_width(right, side_w);
  if (o.next.has_value()) {
    const NextUp &n = *o.next;
    // .dash-next: 14 px / 20 px inside a 1 px border, the 8 px colour bar
    // on the start side; label, 8 px, the 38.4 px line, 8 px, the faces.
    lv_obj_t *card = this->card_(right);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, this->m_.px(8), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(n.color.or_else(p.accent)), LV_PART_MAIN);
    lv_obj_set_style_pad_ver(card, this->m_.px(14) + 1, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, this->m_.px(8), LV_PART_MAIN);
    this->line_box_(this->caps_(card, n.label, p.muted), FontRole::LABEL, 20.8f);
    lv_obj_t *title = this->row_(card, this->m_.px(10));
    int title_w = side_w - this->m_.px(8) - 2 * this->m_.card_pad();
    if (!n.icon.empty()) {
      this->emoji_(title, n.icon, this->m_.emoji_md());
      title_w -= this->m_.emoji_md() + this->m_.px(10);
    }
    lv_obj_t *t = this->text_(title, n.time.empty() ? n.title : n.time + " " + n.title, FontRole::TITLE, p.text);
    this->line_box_(t, FontRole::TITLE, 38.4f);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_width(t, title_w, LV_PART_MAIN);
    lv_obj_t *meta = this->row_(card, this->m_.px(10));
    for (const auto &id : n.people)
      this->avatar_(meta, id, this->m_.px(32));
    // "in 34 min" is no longer true offline (the browser's data-rel).
    if (!n.rel.empty() && !this->offline_)
      this->text_(meta, n.rel, FontRole::BODY, p.muted);
  }
  if (o.messages.has_value() && !o.messages->items.empty()) {
    // .dash-msgs: 12 px padding (8 at the bottom); its head is the 48 px
    // "See all ›" target pulled into that padding, 4 px over the rows.
    lv_obj_t *card = this->card_(right);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_style_pad_top(card, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(card, this->m_.px(8), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(card, this->m_.px(12), LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 0, LV_PART_MAIN);
    lv_obj_t *head = this->row_(card, 0);
    lv_obj_set_size(head, LV_PCT(100), this->m_.tap_min());
    lv_obj_set_style_margin_bottom(head, -this->m_.px(4), LV_PART_MAIN);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label = this->caps_(head, o.messages->label, p.muted);
    this->line_box_(label, FontRole::LABEL, 20.8f);
    lv_obj_set_style_margin_left(label, this->m_.px(8), LV_PART_MAIN);
    if (o.messages->see_all && !this->board_->header.is_night) {
      // .dash-see-all: 18 px bold, its 12 px padding 8 px into the card's.
      lv_obj_t *see = this->text_(head, this->str_("see_all") + " \xE2\x80\xBA", FontRole::TEXT_BOLD, p.accent);
      lv_obj_set_style_margin_right(see, this->m_.px(4), LV_PART_MAIN);
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
    if (!t.label.empty())
      this->line_box_(lv_obj_get_child(card, 0), FontRole::LABEL, 20.8f);  // .dash-label: line-height 1.3
    if (t.mode == "timeline" && t.timeline.has_value()) {
      // .dash-today: 16 px above and below; the label's 8 px margin.
      lv_obj_set_style_pad_ver(card, this->m_.px(16), LV_PART_MAIN);
      lv_obj_set_style_pad_row(card, this->m_.px(8), LV_PART_MAIN);
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
    // What follows the day sits by the card's own margins, not its gap.
    const int card_gap = lv_obj_get_style_pad_row(card, LV_PART_MAIN);
    const int body_line = this->m_.px(29.7f);  // .dash-board: 22 px, line-height 1.35
    if (!t.at_school.empty()) {
      // .dash-school-now: 16 px under the day, one centred flex line —
      // 12 px between items (8 px between wrapped lines), the label's
      // 8 px margin after and below it.
      lv_obj_t *line = this->row_(card, this->m_.px(12));
      lv_obj_set_width(line, LV_PCT(100));
      lv_obj_set_style_margin_top(line, std::max(0, this->m_.px(16) - card_gap), LV_PART_MAIN);
      lv_obj_set_style_pad_row(line, this->m_.px(8), LV_PART_MAIN);
      lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW_WRAP);
      lv_obj_t *label = this->caps_(line, t.at_school_label, p.muted);
      this->line_box_(label, FontRole::LABEL, 20.8f);  // .dash-sublabel: line-height 1.3
      lv_obj_set_style_margin_right(label, this->m_.px(8), LV_PART_MAIN);
      lv_obj_set_style_margin_bottom(label, this->m_.px(8), LV_PART_MAIN);
      for (const auto &item : t.at_school) {
        lv_obj_t *it = this->row_(line, this->m_.px(6));
        for (const auto &id : item.people)
          this->avatar_(it, id, this->m_.px(32));
        if (!item.icon.empty())
          this->emoji_(it, item.icon, this->m_.emoji_sm());
        this->line_box_(this->text_(it, item.title, FontRole::BODY, p.text), FontRole::BODY, 29.7f);
        this->line_box_(this->text_(it, item.until, FontRole::BODY, p.muted), FontRole::BODY, 29.7f);
      }
    }
    if (!t.changes.empty()) {
      // .dash-changes: 12 px under the line above; its label is inline in
      // a body line, the list 4 px below that line.
      lv_obj_t *block = this->column_(card, this->m_.px(4));
      lv_obj_set_width(block, LV_PCT(100));
      lv_obj_set_style_margin_top(block, std::max(0, this->m_.px(t.at_school.empty() ? 16 : 12) - card_gap),
                               LV_PART_MAIN);
      lv_obj_t *label = this->caps_(block, t.changes_label, p.muted);
      const int label_top = this->inline_top_(FontRole::BODY, 29.7f, FontRole::LABEL);
      lv_obj_set_style_margin_top(label, label_top, LV_PART_MAIN);
      lv_obj_set_style_margin_bottom(
          label, body_line - label_top - static_cast<int>(lv_font_get_line_height(this->font_(FontRole::LABEL))),
          LV_PART_MAIN);
      // .dash-change-list: 8 px between lines, 24 px between changes; a
      // change lines its parts up on the baseline, 8 px apart.
      lv_obj_t *line = this->row_(block, this->m_.px(24));
      lv_obj_set_width(line, LV_PCT(100));
      lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW_WRAP);
      lv_obj_set_style_pad_row(line, this->m_.px(8), LV_PART_MAIN);
      lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
      // .dash-tag: a 24 px line in a 1 px border, on the text's baseline.
      const int tag_top = this->inline_top_(FontRole::BODY, 29.7f, FontRole::LABEL, 24.0f) - 1;
      for (const auto &change : t.changes) {
        lv_obj_t *it = this->row_(line, this->m_.px(8));
        lv_obj_set_flex_align(it, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        bool cancelled = change.status == Status::CANCELLED;
        lv_obj_t *icon = this->emoji_(
            it, cancelled ? Emoji{"\xE2\x9A\xA0", "26a0"} : Emoji{"\xF0\x9F\x94\x81", "1f501"}, this->m_.emoji_sm());
        lv_obj_set_style_margin_top(icon, (body_line - this->m_.emoji_sm()) / 2, LV_PART_MAIN);
        if (!change.who.empty())
          this->line_box_(this->text_(it, change.who + ":", FontRole::BODY, p.text), FontRole::BODY, 29.7f);
        lv_obj_t *title = this->text_(it, change.title, FontRole::BODY, p.text);
        this->line_box_(title, FontRole::BODY, 29.7f);
        if (cancelled)
          lv_obj_set_style_text_decor(title, LV_TEXT_DECOR_STRIKETHROUGH, LV_PART_MAIN);
        uint32_t fg = cancelled ? p.error_text : p.text;
        // .dash-tag-warn fills a cancelled tag; the others are outlined.
        lv_obj_t *pill = this->pill_(it, upper(change.status_label), cancelled ? p.error_bg : p.surface, fg,
                                     FontRole::LABEL);
        lv_obj_set_style_border_color(pill, lv_color_hex(fg), LV_PART_MAIN);
        lv_obj_set_style_border_width(pill, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(pill, 0, LV_PART_MAIN);
        this->line_box_(lv_obj_get_child(pill, 0), FontRole::LABEL, 24.0f);
        lv_obj_set_style_margin_top(pill, tag_top, LV_PART_MAIN);
        if (!change.meta.empty())
          this->line_box_(this->text_(it, change.meta, FontRole::BODY, p.muted), FontRole::BODY, 29.7f);
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
      // .dash-see-all: 18 px bold, its 12 px padding 8 px into the card's.
      lv_obj_t *see = this->text_(head, this->str_("see_all") + " \xE2\x80\xBA", FontRole::TEXT_BOLD, p.accent);
      lv_obj_set_style_margin_right(see, this->m_.px(4), LV_PART_MAIN);
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
      this->rich_(sticky, note.text, FontRole::BODY, this->paint_note_(sticky, note));
    }
  }
}

const lv_image_dsc_t *SocialHomeDisplay::stripe_tile_(uint32_t fill, uint8_t opa, int h) {
  for (const auto &tile : this->stripes_) {
    if (tile->fill == fill && tile->opa == opa && static_cast<int>(tile->dsc.header.h) == h)
      return &tile->dsc;
  }
  if (this->stripes_.size() >= MAX_STRIPE_TILES)
    return nullptr;  // a plain bar rather than unbounded memory
  const int w = stripe_tile_width(this->m_.scale);
  if (this->stripe_coverage_.size() != static_cast<size_t>(w) * h)
    this->stripe_coverage_ = stripe_coverage(w, h);
  auto tile = std::make_unique<StripeTile>();
  tile->fill = fill;
  tile->opa = opa;
  tile->pixels = stripe_tile_rgb565(fill, opa, this->stripe_coverage_);
  tile->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  tile->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  tile->dsc.header.w = w;
  tile->dsc.header.h = h;
  tile->dsc.header.stride = w * 2;
  tile->dsc.data = reinterpret_cast<const uint8_t *>(tile->pixels.data());
  tile->dsc.data_size = tile->pixels.size() * 2;
  this->stripes_.push_back(std::move(tile));
  return &this->stripes_.back()->dsc;
}

void SocialHomeDisplay::build_timeline_(lv_obj_t *parent, const Timeline &line) {
  const Palette &p = this->palette_();
  // The "now" mark and line are live data: grey (the line faded) offline.
  const LiveStyle live = this->live_();
  const int width = main_width(this->m_, this->board_->header.rail.shown);
  const int inner = width - 2 * this->m_.card_pad() - 2;
  // .dash-lane: the name column (10rem), a 12 px gap, then the body with
  // .dash-track / .dash-ticks 12 px in from both of its edges.
  const int name_w = this->m_.px(160);
  const int track_inset = this->m_.px(12);
  const int track_x = name_w + this->m_.px(12) + track_inset;
  const int track_w = inner - track_x - track_inset;
  const int pad = this->m_.px(LaneGeometry::PAD);
  const int pitch = this->m_.px(LaneGeometry::ROW_PITCH);
  const int block_h = this->m_.px(LaneGeometry::BAR_HEIGHT);

  // The lanes stack with no gap between them (.dash-timeline), right under
  // the ticks.
  lv_obj_t *lanes = this->column_(parent, 0);
  lv_obj_set_width(lanes, inner);

  // Hour ticks and the "now" marker (.dash-ticks, 28 px).
  lv_obj_t *head = this->box_(lanes);
  lv_obj_set_size(head, inner, this->m_.px(LaneGeometry::TICKS_HEIGHT));
  lv_obj_add_flag(head, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  for (const auto &tick : line.ticks) {
    lv_obj_t *label = this->text_(head, tick.label, FontRole::SMALL, p.muted);
    lv_obj_update_layout(label);
    int x = track_x + Metrics::pct(tick.x, track_w) - lv_obj_get_width(label) / 2;
    x = std::min(x, inner - static_cast<int>(lv_obj_get_width(label)));
    lv_obj_set_pos(label, x, 0);
  }
  if (line.now_x.has_value()) {
    lv_obj_t *now = this->pill_(head, line.now_label, live.accent, live.accent_ink, FontRole::LABEL);
    lv_obj_update_layout(now);
    lv_obj_set_pos(now, track_x + Metrics::pct(*line.now_x, track_w) - lv_obj_get_width(now) / 2, 0);
  }

  for (const auto &lane : line.lanes) {
    // .dash-lane: a 1 px top border, 6 px padding around the body — the
    // person's all-day chips (.dash-allday), 4 px, then the track.
    const int track_h = this->m_.px(LaneGeometry::track_height(lane.rows));
    lv_obj_t *row = this->box_(lanes);
    lv_obj_set_width(row, inner);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, this->m_.px(LaneGeometry::BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(p.border), LV_PART_MAIN);

    int chips_h = 0;
    if (!lane.all_day.empty()) {
      // .dash-chip: 16 px text in a pill on the page colour, wrapping
      // across the track's width.
      lv_obj_t *chips = this->row_(row, this->m_.px(LaneGeometry::CHIP_GAP_X));
      lv_obj_set_width(chips, track_w);
      lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW_WRAP);
      lv_obj_set_style_pad_row(chips, this->m_.px(LaneGeometry::CHIP_GAP_Y), LV_PART_MAIN);
      lv_obj_set_pos(chips, track_x, pad);
      for (const auto &all_day : lane.all_day) {
        lv_obj_t *chip = this->box_(chips);
        lv_obj_set_style_max_width(chip, track_w, LV_PART_MAIN);
        lv_obj_set_style_bg_color(chip, lv_color_hex(p.bg), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(chip, lv_color_hex(p.border), LV_PART_MAIN);
        lv_obj_set_style_border_width(chip, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(chip, this->m_.px(LaneGeometry::CHIP_PAD), LV_PART_MAIN);
        lv_obj_t *t = this->text_(chip, all_day, FontRole::SMALL, p.text);
        this->line_box_(t, FontRole::SMALL, LaneGeometry::CHIP_LINE);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_max_width(t, track_w - 2 * this->m_.px(LaneGeometry::CHIP_PAD) - 2, LV_PART_MAIN);
      }
      lv_obj_update_layout(chips);
      chips_h = lv_obj_get_height(chips);
    }
    const int track_y = LaneGeometry::track_top(chips_h, this->m_);
    lv_obj_set_height(row, LaneGeometry::lane_height(lane.rows, chips_h, this->m_));

    // The face and name, centred on the lane's body (.dash-lane-head).
    lv_obj_t *who = this->row_(row, this->m_.px(8));
    lv_obj_set_size(who, name_w, track_y - pad + track_h);
    lv_obj_set_pos(who, 0, pad);
    this->avatar_(who, lane.person, this->m_.avatar_sm());
    const Person *person = this->board_->header.find_person(lane.person);
    lv_obj_t *name = this->text_(who, person != nullptr ? person->name : "", FontRole::BODY_BOLD, p.text);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(name, 1);

    lv_obj_t *track = this->box_(row);
    lv_obj_set_size(track, track_w, track_h);
    lv_obj_set_pos(track, track_x, track_y);
    const uint32_t color = lane.color.or_else(p.accent);
    for (const auto &block : lane.blocks) {
      lv_obj_t *b = this->row_(track, this->m_.px(6));
      int x = Metrics::pct(block.x, track_w);
      int w = std::max(this->m_.px(18), Metrics::pct(block.w, track_w));
      lv_obj_set_size(b, std::min(w, track_w - x), block_h);
      lv_obj_set_pos(b, x, block.row * pitch);
      // A past bar's fade is composited here, so the label is picked
      // against the colour actually drawn — never faded itself.
      const BarStyle style =
          bar_style(color, lane.ink.valid, lane.ink.rgb, p.surface, block.kind == "school", block.past);
      lv_obj_set_style_bg_color(b, lv_color_hex(style.fill), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_radius(b, this->m_.px(8), LV_PART_MAIN);
      lv_obj_set_style_pad_hor(b, this->m_.px(8), LV_PART_MAIN);
      const lv_image_dsc_t *stripes =
          style.stripe_opa > 0 ? this->stripe_tile_(style.fill, style.stripe_opa, block_h) : nullptr;
      if (stripes != nullptr) {
        // .dash-block-school's 135° stripes, tiled from the bar's corner
        // and clipped to its rounded edge.
        lv_obj_set_style_bg_image_src(b, stripes, LV_PART_MAIN);
        lv_obj_set_style_bg_image_tiled(b, true, LV_PART_MAIN);
      }
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
      // .dash-now-line: 3 px, 6 px past the track at both ends — into the
      // gap under the chips or the lane's padding, so on the lane (a child
      // is clipped to its parent), over the bars.
      lv_obj_t *now = this->box_(row);
      lv_obj_set_size(now, std::max(2, this->m_.px(3)), track_h + 2 * pad);
      lv_obj_set_pos(now, track_x + Metrics::pct(*line.now_x, track_w) - this->m_.px(1), track_y - pad);
      lv_obj_set_style_bg_color(now, lv_color_hex(live.accent), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(now, live.picture_opa, LV_PART_MAIN);
      lv_obj_set_style_radius(now, this->m_.px(2), LV_PART_MAIN);
    }
  }
}

}  // namespace esphome::socialhome_display
