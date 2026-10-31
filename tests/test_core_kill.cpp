// T07 测试：kill_all_mini_cores 过滤规则（select_stale_core_pids 纯函数注入）
//
// 沙箱内无法 spawn 真实进程 → 枚举逻辑抽成 ProcEnumerator 注入，
// 用假进程列表断言过滤规则（对照 state.rs:156-183 + 本项目安全铁律）：
//   同名才杀 / 路径无法查询的跳过 / 作用域外（用户实机内核）不杀 / 自身托管的不杀
#include "test_util.h"

#include "clashwin/core/constants.h"
#include "core/core_manager.h"

#include <algorithm>
#include <cstdio>
#include <set>

using namespace clashwin;
using namespace clashwin::core;

namespace {
core::ProcInfo proc(uint32_t pid, std::wstring name, std::wstring path, bool path_ok = true) {
  return core::ProcInfo{pid, std::move(name), std::move(path), path_ok};
}
}  // namespace

UT_TEST(kill_rule_name_match) {
  // 同名才杀：名字必须含 "mini-mihomo"
  std::vector<ProcInfo> procs = {
      proc(1, L"mini-mihomo.exe", L"C:\\scope\\mini-mihomo.exe"),
      proc(2, L"notepad.exe", L"C:\\scope\\notepad.exe"),
      proc(3, L"mini-mihomo-test.exe", L"C:\\scope\\mini-mihomo-test.exe"),  // contains 也算
      proc(4, L"minimihomo.exe", L"C:\\scope\\minimihomo.exe"),              // 无连字符不算
  };
  auto out = select_stale_core_pids(procs, {}, {L"C:\\scope"});
  std::sort(out.begin(), out.end());
  UT_CHECK(out == std::vector<uint32_t>({1, 3}));
}

UT_TEST(kill_rule_path_unqueryable_skipped) {
  // 路径无法查询（OpenProcess/QueryFullProcessImageName 失败）→ 跳过不杀
  std::vector<ProcInfo> procs = {
      proc(10, L"mini-mihomo.exe", L"", false),
      proc(11, L"mini-mihomo.exe", L"C:\\scope\\mini-mihomo.exe", true),
  };
  auto out = select_stale_core_pids(procs, {}, {L"C:\\scope"});
  UT_CHECK(out == std::vector<uint32_t>({11}));
}

UT_TEST(kill_rule_out_of_scope_never_killed) {
  // 铁律：用户实机 Clash Mini 内核（%APPDATA% 或 Clash Mini 安装目录）绝不触碰。
  // 只有位于本应用作用域（<home>\cores 或 <exe> 同级）内的同名进程才可杀。
  std::vector<ProcInfo> procs = {
      // 用户实机内核（作用域外）
      proc(20, L"mini-mihomo.exe",
           L"C:\\Users\\u\\AppData\\Roaming\\io.github.clash-win.clash-win\\cores\\mini-mihomo.exe"),
      proc(21, L"mini-mihomo.exe", L"D:\\Tools\\Clash Mini\\mini-mihomo.exe"),
      // 本应用测试作用域内
      proc(22, L"mini-mihomo.exe", L"C:\\ClashWin\\build\\bin\\Release\\sidecar\\mini-mihomo.exe"),
      proc(23, L"mini-mihomo.exe",
           L"C:\\Users\\u\\AppData\\Roaming\\io.github.clash-win.clash-win\\cores\\mini-mihomo.exe"),
  };
  auto out = select_stale_core_pids(procs, {}, {L"C:\\ClashWin\\build\\bin\\Release\\sidecar"});
  UT_CHECK(out == std::vector<uint32_t>({22}));
}

UT_TEST(kill_rule_exclude_self_hosted) {
  // 自身托管的子进程（stop 路径已单独处理）与自身 pid 都排除
  std::vector<ProcInfo> procs = {
      proc(30, L"mini-mihomo.exe", L"C:\\scope\\mini-mihomo.exe"),
      proc(31, L"mini-mihomo.exe", L"C:\\scope\\mini-mihomo.exe"),
  };
  auto out = select_stale_core_pids(procs, {30}, {L"C:\\scope"});
  UT_CHECK(out == std::vector<uint32_t>({31}));
}

UT_TEST(kill_rule_scope_case_insensitive) {
  // Windows 路径大小写不敏感；带尾反斜杠的 scope 也应匹配
  std::vector<ProcInfo> procs = {
      proc(40, L"mini-mihomo.exe", L"c:\\Scope\\Mini-Mihomo.exe"),
      proc(41, L"mini-mihomo.exe", L"C:\\ScopeOther\\mini-mihomo.exe"),  // 前缀不可误配
  };
  auto out = select_stale_core_pids(procs, {}, {L"C:\\SCOPE\\"});
  UT_CHECK(out == std::vector<uint32_t>({40}));
}

UT_TEST(kill_rule_scope_boundary) {
  // 目录边界：C:\\scope2 不能匹配 scope
  std::vector<ProcInfo> procs = {
      proc(50, L"mini-mihomo.exe", L"C:\\scope2\\mini-mihomo.exe"),
      proc(51, L"mini-mihomo.exe", L"C:\\scope\\mini-mihomo.exe"),
  };
  auto out = select_stale_core_pids(procs, {}, {L"C:\\scope"});
  UT_CHECK(out == std::vector<uint32_t>({51}));
}

UT_TEST(kill_rule_default_enumerator_runs) {
  // 默认枚举器（Toolhelp32）在本进程可用：返回不抛异常即可（列表可能为空）
  auto procs = enumerate_all_processes();
  for (const auto& p : procs) {
    UT_CHECK(p.name.find(consts::kCoreProcessToken) != std::wstring::npos);
  }
}
