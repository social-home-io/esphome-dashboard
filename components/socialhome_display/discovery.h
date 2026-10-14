// SPDX-License-Identifier: MPL-2.0
//
// Finding Social Home on the home network: Social Home announces its
// dashboard as the DNS-SD service _socialhome-dash._tcp (instance
// "Social Home <id>", host socialhome-<id>.local, TXT api=1, id=<id>,
// path=/api/display/v1/). This file is the pure part: turning raw answers
// into the list the address screen shows. No ESPHome / LVGL / IDF includes:
// covered by the host tests. The browse itself lives in browser.h.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace esphome::socialhome_display {

/// DNS-SD service and protocol labels the panel browses for.
static constexpr const char *DISCOVERY_SERVICE = "_socialhome-dash";
static constexpr const char *DISCOVERY_PROTO = "_tcp";
/// The display API major version this firmware speaks (TXT api=).
static constexpr const char *DISCOVERY_API = "1";

/// One raw mDNS answer, as the browse returned it.
struct Announcement {
  std::string instance;  ///< "Social Home 1a2b3c"
  std::string hostname;  ///< "socialhome-1a2b3c" (".local" optional)
  std::string ipv4;      ///< dotted quad, empty when none came with it
  int port{0};
  std::vector<std::pair<std::string, std::string>> txt;
};

/// A Social Home the address screen can offer.
struct FoundServer {
  std::string key;      ///< TXT id, else the instance name: one row each
  std::string name;     ///< what the row says ("Social Home 1a2b3c")
  std::string address;  ///< what the row shows under it ("192.168.1.20:8100")
  std::string url;      ///< the server URL a tap sets ("http://192.168.1.20:8100")

  bool operator==(const FoundServer &other) const {
    return key == other.key && name == other.name && address == other.address && url == other.url;
  }
};

/// The value of TXT `key` (keys compare case-insensitively, as DNS-SD
/// says), or empty.
std::string txt_value(const Announcement &a, const std::string &key);

/// `a` as a row, when the panel can use it: TXT api=1 (an unknown version is
/// skipped), a port, and an IPv4 address (preferred) or a host name
/// (".local" added when bare). False otherwise.
bool to_found(const Announcement &a, FoundServer &out);

/// The rows the screen shows, kept steady across browses: an answer missed
/// once (mDNS drops a packet now and then) keeps its row; missed `MAX_MISSES`
/// browses in a row it goes. Sorted by name, then address; at most `MAX`.
class FoundList {
 public:
  static constexpr int MAX_MISSES = 2;
  static constexpr size_t MAX = 8;

  /// One finished browse. True when the rows changed.
  bool update(const std::vector<Announcement> &answers);
  const std::vector<FoundServer> &items() const { return this->items_; }
  void clear() {
    this->items_.clear();
    this->misses_.clear();
  }

 protected:
  std::vector<FoundServer> items_;
  std::vector<int> misses_;  ///< per item: browses in a row without it
};

/// Host build only: "name|host|ipv4|port|txt;…" (txt "k=v,k=v") as the
/// answers a fake browse returns, so screenshots can show the list.
std::vector<Announcement> parse_stub_announcements(const std::string &spec);

}  // namespace esphome::socialhome_display
