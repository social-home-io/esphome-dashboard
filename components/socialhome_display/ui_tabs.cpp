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
  // .dash-chat-head: an 18 px line (the board's line-height 1.35) the name,
  // the NEW tag and the time share a baseline in.
  const float head_line = 24.3f;
  for (size_t d = 0; d < tab.days.size(); d++) {
    const auto &day = tab.days[d];
    // .dash-chat-label: the dash-label look, 8 px over the list; a later
    // day 20 px under the one before (.dash-chat-day + .dash-chat-day).
    lv_obj_t *label = this->caps_(parent, day.label, p.muted);
    this->line_box_(label, FontRole::LABEL, 20.8f);
    lv_obj_set_style_margin_bottom(label, this->m_.px(8) - lv_obj_get_style_pad_row(parent, LV_PART_MAIN),
                                   LV_PART_MAIN);
    if (d > 0)
      lv_obj_set_style_margin_top(label, this->m_.gap() - lv_obj_get_style_pad_row(parent, LV_PART_MAIN),
                                  LV_PART_MAIN);
    // .dash-chat-list: 4 px / 12 px padding, at most 52rem.
    lv_obj_t *card = this->card_(parent);
    lv_obj_set_width(card, std::min(width, this->m_.px(832)));
    lv_obj_set_style_pad_ver(card, this->m_.px(4), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(card, this->m_.px(12), LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 0, LV_PART_MAIN);
    for (size_t k = 0; k < day.rows.size(); k++) {
      const Message &msg = day.rows[k];
      if (k > 0) {
        // .dash-chat-item + .dash-chat-item: a 1 px rule between rows.
        lv_obj_t *rule = this->box_(card);
        lv_obj_set_size(rule, LV_PCT(100), 1);
        lv_obj_set_style_bg_color(rule, lv_color_hex(p.border), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
      }
      // .dash-chat-msg: 4 px margins, 12 px / 8 px padding after a 6 px
      // start border (the accent on a new message), at least 72 px.
      lv_obj_t *row = this->row_(card, this->m_.px(16));
      lv_obj_set_width(row, LV_PCT(100));
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
      lv_obj_set_style_min_height(row, this->m_.px(72), LV_PART_MAIN);
      lv_obj_set_style_margin_ver(row, this->m_.px(4), LV_PART_MAIN);
      lv_obj_set_style_pad_ver(row, this->m_.px(12), LV_PART_MAIN);
      lv_obj_set_style_pad_hor(row, this->m_.px(8), LV_PART_MAIN);
      lv_obj_set_style_radius(row, this->m_.px(10), LV_PART_MAIN);
      lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
      lv_obj_set_style_border_width(row, this->m_.px(6), LV_PART_MAIN);
      lv_obj_set_style_border_color(row, lv_color_hex(p.accent), LV_PART_MAIN);
      lv_obj_set_style_border_opa(row, msg.is_new ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
      this->avatar_(row, msg.person, this->m_.avatar_md());
      lv_obj_t *body = this->column_(row, this->m_.px(2));
      lv_obj_set_flex_grow(body, 1);
      lv_obj_t *head = this->row_(body, this->m_.px(10));
      lv_obj_set_height(head, this->m_.px(head_line));
      lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
      lv_obj_t *who = this->text_(head, msg.name, FontRole::TEXT_BOLD, p.text);
      lv_obj_set_style_margin_top(who, this->inline_top_(FontRole::TEXT_BOLD, head_line, FontRole::TEXT_BOLD),
                                  LV_PART_MAIN);
      // .dash-chat-when: 16 px, the NEW tag (13 px) and the time 8 px apart.
      const int time_top = this->inline_top_(FontRole::TEXT_BOLD, head_line, FontRole::SMALL);
      if (msg.is_new) {
        lv_obj_t *tag = this->tag_(head, upper(this->str_("new")), p.accent, p.accent_text);
        lv_obj_update_layout(tag);
        const int font_line = lv_font_get_line_height(this->font_(FontRole::SMALL));
        lv_obj_set_style_margin_top(tag, time_top - (lv_obj_get_height(tag) - font_line) / 2, LV_PART_MAIN);
        lv_obj_set_style_margin_right(tag, this->m_.px(8) - this->m_.px(10), LV_PART_MAIN);
      }
      // A new message's "11 min ago" is no longer true offline (data-rel).
      if (!msg.time.empty() || !(msg.is_new && this->offline_)) {
        lv_obj_t *when = this->text_(head, msg.time.empty() ? msg.when : msg.time, FontRole::SMALL, p.muted);
        lv_obj_set_style_margin_top(when, time_top, LV_PART_MAIN);
      }
      // .dash-chat-text: 24 px, line-height 1.3; the icon leads the words.
      lv_obj_t *line = this->row_(body, this->m_.px(8));
      lv_obj_set_width(line, LV_PCT(100));
      lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
      if (!msg.icon.empty()) {
        lv_obj_t *icon = this->emoji_(line, msg.icon, this->emoji_px_(FontRole::MESSAGE));
        lv_obj_set_style_margin_top(icon, this->inline_top_(FontRole::MESSAGE, 31.2f, FontRole::MESSAGE), LV_PART_MAIN);
      }
      lv_obj_t *text = this->rich_(line, msg.text, FontRole::MESSAGE, p.text);
      this->line_box_(text, FontRole::MESSAGE, 31.2f);
      lv_obj_set_width(text, 0);
      lv_obj_set_flex_grow(text, 1);
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
    const uint32_t ink = this->paint_note_(sticky, note);
    if (note_look(note, this->dark_()).shadow) {
      lv_obj_set_style_shadow_width(sticky, this->m_.px(8), LV_PART_MAIN);
      lv_obj_set_style_shadow_offset_y(sticky, this->m_.px(2), LV_PART_MAIN);
      lv_obj_set_style_shadow_opa(sticky, LV_OPA_20, LV_PART_MAIN);
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
  const int list_w = this->m_.map_list_width();
  lv_obj_t *row = this->row_(parent, gap);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  // The map as one picture (map.png), framed like the browser's Map tab
  // with the faces, pins and tile credit drawn in; the list beside it.
  const int map_w = width - list_w - gap;
  const int map_h = this->m_.map_height();
  const lv_image_dsc_t *picture = map.image.has_value() ? this->map_.picture(this->map_path_()) : nullptr;
  if (picture != nullptr) {
    // .dash-mp-box: a 1 px border round the picture, which fills the rest
    // (map_path_ asks for exactly that size, so the server frames the
    // same area as for the browser's box).
    lv_obj_t *box = this->box_(row);
    lv_obj_set_size(box, map_w, map_h);
    lv_obj_set_style_bg_color(box, lv_color_hex(p.bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(box, lv_color_hex(p.border), LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(box, this->m_.radius(), LV_PART_MAIN);
    lv_obj_t *img = lv_image_create(box);
    lv_image_set_src(img, picture);
    lv_obj_set_size(img, map_w - 2, map_h - 2);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CENTER);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    // Rounded inside the border (LVGL clips an image to its radius).
    lv_obj_set_style_radius(img, std::max(0, this->m_.radius() - 1), LV_PART_MAIN);
  } else {
    // Loading, no tiles right now (503), or a server without the picture.
    lv_obj_t *area = this->card_(row);
    lv_obj_set_size(area, map_w, map_h);
    lv_obj_set_flex_align(area, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(area, lv_color_hex(p.bg), LV_PART_MAIN);
    this->emoji_(area, Emoji{"\xF0\x9F\x97\xBA", "1f5fa"}, this->m_.emoji_lg());
    if (!map.image_field || !map.image.has_value()) {
      lv_obj_t *note = this->wrap_(area, this->str_(map.image_field ? "map_none" : "map_placeholder"),
                                   FontRole::BODY, p.muted);
      lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    if (!map.attribution.empty())
      this->text_(area, map.attribution, FontRole::SMALL, p.muted);
  }

  // .dash-mp-side: 16 px / 12 px padding (8 px at the bottom), the
  // dash-label 8 px over the rows, 4 px between rows.
  lv_obj_t *list = this->section_card_(row, map.title.empty() ? this->str_("map_title") : map.title);
  lv_obj_set_width(list, list_w);
  lv_obj_set_style_min_height(list, map_h, LV_PART_MAIN);
  lv_obj_set_style_pad_top(list, this->m_.px(16), LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(list, this->m_.px(8), LV_PART_MAIN);
  lv_obj_set_style_pad_hor(list, this->m_.px(12), LV_PART_MAIN);
  lv_obj_set_style_pad_row(list, this->m_.px(4), LV_PART_MAIN);
  lv_obj_t *heading = lv_obj_get_child(list, 0);
  this->line_box_(heading, FontRole::LABEL, 20.8f);
  // .dash-mp-side .dash-label: 8 px in.
  lv_obj_set_style_margin_left(heading, this->m_.px(8), LV_PART_MAIN);
  lv_obj_set_style_margin_bottom(heading, this->m_.px(8) - this->m_.px(4), LV_PART_MAIN);
  // .dash-mp-row .dash-mp-av: 64 px with its 4 px frame, 16 px round.
  const int face = this->m_.px(64);
  for (const auto &spot : map.spots) {
    // .dash-mp-row: at least 80 px, 8 px padding after a 6 px (unseen)
    // start border, the face 14 px from the words, centred.
    lv_obj_t *item = this->row_(list, this->m_.px(14));
    lv_obj_set_width(item, LV_PCT(100));
    lv_obj_set_style_min_height(item, this->m_.px(80), LV_PART_MAIN);
    lv_obj_set_style_pad_ver(item, this->m_.px(8), LV_PART_MAIN);
    lv_obj_set_style_pad_left(item, this->m_.px(8 + 6), LV_PART_MAIN);
    lv_obj_set_style_pad_right(item, this->m_.px(8), LV_PART_MAIN);
    const Person *person = this->board_->header.find_person(spot.person);
    lv_obj_t *frame = this->box_(item);
    lv_obj_set_size(frame, face, face);
    lv_obj_set_style_radius(frame, this->m_.px(16), LV_PART_MAIN);
    lv_obj_set_style_border_width(frame, this->m_.px(4), LV_PART_MAIN);
    // The frame in --kf: the person's colour at 3:1 on the bubble.
    const uint32_t frame_color = person != nullptr && person->color.valid
                                     ? map_frame_color(person->color.rgb, this->scheme_() != Scheme::LIGHT)
                                     : p.border;
    lv_obj_set_style_border_color(frame, lv_color_hex(frame_color), LV_PART_MAIN);
    lv_obj_t *avatar = this->avatar_(frame, spot.person, face - this->m_.px(8));
    lv_obj_center(avatar);
    // A spot over an hour old greys and fades its whole face — frame,
    // initials and photo, as one layer (`.dash-mp-av.dash-mp-stale`); the
    // words keep their contrast.
    if (spot.stale) {
      this->grey_picture_(frame, GREY_STALE_SPOT);
      lv_obj_set_style_opa_layered(frame, OPA_STALE_FACE, LV_PART_MAIN);
    }
    lv_obj_t *body = this->column_(item, 0);
    lv_obj_set_flex_grow(body, 1);
    // .dash-mp-row-name: 22 px bold, line-height 1.25.
    this->line_box_(this->text_(body, spot.name, FontRole::BODY_BOLD, p.text), FontRole::BODY_BOLD, 27.5f);
    // .dash-mp-row-place: 18 px, line-height 1.35, the icon as a word.
    const float place_line = 24.3f;
    lv_obj_t *where = this->row_(body, lv_font_get_glyph_width(this->font_(FontRole::TEXT), ' ', 0));
    lv_obj_set_flex_align(where, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *pic = this->emoji_(where, spot.icon, this->emoji_px_(FontRole::TEXT));
    // The browser's emoji glyph advances ~1.05 Roboto lines.
    lv_obj_set_style_margin_right(
        pic, std::max(0, lv_font_get_line_height(this->font_(FontRole::TEXT)) * 105 / 100 - this->emoji_px_(FontRole::TEXT)),
        LV_PART_MAIN);
    this->line_box_(this->text_(where, spot.place, FontRole::TEXT, p.text), FontRole::TEXT, place_line);
    // "5 min ago" is no longer true offline (the browser's data-rel); a
    // stale spot says "⏱ last seen 1 h ago" in bold.
    const SpotAge age = spot_age(spot, this->offline_);
    if (age.shown) {
      const FontRole font = age.bold ? FontRole::LABEL : FontRole::SMALL;
      lv_obj_t *line = this->row_(body, this->m_.px(4));
      lv_obj_set_width(line, LV_PCT(100));
      lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
      if (age.clock)
        this->emoji_(line, Emoji{"\xE2\x8F\xB1", "23f1"}, this->emoji_px_(font));
      // .dash-mp-row-age: 16 px on the board's 1.35 line.
      lv_obj_t *label = this->text_(line, age.text, font, p.muted);
      this->line_box_(label, font, 21.6f);
      lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
      // Wrapped where the browser wraps it: its text is a little wider
      // than the panel's (css_width), so it breaks a line sooner.
      lv_obj_update_layout(line);
      int room = lv_obj_get_content_width(line);
      if (age.clock)
        room -= this->emoji_px_(font) + this->m_.px(4);
      const int w = this->text_width_(age.text, font);
      const int wider = css_width(w, utf8_glyphs(age.text), lv_font_get_line_height(this->font_(font))) - w;
      if (room > 0)
        lv_obj_set_width(label, std::min(w, room - wider));
    }
  }
}

}  // namespace esphome::socialhome_display
