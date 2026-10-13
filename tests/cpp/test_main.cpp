// SPDX-License-Identifier: MPL-2.0
#include <fstream>

#include "check.h"

namespace check {

std::string read_file(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    fail(__FILE__, __LINE__, "cannot open " + path);
    return {};
  }
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

std::string fixture(const std::string &name) { return read_file(std::string(FIXTURES_DIR) + "/" + name); }

}  // namespace check

int main() {
  int ran = 0;
  for (const auto &c : check::registry()) {
    int before = check::failures();
    c.body();
    ran++;
    std::cout << (check::failures() == before ? "ok   " : "FAIL ") << c.name << "\n";
  }
  std::cout << ran << " tests, " << check::failures() << " failed checks\n";
  return check::failures() == 0 ? 0 : 1;
}
