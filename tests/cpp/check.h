// SPDX-License-Identifier: MPL-2.0
// A tiny test harness: TEST(name) { CHECK(...); CHECK_EQ(a, b); }
#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace check {

struct Case {
  const char *name;
  std::function<void()> body;
};

inline std::vector<Case> &registry() {
  static std::vector<Case> cases;
  return cases;
}

inline int &failures() {
  static int n = 0;
  return n;
}

struct Register {
  Register(const char *name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

inline void fail(const char *file, int line, const std::string &what) {
  failures()++;
  std::cerr << file << ":" << line << ": FAILED " << what << "\n";
}

template<typename A, typename B> void eq(const A &a, const B &b, const char *as, const char *bs, const char *file,
                                         int line) {
  if (!(a == b)) {
    std::ostringstream out;
    out << as << " == " << bs << " (" << a << " vs " << b << ")";
    fail(file, line, out.str());
  }
}

std::string read_file(const std::string &path);
std::string fixture(const std::string &name);

}  // namespace check

#define CHECK_CAT2(a, b) a##b
#define CHECK_CAT(a, b) CHECK_CAT2(a, b)
#define TEST(name) \
  static void name(); \
  static check::Register CHECK_CAT(reg_, name)(#name, name); \
  static void name()
#define CHECK(cond) \
  do { \
    if (!(cond)) \
      check::fail(__FILE__, __LINE__, #cond); \
  } while (0)
#define CHECK_EQ(a, b) check::eq((a), (b), #a, #b, __FILE__, __LINE__)
