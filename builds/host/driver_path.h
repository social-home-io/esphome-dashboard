// SPDX-License-Identifier: MPL-2.0
//
// Which command file a host panel's screenshot driver reads (host build
// only, see host_driver.h). Platform-free: covered by the host tests.
//
// Two host panels on one machine must never share it — each would run the
// other's taps and shots. So, first match wins:
//
//   1. `-s driver_file PATH` (builds/host-sdl.yaml), when given;
//   2. $ESPHOME_PREFDIR/socialhome-host.cmd — the panel's own prefs dir
//      (where its server and token live, so already one per panel);
//   3. /tmp/socialhome-host-<pid>.cmd.
//
// The driver logs the path it picked at start-up, and refuses a file another
// running panel already reads (host_driver.h).
#pragma once

#include <string>

namespace host_driver {

inline std::string driver_path(const char *configured, const char *prefdir, long pid) {
  if (configured != nullptr && configured[0] != '\0')
    return configured;
  if (prefdir != nullptr && prefdir[0] != '\0') {
    std::string dir = prefdir;
    while (dir.size() > 1 && dir.back() == '/')
      dir.pop_back();
    return dir + "/socialhome-host.cmd";
  }
  return "/tmp/socialhome-host-" + std::to_string(pid) + ".cmd";
}

}  // namespace host_driver
