// T39: Windows 双层防误杀与零断流自愈守护验证（对齐 Clash Mini v3.2.5 / BUG-316 + BUG-363）
//
// 覆盖：
//   1. parse_guardian_args / parse_respawn_info 命令行解析（含 --target-exe 与兼容回退）与缺参防御
//   2. compute_next_generation 快速自愈代数退避与 120s 稳定运行归零 + compute_guard_rebuild_backoff_ms 看门狗重建退避
//   3. sync_guardian_binary_to_dir 异名异目录伴生二进制原子同步、复用、更新与锁定回退（clashwin-guard-alt.exe）
//   4. format_exit_code_diagnosis 退出码诊断（含 360 加速球 99/0x63 强杀特征码与通用 NTSTATUS）
//   5. append_forensic_log 独立原子追加写 clashwin.log 取证记录
//   6. create_sentinel_window_pair 常驻防清理哨兵窗口对 Win32 属性不变量
//      （owner 不可见、sentinel 可见、位于主屏工作区内 MonitorFromWindow != NULL、
//       非空标题、alpha=1、WM_NCHITTEST==HTTRANSPARENT、无 TOOLWINDOW/TOPMOST/APPWINDOW、销毁干净）
//   7. 命名内核事件信令（轻量模式切换、主动退出通知、对端 Clash Mini 互斥礼貌置位）
//   8. CoreManager 零断流无感挂载标志与 CoreMonitor 启动跳过首轮自动选点 + 重试恢复闭环
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
      L"C:\\Users\\test\\AppData\\Local\\io.github.clash-win.clash-win\\guard\\clashwin-guard.exe",
      L"--guardian",
      L"--parent-pid",     L"4242",
      L"--log-dir",        L"C:\\Temp\\ClashWin\\logs",
      L"--singleton-port", L"33339",
      L"--gen",            L"2",
      L"--target-exe",     L"C:\\Program Files\\Clash Win\\ClashWin.exe",
  };
  const auto parsed = parse_guardian_args(valid);
  UT_CHECK(parsed.has_value());
  if (!parsed.has_value()) return;
  UT_CHECK_EQ(parsed->parent_pid, 4242u);
  UT_CHECK(parsed->log_dir == std::filesystem::path(L"C:\\Temp\\ClashWin\\logs"));
  UT_CHECK_EQ(parsed->singleton_port, 33339u);
  UT_CHECK_EQ(parsed->generation, 2u);
  UT_CHECK(parsed->target_exe ==
           std::filesystem::path(L"C:\\Program Files\\Clash Win\\ClashWin.exe"));

  // 兼容旧版未传 --target-exe：自动回退至 args[0]
  const std::vector<std::wstring> legacy = {
      L"C:\\Program Files\\Clash Win\\ClashWin.exe",
      L"--guardian",
      L"--parent-pid", L"4242",
      L"--log-dir",    L"C:\\Temp\\ClashWin\\logs",
  };
  const auto legacy_parsed = parse_guardian_args(legacy);
  UT_CHECK(legacy_parsed.has_value());
  if (legacy_parsed.has_value()) {
    UT_CHECK(legacy_parsed->target_exe ==
             std::filesystem::path(L"C:\\Program Files\\Clash Win\\ClashWin.exe"));
  }

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
  // v3.3.0: 同一开机与会话纪元内累计自愈预算（0 -> 1 -> 2 -> 3 -> nullopt，无时间重置）
  UT_CHECK(compute_next_generation(0) == std::optional<uint32_t>(1u));
  UT_CHECK(compute_next_generation(1) == std::optional<uint32_t>(2u));
  UT_CHECK(compute_next_generation(2) == std::optional<uint32_t>(3u));
  UT_CHECK(!compute_next_generation(3).has_value());
  UT_CHECK(!compute_next_generation(99).has_value());

  const RespawnSessionState st{"epoch-1", "boot:1000", 3, 1000, 4242};
  UT_CHECK(!evaluate_respawn_budget(st, 3, "epoch-1", "boot:1000").has_value());
  UT_CHECK(!evaluate_respawn_budget(st, 0, "epoch-2", "boot:1000").has_value());
  UT_CHECK(evaluate_respawn_budget(st, 0, "epoch-1", "boot:2000") ==
           std::optional<uint32_t>(1u));

  // 主进程监督看门狗重建退避阶梯（BUG-363）
  UT_CHECK_EQ(compute_guard_rebuild_backoff_ms(0), 1500u);
  UT_CHECK_EQ(compute_guard_rebuild_backoff_ms(1), 2000u);
  UT_CHECK_EQ(compute_guard_rebuild_backoff_ms(2), 5000u);
  UT_CHECK_EQ(compute_guard_rebuild_backoff_ms(3), 15000u);
  UT_CHECK_EQ(compute_guard_rebuild_backoff_ms(4), 30000u);
  UT_CHECK_EQ(compute_guard_rebuild_backoff_ms(99), 30000u);
}

UT_TEST(guardian_sync_isolated_binary_stages_reuses_updates_and_fallback) {
  const auto temp_dir =
      std::filesystem::temp_directory_path() /
      (L"clashwin_guard_sync_test_" + std::to_wstring(::GetCurrentProcessId()) +
       L"_" + std::to_wstring(::GetTickCount64()));
  std::error_code ec;
  std::filesystem::remove_all(temp_dir, ec);
  std::filesystem::create_directories(temp_dir, ec);

  const auto fake_main = temp_dir / L"ClashWin.exe";
  const auto guard_dir = temp_dir / L"guard";
  std::filesystem::create_directories(guard_dir, ec);

  {
    std::ofstream ofs(fake_main, std::ios::binary | std::ios::trunc);
    ofs << "fake-clashwin-binary-v1";
  }

  // 制造历史残留 staging 临时文件，验证同步前自动清理
  const auto stale_staging = guard_dir / L"clashwin-guard.staging.99999.exe";
  {
    std::ofstream ofs(stale_staging, std::ios::binary | std::ios::trunc);
    ofs << "stale";
  }
  UT_CHECK(std::filesystem::exists(stale_staging));

  // 1. 首次同步创建 clashwin-guard.exe 并清理 staging
  const auto synced1 = sync_guardian_binary_to_dir(fake_main, guard_dir);
  UT_CHECK(synced1.has_value());
  if (!synced1.has_value()) return;
  UT_CHECK_EQ(synced1->filename().wstring(), std::wstring(kIsolatedGuardianBinaryName));
  UT_CHECK(std::filesystem::exists(*synced1));
  UT_CHECK(!std::filesystem::exists(stale_staging));
  UT_CHECK_EQ(std::filesystem::file_size(*synced1, ec),
              std::filesystem::file_size(fake_main, ec));

  // 2. 二进制大小和时间戳一致时直接复用
  const auto mtime1 = std::filesystem::last_write_time(*synced1, ec);
  const auto synced2 = sync_guardian_binary_to_dir(fake_main, guard_dir);
  UT_CHECK(synced2.has_value());
  if (!synced2.has_value()) return;
  UT_CHECK(*synced2 == *synced1);
  UT_CHECK(std::filesystem::last_write_time(*synced2, ec) == mtime1);

  // 3. 主二进制内容更新后重新同步覆盖
  {
    std::ofstream ofs(fake_main, std::ios::binary | std::ios::trunc);
    ofs << "fake-clashwin-binary-v2-updated-longer-payload";
  }
  const auto synced3 = sync_guardian_binary_to_dir(fake_main, guard_dir);
  UT_CHECK(synced3.has_value());
  if (!synced3.has_value()) return;
  UT_CHECK(*synced3 == *synced1);
  UT_CHECK_EQ(std::filesystem::file_size(*synced3, ec),
              std::filesystem::file_size(fake_main, ec));

  // 4. 当主槽位 clashwin-guard.exe 被独占锁定时（模拟旧看门狗正在运行），回退至 clashwin-guard-alt.exe
  {
    std::ofstream ofs(fake_main, std::ios::binary | std::ios::trunc);
    ofs << "fake-clashwin-binary-v3-locked-fallback-payload-extra";
  }
  HANDLE h_lock = ::CreateFileW(synced1->c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                nullptr);
  UT_CHECK(h_lock != INVALID_HANDLE_VALUE);
  if (h_lock != INVALID_HANDLE_VALUE) {
    const auto synced_alt = sync_guardian_binary_to_dir(fake_main, guard_dir);
    UT_CHECK(synced_alt.has_value());
    if (synced_alt.has_value()) {
      UT_CHECK_EQ(synced_alt->filename().wstring(),
                  std::wstring(kIsolatedGuardianFallbackName));
      UT_CHECK(std::filesystem::exists(*synced_alt));
      UT_CHECK_EQ(std::filesystem::file_size(*synced_alt, ec),
                  std::filesystem::file_size(fake_main, ec));
    }
    ::CloseHandle(h_lock);
  }

  std::filesystem::remove_all(temp_dir, ec);
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
  UT_CHECK(format_exit_code_diagnosis(0xC0000142u).find("NTSTATUS") !=
           std::string::npos);
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
  auto pair = create_sentinel_window_pair(L"ClashWinTestSentinelWindowClass",
                                          L"Clash Win Test Sentinel");
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

  // BUG-363：必须落在有效显示器工作区内、拥有非空标题、alpha==1、且 WM_NCHITTEST 返回 HTTRANSPARENT
  UT_CHECK(::MonitorFromWindow(sentinel, MONITOR_DEFAULTTONULL) != nullptr);
  UT_CHECK(::GetWindowTextLengthW(sentinel) > 0);
  BYTE alpha = 0;
  DWORD layered_flags = 0;
  UT_CHECK(::GetLayeredWindowAttributes(sentinel, nullptr, &alpha,
                                        &layered_flags) != FALSE);
  UT_CHECK_EQ(alpha, kSentinelVisibleAlpha);
  UT_CHECK_EQ((layered_flags & LWA_ALPHA), static_cast<DWORD>(LWA_ALPHA));
  UT_CHECK_EQ(::SendMessageW(sentinel, WM_NCHITTEST, 0, 0),
              static_cast<LRESULT>(HTTRANSPARENT));

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

UT_TEST(guardian_v330_two_phase_exit_proof_and_fail_closed_respawn_budget) {
  const auto temp_dir =
      std::filesystem::temp_directory_path() /
      (L"clashwin_guard_v330_test_" + std::to_wstring(::GetCurrentProcessId()) +
       L"_" + std::to_wstring(::GetTickCount64()));
  const auto guard_dir = temp_dir / L"guard";
  const auto log_dir = temp_dir / L"logs";
  std::error_code ec;
  std::filesystem::remove_all(temp_dir, ec);
  std::filesystem::create_directories(guard_dir, ec);
  std::filesystem::create_directories(log_dir, ec);

  const uint64_t base_ms = 100000ULL;
  UT_CHECK(write_intentional_exit_proof_in(guard_dir, 4242u, "sess-1",
                                           "prepared", "unit_prep", base_ms));
  UT_CHECK(evaluate_intentional_exit_proof_in(guard_dir, 4242u, "sess-1",
                                              base_ms + 10000ULL)
               .status == IntentionalProofStatus::ValidPrepared);
  UT_CHECK(evaluate_intentional_exit_proof_in(guard_dir, 4242u, "sess-1",
                                              base_ms + 35000ULL)
               .status == IntentionalProofStatus::Rejected);

  UT_CHECK(write_intentional_exit_proof_in(guard_dir, 4242u, "sess-1",
                                           "confirmed", "unit_quit", base_ms));
  UT_CHECK(evaluate_intentional_exit_proof_in(guard_dir, 4242u, "sess-1",
                                              base_ms + 100000ULL)
               .status == IntentionalProofStatus::ValidConfirmed);
  UT_CHECK(evaluate_intentional_exit_proof_in(guard_dir, 9999u, "sess-1",
                                              base_ms + 1000ULL)
               .status == IntentionalProofStatus::Rejected);
  UT_CHECK(evaluate_intentional_exit_proof_in(guard_dir, 4242u, "sess-2",
                                              base_ms + 1000ULL)
               .status == IntentionalProofStatus::Rejected);
  // 过期评估会顺带清理凭证文件，故放在 PID / session 校验之后
  UT_CHECK(evaluate_intentional_exit_proof_in(guard_dir, 4242u, "sess-1",
                                              base_ms + 130000ULL)
               .status == IntentionalProofStatus::Rejected);

  clear_intentional_exit_proof_in(guard_dir);
  UT_CHECK(evaluate_intentional_exit_proof_in(guard_dir, 4242u, "sess-1",
                                              base_ms + 1000ULL)
               .status == IntentionalProofStatus::NotPresent);

  reset_respawn_state_for_clean_start_in(guard_dir, log_dir, "epoch-a",
                                         "boot:1000");
  auto r1 = evaluate_and_record_respawn_in(guard_dir, "epoch-a", 0u, 4242u,
                                           base_ms, "boot:1000", /*commit=*/true);
  UT_CHECK(r1.is_ok() && r1.value() == std::optional<uint32_t>(1u));
  auto r2 = evaluate_and_record_respawn_in(guard_dir, "epoch-a", 1u, 4242u,
                                           base_ms + 1000ULL, "boot:1000", true);
  UT_CHECK(r2.is_ok() && r2.value() == std::optional<uint32_t>(2u));
  auto r3 = evaluate_and_record_respawn_in(guard_dir, "epoch-a", 2u, 4242u,
                                           base_ms + 2000ULL, "boot:1000", true);
  UT_CHECK(r3.is_ok() && r3.value() == std::optional<uint32_t>(3u));
  auto r4 = evaluate_and_record_respawn_in(
      guard_dir, "epoch-a", 3u, 4242u, base_ms + 600000ULL, "boot:1000", true);
  UT_CHECK(r4.is_ok() && !r4.value().has_value());

  {
    std::ofstream ofs(guard_dir / L"respawn_state.json",
                      std::ios::binary | std::ios::trunc);
    ofs << "{corrupt-json";
  }
  auto r_bad = evaluate_and_record_respawn_in(
      guard_dir, "epoch-a", 0u, 4242u, base_ms + 700000ULL, "boot:1000", true);
  UT_CHECK(r_bad.is_err() || !r_bad.value().has_value());

  std::filesystem::remove_all(temp_dir, ec);
}
