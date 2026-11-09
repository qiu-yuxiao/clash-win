// 互斥共存方针测试（← 总纲 §5.2，用户 2026-11-09 改定）
// 覆盖：路径归属判定（本方/对方/大小写/尾分隔符）+ 服务缺失时 service_image_is_own 安全语义。
// 不触碰真实进程/服务写操作（kill/stop/delete 仅人工桌面验证）。
#include "test_util.h"

#include <windows.h>

#include <algorithm>
#include <string>

#include "core/service_mode.h"
#include "system/coexist.h"
#include "util/dirs.h"

using clashwin::system::coexist::is_own_path;
using clashwin::system::coexist::service_image_is_own;

namespace {
std::wstring to_upper_copy(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(::towupper(c)); });
  return s;
}
}  // namespace

UT_TEST(coexist_own_path_matches_exe_dir_and_home) {
  const std::wstring exe_dir = clashwin::util::dirs::exe_dir();
  const std::wstring home = clashwin::util::dirs::app_home_dir();

  // 本方：exe 目录下的自家应用、资源、内核
  UT_CHECK(is_own_path(exe_dir + L"\\ClashWin.exe"));
  UT_CHECK(is_own_path(exe_dir + L"\\resources\\clash-verge-service.exe"));
  UT_CHECK(is_own_path(exe_dir + L"\\mini-mihomo.exe"));
  // 本方：app_home 下的 cores（内核缓存）
  UT_CHECK(is_own_path(home + L"\\cores\\mini-mihomo.exe"));

  // 对方：典型 Clash Mini 安装布局（Program Files 与自定义目录）
  UT_CHECK(!is_own_path(L"C:\\Program Files\\Clash Mini\\Clash Mini.exe"));
  UT_CHECK(!is_own_path(L"C:\\Program Files\\Clash Mini\\resources\\mini-mihomo.exe"));
  UT_CHECK(!is_own_path(L"D:\\Tools\\clash-mini\\clash-mini.exe"));
}

UT_TEST(coexist_own_path_case_and_separator_insensitive) {
  const std::wstring exe_dir = clashwin::util::dirs::exe_dir();
  // 大小写不敏感（NTFS 语义）
  UT_CHECK(is_own_path(to_upper_copy(exe_dir) + L"\\CLASHWIN.EXE"));
  // 正斜杠混用同样命中
  std::wstring fwd = exe_dir;
  for (auto& c : fwd) if (c == L'\\') c = L'/';
  UT_CHECK(is_own_path(fwd + L"/mini-mihomo.exe"));
  // 相似前缀目录不得误判（own_dirX ≠ own_dir\）
  UT_CHECK(!is_own_path(exe_dir + L"x\\mini-mihomo.exe"));
  // 空路径 → false
  UT_CHECK(!is_own_path(L""));
}

UT_TEST(coexist_service_missing_means_not_own) {
  // 测试环境通常无 clash_verge_service；缺失 → false（宁可判对方走接管，不误判本方）。
  // 若本机真实注册了服务且指向本方路径，该用例自动跳过判定（两侧皆可接受）。
  if (!clashwin::core::service_mode::is_service_installed()) {
    UT_CHECK(!service_image_is_own());
  } else {
    UT_CHECK(true);  // 服务存在：结果取决于 ImagePath 归属，桌面验证覆盖
  }
}
