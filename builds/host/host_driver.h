// SPDX-License-Identifier: MPL-2.0
//
// Screenshot driver for builds/host-sdl.yaml (host build only, never on a
// panel). Polls a command file; each line is one step, run one per tick so
// LVGL can settle in between:
//
//   shot NAME          write snapshots/<node>/NAME.bmp
//   dump NAME          write NAME.txt next to the snapshots: every visible
//                      object's box (x y w h, screen px), a label's font
//                      weight (@400) and text, to measure the layout
//                      against the browser's
//   tap X Y            click whatever is at (X, Y)
//   hold X Y           press (X, Y) for 5.5 s (the settings long-press)
//   type TEXT          put TEXT in the text field on screen and press Enter
//   scroll Y           scroll the scrollable area under the centre to Y
//   wait N             skip N ticks
//
// The file is removed once read; "done" is written to <file>.done after the
// last step, so a script knows when to look. Which file: driver_path.h (one
// per panel; logged at start-up). A panel whose file another running panel
// already holds (a <file>.lock flock) drives nothing and says so.
#pragma once

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/sdl/sdl_esphome.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "driver_path.h"

namespace host_driver {

inline std::deque<std::string> &queue() {
  static std::deque<std::string> q;
  return q;
}

inline lv_obj_t *find_textarea(lv_obj_t *obj) {
  if (lv_obj_check_type(obj, &lv_textarea_class))
    return obj;
  for (uint32_t k = 0; k < lv_obj_get_child_count(obj); k++)
    if (lv_obj_t *found = find_textarea(lv_obj_get_child(obj, k)))
      return found;
  return nullptr;
}

/// The first object (depth first) that can actually scroll vertically.
inline lv_obj_t *find_scrollable(lv_obj_t *obj) {
  if (obj != lv_screen_active() && lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE) &&
      lv_obj_get_scroll_bottom(obj) + lv_obj_get_scroll_y(obj) > 0 &&
      lv_obj_get_width(obj) > lv_display_get_horizontal_resolution(nullptr) / 2)
    return obj;
  for (uint32_t k = 0; k < lv_obj_get_child_count(obj); k++)
    if (lv_obj_t *found = find_scrollable(lv_obj_get_child(obj, k)))
      return found;
  return nullptr;
}

inline lv_obj_t *&held() {
  static lv_obj_t *obj = nullptr;
  return obj;
}

inline lv_obj_t *target_at(int x, int y) {
  lv_point_t point{x, y};
  lv_obj_t *obj = lv_indev_search_obj(lv_layer_top(), &point);
  if (obj == nullptr || obj == lv_layer_top())
    obj = lv_indev_search_obj(lv_screen_active(), &point);
  while (obj != nullptr && !lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE))
    obj = lv_obj_get_parent(obj);
  return obj;
}

inline void tap(int x, int y) {
  lv_point_t point{x, y};
  lv_obj_t *obj = lv_indev_search_obj(lv_layer_top(), &point);
  if (obj == nullptr || obj == lv_layer_top())
    obj = lv_indev_search_obj(lv_screen_active(), &point);
  while (obj != nullptr && !lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE))
    obj = lv_obj_get_parent(obj);
  if (obj == nullptr) {
    ESP_LOGW("host_driver", "Nothing to tap at %d,%d", x, y);
    return;
  }
  lv_obj_send_event(obj, LV_EVENT_PRESSED, nullptr);
  lv_obj_send_event(obj, LV_EVENT_RELEASED, nullptr);
  lv_display_trigger_activity(lv_obj_get_display(obj));
  lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
}

/// The weight of a label's font for `dump` (0: unknown, not written). The
/// host config points it at the dashboard component's font roles.
inline std::function<int(const lv_font_t *)> &font_weight() {
  static std::function<int(const lv_font_t *)> lookup;
  return lookup;
}

/// One line per visible object, depth first: indent, x y w h, then for a
/// label its font's weight (`@400`, when known) and its text.
inline void dump_tree(std::ofstream &out, lv_obj_t *obj, int depth) {
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN))
    return;
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  out << std::string(depth * 2, ' ') << a.x1 << ' ' << a.y1 << ' ' << lv_area_get_width(&a) << ' '
      << lv_area_get_height(&a);
  if (lv_obj_check_type(obj, &lv_label_class)) {
    const int weight = font_weight() ? font_weight()(lv_obj_get_style_text_font(obj, LV_PART_MAIN)) : 0;
    if (weight > 0)
      out << " @" << weight;
    out << " \"" << lv_label_get_text(obj) << '"';
  }
  out << '\n';
  for (uint32_t k = 0; k < lv_obj_get_child_count(obj); k++)
    dump_tree(out, lv_obj_get_child(obj, k), depth + 1);
}

/// The command file this panel reads, or "" when another panel has it.
inline const std::string &resolved_path(const char *configured) {
  static std::string path;
  static bool resolved = false;
  if (resolved)
    return path;
  resolved = true;
  const std::string wanted = driver_path(configured, std::getenv("ESPHOME_PREFDIR"), static_cast<long>(getpid()));
  // Held for the panel's lifetime; the kernel drops it when the panel exits.
  const int fd = open((wanted + ".lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
  if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) != 0) {
    ESP_LOGE("host_driver",
             "%s is already read by another host panel: this one takes no commands. Give each panel its own "
             "(-s driver_file PATH, or its own ESPHOME_PREFDIR)",
             wanted.c_str());
    if (fd >= 0)
      close(fd);
    return path;
  }
  path = wanted;
  ESP_LOGI("host_driver", "Reading commands from %s", path.c_str());
  return path;
}

inline void tick(const char *configured, esphome::sdl::Sdl *display) {
  const std::string &file = resolved_path(configured);
  if (file.empty())
    return;
  const char *path = file.c_str();
  static int waiting = 0;
  static bool pending_done = false;
  {
    std::ifstream in(path);
    if (in) {
      std::string line;
      while (std::getline(in, line))
        if (!line.empty())
          queue().push_back(line);
      in.close();
      std::remove(path);
      pending_done = true;
    }
  }
  static uint32_t hold_until = 0;
  if (held() != nullptr && esphome::millis() >= hold_until) {
    lv_obj_send_event(held(), LV_EVENT_PRESSING, nullptr);
    lv_obj_send_event(held(), LV_EVENT_RELEASED, nullptr);
    held() = nullptr;
  }
  if (waiting > 0) {
    waiting--;
    return;
  }
  if (queue().empty()) {
    if (pending_done) {
      pending_done = false;
      std::ofstream(std::string(path) + ".done") << "done\n";
    }
    return;
  }
  std::string line = queue().front();
  queue().pop_front();
  std::istringstream cmd(line);
  std::string verb;
  cmd >> verb;
  ESP_LOGI("host_driver", "%s", line.c_str());
  if (verb == "shot") {
    std::string name;
    cmd >> name;
    lv_refr_now(nullptr);
    display->take_snapshot((name + ".bmp").c_str());
  } else if (verb == "dump") {
    std::string name;
    cmd >> name;
    lv_refr_now(nullptr);
    const char *dir = std::getenv("ESPHOME_SNAPSHOT_DIR");
    std::ofstream out((dir != nullptr && dir[0] != '\0' ? std::string(dir) + "/" : std::string()) + name + ".txt");
    dump_tree(out, lv_screen_active(), 0);
    dump_tree(out, lv_layer_top(), 0);
  } else if (verb == "tap") {
    int x = 0, y = 0;
    cmd >> x >> y;
    tap(x, y);
  } else if (verb == "hold") {
    int x = 0, y = 0;
    cmd >> x >> y;
    held() = target_at(x, y);
    if (held() != nullptr) {
      lv_obj_send_event(held(), LV_EVENT_PRESSED, nullptr);
      hold_until = esphome::millis() + 5500;
    }
  } else if (verb == "type") {
    std::string text;
    std::getline(cmd >> std::ws, text);
    if (lv_obj_t *ta = find_textarea(lv_screen_active())) {
      lv_textarea_set_text(ta, text.c_str());
      lv_obj_send_event(ta, LV_EVENT_READY, nullptr);
    }
  } else if (verb == "scroll") {
    int y = 0;
    cmd >> y;
    lv_obj_t *obj = find_scrollable(lv_screen_active());
    if (obj != nullptr)
      lv_obj_scroll_to_y(obj, y, LV_ANIM_OFF);
  } else if (verb == "wait") {
    cmd >> waiting;
  }
}

}  // namespace host_driver
