#pragma once
// 极简单测框架：UT_TEST 注册用例，UT_CHECK 断言；main 驱动（tests/test_main.cpp）
#include <cstdio>
#include <vector>

namespace testfw {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}

inline int& failures() {
  static int f = 0;
  return f;
}

}  // namespace testfw

#define UT_TEST(name)                                              \
  static void ut_##name();                                         \
  static const bool ut_reg_##name = [] {                           \
    testfw::registry().push_back({#name, &ut_##name});             \
    return true;                                                   \
  }();                                                             \
  static void ut_##name()

#define UT_CHECK(cond)                                        \
  do {                                                        \
    if (!(cond)) {                                            \
      ++testfw::failures();                                   \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                         \
  } while (0)

#define UT_CHECK_EQ(a, b)                                                \
  do {                                                                   \
    if (!((a) == (b))) {                                                 \
      ++testfw::failures();                                              \
      std::printf("  FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
    }                                                                    \
  } while (0)
