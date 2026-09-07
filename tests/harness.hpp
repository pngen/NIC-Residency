#pragma once
// Minimal, self-contained test harness.  No third-party test dependency so the
// repository builds and runs anywhere with a C++20 compiler.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <atomic>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace nrtest {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}

inline std::atomic<int>& failures() { static std::atomic<int> f{0}; return f; }
inline std::atomic<int>& checks() { static std::atomic<int> c{0}; return c; }

struct Registrar {
  Registrar(std::string name, std::function<void()> fn) {
    registry().push_back({std::move(name), std::move(fn)});
  }
};

int run_all() {
  int ran = 0;
  for (auto& t : registry()) {
    std::printf("[ RUN  ] %s\n", t.name.c_str());
    int before = checks();
    int beforeFail = failures();
    t.fn();
    ++ran;
    if (failures() == beforeFail) {
      std::printf("[  OK  ] %s (%d checks)\n", t.name.c_str(),
                  static_cast<int>(checks()) - before);
    } else {
      std::printf("[ FAIL ] %s\n", t.name.c_str());
    }
  }
  std::printf("\n===== %d test(s) ran, %d check(s), %d failure(s) =====\n", ran,
              static_cast<int>(checks()), static_cast<int>(failures()));
  std::fflush(stdout);
  return failures() == 0 ? 0 : 1;
}

}  // namespace nrtest

#define NRTEST(name)                                                     \
  static void name();                                                    \
  static ::nrtest::Registrar nrtest_reg_##name(#name, name);             \
  static void name()

#define NREXPECT(cond)                                                          \
  do {                                                                          \
    ++::nrtest::checks();                                                      \
    if (!(cond)) {                                                             \
      ++::nrtest::failures();                                                  \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
      std::fflush(stdout);                                                     \
    }                                                                           \
  } while (0)

#define NREXPECT_MSG(cond, msg)                                                 \
  do {                                                                          \
    ++::nrtest::checks();                                                      \
    if (!(cond)) {                                                             \
      ++::nrtest::failures();                                                  \
      std::printf("  FAIL %s:%d  %s  (%s)\n", __FILE__, __LINE__, #cond, (msg));\
      std::fflush(stdout);                                                     \
    }                                                                           \
  } while (0)
