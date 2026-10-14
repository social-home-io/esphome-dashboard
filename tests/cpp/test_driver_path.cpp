// SPDX-License-Identifier: MPL-2.0
// The host panel's screenshot driver: one command file per panel.
#include "check.h"
#include "driver_path.h"

using host_driver::driver_path;

TEST(driver_file_given_wins) {
  CHECK_EQ(driver_path("/tmp/a.cmd", "/prefs", 42), std::string("/tmp/a.cmd"));
}

TEST(driver_file_follows_the_prefs_dir) {
  CHECK_EQ(driver_path("", "/h/prefs", 42), std::string("/h/prefs/socialhome-host.cmd"));
  CHECK_EQ(driver_path(nullptr, "/h/prefs/", 42), std::string("/h/prefs/socialhome-host.cmd"));
}

TEST(driver_file_otherwise_per_process) {
  CHECK_EQ(driver_path("", nullptr, 4242), std::string("/tmp/socialhome-host-4242.cmd"));
  CHECK_EQ(driver_path("", "", 7), std::string("/tmp/socialhome-host-7.cmd"));
  // Two panels, two files.
  CHECK(driver_path("", nullptr, 1) != driver_path("", nullptr, 2));
  CHECK(driver_path("", "/a", 1) != driver_path("", "/b", 1));
}
