// SPDX-License-Identifier: MPL-2.0
//
// Home › Messages, Notes and Map (the browser's _home_messages / _home_notes
// / _home_map templates).
#include <algorithm>

#include "socialhome_display.h"

namespace esphome::socialhome_display {

namespace {
int main_width(const Metrics &m, bool rail) { return m.width - (rail ? m.rail_width() : 0) - 2 * m.pad(); }
}  // namespace


void SocialHomeDisplay::build_messages_tab_(lv_obj_t *parent) {
  const Palette &p = this->palette_();
  const MessagesTab &tab = this->board_->messages;
  const int width = main_width(this->m_, this->board_->header.rail.shown);
  if (tab.days.empty() && tab.empty.has_value()) {
    lv_obj_t *card = this->card_(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_t *line = this->row_(card, this->m_.px(12));
    if (!tab.empty->icon.empty())
      this->emoji_(line, tab.empty->icon, this->m_.emoji_md());
    lv_obj_t *texts = this->column_(line, this->m_.px(4));
    this->text_(texts, tab.empty->title, FontRole::BODY_BOLD, p.text);
    this->text_(texts, tab.empty->text, FontRole::BODY, p.muted);
    return;
  }
  for (const auto &day : tab.days) {
    this->caps_(parent, day.label, p.muted);
    lv_obj_t *card = this->card_(parent);
    lv_obj_set_width(card, std::min(width, this->m_.px(832)));
    lv_obj_set_style_pad_all(card, this->m_.px(12), LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 0, LV_PART_MAIN);
    for (size_t k = 0; k < day.rows.size(); k++) {
      const Message &msg = day.rows[k];
      lv_obj_t *row = this->row_(card, this->m_.px(16));
      lv_obj_set_width(row, LV_PCT(100));
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
      lv_obj_set_style_pad_ver(row, this->m_.px(12), LV_PART_MAIN);
      lv_obj_set_style_pad_left(row, this->m_.px(14), LV_PART_MAIN);
      if (msg.is_new) {
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, this->m_.px(6), LV_PART_MAIN);
        lv_obj_set_style_border_color(row, lv_color_hex(p.accent), LV_PART_MAIN);
      }
      this->avatar_(row, msg.person, this->m_.avatar_md());
      lv_obj_t *body = this->column_(row, this->m_.px(2));
      lv_obj_set_flex_grow(body, 1);
      lv_obj_t *head = this->row_(body, this->m_.px(10));
      this->text_(head, msg.name, FontRole::LABEL, p.text);
      if (msg.is_new)
        this->pill_(head, upper(this->str_("new")), p.accent, p.accent_text, FontRole::LABEL);
      this->text_(head, msg.time.empty() ? msg.when : msg.time, FontRole::SMALL, p.muted);
      lv_obj_t *line = this->row_(body, this->m_.px(8));
      lv_obj_set_width(line, LV_PCT(100));
      if (!msg.icon.empty())
        this->emoji_(line, msg.icon, this->m_.emoji_md());
      lv_obj_t *text = this->rich_(line, msg.text, FontRole::LARGE, p.text);
      lv_obj_set_width(text, 0);
      lv_obj_set_flex_grow(text, 1);
      if (k + 1 < day.rows.size()) {
        lv_obj_t *rule = this->box_(card);
        lv_obj_set_size(rule, LV_PCT(100), 1);
        lv_obj_set_style_bg_color(rule, lv_color_hex(p.border), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
      }
    }
  }
}

void SocialHomeDisplay::build_notes_tab_(lv_obj_t *parent) {
  const Palette &p = this->palette_();
  const NotesTab &tab = this->board_->notes;
  const int width = main_width(this->m_, this->board_->header.rail.shown);
  if (tab.items.empty() && tab.empty.has_value()) {
    lv_obj_t *card = this->card_(parent);
    lv_obj_set_width(card, LV_PCT(100));
    this->text_(card, tab.empty->title, FontRole::BODY_BOLD, p.text);
    this->wrap_(card, tab.empty->text, FontRole::BODY, p.muted);
    return;
  }
  const int gap = this->m_.gap();
  const int cols = this->m_.columns(width, 240);
  const int w = (width - gap * (cols - 1)) / cols;
  lv_obj_t *grid = this->row_(parent, gap);
  lv_obj_set_width(grid, LV_PCT(100));
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(grid, gap, LV_PART_MAIN);
  for (const auto &note : tab.items) {
    lv_obj_t *sticky = this->box_(grid);
    lv_obj_set_width(sticky, w);
    lv_obj_set_style_min_height(sticky, this->m_.px(168), LV_PART_MAIN);
    lv_obj_set_style_pad_all(sticky, this->m_.px(18), LV_PART_MAIN);
    lv_obj_set_style_radius(sticky, this->m_.px(4), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sticky, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(sticky, this->m_.px(8), LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(sticky, this->m_.px(2), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(sticky, LV_OPA_20, LV_PART_MAIN);
    uint32_t ink = p.text;
    if (note.light.has_value()) {
      lv_obj_set_style_bg_color(sticky, lv_color_hex(note.light->bg.or_else(p.surface)), LV_PART_MAIN);
      ink = note.light->ink.or_else(p.text);
    } else {
      lv_obj_set_style_bg_color(sticky, lv_color_hex(p.surface), LV_PART_MAIN);
      lv_obj_set_style_border_color(sticky, lv_color_hex(p.border), LV_PART_MAIN);
      lv_obj_set_style_border_width(sticky, 1, LV_PART_MAIN);
    }
    this->rich_(sticky, note.text, FontRole::LARGE, ink);
  }
  if (!tab.more.empty())
    this->text_(parent, tab.more, FontRole::BODY, p.muted);
}

void SocialHomeDisplay::build_map_tab_(lv_obj_t *parent) {
  const Palette &p = this->palette_();
  const MapTab &map = this->board_->map;
  const int width = main_width(this->m_, this->board_->header.rail.shown);
  const int gap = this->m_.gap();
  const int list_w = this->m_.px(336);
  lv_obj_t *row = this->row_(parent, gap);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  // The map picture arrives with a later server release (GET /map.png);
  // until then the area keeps its place and says so.
  lv_obj_t *area = this->card_(row);
  lv_obj_set_size(area, width - list_w - gap, this->m_.px(560));
  lv_obj_set_flex_align(area, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_color(area, lv_color_hex(p.bg), LV_PART_MAIN);
  this->emoji_(area, Emoji{"\xF0\x9F\x97\xBA", "1f5fa"}, this->m_.emoji_lg());
  lv_obj_t *note = this->text_(area, this->str_("map_placeholder"), FontRole::BODY, p.muted);
  lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  if (!map.attribution.empty())
    this->text_(area, map.attribution, FontRole::SMALL, p.muted);

  lv_obj_t *list = this->section_card_(row, map.title.empty() ? this->str_("map_title") : map.title);
  lv_obj_set_width(list, list_w);
  lv_obj_set_style_pad_row(list, this->m_.px(14), LV_PART_MAIN);
  const int face = this->m_.px(64);
  for (const auto &spot : map.spots) {
    lv_obj_t *item = this->row_(list, this->m_.px(14));
    lv_obj_set_width(item, LV_PCT(100));
    const Person *person = this->board_->header.find_person(spot.person);
    lv_obj_t *frame = this->box_(item);
    lv_obj_set_size(frame, face, face);
    lv_obj_set_style_radius(frame, this->m_.px(14), LV_PART_MAIN);
    lv_obj_set_style_border_width(frame, this->m_.px(4), LV_PART_MAIN);
    lv_obj_set_style_border_color(
        frame, lv_color_hex(person != nullptr ? person->color.or_else(p.border) : p.border), LV_PART_MAIN);
    lv_obj_t *avatar = this->avatar_(frame, spot.person, face - this->m_.px(12));
    lv_obj_center(avatar);
    lv_obj_t *body = this->column_(item, this->m_.px(2));
    lv_obj_set_flex_grow(body, 1);
    this->text_(body, spot.name, FontRole::BODY_BOLD, p.text);
    lv_obj_t *where = this->row_(body, this->m_.px(6));
    this->emoji_(where, spot.icon, this->m_.px(20));
    this->text_(where, spot.place, FontRole::BODY, p.text);
    this->text_(body, spot.when, FontRole::SMALL, p.muted);
    if (spot.stale)
      lv_obj_set_style_opa(item, LV_OPA_60, LV_PART_MAIN);
  }
}

}  // namespace esphome::socialhome_display
