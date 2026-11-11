// T39: Windows 双层防误杀与零断流自愈守护验证（对齐 Clash Mini v3.2.5 / BUG-316）
//
// 覆盖：
//   1. parse_guardian_args / parse_respawn_info 命令行解析与缺参防御
//   2. compute_next_generation 快速自愈代数退避与 60s 稳定运行归零
//   3. format_exit_code_diagnosis 退出码诊断（含 360 加速球 99/0x63 强杀特征码）
//   4. append_forensic_log 独立原子追加写 clashwin.log 取证记录
//   5. create_sentinel_window_pair 常驻防清理哨兵窗口对 Win32 属性不变量
//      （owner 不可见、sentinel 可见、无 TOOLWINDOW/TOPMOST/APPWINDOW、销毁干净）
//   6. 命名内核事件信令（轻量模式切换、主动退出通知、对端 Clash Mini 互斥礼貌置位）
//   7. CoreManager 零断流无感挂载标志与 CoreMonitor 启动跳过首轮自动选点 + 重试恢复闭环
#include "system/guardian.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "app/lifecycle.h"
#include "core/core_manager.h"
#include "core/monitor.h"
#include "test_util.h"

using namespace clashwin;
using namespace clashwin::system::guardian;

UT_TEST(guardian_parse_args_and_respawn_info) {
  const std::vector<std::wstring> normal = {L"ClashWin.exe"};
  UT_CHECK(!parse_guardian_args(normal).has_value());
  UT_CHECK(!parse_respawn_info(normal).has_value());
  UT_CHECK(!is_guardian_respawn_args(normal));

  const std::vector<std::wstring> valid = {
      L"ClashWin.exe",     L"--guardian",
      L"--parent-pid",     L"4242",
      L"--log-dir",        L"C:\\Temp\\ClashWin\\logs",
      L"--singleton-port", L"33339",
      L"--gen",            L"2",
  };
  const auto parsed = parse_guardian_args(valid);
  UT_CHECK(parsed.has_value());
  if (!parsed.has_value()) return;
  UT_CHECK_EQ(parsed->parent_pid, 4242u);
  UT_CHECK(parsed->log_dir == std::filesystem::path(L"C:\\Temp\\ClashWin\\logs"));
  UT_CHECK_EQ(parsed->singleton_port, 33339u);
  UT_CHECK_EQ(parsed->generation, 2u);

  const std::vector<std::wstring> missing_pid = {
      L"ClashWin.exe", L"--guardian", L"--log-dir", L"C:\\Temp\\logs"};
  UT_CHECK(!parse_guardian_args(missing_pid).has_value());

  const std::vector<std::wstring> respawn = {
      L"ClashWin.exe",     L"--guardian-respawn",
      L"--guardian-gen",   L"3",
      L"--prev-pid",       L"9527",
      L"--prev-exit",      L"99",
  };
  UT_CHECK(is_guardian_respawn_args(respawn));
  const auto info = parse_respawn_info(respawn);
  UT_CHECK(info.has_value());
  if (!info.has_value()) return;
  UT_CHECK_EQ(info->generation, 3u);
  UT_CHECK_EQ(info->prev_pid, 9527u);
  UT_CHECK_EQ(info->prev_exit, 99u);
}

UT_TEST(guardian_compute_next_generation_backoff_and_reset) {
  // 短时间连续崩溃：0 -> 1 -> 2 -> 3 -> nullopt（阻止无限拉起风暴）
  UT_CHECK(compute_next_generation(0, 10) == std::optional<uint32_t>(1u));
  UT_CHECK(compute_next_generation(1, 25) == std::optional<uint32_t>(2u));
  UT_CHECK(compute_next_generation(2, 59) == std::optional<uint32_t>(3u));
  UT_CHECK(!compute_next_generation(3, 5).has_value());

  // 稳定运行满 60s 后再次意外被杀：代数重置为 1
  UT_CHECK(compute_next_generation(3, 60) == std::optional<uint32_t>(1u));
  UT_CHECK(compute_next_generation(2, 3600) == std::optional<uint32_t>(1u));
}

UT_TEST(guardian_format_exit_code_diagnosis) {
  UT_CHECK(format_exit_code_diagnosis(99).find("360") != std::string::npos);
  UT_CHECK(format_exit_code_diagnosis(99).find("0x63") != std::string::npos);
  UT_CHECK(format_exit_code_diagnosis(0).find("STATUS_SUCCESS") !=
           std::string::npos);
  UT_CHECK(format_exit_code_diagnosis(1).find("TerminateProcess") !=
           std::string::npos);
  UT_CHECK(format_exit_code_diagnosis(0xC0000005u)
               .find("STATUS_ACCESS_VIOLATION") != std::string::npos);
  UT_CHECK(format_exit_code_diagnosis(0xC00000FDu)
               .find("STATUS_STACK_OVERFLOW") != std::string::npos);
  UT_CHECK(format_exit_code_diagnosis(0xC0000409u)
               .find("STATUS_STACK_BUFFER_OVERRUN") != std::string::npos);
  UT_CHECK(format_exit_code_diagnosis(0xC0000374u)
               .find("STATUS_HEAP_CORRUPTION") != std::string::npos);
  UT_CHECK(format_exit_code_diagnosis(0x12345678u).find("0x12345678") !=
           std::string::npos);
}

UT_TEST(guardian_append_forensic_log_writes_clashwin_log) {
  const auto temp_dir =
      std::filesystem::temp_directory_path() /
      (L"clashwin_guardian_test_" + std::to_wstring(::GetCurrentProcessId()) +
       L"_" + std::to_wstring(::GetTickCount64()));
  std::error_code ec;
  std::filesystem::remove_all(temp_dir, ec);

  append_forensic_log(temp_dir, "ERROR",
                      "[Guardian] 模拟强杀取证日志写入测试 exit=99");

  const auto log_file = temp_dir / L"clashwin.log";
  UT_CHECK(std::filesystem::exists(log_file));
  if (std::filesystem::exists(log_file)) {
    std::ifstream ifs(log_file, std::ios::binary);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    const std::string text = oss.str();
    ifs.close();

    UT_CHECK(text.find("[ERROR] [System] [Guardian] 模拟强杀取证日志写入测试 exit=99") !=
             std::string::npos);
  }

  std::filesystem::remove_all(temp_dir, ec);
}

UT_TEST(guardian_sentinel_window_pair_anti_360_invariants) {
  auto pair = create_sentinel_window_pair(L"ClashWinTestSentinelWindowClass");
  UT_CHECK(pair.has_value());
  if (!pair.has_value()) return;

  HWND owner = static_cast<HWND>(pair->owner_hwnd);
  HWND sentinel = static_cast<HWND>(pair->sentinel_hwnd);
  UT_CHECK(owner != nullptr);
  UT_CHECK(sentinel != nullptr);
  UT_CHECK(::IsWindow(owner) != FALSE);
  UT_CHECK(::IsWindow(sentinel) != FALSE);

  // owner 隐藏、sentinel 可见且被 owner 持有
  UT_CHECK(::IsWindowVisible(owner) == FALSE);
  UT_CHECK(::IsWindowVisible(sentinel) != FALSE);
  UT_CHECK(::GetWindow(sentinel, GW_OWNER) == owner);

  // 扩展样式必须不含 TOOLWINDOW / TOPMOST / APPWINDOW，且含 LAYERED + NOACTIVATE
  const LONG_PTR ex_style = ::GetWindowLongPtrW(sentinel, GWL_EXSTYLE);
  UT_CHECK_EQ((ex_style & WS_EX_TOOLWINDOW), 0);
  UT_CHECK_EQ((ex_style & WS_EX_TOPMOST), 0);
  UT_CHECK_EQ((ex_style & WS_EX_APPWINDOW), 0);
  UT_CHECK((ex_style & WS_EX_LAYERED) != 0);
  UT_CHECK((ex_style & WS_EX_NOACTIVATE) != 0);

  destroy_sentinel_window_pair(*pair);
  UT_CHECK(pair->owner_hwnd == nullptr);
  UT_CHECK(pair->sentinel_hwnd == nullptr);
  UT_CHECK(::IsWindow(sentinel) == FALSE);
  UT_CHECK(::IsWindow(owner) == FALSE);
}

UT_TEST(guardian_kernel_events_and_foreign_polite_exit) {
  UT_CHECK(ensure_guardian_events(/*initial_lightweight=*/false));
  const uint32_t self_pid = ::GetCurrentProcessId();

  HANDLE h_lw = ::OpenEventW(SYNCHRONIZE, FALSE,
                             lightweight_mode_event_name(self_pid).c_str());
  HANDLE h_exit = ::OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE,
                               intentional_exit_event_name(self_pid).c_str());
  UT_CHECK(h_lw != nullptr);
  UT_CHECK(h_exit != nullptr);
  if (!h_lw || !h_exit) {
    if (h_lw) ::CloseHandle(h_lw);
    if (h_exit) ::CloseHandle(h_exit);
    return;
  }

  // Lifecycle 进出轻量模式联动内核事件
  auto& lc = app::Lifecycle::instance();
  lc.reset();
  UT_CHECK_EQ(::WaitForSingleObject(h_lw, 0), WAIT_TIMEOUT);

  UT_CHECK(lc.entry_lightweight_mode(/*force=*/true));
  UT_CHECK_EQ(::WaitForSingleObject(h_lw, 0), WAIT_OBJECT_0);

  UT_CHECK(lc.exit_lightweight_mode());
  UT_CHECK_EQ(::WaitForSingleObject(h_lw, 0), WAIT_TIMEOUT);

  // 主动退出通知置位
  ::ResetEvent(h_exit);
  UT_CHECK_EQ(::WaitForSingleObject(h_exit, 0), WAIT_TIMEOUT);
  notify_intentional_exit();
  UT_CHECK_EQ(::WaitForSingleObject(h_exit, 0), WAIT_OBJECT_0);
  ::ResetEvent(h_exit);

  ::CloseHandle(h_exit);
  ::CloseHandle(h_lw);
  lc.reset();

  // 对端 Clash Mini 互斥接管礼貌置位：模拟对方伴生看门狗创建的事件
  const uint32_t fake_mini_pid = 999991u;
  const std::wstring mini_ev_name =
      L"Local\\ClashMiniIntentionalExit_" + std::to_wstring(fake_mini_pid);
  HANDLE h_mini = ::CreateEventW(nullptr, TRUE, FALSE, mini_ev_name.c_str());
  UT_CHECK(h_mini != nullptr);
  if (h_mini) {
    UT_CHECK_EQ(::WaitForSingleObject(h_mini, 0), WAIT_TIMEOUT);
    UT_CHECK(signal_foreign_guardian_exit(fake_mini_pid));
    UT_CHECK_EQ(::WaitForSingleObject(h_mini, 0), WAIT_OBJECT_0);
    ::CloseHandle(h_mini);
  }
}

UT_TEST(guardian_zero_drop_reattach_skips_headless_auto_select) {
  auto& mgr = core::CoreManager::instance();
  (void)mgr.take_reattached_existing_core();  // 清空初始态
  UT_CHECK(!mgr.take_reattached_existing_core());

  mgr.mark_reattached_existing_core();
  UT_CHECK(mgr.take_reattached_existing_core());
  // 单次消费语义：第二次读取应返回 false
  UT_CHECK(!mgr.take_reattached_existing_core());

  // 验证 CoreMonitor::start() 在 headless + reattached_existing_core 时跳过首次自动选点
  auto& mon = core::CoreMonitor::instance();
  mon.stop();
  mon.reset_state();

  std::atomic<int> auto_select_calls{0};
  core::CoreMonitor::Hooks hooks{};
  hooks.core_alive = [] { return true; };
  hooks.api_alive = [] { return true; };
  hooks.network_online = [] { return true; };
  hooks.is_headless = [] { return true; };
  hooks.current_uid = [] { return std::string("prf_test"); };
  hooks.auto_select_trigger = [&] {
    auto_select_calls.fetch_add(1);
    return Result<bool>::ok(true);
  };
  mon.set_hooks(hooks);

  mgr.mark_reattached_existing_core();
  mon.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  mon.stop();

  UT_CHECK_EQ(auto_select_calls.load(), 0);
  UT_CHECK(!mgr.take_reattached_existing_core());

  mon.set_hooks({});
  mon.reset_state();
}

UT_TEST(guardian_monitor_transient_failure_recovery_closure) {
  auto& mon = core::CoreMonitor::instance();
  mon.stop();
  mon.reset_state();

  bool healthy = false;
  int heal_calls = 0;
  int64_t now = 100'000;

  core::CoreMonitor::Hooks h{};
  h.core_alive = [] { return true; };
  h.network_online = [] { return true; };
  h.api_alive = [] { return true; };
  h.active_node_name = [] { return std::string("HK-01"); };
  h.is_dummy_active = [] { return false; };
  h.probe_metrics_with_fallback = [] { return true; };
  h.node_healthy = [&] { return healthy; };
  h.current_uid = [] { return std::string("uid_1"); };
  h.now_ms = [&] { return now; };
  h.auto_select_trigger = [&] {
    ++heal_calls;
    return Result<bool>::ok(true);
  };
  mon.set_hooks(h);

  // 首轮建立基线节点并跳过 20s 切换宽限期
  healthy = true;
  UT_CHECK(mon.tick_once() == core::CoreMonitor::Action::Idle);
  now += 25'000;

  // 第 1 次偶发抖动失败：进入 3s 快速复检态，不触发 heal
  healthy = false;
  UT_CHECK(mon.tick_once() == core::CoreMonitor::Action::Idle);
  UT_CHECK_EQ(mon.current_interval_secs(), 3);
  UT_CHECK_EQ(heal_calls, 0);

  // 下一轮复检恢复健康：解除重试态恢复 15s 常规周期，闭环不切点
  now += 3'000;
  healthy = true;
  UT_CHECK(mon.tick_once() == core::CoreMonitor::Action::Idle);
  UT_CHECK_EQ(mon.current_interval_secs(), 15);
  UT_CHECK_EQ(heal_calls, 0);

  mon.set_hooks({});
  mon.reset_state();
}
