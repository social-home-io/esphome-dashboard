// SPDX-License-Identifier: MPL-2.0
//
// The mDNS browse for Social Home, off the main loop. A worker task asks
// the network for _socialhome-dash._tcp (a 3 s PTR query through ESP-IDF's
// mdns component, which ESPHome's `mdns:` brings in) about every 10 s while
// the address screen is shown, and hands the rows to the component through
// a mailbox it drains in loop(). The host build (SDL screenshots) has no
// mDNS: there a fixed list (`discovery_stub`) stands in for the network.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "discovery.h"

namespace esphome::socialhome_display {

class ServerBrowser {
 public:
  ~ServerBrowser();

  /// Host build: the answers a browse returns (see parse_stub_announcements).
  void set_stub(const std::string &spec) { this->stub_ = spec; }
  /// Whether this build can browse at all (mDNS on, or a stub on the host).
  bool supported() const;
  /// Browse while true (the address screen is up). Starts the task on first use.
  void set_active(bool active);
  /// The rows, when they changed since the last call (main loop only).
  /// `searched`: at least one browse has finished.
  bool poll(std::vector<FoundServer> &rows, bool &searched);

 protected:
  void run_();
  /// One browse; false when the network couldn't be asked (no list change).
  bool browse_(std::vector<Announcement> &answers);

  std::string stub_;
  std::mutex lock_;
  std::condition_variable wake_;
  std::thread thread_;
  bool running_{false};
  bool active_{false};
  bool stop_{false};
  FoundList list_;
  bool searched_{false};
  uint32_t generation_{0};
  uint32_t seen_{0};
};

}  // namespace esphome::socialhome_display
