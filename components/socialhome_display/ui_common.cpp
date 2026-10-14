// SPDX-License-Identifier: MPL-2.0
//
// Widget helpers shared by every screen, and the frame around the boards:
// the people rail, the tab bar, @here banners and the offline note.
// Geometry follows socialhome/dashboard/static/dashboard.css at 1280×800.
#include <algorithm>
#include <initializer_list>
#include <vector>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "socialhome_display.h"

namespace esphome::socialhome_display {

static const char *const HOME_PATH = "/api/display/v1/home";
static const char *const HOME_EMOJI = "1f3e0";

// ---- basics ----------------------------------------------------------------

const lv_font_t *SocialHomeDisplay::font_(FontRole role) const {
  font::Font *f = this->fonts_[static_cast<size_t>(role)];
  if (f == nullptr) {
    // Fall back along the size ladder so a device can ship fewer fonts.
    switch (role) {
      case FontRole::NIGHT:
        return this->font_(FontRole::CLOCK);
      case FontRole::CLOCK:
        return this->font_(FontRole::DISPLAY);
      case FontRole::DISPLAY:
        return this->font_(FontRole::TITLE);
      case FontRole::TITLE:
        return this->font_(FontRole::LARGE);
      case FontRole::LARGE:
        return this->font_(FontRole::BODY_BOLD);
      case FontRole::BODY_BOLD:
      case FontRole::SMALL:
        return this->font_(FontRole::BODY);
      case FontRole::LABEL:
        return this->font_(FontRole::SMALL);
      default:
        return LV_FONT_DEFAULT;
    }
  }
  return f->get_lv_font();
}

Scheme SocialHomeDisplay::scheme_() const {
  // The server's flag is the one source of "night": a new board that flips
  // it re-renders the panel (render_ asks again), so the switch at the
  // window's edges needs no clock of its own. While locked or offline the
  // last board's flag holds until the next board; without a board (setup,
  // first boot) it is day. The settings sheet over the night board is not
  // the board: it follows the mode.
  const bool night = this->board_ != nullptr && this->board_->header.is_night;
  const bool night_board = this->mode_ == Mode::RUNNING && night && !this->building_overlay_;
  return scheme_for(this->theme_.mode, night, night_board);
}

const Palette &SocialHomeDisplay::palette_() const { return this->theme_.palette(this->scheme_()); }

lv_obj_t *SocialHomeDisplay::box_(lv_obj_t *parent) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  return obj;
}

lv_obj_t *SocialHomeDisplay::column_(lv_obj_t *parent, int gap) {
  lv_obj_t *obj = this->box_(parent);
  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(obj, gap, LV_PART_MAIN);
  return obj;
}

lv_obj_t *SocialHomeDisplay::row_(lv_obj_t *parent, int gap) {
  lv_obj_t *obj = this->box_(parent);
  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(obj, gap, LV_PART_MAIN);
  return obj;
}

lv_obj_t *SocialHomeDisplay::card_(lv_obj_t *parent) {
  const Palette &p = this->palette_();
  lv_obj_t *obj = this->column_(parent, this->m_.px(12));
  lv_obj_set_style_bg_color(obj, lv_color_hex(p.surface), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(obj, lv_color_hex(p.border), LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(obj, this->m_.radius(), LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, this->m_.card_pad(), LV_PART_MAIN);
  return obj;
}

lv_obj_t *SocialHomeDisplay::text_(lv_obj_t *parent, const std::string &text, FontRole font, uint32_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, strip_emoji(text).c_str());
  lv_obj_set_style_text_font(label, this->font_(font), LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  return label;
}

lv_obj_t *SocialHomeDisplay::wrap_(lv_obj_t *parent, const std::string &text, FontRole font, uint32_t color) {
  lv_obj_t *label = this->text_(parent, text, font, color);
  lv_obj_set_width(label, LV_PCT(100));
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
  return label;
}

int SocialHomeDisplay::emoji_px_(FontRole font) const {
  // A touch under the line height, so a line with an emoji isn't taller.
  return std::max(12, static_cast<int>(lv_font_get_line_height(this->font_(font))) * 9 / 10);
}

lv_obj_t *SocialHomeDisplay::rich_(lv_obj_t *parent, const std::string &text, FontRole font, uint32_t color) {
  // The fonts carry no emoji: each word is a label and each emoji a picture
  // in one wrapping row, so the emoji stay where they were typed.
  lv_obj_t *row = this->row_(parent, 0);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  auto runs = text_runs(text, true);
  bool has_emoji = std::any_of(runs.begin(), runs.end(), [](const TextRun &r) { return r.kind == TextRun::EMOJI; });
  // Without emoji (or for a long text, where a label per word would cost
  // too much memory) one wrapping label does it.
  static constexpr size_t MAX_PIECES = 80;
  if (!has_emoji || runs.size() > MAX_PIECES) {
    lv_obj_t *label = this->text_(row, text, font, color);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    if (has_emoji) {
      for (const auto &code : emoji_codes(text, 3))
        this->emoji_(row, Emoji{"", code}, this->emoji_px_(font));
    }
    return row;
  }
  const lv_font_t *f = this->font_(font);
  const int size = this->emoji_px_(font);
  const int space = std::max(2, static_cast<int>(lv_font_get_glyph_width(f, ' ', 0)));
  bool new_line = false;
  for (const auto &run : runs) {
    lv_obj_t *piece = nullptr;
    switch (run.kind) {
      case TextRun::BREAK:
        new_line = true;
        continue;
      case TextRun::TEXT:
        piece = lv_label_create(row);
        lv_label_set_text(piece, run.value.c_str());
        lv_obj_set_style_text_font(piece, f, LV_PART_MAIN);
        lv_obj_set_style_text_color(piece, lv_color_hex(color), LV_PART_MAIN);
        // A word longer than the row still wraps inside itself.
        lv_obj_set_style_max_width(piece, LV_PCT(100), LV_PART_MAIN);
        lv_label_set_long_mode(piece, LV_LABEL_LONG_MODE_WRAP);
        break;
      case TextRun::EMOJI:
        piece = this->emoji_(row, Emoji{"", run.value}, size);
        if (run.space_after)
          lv_obj_set_style_margin_right(piece, space, LV_PART_MAIN);
        break;
    }
    if (new_line) {
      lv_obj_add_flag(piece, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
      new_line = false;
    }
  }
  return row;
}

lv_obj_t *SocialHomeDisplay::inline_(lv_obj_t *parent, const std::string &text, FontRole font, uint32_t color,
                                     int max_width) {
  lv_obj_t *row = this->row_(parent, 0);
  const lv_font_t *f = this->font_(font);
  const int size = this->emoji_px_(font);
  const int space = std::max(2, static_cast<int>(lv_font_get_glyph_width(f, ' ', 0)));
  int used = 0;
  for (const auto &run : text_runs(text, false)) {
    if (run.kind == TextRun::BREAK) {
      // One line: a break is a space.
      used += space;
      if (lv_obj_get_child_count(row) > 0)
        lv_obj_set_style_margin_right(lv_obj_get_child(row, -1), space, LV_PART_MAIN);
      continue;
    }
    if (run.kind == TextRun::EMOJI) {
      if (used + size > max_width) {
        // No room for the picture: end with an ellipsis.
        if (used + space < max_width)
          this->text_(row, "\xE2\x80\xA6", font, color);
        break;
      }
      lv_obj_t *img = this->emoji_(row, Emoji{"", run.value}, size);
      used += size;
      if (run.space_after) {
        lv_obj_set_style_margin_right(img, space, LV_PART_MAIN);
        used += space;
      }
      continue;
    }
    lv_point_t extent;
    lv_text_get_size(&extent, run.value.c_str(), f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, run.value.c_str());
    lv_obj_set_style_text_font(label, f, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    if (used + extent.x > max_width) {
      // The rest is cut with "…".
      lv_obj_set_width(label, std::max(0, max_width - used));
      lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
      break;
    }
    used += extent.x;
  }
  return row;
}

lv_obj_t *SocialHomeDisplay::caps_(lv_obj_t *parent, const std::string &text, uint32_t color) {
  lv_obj_t *label = this->text_(parent, upper(text), FontRole::LABEL, color);
  lv_obj_set_style_text_letter_space(label, std::max(1, this->m_.px(1.5f)), LV_PART_MAIN);
  return label;
}

lv_obj_t *SocialHomeDisplay::emoji_(lv_obj_t *parent, const Emoji &emoji, int size) {
  if (!emoji.code.empty()) {
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_set_size(img, size, size);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    std::string path = "/api/display/v1/emoji/" + emoji.code + "?size=" + std::to_string(pick_emoji_size(size));
    this->images_.bind(img, path, size, false);
    return img;
  }
  // No picture (e.g. a zone without an emoji shows its first letter).
  lv_obj_t *holder = this->box_(parent);
  lv_obj_set_size(holder, size, size);
  lv_obj_t *label = this->text_(holder, emoji.text, size >= this->m_.px(40) ? FontRole::TITLE : FontRole::BODY_BOLD,
                                this->palette_().muted);
  lv_obj_center(label);
  return holder;
}

lv_obj_t *SocialHomeDisplay::avatar_of_(lv_obj_t *parent, const std::string &initials, const std::string &avatar,
                                        Color color, Color ink, int size) {
  const Palette &p = this->palette_();
  lv_obj_t *circle = this->box_(parent);
  lv_obj_set_size(circle, size, size);
  lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(circle, lv_color_hex(color.or_else(p.border)), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_add_flag(circle, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  FontRole font = size >= this->m_.px(80) ? FontRole::DISPLAY
                  : size >= this->m_.px(56) ? FontRole::TITLE
                  : size >= this->m_.px(36) ? FontRole::BODY_BOLD
                                            : FontRole::LABEL;
  lv_obj_t *label = this->text_(circle, initials, font, ink.or_else(p.text));
  lv_obj_center(label);
  if (!avatar.empty()) {
    lv_obj_t *img = lv_image_create(circle);
    lv_obj_set_size(img, size, size);
    lv_obj_center(img);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    std::string path = avatar + (avatar.find('?') == std::string::npos ? "?" : "&") +
                       "size=" + std::to_string(pick_avatar_size(size));
    this->images_.bind(img, path, size, true);
  }
  return circle;
}

lv_obj_t *SocialHomeDisplay::avatar_(lv_obj_t *parent, const std::string &person_id, int size) {
  const Person *person = this->board_ != nullptr ? this->board_->header.find_person(person_id) : nullptr;
  if (person == nullptr) {
    if (this->board_ != nullptr && this->board_->person.id == person_id) {
      const PersonHead &h = this->board_->person;
      return this->avatar_of_(parent, h.initials, h.avatar, h.color, h.ink, size);
    }
    return this->avatar_of_(parent, "?", "", Color{}, Color{}, size);
  }
  return this->avatar_of_(parent, person->initials, person->avatar, person->color, person->ink, size);
}

lv_obj_t *SocialHomeDisplay::pill_(lv_obj_t *parent, const std::string &text, uint32_t bg, uint32_t fg,
                                   FontRole font) {
  lv_obj_t *pill = this->box_(parent);
  lv_obj_set_style_bg_color(pill, lv_color_hex(bg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(pill, this->m_.px(6), LV_PART_MAIN);
  lv_obj_set_style_pad_hor(pill, this->m_.px(8), LV_PART_MAIN);
  lv_obj_set_style_pad_ver(pill, this->m_.px(2), LV_PART_MAIN);
  this->text_(pill, text, font, fg);
  return pill;
}

lv_obj_t *SocialHomeDisplay::button_(lv_obj_t *parent, const std::string &text, bool primary, lv_event_cb_t cb,
                                     void *user_data) {
  const Palette &p = this->palette_();
  lv_obj_t *button = this->row_(parent, this->m_.px(10));
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_min_height(button, this->m_.px(56), LV_PART_MAIN);
  lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(button, this->m_.px(24), LV_PART_MAIN);
  lv_obj_set_style_pad_ver(button, this->m_.px(10), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  if (primary) {
    lv_obj_set_style_bg_color(button, lv_color_hex(p.accent), LV_PART_MAIN);
  } else {
    lv_obj_set_style_bg_color(button, lv_color_hex(p.surface), LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(p.border), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, std::max(1, this->m_.px(2)), LV_PART_MAIN);
  }
  lv_obj_set_style_bg_color(button, lv_color_hex(primary ? p.text : p.bg), LV_STATE_PRESSED);
  this->text_(button, text, FontRole::BODY_BOLD, primary ? p.accent_text : p.text);
  if (cb != nullptr)
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
  return button;
}

namespace {
void free_points(lv_event_t *event) {
  delete static_cast<std::vector<lv_point_precise_t> *>(lv_event_get_user_data(event));
}
}  // namespace

lv_obj_t *SocialHomeDisplay::poly_(lv_obj_t *parent, std::initializer_list<lv_point_precise_t> points, int width,
                                   uint32_t color) {
  auto *pts = new std::vector<lv_point_precise_t>(points);
  lv_obj_t *line = lv_line_create(parent);
  lv_line_set_points(line, pts->data(), static_cast<uint32_t>(pts->size()));
  lv_obj_set_style_line_width(line, width, LV_PART_MAIN);
  lv_obj_set_style_line_color(line, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_line_rounded(line, true, LV_PART_MAIN);
  lv_obj_add_event_cb(line, free_points, LV_EVENT_DELETE, pts);
  return line;
}

lv_obj_t *SocialHomeDisplay::check_mark_(lv_obj_t *parent, int size, uint32_t color, int width) {
  lv_value_precise_t s = size;
  lv_obj_t *line = this->poly_(parent, {{s * 12 / 100, s * 52 / 100}, {s * 40 / 100, s * 80 / 100}, {s * 90 / 100, s * 22 / 100}},
                               width, color);
  lv_obj_set_size(line, size, size);
  return line;
}

lv_obj_t *SocialHomeDisplay::clock_label_(lv_obj_t *parent, FontRole font, uint32_t color) {
  lv_obj_t *label = this->text_(parent, this->clock_text_(), font, color);
  this->clock_labels_.push_back(label);
  this->long_press_settings_(label);
  return label;
}

void SocialHomeDisplay::long_press_settings_(lv_obj_t *obj) {
  lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  // A short tap still reaches what's underneath (e.g. waking the night screen).
  lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(obj, SocialHomeDisplay::on_settings_press_, LV_EVENT_ALL, this);
}

void SocialHomeDisplay::on_settings_press_(lv_event_t *event) {
  auto *self = static_cast<SocialHomeDisplay *>(lv_event_get_user_data(event));
  switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
      self->press_started_ = millis();
      self->press_fired_ = false;
      break;
    case LV_EVENT_PRESSING:
      // Held for 5 s: the settings screen (deliberately slow, so kids don't).
      if (!self->press_fired_ && millis() - self->press_started_ >= 5000) {
        self->press_fired_ = true;
        self->open_settings_();
      }
      break;
    default:
      break;
  }
}

void SocialHomeDisplay::people_row_(lv_obj_t *parent, const std::vector<std::string> &people, int size) {
  lv_obj_t *row = this->row_(parent, this->m_.px(6));
  for (const auto &id : people)
    this->avatar_(row, id, size);
}

lv_obj_t *SocialHomeDisplay::section_card_(lv_obj_t *parent, const std::string &label) {
  lv_obj_t *card = this->card_(parent);
  if (!label.empty())
    this->caps_(card, label, this->palette_().muted);
  return card;
}

lv_obj_t *SocialHomeDisplay::agenda_row_(lv_obj_t *parent, const AgendaRow &row, bool show_people) {
  const Palette &p = this->palette_();
  lv_obj_t *item = this->row_(parent, 0);
  lv_obj_set_width(item, LV_PCT(100));
  lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_bg_color(item, lv_color_hex(row.now ? p.surface : p.bg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(item, this->m_.px(8), LV_PART_MAIN);
  if (row.now) {
    lv_obj_set_style_outline_color(item, lv_color_hex(p.accent), LV_PART_MAIN);
    lv_obj_set_style_outline_width(item, this->m_.px(2), LV_PART_MAIN);
  }
  // The colour strip on the start side.
  lv_obj_set_style_border_side(item, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
  lv_obj_set_style_border_width(item, this->m_.px(6), LV_PART_MAIN);
  lv_obj_set_style_border_color(item, lv_color_hex(row.color.or_else(p.border)), LV_PART_MAIN);
  lv_obj_t *body = this->column_(item, this->m_.px(4));
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_style_pad_hor(body, this->m_.px(12), LV_PART_MAIN);
  lv_obj_set_style_pad_ver(body, this->m_.px(8), LV_PART_MAIN);
  this->text_(body, row.time, FontRole::BODY, p.muted);
  lv_obj_t *line = this->row_(body, this->m_.px(8));
  lv_obj_set_width(line, LV_PCT(100));
  if (!row.icon.empty())
    this->emoji_(line, row.icon, this->m_.emoji_sm());
  lv_obj_t *title = this->text_(line, row.title, FontRole::BODY, row.is_break ? p.muted : p.text);
  if (row.status == Status::CANCELLED)
    lv_obj_set_style_text_decor(title, LV_TEXT_DECOR_STRIKETHROUGH, LV_PART_MAIN);
  if (row.status != Status::NONE && !row.status_label.empty()) {
    uint32_t fg = row.status == Status::CANCELLED ? p.error_text : p.text;
    lv_obj_t *pill = this->pill_(line, upper(row.status_label), p.surface, fg, FontRole::LABEL);
    lv_obj_set_style_border_color(pill, lv_color_hex(fg), LV_PART_MAIN);
    lv_obj_set_style_border_width(pill, 1, LV_PART_MAIN);
  }
  if (!row.meta.empty())
    this->text_(body, row.meta, FontRole::SMALL, p.muted);
  if (show_people && !row.people.empty())
    this->people_row_(body, row.people, this->m_.px(28));
  if (row.past || row.status == Status::CANCELLED)
    lv_obj_set_style_opa(item, LV_OPA_60, LV_PART_MAIN);
  return item;
}

lv_obj_t *SocialHomeDisplay::chore_row_(lv_obj_t *parent, const Chore &chore, bool show_people) {
  const Palette &p = this->palette_();
  lv_obj_t *item = this->row_(parent, this->m_.px(12));
  lv_obj_set_width(item, LV_PCT(100));
  int mark = this->m_.px(22);
  if (chore.state == ChoreState::OVERDUE) {
    this->emoji_(item, Emoji{"\xE2\x9A\xA0", "26a0"}, mark);
  } else {
    lv_obj_t *box = this->box_(item);
    lv_obj_set_size(box, mark, mark);
    if (chore.state == ChoreState::DONE) {
      this->check_mark_(box, mark, p.text, std::max(2, this->m_.px(3)));
    } else {
      lv_obj_set_style_border_color(box, lv_color_hex(p.text), LV_PART_MAIN);
      lv_obj_set_style_border_width(box, std::max(1, this->m_.px(2)), LV_PART_MAIN);
      lv_obj_set_style_radius(box, this->m_.px(3), LV_PART_MAIN);
    }
  }
  lv_obj_t *body = this->column_(item, this->m_.px(2));
  lv_obj_set_flex_grow(body, 1);
  lv_obj_t *title = this->wrap_(body, chore.title, FontRole::BODY, chore.state == ChoreState::DONE ? p.muted : p.text);
  if (chore.state == ChoreState::DONE)
    lv_obj_set_style_text_decor(title, LV_TEXT_DECOR_STRIKETHROUGH, LV_PART_MAIN);
  if (!chore.late.empty())
    this->text_(body, chore.late, FontRole::SMALL, p.error_text);
  else if (!chore.due.empty())
    this->text_(body, chore.due, FontRole::SMALL, p.muted);
  if (show_people && !chore.people.empty())
    this->people_row_(item, chore.people, this->m_.px(32));
  return item;
}

lv_obj_t *SocialHomeDisplay::message_row_(lv_obj_t *parent, const Message &message, bool show_time, FontRole font) {
  const Palette &p = this->palette_();
  lv_obj_t *item = this->row_(parent, this->m_.px(12));
  lv_obj_set_width(item, LV_PCT(100));
  lv_obj_set_style_pad_ver(item, this->m_.px(6), LV_PART_MAIN);
  lv_obj_set_style_pad_left(item, this->m_.px(12), LV_PART_MAIN);
  if (message.is_new) {
    lv_obj_set_style_border_side(item, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
    lv_obj_set_style_border_width(item, this->m_.px(5), LV_PART_MAIN);
    lv_obj_set_style_border_color(item, lv_color_hex(p.accent), LV_PART_MAIN);
  }
  this->avatar_(item, message.person, this->m_.avatar_sm());
  lv_obj_t *line = this->row_(item, this->m_.px(8));
  lv_obj_set_flex_grow(line, 1);
  if (!message.icon.empty())
    this->emoji_(line, message.icon, this->m_.emoji_sm());
  lv_obj_t *text = this->rich_(line, message.text, font, p.text);
  lv_obj_set_width(text, 0);
  lv_obj_set_flex_grow(text, 1);
  lv_obj_t *meta = this->row_(item, this->m_.px(8));
  if (message.is_new)
    this->pill_(meta, upper(this->str_("new")), p.accent, p.accent_text, FontRole::LABEL);
  if (show_time)
    this->text_(meta, message.when, FontRole::SMALL, p.muted);
  return item;
}

// ---- frame -------------------------------------------------------------------

void SocialHomeDisplay::on_navigate_(lv_event_t *event) {
  auto *path = static_cast<std::string *>(lv_event_get_user_data(event));
  lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
  auto *self = static_cast<SocialHomeDisplay *>(lv_obj_get_user_data(target));
  if (self != nullptr && path != nullptr) {
    std::string copy = *path;  // the rebuild frees the payload
    self->night_peek_ = self->night_peek_ && copy == HOME_PATH;
    self->navigate_(copy);
  }
}

void SocialHomeDisplay::build_frame_(lv_obj_t *screen) {
  const Board &b = *this->board_;
  lv_obj_t *root = this->row_(screen, 0);
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  if (b.header.rail.shown) {
    this->build_rail_(root);
  } else if (this->can_lock_()) {
    // One person, no rail: the lock button sits in the corner.
    lv_obj_t *lock = this->build_lock_button_(screen);
    lv_obj_add_flag(lock, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(lock, LV_ALIGN_BOTTOM_RIGHT, -this->m_.px(16), -this->m_.px(16));
  }
  lv_obj_t *main = this->column_(root, this->m_.px(18));
  lv_obj_set_flex_grow(main, 1);
  lv_obj_set_height(main, LV_PCT(100));
  lv_obj_add_flag(main, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(main, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(main, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_pad_hor(main, this->m_.pad(), LV_PART_MAIN);
  lv_obj_set_style_pad_top(main, this->m_.px(16), LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(main, this->m_.pad(), LV_PART_MAIN);
  this->main_scroll_ = main;
  if (this->offline_)
    this->build_offline_(main);
  // At night the tabs stay hidden, woken or not (the browser's
  // `.dash-night .dash-tabs`): the server bounces every tab but Overview.
  const bool tabs = !b.header.is_night;
  switch (b.kind) {
    case BoardKind::OVERVIEW:
      if (tabs)
        this->build_tabs_(main);
      this->build_alerts_(main, b.header.alerts);
      this->build_overview_(main);
      break;
    case BoardKind::MESSAGES:
      if (tabs)
        this->build_tabs_(main);
      this->build_alerts_(main, b.header.alerts);
      this->build_messages_tab_(main);
      break;
    case BoardKind::NOTES:
      if (tabs)
        this->build_tabs_(main);
      this->build_alerts_(main, b.header.alerts);
      this->build_notes_tab_(main);
      break;
    case BoardKind::MAP:
      if (tabs)
        this->build_tabs_(main);
      this->build_alerts_(main, b.header.alerts);
      this->build_map_tab_(main);
      break;
    case BoardKind::PERSON:
      this->build_person_(main);
      break;
    default:
      break;
  }
}

void SocialHomeDisplay::build_rail_(lv_obj_t *parent) {
  const Palette &p = this->palette_();
  const Header &h = this->board_->header;
  lv_obj_t *rail = this->column_(parent, this->m_.px(10));
  lv_obj_set_size(rail, this->m_.rail_width(), LV_PCT(100));
  lv_obj_set_flex_align(rail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_color(rail, lv_color_hex(p.surface), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_side(rail, LV_BORDER_SIDE_RIGHT, LV_PART_MAIN);
  lv_obj_set_style_border_width(rail, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(rail, lv_color_hex(p.border), LV_PART_MAIN);
  lv_obj_set_style_pad_ver(rail, this->m_.px(16), LV_PART_MAIN);
  lv_obj_add_flag(rail, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(rail, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(rail, LV_SCROLLBAR_MODE_OFF);
  const int size = h.rail.many ? this->m_.px(56) : this->m_.rail_avatar();
  const bool home_current = this->board_->kind != BoardKind::PERSON;

  auto item = [&](bool current, const std::string &path) {
    lv_obj_t *it = this->column_(rail, this->m_.px(6));
    lv_obj_set_width(it, this->m_.px(88));
    lv_obj_set_flex_align(it, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(it, this->m_.px(8), LV_PART_MAIN);
    lv_obj_set_style_radius(it, this->m_.px(14), LV_PART_MAIN);
    if (current) {
      lv_obj_set_style_bg_color(it, lv_color_hex(p.bg), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(it, LV_OPA_COVER, LV_PART_MAIN);
    }
    lv_obj_add_flag(it, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(it, this);
    lv_obj_add_event_cb(it, SocialHomeDisplay::on_navigate_, LV_EVENT_CLICKED, this->payload_(path));
    return it;
  };
  auto ring = [&](lv_obj_t *avatar) {
    lv_obj_set_style_outline_color(avatar, lv_color_hex(p.accent), LV_PART_MAIN);
    lv_obj_set_style_outline_width(avatar, this->m_.px(4), LV_PART_MAIN);
    lv_obj_set_style_outline_pad(avatar, this->m_.px(3), LV_PART_MAIN);
  };

  lv_obj_t *home = item(home_current, HOME_PATH);
  lv_obj_t *house = this->box_(home);
  lv_obj_set_size(house, size, size);
  lv_obj_set_style_radius(house, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(house, lv_color_hex(p.bg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(house, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(house, lv_color_hex(p.border), LV_PART_MAIN);
  lv_obj_set_style_border_width(house, 1, LV_PART_MAIN);
  lv_obj_t *icon = this->emoji_(house, Emoji{"\xF0\x9F\x8F\xA0", HOME_EMOJI}, size * 5 / 9);
  lv_obj_center(icon);
  if (home_current)
    ring(house);
  this->text_(home, this->str_("nav_home"), home_current ? FontRole::LABEL : FontRole::SMALL, p.text);

  for (const auto &person : h.people) {
    bool current = h.rail.current == person.id;
    lv_obj_t *it = item(current, "/api/display/v1/person/" + person.id);
    lv_obj_t *avatar = this->avatar_of_(it, person.initials, person.avatar, person.color, person.ink, size);
    if (current)
      ring(avatar);
    if (person.badge > 0) {
      lv_obj_t *dot = this->box_(avatar);
      int d = this->m_.px(20);
      lv_obj_set_size(dot, d, d);
      lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
      lv_obj_set_style_bg_color(dot, lv_color_hex(p.accent), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_color(dot, lv_color_hex(p.surface), LV_PART_MAIN);
      lv_obj_set_style_border_width(dot, this->m_.px(3), LV_PART_MAIN);
      lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, this->m_.px(4), -this->m_.px(4));
    }
    lv_obj_t *name = this->text_(it, person.name, current ? FontRole::LABEL : FontRole::SMALL, p.text);
    lv_obj_set_width(name, LV_PCT(100));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
  }
  if (this->can_lock_()) {
    // At the bottom of the rail (pushed down while the people fit).
    lv_obj_t *spacer = this->box_(rail);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_t *lock = this->build_lock_button_(rail);
    lv_obj_set_style_margin_top(lock, this->m_.px(8), LV_PART_MAIN);
  }
}

void SocialHomeDisplay::build_tabs_(lv_obj_t *parent) {
  const Palette &p = this->palette_();
  const Header &h = this->board_->header;
  lv_obj_t *bar = this->row_(parent, 0);
  lv_obj_set_width(bar, LV_PCT(100));
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(bar, lv_color_hex(p.border), LV_PART_MAIN);
  for (const auto &tab : h.tabs) {
    lv_obj_t *t = this->row_(bar, this->m_.px(8));
    lv_obj_set_style_pad_hor(t, this->m_.px(16), LV_PART_MAIN);
    lv_obj_set_style_pad_top(t, this->m_.px(10), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(t, this->m_.px(12), LV_PART_MAIN);
    lv_obj_set_style_min_height(t, this->m_.tap_min(), LV_PART_MAIN);
    // Every tab carries the underline (transparent unless current) so the
    // labels share a baseline.
    lv_obj_set_style_border_side(t, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_width(t, this->m_.px(4), LV_PART_MAIN);
    lv_obj_set_style_border_color(t, lv_color_hex(p.accent), LV_PART_MAIN);
    lv_obj_set_style_border_opa(t, tab.current ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    this->text_(t, tab.label, FontRole::BODY_BOLD, tab.current ? p.text : p.muted);
    // The browser's .dash-tab-badge: the count of new messages (< 15 min,
    // never "unread") as an accent pill that grows with the number — not on
    // the tab being shown.
    if (tab.badge > 0 && !tab.current) {
      lv_obj_t *badge = this->row_(t, 0);
      lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_height(badge, this->m_.px(24));
      lv_obj_set_style_min_width(badge, this->m_.px(24), LV_PART_MAIN);
      lv_obj_set_style_pad_hor(badge, this->m_.px(6), LV_PART_MAIN);
      lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
      lv_obj_set_style_bg_color(badge, lv_color_hex(p.accent), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
      this->text_(badge, std::to_string(tab.badge), FontRole::LABEL, p.accent_text);
    }
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(t, this);
    lv_obj_add_event_cb(t, SocialHomeDisplay::on_navigate_, LV_EVENT_CLICKED, this->payload_(tab.path));
  }
  if (this->board_->kind != BoardKind::OVERVIEW) {
    lv_obj_t *spacer = this->box_(bar);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_t *clock = this->clock_label_(bar, FontRole::TITLE, p.text);
    lv_obj_set_style_pad_bottom(clock, this->m_.px(6), LV_PART_MAIN);
  }
}

void SocialHomeDisplay::on_ack_(lv_event_t *event) {
  auto *key = static_cast<std::string *>(lv_event_get_user_data(event));
  lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
  auto *self = static_cast<SocialHomeDisplay *>(lv_obj_get_user_data(target));
  if (self == nullptr || key == nullptr)
    return;
  std::string copy = *key;  // the rebuild frees the payload
  self->request_ack_(copy);
}

bool SocialHomeDisplay::can_lock_() const {
  return this->mode_ == Mode::RUNNING && this->board_ != nullptr && this->board_->header.settings.pin_set &&
         !this->lock_refused_;
}

void SocialHomeDisplay::on_lock_(lv_event_t *event) {
  auto *self = static_cast<SocialHomeDisplay *>(lv_event_get_user_data(event));
  if (self != nullptr)
    self->request_lock_();
}

lv_obj_t *SocialHomeDisplay::build_lock_button_(lv_obj_t *parent) {
  // As on the tablet: a round 🔒 button, only while the household has a PIN.
  const Palette &p = this->palette_();
  lv_obj_t *button = this->box_(parent);
  const int d = this->m_.px(64);
  lv_obj_set_size(button, d, d);
  lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(p.bg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(p.border), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(button, lv_color_hex(p.border), LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(button, this->m_.px(8));
  lv_obj_add_event_cb(button, SocialHomeDisplay::on_lock_, LV_EVENT_CLICKED, this);
  lv_obj_t *icon = this->emoji_(button, Emoji{"\xF0\x9F\x94\x92", "1f512"}, this->m_.px(30));
  lv_obj_center(icon);
  return button;
}

void SocialHomeDisplay::build_alerts_(lv_obj_t *parent, const std::vector<Mention> &alerts) {
  const Palette &p = this->palette_();
  for (const auto &alert : alerts) {
    if (this->dismissed_.count(alert.key))
      continue;
    // Accent frame: a 2 px border with an 8 px bar on the start side.
    lv_obj_t *frame = this->box_(parent);
    lv_obj_set_width(frame, LV_PCT(100));
    lv_obj_set_style_bg_color(frame, lv_color_hex(p.accent), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(frame, this->m_.px(14), LV_PART_MAIN);
    lv_obj_set_style_pad_all(frame, std::max(1, this->m_.px(2)), LV_PART_MAIN);
    lv_obj_set_style_pad_left(frame, this->m_.px(8), LV_PART_MAIN);
    lv_obj_t *inner = this->row_(frame, this->m_.px(16));
    lv_obj_set_width(inner, LV_PCT(100));
    lv_obj_set_style_bg_color(inner, lv_color_hex(p.surface), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(inner, this->m_.px(12), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(inner, this->m_.px(14), LV_PART_MAIN);
    lv_obj_set_style_pad_ver(inner, this->m_.px(12), LV_PART_MAIN);
    this->avatar_(inner, alert.person, this->m_.avatar_sm());
    lv_obj_t *body = this->column_(inner, this->m_.px(4));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_t *head = this->row_(body, this->m_.px(10));
    this->caps_(head, alert.label, p.accent);
    this->text_(head, alert.name, FontRole::LABEL, p.text);
    this->text_(head, alert.when, FontRole::SMALL, p.muted);
    this->rich_(body, alert.text, FontRole::BODY, p.text);
    if (alert.ack.has_value()) {
      lv_obj_t *button = this->button_(inner, this->str_("got_it"), true, SocialHomeDisplay::on_ack_,
                                       this->payload_(alert.key));
      lv_obj_set_user_data(button, this);
      lv_obj_t *check = this->check_mark_(button, this->m_.px(20), p.accent_text, std::max(2, this->m_.px(3)));
      lv_obj_move_to_index(check, 0);
    }
  }
}

void SocialHomeDisplay::build_offline_(lv_obj_t *parent) {
  const Palette &p = this->palette_();
  const std::string text = fill_placeholder(this->str_("offline"), "time", this->offline_since_);
  lv_obj_t *note = this->box_(parent);
  lv_obj_set_width(note, LV_PCT(100));
  lv_obj_set_style_bg_color(note, lv_color_hex(p.error_bg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(note, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(note, this->m_.px(10), LV_PART_MAIN);
  lv_obj_set_style_pad_all(note, this->m_.px(12), LV_PART_MAIN);
  this->wrap_(note, text, FontRole::BODY, p.error_text);
}

}  // namespace esphome::socialhome_display
