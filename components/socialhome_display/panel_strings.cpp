// SPDX-License-Identifier: MPL-2.0
#include "panel_strings.h"

#include <cstring>
#include <map>

namespace esphome::socialhome_display {

namespace {
const std::map<std::string, std::string> &builtin() {
  // The panel's own English. The keys are the display API theme's
  // (GET /theme "strings"); the ones the theme doesn't carry (yet) are
  // marked.
  static const std::map<std::string, std::string> BUILTIN = {
      {"wifi_title", "Connect the panel to Wi-Fi"},
      {"wifi_text",
       "Use Improv to give the panel your Wi-Fi: the web installer asks right after flashing, or a phone sends it "
       "over Bluetooth (Home Assistant finds the panel, or open improv-wifi.com). Tap Allow when the panel asks."},
      {"wifi_waiting", "Waiting for Wi-Fi\xE2\x80\xA6"},
      // Improv over Bluetooth asks on the panel first (common/improv.yaml).
      // Not in the theme.
      {"improv_title", "A phone wants to set up Wi-Fi"},
      {"improv_allow", "Allow"},
      {"server_title", "Find Social Home"},
      {"server_text", "Type the address of your Social Home. The dashboard port 8100 is added for you."},
      // The address screen's "found on this network" list. Not in the theme.
      {"server_text_found", "Tap your Social Home, or type its address. The dashboard port 8100 is added for you."},
      {"found_title", "Found on this network"},
      {"found_looking", "Looking for Social Home on this network\xE2\x80\xA6"},
      {"found_none", "None found yet. Using the Home Assistant App? Type its address below."},
      {"server_placeholder", "192.168.1.20 or socialhome.local"},
      {"server_invalid", "That doesn't look like an address. Try 192.168.1.20 or socialhome.local."},
      {"server_ha", "Or adopt this panel in Home Assistant and press Connect in Social Home."},
      {"connect", "Connect"},
      {"connecting", "Connecting to Social Home\xE2\x80\xA6"},
      {"pair_title", "Pair this panel"},
      {"pair_hint", "In Social Home, open Admin \xE2\x80\xBA Dashboard \xE2\x80\xBA Wall panels \xE2\x80\xBA Add "
                    "and enter this code:"},
      {"pair_expired", "The code expired \xE2\x80\x94 getting a new one\xE2\x80\xA6"},
      {"pair_unreachable", "Can't reach Social Home at this address."},
      {"pair_busy", "Too many pairing attempts. Trying again shortly\xE2\x80\xA6"},
      {"claim_title", "Connect to Social Home"},
      {"claim_expired", "This connect request expired. Press Connect in Social Home again."},
      {"claim_disabled", "The household dashboard is turned off in Social Home."},
      {"claim_busy", "Too many tries, wait a moment and press Connect again."},
      {"claim_unreachable", "Can't reach Social Home at {host}."},
      // A connect request sealed to another key. Not in the theme.
      {"claim_sealed", "This connect request can't be opened on this panel. Press Connect in Social Home again."},
      {"ok", "OK"},
      {"unpaired", "This panel isn't paired any more. Pair it again in Admin \xE2\x80\xBA Dashboard."},
      {"change_server", "Change address"},
      {"retry", "Try again"},
      {"update_title", "This panel needs an update"},
      {"update_text",
       "Your Social Home speaks a newer panel API. Update the panel's firmware from Home Assistant or the web "
       "installer."},
      {"offline", "Can't reach Social Home \xE2\x80\x94 showing info from {time}"},
      {"got_it", "Got it"},
      {"see_all", "See all"},
      {"new", "new"},
      {"empty_day", "Nothing planned today"},
      {"nav_home", "Home"},
      {"map_title", "Who's where"},
      {"map_placeholder", "The map comes with a Social Home update."},
      {"map_none", "No map picture on this Social Home \xE2\x80\x94 the list shows who's where."},
      {"settings_title", "Panel settings"},
      {"settings_server", "Social Home"},
      {"settings_panel", "Panel"},
      {"settings_firmware", "Firmware"},
      {"forget", "Forget server"},
      {"forget_confirm", "Forget this Social Home? The panel shows the setup again and needs a new pairing code."},
      {"cancel", "Cancel"},
      {"close", "Close"},
      {"credits",
       "Emoji: Twemoji \xC2\xA9 Twitter, Inc and other contributors, \xC2\xA9 jdecked and other contributors, "
       "CC-BY 4.0. Font: Roboto, SIL OFL 1.1. Firmware: MPL-2.0, built on ESPHome (GPLv3) and LVGL (MIT)."},
      {"locked", "Screen locked"},
      {"enter_pin", "Enter PIN"},
      {"unlock", "Unlock"},
      {"pin_button", "PIN"},
      {"wrong_pin", "Wrong PIN"},
      {"pin_remaining", "{n} tries left"},
      {"pin_wait", "Too many tries. Try again in {n} s."},
      {"pin_offline", "Can't reach Social Home. Try again."},
      {"lock", "Lock screen"},
      {"night_wake", "Tap to show the dashboard"},
  };
  return BUILTIN;
}
}  // namespace

const std::string &builtin_string(const char *key) {
  static const std::string EMPTY;
  const auto &table = builtin();
  auto it = table.find(key);
  return it == table.end() ? EMPTY : it->second;
}

bool is_panel_string(const std::string &key) { return builtin().count(key) != 0; }

const std::string &panel_string(const Theme &theme, const char *key) {
  return theme.str(key, builtin_string(key));
}

std::string fill_placeholder(const std::string &text, const char *name, const std::string &value) {
  const std::string token = std::string("{") + name + "}";
  std::string out;
  out.reserve(text.size() + value.size());
  size_t from = 0;
  for (size_t at; (at = text.find(token, from)) != std::string::npos; from = at + token.size()) {
    out.append(text, from, at - from);
    out += value;
  }
  out.append(text, from, std::string::npos);
  return out;
}

}  // namespace esphome::socialhome_display
