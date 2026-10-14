// SPDX-License-Identifier: MPL-2.0
//
// The words the panel draws itself: the household's language from the
// display API theme (GET /theme "strings"), else the panel's own English.
// No ESPHome / LVGL includes: covered by the host tests.
#pragma once

#include <string>

#include "model.h"

namespace esphome::socialhome_display {

/// The panel's built-in English for `key`: what a panel shows before it ever
/// got a theme (the setup screens, before there is a server) and for the
/// few strings the theme doesn't carry. Empty for a key the firmware
/// doesn't know.
const std::string &builtin_string(const char *key);

/// True when the firmware draws `key` itself (it has built-in English for
/// it): the strings worth keeping across a reboot (theme_cache.h).
bool is_panel_string(const std::string &key);

/// The household's `key` from `theme`, else the built-in English.
const std::string &panel_string(const Theme &theme, const char *key);

/// `text` with every `{name}` replaced by `value` — ("Tries left: {n}",
/// "n", "3") → "Tries left: 3". The value is inserted as is and never
/// searched again, so a value that itself reads "{n}" can't loop or expand.
/// A translation without the placeholder is kept as it is.
std::string fill_placeholder(const std::string &text, const char *name, const std::string &value);

}  // namespace esphome::socialhome_display
