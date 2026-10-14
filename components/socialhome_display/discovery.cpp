// SPDX-License-Identifier: MPL-2.0
#include "discovery.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace esphome::socialhome_display {

namespace {

std::string lower(std::string text) {
  for (auto &ch : text)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return text;
}

bool is_ipv4(const std::string &text) {
  int parts = 0;
  size_t at = 0;
  while (at <= text.size()) {
    size_t end = text.find('.', at);
    if (end == std::string::npos)
      end = text.size();
    std::string part = text.substr(at, end - at);
    if (part.empty() || part.size() > 3 || !std::all_of(part.begin(), part.end(), ::isdigit))
      return false;
    if (std::atoi(part.c_str()) > 255)
      return false;
    parts++;
    at = end + 1;
  }
  return parts == 4;
}

/// A host name usable in a URL ("socialhome-1a2b3c.local"), or empty.
std::string clean_host(std::string host) {
  while (!host.empty() && host.back() == '.')
    host.pop_back();
  if (host.empty() || host.size() > 253)
    return {};
  for (char ch : host)
    if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '.')
      return {};
  if (host.find('.') == std::string::npos)
    host += ".local";
  return host;
}

/// Control characters out, at most 64 bytes (not splitting a UTF-8 letter).
std::string clean_name(const std::string &name) {
  std::string out;
  for (char ch : name)
    if (static_cast<unsigned char>(ch) >= 0x20 && ch != 0x7F)
      out += ch;
  if (out.size() > 64) {
    size_t cut = 64;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
      cut--;
    out.resize(cut);
  }
  return out;
}

std::vector<std::string> split(const std::string &text, char sep) {
  std::vector<std::string> parts;
  size_t at = 0;
  while (true) {
    size_t end = text.find(sep, at);
    parts.push_back(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
    if (end == std::string::npos)
      return parts;
    at = end + 1;
  }
}

bool before(const FoundServer &a, const FoundServer &b) {
  if (a.name != b.name)
    return a.name < b.name;
  return a.address < b.address;
}

}  // namespace

std::string txt_value(const Announcement &a, const std::string &key) {
  const std::string want = lower(key);
  for (const auto &item : a.txt)
    if (lower(item.first) == want)
      return item.second;
  return {};
}

bool to_found(const Announcement &a, FoundServer &out) {
  if (txt_value(a, "api") != DISCOVERY_API)
    return false;
  if (a.port <= 0 || a.port > 65535)
    return false;
  std::string host;
  if (is_ipv4(a.ipv4))
    host = a.ipv4;
  else
    host = clean_host(a.hostname);
  if (host.empty())
    return false;
  FoundServer found;
  found.address = host + ":" + std::to_string(a.port);
  found.url = "http://" + found.address;
  found.name = clean_name(a.instance);
  if (found.name.empty())
    found.name = host;
  std::string id = lower(txt_value(a, "id"));
  found.key = id.empty() ? found.name : id;
  out = std::move(found);
  return true;
}

bool FoundList::update(const std::vector<Announcement> &answers) {
  // This browse: one row per key, an IPv4 answer winning over a host name
  // (the same household answers once per interface / address family).
  std::vector<FoundServer> fresh;
  std::vector<bool> fresh_v4;
  for (const auto &a : answers) {
    FoundServer found;
    if (!to_found(a, found))
      continue;
    const bool v4 = is_ipv4(a.ipv4);
    auto it = std::find_if(fresh.begin(), fresh.end(), [&](const FoundServer &f) { return f.key == found.key; });
    if (it == fresh.end()) {
      fresh.push_back(found);
      fresh_v4.push_back(v4);
    } else if (v4 && !fresh_v4[it - fresh.begin()]) {
      *it = found;
      fresh_v4[it - fresh.begin()] = true;
    }
  }
  std::vector<FoundServer> items;
  std::vector<int> misses;
  for (size_t k = 0; k < this->items_.size(); k++) {
    const FoundServer &old = this->items_[k];
    auto it = std::find_if(fresh.begin(), fresh.end(), [&](const FoundServer &f) { return f.key == old.key; });
    if (it != fresh.end()) {
      items.push_back(*it);
      misses.push_back(0);
      fresh.erase(it);
    } else if (this->misses_[k] + 1 < MAX_MISSES) {
      items.push_back(old);
      misses.push_back(this->misses_[k] + 1);
    }
  }
  for (auto &f : fresh) {
    items.push_back(std::move(f));
    misses.push_back(0);
  }
  // Sort both together.
  std::vector<size_t> order(items.size());
  for (size_t k = 0; k < order.size(); k++)
    order[k] = k;
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return before(items[a], items[b]); });
  std::vector<FoundServer> sorted;
  std::vector<int> sorted_misses;
  for (size_t k = 0; k < order.size() && k < MAX; k++) {
    sorted.push_back(items[order[k]]);
    sorted_misses.push_back(misses[order[k]]);
  }
  const bool changed = sorted != this->items_;
  this->items_ = std::move(sorted);
  this->misses_ = std::move(sorted_misses);
  return changed;
}

std::vector<Announcement> parse_stub_announcements(const std::string &spec) {
  std::vector<Announcement> out;
  if (spec.empty())
    return out;
  for (const auto &entry : split(spec, ';')) {
    std::vector<std::string> fields = split(entry, '|');
    if (fields.size() != 5)
      continue;
    Announcement a;
    a.instance = fields[0];
    a.hostname = fields[1];
    a.ipv4 = fields[2];
    a.port = std::atoi(fields[3].c_str());
    if (!fields[4].empty()) {
      for (const auto &pair : split(fields[4], ',')) {
        size_t eq = pair.find('=');
        if (eq == std::string::npos)
          a.txt.emplace_back(pair, "");
        else
          a.txt.emplace_back(pair.substr(0, eq), pair.substr(eq + 1));
      }
    }
    out.push_back(std::move(a));
  }
  return out;
}

}  // namespace esphome::socialhome_display
