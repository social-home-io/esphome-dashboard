// SPDX-License-Identifier: MPL-2.0
// The panel's words: the household's theme strings, the built-in English
// fallback and the {n} / {host} / {time} placeholders.
#include "check.h"
#include "panel_strings.h"
#include "parser.h"

using namespace esphome::socialhome_display;

namespace {
Theme german() {
  Theme t;
  t.locale = "de";
  t.strings = {
      {"pin_remaining", "Noch {n} Versuche"},
      {"claim_unreachable", "Social Home unter {host} nicht erreichbar."},
      {"forget", "Server vergessen"},
  };
  return t;
}
}  // namespace

TEST(placeholder_filled) {
  CHECK_EQ(fill_placeholder("Tries left: {n}", "n", "3"), std::string("Tries left: 3"));
  CHECK_EQ(fill_placeholder("{n}", "n", "12"), std::string("12"));
  CHECK_EQ(fill_placeholder("Noch {n} Versuche", "n", "0"), std::string("Noch 0 Versuche"));
  CHECK_EQ(fill_placeholder("Can't reach Social Home at {host}.", "host", "192.168.1.20"),
           std::string("Can't reach Social Home at 192.168.1.20."));
  // Every occurrence, and other placeholders are left alone.
  CHECK_EQ(fill_placeholder("{n} of {n} ({time})", "n", "2"), std::string("2 of 2 ({time})"));
}

TEST(placeholder_safe) {
  // A translation without the placeholder stays as it is.
  CHECK_EQ(fill_placeholder("Zu viele Versuche.", "n", "30"), std::string("Zu viele Versuche."));
  CHECK_EQ(fill_placeholder("", "n", "1"), std::string(""));
  // The value is never searched again: no loop, no expansion.
  CHECK_EQ(fill_placeholder("at {host}", "host", "{host}"), std::string("at {host}"));
  CHECK_EQ(fill_placeholder("{n}{n}", "n", "{n}"), std::string("{n}{n}"));
  // Half a placeholder isn't one.
  CHECK_EQ(fill_placeholder("{n", "n", "1"), std::string("{n"));
  CHECK_EQ(fill_placeholder("n}", "n", "1"), std::string("n}"));
  CHECK_EQ(fill_placeholder("{N}", "n", "1"), std::string("{N}"));
}

TEST(theme_string_or_builtin_english) {
  Theme t = german();
  CHECK_EQ(panel_string(t, "forget"), std::string("Server vergessen"));
  CHECK_EQ(fill_placeholder(panel_string(t, "pin_remaining"), "n", "2"), std::string("Noch 2 Versuche"));
  // Missing from the theme: the panel's English.
  CHECK_EQ(panel_string(t, "cancel"), std::string("Cancel"));
  CHECK_EQ(panel_string(t, "improv_allow"), std::string("Allow"));
  CHECK_EQ(panel_string(t, "improv_title"), std::string("A phone wants to set up Wi-Fi"));
  // No theme yet (a new panel): all English.
  Theme none;
  CHECK_EQ(panel_string(none, "wifi_title"), std::string("Connect the panel to Wi-Fi"));
  CHECK_EQ(fill_placeholder(panel_string(none, "pin_wait"), "n", "30"),
           std::string("Too many tries. Try again in 30 s."));
  // A key the firmware doesn't know: empty, never a crash.
  CHECK_EQ(panel_string(none, "no_such_key"), std::string(""));
  CHECK(!is_panel_string("no_such_key"));
  CHECK(is_panel_string("forget_confirm"));
}

TEST(theme_fixture_covers_the_panel_strings) {
  // Every panel string comes from the pinned theme (the household's
  // language); the built-in English is only for a panel that never reached
  // a server.
  std::string json = check::fixture("theme.json");
  Theme t;
  CHECK(parse_theme(json.data(), json.size(), t) == ParseResult::OK);
  static const std::string MISSING = "\x01missing";
  for (const char *key : {"pin_remaining", "pin_wait", "pin_offline", "wifi_title", "wifi_text", "wifi_waiting",
                          "server_title", "server_text", "server_placeholder", "server_invalid", "server_ha",
                          "connect", "pair_unreachable", "pair_busy", "claim_title", "claim_expired",
                          "claim_disabled", "claim_busy", "claim_unreachable", "ok", "change_server", "update_title",
                          "update_text", "map_placeholder", "map_none", "settings_title", "settings_server",
                          "settings_panel", "settings_firmware", "forget", "forget_confirm", "cancel", "close",
                          "credits", "improv_title", "improv_allow", "server_text_found", "found_title",
                          "found_looking", "found_none", "claim_sealed", "pin_last_try", "pin_forgot"}) {
    CHECK(is_panel_string(key));
    CHECK(t.str(key, MISSING) != MISSING);
  }
  CHECK_EQ(fill_placeholder(panel_string(t, "pin_remaining"), "n", "4"), std::string("Tries left: 4"));
  // The built-in English matches the server's.
  for (const char *key : {"pin_remaining", "pin_last_try", "pin_forgot"})
    CHECK_EQ(builtin_string(key), t.str(key, MISSING));
  CHECK_EQ(fill_placeholder(panel_string(t, "pin_wait"), "n", "30"),
           std::string("Too many tries. Try again in 30 s."));
}

TEST(pin_tries_note_like_the_tablet) {
  const std::string tries = "Tries left: {n}", last = "Last try";
  CHECK_EQ(pin_tries_note(4, tries, last), std::string("Tries left: 4"));
  CHECK_EQ(pin_tries_note(2, tries, last), std::string("Tries left: 2"));
  // One left: the warning instead of a count (0 is never sent; it reads
  // the same).
  CHECK_EQ(pin_tries_note(1, tries, last), last);
  CHECK_EQ(pin_tries_note(0, tries, last), last);
  // No count from the server: no line.
  CHECK_EQ(pin_tries_note(-1, tries, last), std::string(""));
}
