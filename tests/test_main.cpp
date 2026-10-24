// clashwin_test.exe 入口：运行全部 UT_TEST 用例；可选 argv[1]=名称子串过滤
#include <cstring>
#include <exception>

#include "test_util.h"

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int total = 0;
  int failed_cases = 0;
  for (const auto& c : testfw::registry()) {
    if (filter && !std::strstr(c.name, filter)) continue;
    ++total;
    int before = testfw::failures();
    try {
      c.fn();
    } catch (const std::exception& ex) {
      ++testfw::failures();
      std::printf("  EXCEPTION in %s: %s\n", c.name, ex.what());
    } catch (...) {
      ++testfw::failures();
      std::printf("  UNKNOWN EXCEPTION in %s\n", c.name);
    }
    if (testfw::failures() > before) {
      ++failed_cases;
      std::printf("[FAIL] %s\n", c.name);
    } else {
      std::printf("[ ok ] %s\n", c.name);
    }
  }
  std::printf("\n%d cases, %d failed, %d assertion failures\n", total,
              failed_cases, testfw::failures());
  return failed_cases == 0 ? 0 : 1;
}
