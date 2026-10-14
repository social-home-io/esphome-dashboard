// SPDX-License-Identifier: MPL-2.0
#include "images.h"

#include <algorithm>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "png.h"
#include "util.h"

// Not re-exported by lvgl.h in 9.5.
#include "src/misc/cache/instance/lv_image_cache.h"

namespace esphome::socialhome_display {

static const char *const TAG = "socialhome_display.images";
/// A picture that failed is asked for again after this long.
static constexpr uint32_t RETRY_MS = 5 * 60 * 1000;
/// At most this many downloads queued on the worker at once.
static constexpr size_t MAX_IN_FLIGHT = 6;

std::string ImageCache::key_(const std::string &path, int size, bool round, uint8_t grey) {
  return path + "@" + std::to_string(size) + (round ? "o" : "") + (grey != 0 ? "g" + std::to_string(grey) : "");
}

ImageCache::Entry &ImageCache::entry_(const std::string &key, const std::string &path, int size, bool round,
                                      uint8_t grey) {
  auto it = this->entries_.find(key);
  if (it == this->entries_.end()) {
    Entry entry;
    entry.path = path;
    entry.size = size;
    entry.round = round;
    entry.grey = grey;
    it = this->entries_.emplace(key, std::move(entry)).first;
  }
  this->lru_.remove(key);
  this->lru_.push_front(key);
  return it->second;
}

void ImageCache::ready_(Entry &entry, std::shared_ptr<Pixels> pixels) {
  entry.pixels = std::move(pixels);
  entry.dsc = lv_image_dsc_t{};
  entry.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  entry.dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
  entry.dsc.header.w = entry.pixels->width;
  entry.dsc.header.h = entry.pixels->height;
  entry.dsc.header.stride = entry.pixels->width * 4;
  entry.dsc.data_size = entry.pixels->size;
  entry.dsc.data = entry.pixels->data;
  entry.state = Entry::READY;
  this->bytes_ += entry.pixels->size;
}

void ImageCache::bind(lv_obj_t *img, const std::string &path, int size, bool round) {
  Entry &entry = this->entry_(key_(path, size, round), path, size, round, 0);
  entry.objects.push_back(img);
  lv_obj_add_event_cb(img, ImageCache::on_delete_, LV_EVENT_DELETE, this);
  if (entry.state == Entry::READY) {
    this->show_(entry, img);
  } else {
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    if (entry.state == Entry::FAILED && millis() - entry.failed_at > RETRY_MS)
      entry.state = Entry::QUEUED;
  }
  this->start_downloads_();
}

void ImageCache::grey(lv_obj_t *img, uint8_t amount) {
  Entry *colour = nullptr;
  for (auto &kv : this->entries_) {
    auto &objs = kv.second.objects;
    if (kv.second.grey == 0 && std::find(objs.begin(), objs.end(), img) != objs.end()) {
      colour = &kv.second;
      break;
    }
  }
  if (colour == nullptr || amount == 0)
    return;
  // std::map: adding the grey entry leaves `colour` valid.
  Entry &entry = this->entry_(key_(colour->path, colour->size, colour->round, amount), colour->path, colour->size,
                              colour->round, amount);
  auto &objs = colour->objects;
  objs.erase(std::remove(objs.begin(), objs.end(), img), objs.end());
  entry.objects.push_back(img);
  if (entry.state != Entry::READY && colour->state == Entry::READY) {
    auto pixels = grey_copy(*colour->pixels, amount);
    if (pixels != nullptr)
      this->ready_(entry, std::move(pixels));
  }
  if (entry.state == Entry::READY) {
    this->show_(entry, img);
  } else {
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    if (entry.state == Entry::FAILED && millis() - entry.failed_at > RETRY_MS)
      entry.state = Entry::QUEUED;
  }
  this->evict_();
  this->start_downloads_();
}

void ImageCache::on_delete_(lv_event_t *event) {
  auto *self = static_cast<ImageCache *>(lv_event_get_user_data(event));
  self->unbind_(static_cast<lv_obj_t *>(lv_event_get_target(event)));
}

void ImageCache::unbind_(lv_obj_t *img) {
  for (auto &kv : this->entries_) {
    auto &objs = kv.second.objects;
    objs.erase(std::remove(objs.begin(), objs.end(), img), objs.end());
  }
}

void ImageCache::show_(Entry &entry, lv_obj_t *img) {
  lv_image_set_src(img, &entry.dsc);
  lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
}

void ImageCache::start_downloads_() {
  if (!this->factory_ || millis() < this->paused_until_)
    return;
  for (auto &kv : this->entries_) {
    if (this->in_flight_.size() >= MAX_IN_FLIGHT)
      return;
    Entry &entry = kv.second;
    if (entry.state != Entry::QUEUED || entry.objects.empty())
      continue;
    HttpRequest request = this->factory_(entry.path);
    request.id = this->next_id_++;
    request.decode_size = entry.size;
    request.round = entry.round;
    if (!this->worker_.submit(std::move(request)))
      return;
    this->in_flight_[this->next_id_ - 1] = kv.first;
    entry.state = Entry::LOADING;
  }
}

bool ImageCache::loop() {
  bool changed = false;
  HttpResponse response;
  while (this->worker_.poll(response)) {
    auto flight = this->in_flight_.find(response.id);
    if (flight == this->in_flight_.end())
      continue;
    std::string key = flight->second;
    this->in_flight_.erase(flight);
    auto it = this->entries_.find(key);
    if (it == this->entries_.end())
      continue;
    Entry &entry = it->second;
    if (response.status == 429 || response.status == 423 || response.status == 0) {
      // Budget, privacy lock or no network: not this picture's fault.
      uint32_t wait = response.status == 429 ? parse_retry_after(response.retry_after, 30) : 30;
      this->pause_until(millis() + wait * 1000);
      entry.state = Entry::QUEUED;
      continue;
    }
    if (response.status != 200 || response.pixels == nullptr) {
      ESP_LOGD(TAG, "No picture for %s (%d)", entry.path.c_str(), response.status);
      entry.state = Entry::FAILED;
      entry.failed_at = millis();
      continue;
    }
    // A grey variant fetched on its own (its colour picture wasn't cached):
    // this download is the entry's alone, so it is greyed in place.
    if (entry.grey != 0 && response.pixels->format == Pixels::ARGB8888)
      grey_argb8888(response.pixels->data, response.pixels->size / 4, entry.grey);
    this->ready_(entry, response.pixels);
    for (lv_obj_t *img : entry.objects)
      this->show_(entry, img);
    changed = changed || !entry.objects.empty();
  }
  this->evict_();
  this->start_downloads_();
  return changed;
}

void ImageCache::evict_() {
  // Oldest first; never a picture something on screen still shows.
  for (auto it = this->lru_.rbegin(); it != this->lru_.rend() && this->bytes_ > this->budget_;) {
    auto entry = this->entries_.find(*it);
    if (entry == this->entries_.end() || !entry->second.objects.empty() || entry->second.state == Entry::LOADING) {
      ++it;
      continue;
    }
    if (entry->second.state == Entry::READY) {
      lv_image_cache_drop(&entry->second.dsc);
      this->bytes_ -= entry->second.pixels->size;
    }
    this->entries_.erase(entry);
    it = std::list<std::string>::reverse_iterator(this->lru_.erase(std::next(it).base()));
  }
}

void ImageCache::clear() {
  for (auto &kv : this->entries_)
    if (kv.second.state == Entry::READY)
      lv_image_cache_drop(&kv.second.dsc);
  this->entries_.clear();
  this->in_flight_.clear();
  this->lru_.clear();
  this->bytes_ = 0;
  this->worker_.cancel_if([](const HttpRequest &) { return true; });
}

}  // namespace esphome::socialhome_display
