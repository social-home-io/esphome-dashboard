// SPDX-License-Identifier: MPL-2.0
#include "browser.h"

#include <chrono>
#include <cstdio>
#include <utility>

#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#if defined(USE_ESP32) && defined(USE_MDNS)
#include <esp_pthread.h>
#include <mdns.h>
#define SH_BROWSE_MDNS
#endif

namespace esphome::socialhome_display {

static const char *const TAG = "socialhome_display.mdns";

/// One PTR query listens this long for answers.
static constexpr uint32_t QUERY_MS = 3000;
/// A browse starts this often while the address screen is up (the query
/// itself takes QUERY_MS of it).
static constexpr uint32_t BROWSE_EVERY_MS = 10000;
/// mDNS not up yet (Wi-Fi just joined): ask again after this.
static constexpr uint32_t NOT_READY_MS = 3000;
/// At most this many answers per query.
static constexpr size_t MAX_ANSWERS = 8;

ServerBrowser::~ServerBrowser() {
  {
    std::lock_guard<std::mutex> guard(this->lock_);
    this->stop_ = true;
  }
  this->wake_.notify_all();
  if (this->thread_.joinable())
    this->thread_.join();
}

bool ServerBrowser::supported() const {
#ifdef SH_BROWSE_MDNS
  return true;
#else
  return !this->stub_.empty();
#endif
}

void ServerBrowser::set_active(bool active) {
  if (!this->supported())
    return;
  {
    std::lock_guard<std::mutex> guard(this->lock_);
    if (this->active_ == active)
      return;
    this->active_ = active;
    if (active && !this->running_) {
#ifdef SH_BROWSE_MDNS
      // A small worker task: the query blocks on the mdns task's answer.
      esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
      cfg.stack_size = 6 * 1024;
      cfg.prio = 2;
      cfg.thread_name = "sh_mdns";
      esp_pthread_set_cfg(&cfg);
#endif
      this->thread_ = std::thread([this]() { this->run_(); });
      this->running_ = true;
    }
  }
  ESP_LOGD(TAG, "Browse %s", active ? "on" : "off");
  this->wake_.notify_all();
}

bool ServerBrowser::poll(std::vector<FoundServer> &rows, bool &searched) {
  std::lock_guard<std::mutex> guard(this->lock_);
  if (this->seen_ == this->generation_)
    return false;
  this->seen_ = this->generation_;
  rows = this->list_.items();
  searched = this->searched_;
  return true;
}

void ServerBrowser::run_() {
  while (true) {
    {
      std::unique_lock<std::mutex> guard(this->lock_);
      this->wake_.wait(guard, [this]() { return this->stop_ || this->active_; });
      if (this->stop_)
        return;
    }
    std::vector<Announcement> answers;
    const bool asked = this->browse_(answers);
    std::unique_lock<std::mutex> guard(this->lock_);
    if (asked) {
      const bool changed = this->list_.update(answers);
      if (changed || !this->searched_)
        this->generation_++;
      this->searched_ = true;
      ESP_LOGD(TAG, "%u answer(s), %u Social Home(s)", static_cast<unsigned>(answers.size()),
               static_cast<unsigned>(this->list_.items().size()));
    }
    const uint32_t pause = asked ? BROWSE_EVERY_MS - QUERY_MS : NOT_READY_MS;
    this->wake_.wait_for(guard, std::chrono::milliseconds(pause),
                         [this]() { return this->stop_ || !this->active_; });
    if (this->stop_)
      return;
  }
}

#ifdef SH_BROWSE_MDNS
bool ServerBrowser::browse_(std::vector<Announcement> &answers) {
  mdns_result_t *results = nullptr;
  // A one-shot PTR query: works with CONFIG_MDNS_ENABLE_BROWSE off (that only
  // drops the continuous mdns_browse_* API ESPHome doesn't use).
  esp_err_t err = mdns_query_ptr(DISCOVERY_SERVICE, DISCOVERY_PROTO, QUERY_MS, MAX_ANSWERS, &results);
  if (err != ESP_OK) {
    ESP_LOGD(TAG, "Query failed: %s", esp_err_to_name(err));
    return false;
  }
  for (mdns_result_t *r = results; r != nullptr; r = r->next) {
    Announcement a;
    if (r->instance_name != nullptr)
      a.instance = r->instance_name;
    if (r->hostname != nullptr)
      a.hostname = r->hostname;
    a.port = r->port;
    for (size_t k = 0; k < r->txt_count; k++) {
      const mdns_txt_item_t &item = r->txt[k];
      if (item.key == nullptr)
        continue;
      std::string value;
      if (item.value != nullptr)
        value.assign(item.value, r->txt_value_len != nullptr ? r->txt_value_len[k] : 0);
      a.txt.emplace_back(item.key, std::move(value));
    }
    for (mdns_ip_addr_t *addr = r->addr; addr != nullptr; addr = addr->next) {
      if (addr->addr.type == ESP_IPADDR_TYPE_V4) {
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&addr->addr.u_addr.ip4));
        a.ipv4 = ip;
        break;
      }
    }
    answers.push_back(std::move(a));
  }
  mdns_query_results_free(results);
  return true;
}
#else
bool ServerBrowser::browse_(std::vector<Announcement> &answers) {
  if (this->stub_.empty())
    return false;
  // Pretend to listen for a moment, so "Looking…" shows first.
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  answers = parse_stub_announcements(this->stub_);
  return true;
}
#endif

}  // namespace esphome::socialhome_display
