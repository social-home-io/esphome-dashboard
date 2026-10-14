// SPDX-License-Identifier: MPL-2.0
//
// Avatars and emoji from the server, decoded once into ARGB8888 (PSRAM) and
// kept in a small LRU cache keyed by path + size. Paths are immutable per
// picture (avatars carry ?v=<hash>, emoji are a fixed bundle), so the key is
// the cache tag: a new picture is a new path.
#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "net.h"

namespace esphome::socialhome_display {

class ImageCache {
 public:
  /// Builds an authenticated request for a server path.
  using RequestFactory = std::function<HttpRequest(const std::string &path)>;

  void set_factory(RequestFactory factory) { this->factory_ = std::move(factory); }
  void set_budget(size_t bytes) { this->budget_ = bytes; }

  /// Show the picture at `path` (a server path including its ?size=) in
  /// `img`, scaled to size×size, now if cached, else once it has loaded.
  /// `img` stays hidden until then.
  void bind(lv_obj_t *img, const std::string &path, int size, bool round);
  /// Show `img` (already bound) with `amount` of its colour drained — the
  /// browser's `filter: grayscale(…)`. The grey copy is made from the
  /// cached colour picture (so it works offline); one not loaded yet is
  /// fetched and greyed as it arrives.
  void grey(lv_obj_t *img, uint8_t amount);
  /// Feed finished downloads; true when something new is on screen.
  bool loop();
  /// Forget everything (server changed / unpaired).
  void clear();
  /// Delay new downloads (429).
  void pause_until(uint32_t millis_at) { this->paused_until_ = millis_at; }

 protected:
  struct Entry {
    std::string path;
    int size{0};
    bool round{false};
    uint8_t grey{0};
    enum State : uint8_t { QUEUED, LOADING, READY, FAILED } state{QUEUED};
    uint32_t failed_at{0};
    std::shared_ptr<Pixels> pixels;
    lv_image_dsc_t dsc{};
    std::vector<lv_obj_t *> objects;
  };

  static std::string key_(const std::string &path, int size, bool round, uint8_t grey = 0);
  Entry &entry_(const std::string &key, const std::string &path, int size, bool round, uint8_t grey);
  void ready_(Entry &entry, std::shared_ptr<Pixels> pixels);
  static void on_delete_(lv_event_t *event);
  void show_(Entry &entry, lv_obj_t *img);
  void unbind_(lv_obj_t *img);
  void start_downloads_();
  void evict_();

  RequestFactory factory_;
  HttpWorker worker_{"sh_images", 24};
  std::map<std::string, Entry> entries_;
  std::map<uint32_t, std::string> in_flight_;
  std::list<std::string> lru_;  ///< front = most recently used
  size_t bytes_{0};
  size_t budget_{3 * 1024 * 1024};
  uint32_t next_id_{1};
  uint32_t paused_until_{0};
};

}  // namespace esphome::socialhome_display
