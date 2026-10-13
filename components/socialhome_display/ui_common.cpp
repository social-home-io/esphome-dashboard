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

const Palette &SocialHomeDisplay::palette_() const {
  if (this->mode_ == Mode::RUNNING && this->board_ != nullptr && this->board_->header.is_night)
    return this->theme_.night;
  if (this->theme_.mode == "dark")
    return this->theme_.dark;
  return this->theme_.light;
}

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

namespace {
/// Text + emoji row: keep the emoji right after a short text, and let a long
/// text wrap across the full width (its emoji then follow on the next line).
void fit_rich(lv_event_t *event) {
  lv_obj_t *row = lv_event_get_current_target_obj(event);
  lv_obj_t *label = lv_obj_get_child(row, 0);
  if (label == nullptr)
    return;
  int32_t avail = lv_obj_get_content_width(row);
  if (avail <= 0)
    return;
  int32_t extra = 0;
  for (uint32_t k = 1; k < lv_obj_get_child_count(row); k++)
    extra += lv_obj_get_width(lv_obj_get_child(row, k)) + lv_obj_get_style_pad_column(row, LV_PART_MAIN);
  lv_point_t size;
  lv_text_get_size(&size, lv_label_get_text(label), lv_obj_get_style_text_font(label, LV_PART_MAIN),
                   lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  int32_t want = size.x + extra <= avail ? LV_SIZE_CONTENT : LV_PCT(100);
  if (lv_obj_get_style_width(label, LV_PART_MAIN) != want)
    lv_obj_set_width(label, want);
}
}  // namespace

lv_obj_t *SocialHomeDisplay::rich_(lv_obj_t *parent, const std::string &text, FontRole font, uint32_t color) {
  // Text and its emoji (as pictures, after the words): the fonts have none.
  lv_obj_t *row = this->row_(parent, this->m_.px(6));
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
  lv_obj_t *label = this->text_(row, text, font, color);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
  auto codes = emoji_codes(text, 3);
  if (codes.empty()) {
    lv_obj_set_width(label, LV_PCT(100));
    return row;
  }
  int size = font == FontRole::LARGE ? this->m_.px(30) : font == FontRole::SMALL ? this->m_.px(18) : this->m_.px(26);
  for (const auto &code : codes) {
    lv_obj_t *img = this->emoji_(row, Emoji{"", code}, size);
    lv_obj_set_style_margin_bottom(img, this->m_.px(2), LV_PART_MAIN);
  }
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_add_event_cb(row, fit_rich, LV_EVENT_SIZE_CHANGED, nullptr);
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
  if (b.header.rail.shown)
    this->build_rail_(root);
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
  switch (b.kind) {
    case BoardKind::OVERVIEW:
      this->build_tabs_(main);
      this->build_alerts_(main, b.header.alerts);
      this->build_overview_(main);
      break;
    case BoardKind::MESSAGES:
      this->build_tabs_(main);
      this->build_alerts_(main, b.header.alerts);
      this->build_messages_tab_(main);
      break;
    case BoardKind::NOTES:
      this->build_tabs_(main);
      this->build_alerts_(main, b.header.alerts);
      this->build_notes_tab_(main);
      break;
    case BoardKind::MAP:
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
    if (tab.badge > 0) {
      lv_obj_t *badge = this->box_(t);
      int d = this->m_.px(24);
      lv_obj_set_size(badge, d, d);
      lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
      lv_obj_set_style_bg_color(badge, lv_color_hex(p.accent), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_t *n = this->text_(badge, std::to_string(tab.badge), FontRole::LABEL, p.accent_text);
      lv_obj_center(n);
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
  // The server's acknowledge endpoint comes with a later Social Home
  // release; until then "Got it" hides the banner on this panel only.
  self->dismissed_.insert(*key);
  self->dirty_ = true;
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
  std::string text = this->str_("offline");
  size_t at = text.find("{time}");
  if (at != std::string::npos)
    text.replace(at, 6, this->offline_since_);
  lv_obj_t *note = this->box_(parent);
  lv_obj_set_width(note, LV_PCT(100));
  lv_obj_set_style_bg_color(note, lv_color_hex(p.error_bg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(note, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(note, this->m_.px(10), LV_PART_MAIN);
  lv_obj_set_style_pad_all(note, this->m_.px(12), LV_PART_MAIN);
  this->wrap_(note, text, FontRole::BODY, p.error_text);
}

}  // namespace esphome::socialhome_display
