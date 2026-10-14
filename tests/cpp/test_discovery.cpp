// SPDX-License-Identifier: MPL-2.0
// Finding Social Home over mDNS: which answers become rows, and their URLs.
#include "check.h"
#include "discovery.h"

using namespace esphome::socialhome_display;

namespace {
Announcement social_home(const std::string &id, const std::string &ipv4, int port = 8100) {
  Announcement a;
  a.instance = "Social Home " + id;
  a.hostname = "socialhome-" + id;
  a.ipv4 = ipv4;
  a.port = port;
  a.txt = {{"api", "1"}, {"id", id}, {"path", "/api/display/v1/"}};
  return a;
}
}  // namespace

TEST(found_prefers_ipv4) {
  FoundServer f;
  CHECK(to_found(social_home("1a2b3c", "192.168.1.20"), f));
  CHECK_EQ(f.url, std::string("http://192.168.1.20:8100"));
  CHECK_EQ(f.address, std::string("192.168.1.20:8100"));
  CHECK_EQ(f.name, std::string("Social Home 1a2b3c"));
  CHECK_EQ(f.key, std::string("1a2b3c"));
}

TEST(found_falls_back_to_host_name) {
  FoundServer f;
  CHECK(to_found(social_home("1a2b3c", "", 8400), f));
  CHECK_EQ(f.url, std::string("http://socialhome-1a2b3c.local:8400"));
  Announcement a = social_home("1a2b3c", "not-an-ip");
  a.hostname = "socialhome-1a2b3c.local.";
  CHECK(to_found(a, f));
  CHECK_EQ(f.url, std::string("http://socialhome-1a2b3c.local:8100"));
}

TEST(found_skips_unusable) {
  FoundServer f;
  Announcement v2 = social_home("1a2b3c", "192.168.1.20");
  v2.txt = {{"api", "2"}, {"id", "1a2b3c"}};
  CHECK(!to_found(v2, f));
  Announcement no_api = social_home("1a2b3c", "192.168.1.20");
  no_api.txt = {{"id", "1a2b3c"}};
  CHECK(!to_found(no_api, f));
  CHECK(!to_found(social_home("1a2b3c", "192.168.1.20", 0), f));
  CHECK(!to_found(social_home("1a2b3c", "192.168.1.20", 70000), f));
  Announcement nowhere = social_home("1a2b3c", "");
  nowhere.hostname = "";
  CHECK(!to_found(nowhere, f));
  Announcement odd_host = social_home("1a2b3c", "");
  odd_host.hostname = "evil/host@x";
  CHECK(!to_found(odd_host, f));
  Announcement bad_ip = social_home("1a2b3c", "300.1.1.1");
  bad_ip.hostname = "";
  CHECK(!to_found(bad_ip, f));
}

TEST(found_txt_keys_ignore_case) {
  FoundServer f;
  Announcement a = social_home("1a2b3c", "10.0.0.2");
  a.txt = {{"API", "1"}, {"Id", "ABCDEF"}, {"future", "x"}};
  CHECK(to_found(a, f));
  CHECK_EQ(f.key, std::string("abcdef"));
}

TEST(found_list_dedupes_sorts_and_ages) {
  FoundList list;
  Announcement b6 = social_home("bbbbbb", "");  // IPv6-only answer first
  std::vector<Announcement> first = {social_home("cccccc", "10.0.0.3"), b6, social_home("bbbbbb", "10.0.0.2"),
                                     social_home("aaaaaa", "10.0.0.1")};
  Announcement v2 = social_home("dddddd", "10.0.0.4");
  v2.txt = {{"api", "2"}};
  first.push_back(v2);
  CHECK(list.update(first));
  CHECK_EQ(list.items().size(), size_t(3));
  CHECK_EQ(list.items()[0].key, std::string("aaaaaa"));
  CHECK_EQ(list.items()[1].url, std::string("http://10.0.0.2:8100"));
  CHECK_EQ(list.items()[2].key, std::string("cccccc"));
  // Same answers again: nothing to redraw.
  CHECK(!list.update(first));
  // One browse misses "cccccc": its row stays.
  CHECK(!list.update({social_home("aaaaaa", "10.0.0.1"), social_home("bbbbbb", "10.0.0.2")}));
  CHECK_EQ(list.items().size(), size_t(3));
  // Missed twice in a row: gone.
  CHECK(list.update({social_home("aaaaaa", "10.0.0.1"), social_home("bbbbbb", "10.0.0.2")}));
  CHECK_EQ(list.items().size(), size_t(2));
  // A new address for a known household moves its row.
  CHECK(list.update({social_home("aaaaaa", "10.0.0.9"), social_home("bbbbbb", "10.0.0.2")}));
  CHECK_EQ(list.items()[0].url, std::string("http://10.0.0.9:8100"));
  CHECK(!list.update({}));  // a missed browse keeps the rows
  CHECK(list.update({}));
  CHECK(list.items().empty());
}

TEST(found_list_caps) {
  FoundList list;
  std::vector<Announcement> many;
  for (int k = 0; k < 12; k++)
    many.push_back(social_home("00000" + std::to_string(k % 10) + std::to_string(k), "10.0.0." + std::to_string(k + 1)));
  list.update(many);
  CHECK_EQ(list.items().size(), FoundList::MAX);
}

TEST(found_name_is_cleaned) {
  FoundServer f;
  Announcement a = social_home("1a2b3c", "10.0.0.2");
  a.instance = std::string("Social\nHome ") + std::string(80, 'x');
  CHECK(to_found(a, f));
  CHECK_EQ(f.name.size(), size_t(64));
  CHECK(f.name.find('\n') == std::string::npos);
  a.instance = "";
  CHECK(to_found(a, f));
  CHECK_EQ(f.name, std::string("10.0.0.2"));
}

TEST(stub_announcements) {
  auto list = parse_stub_announcements(
      "Social Home 1a2b3c|socialhome-1a2b3c|127.0.0.1|8400|api=1,id=1a2b3c;bad;Old|old||8100|api=2");
  CHECK_EQ(list.size(), size_t(2));
  CHECK_EQ(list[0].port, 8400);
  CHECK_EQ(txt_value(list[0], "id"), std::string("1a2b3c"));
  CHECK_EQ(list[1].ipv4, std::string(""));
  CHECK(parse_stub_announcements("").empty());
}
