// clashwin_test.exe 入口：运行全部 UT_TEST 用例；可选 argv[1]=名称子串过滤
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>

#include "test_util.h"

int main(int argc, char** argv) {
  // 无缓冲输出：崩溃/段错误时不丢失已完成用例的进度
  setvbuf(stdout, nullptr, _IONBF, 0);
  const char* filter = argc > 1 ? argv[1] : nullptr;

  // 【测试隔离铁律】每个用例执行前强制把 home 指向独立临时目录，
  // 避免任何用例（含清空 CLASHWIN_HOME_OVERRIDE 的用例）写到真实的
  // %APPDATA%\<APPID>。此前 test_t23 的 prf-restart-node-test 就是这样
  // 污染了装机用户的 profiles.yaml，导致 current 指向 file:null 的残留、
  // 内核拿不到有效配置（mixed-port 落到默认 7890、/proxies 无 PROXY 组）。
  std::filesystem::path suite_root =
      std::filesystem::temp_directory_path() / "clashwin_suite_isolated";
  std::filesystem::create_directories(suite_root);

  int total = 0;
  int failed_cases = 0;
  int case_index = 0;
  for (const auto& c : testfw::registry()) {
    if (filter && !std::strstr(c.name, filter)) continue;
    ++total;

    // 每个用例一个全新子目录（部分用例依赖“空 profiles”前置状态）
    std::filesystem::path case_root = suite_root / ("case_" + std::to_string(case_index++));
    std::filesystem::remove_all(case_root);
    std::filesystem::create_directories(case_root);
    std::wstring case_root_w = case_root.wstring();
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", case_root_w.c_str());

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
