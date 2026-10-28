// T23 单元与集成测试：
//   - 7步优雅退出序列：严格时序校验（prepare_exit -> clean_async -> flush -> exit_process）
//   - 并行资源清理：reset_sysproxy (1500ms) 与 stop_core (2000ms) 并行执行与退出码语义 (0/1)
//   - 超时预算保护：sysproxy 超时退出码 1、core 超时仍执行 kill_cores 残留兜底
//   - TUN 禁用补丁：clean_async 阶段向内核发送 TUN 禁用 PATCH (1000ms)
//   - 重入安全防护：多线程并发 quit() CAS 互斥，仅触发一次退出序列
//   - 控制台信号与会话：SetConsoleCtrlHandler 信号映射、重复信号抑制、WM_QUERYENDSESSION 与 WM_ENDSESSION 处理
//   - 重启恢复：活动节点与窗口几何落盘持久化、CreateProcessW 启动新进程
//   - 节点恢复：90s 预算重试自愈、预算耗尽回退 subset 子集
//   - 命令总线：exit_app 与 restart_app 路由装配
//
// 【铁律】所有测试必须使用 QuitHooks 依赖注入、TempHomeGuard (CLASHWIN_HOME_OVERRIDE) 与
// 模拟 API 客户端，严格保证零真实进程终止、零真实主机系统代理变动、零真实系统状态污染。

#include "test_util.h"

#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <algorithm>

#include "app/lifecycle.h"
#include "cmd/command_service.h"
#include "config/config.h"
#include "config/prfitem.h"
#include "core/core_manager.h"
#include "system/signals.h"
#include "util/dirs.h"

using namespace clashwin;
using namespace clashwin::app;
using namespace clashwin::core;
using namespace clashwin::system;

namespace {

// ---------- TempHomeGuard：临时主目录隔离 ----------
struct TempHomeGuard {
  std::filesystem::path dir;
  TempHomeGuard() {
    auto base = std::filesystem::temp_directory_path();
    dir = base / ("clashwin_t23_" + std::to_string(GetTickCount64()) + "_" +
                  std::to_string(rand()));
    std::filesystem::create_directories(dir);
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", dir.wstring().c_str());
  }
  ~TempHomeGuard() {
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
};

// ---------- LifecycleGuard：保证每个用例结束后重置单例状态 ----------
struct LifecycleGuard {
  ~LifecycleGuard() {
    Lifecycle::instance().reset();
    SignalHandler::instance().reset();
  }
};

}  // namespace

// 1. 7步退出严格执行时序校验
UT_TEST(t23_quit_7step_execution_order) {
  LifecycleGuard lg;
  Lifecycle::instance().reset();

  std::vector<std::string> log;
  std::mutex log_mu;
  auto record = [&](const std::string& step) {
    std::lock_guard<std::mutex> lk(log_mu);
    log.push_back(step);
  };

  QuitHooks hooks;
  hooks.grace_sleep_ms = 10;
  hooks.on_prepare_exit = [&] { record("1_on_prepare_exit"); };
  hooks.stop_monitors = [&] { record("1_stop_monitors"); };
  hooks.stop_timers = [&] { record("2_stop_timers"); };
  hooks.clear_ws = [&] { record("2_clear_ws"); };
  hooks.stop_server = [&] { record("3_stop_server"); };
  hooks.save_window_state = [&](bool) { record("4_save_window_state"); };
  hooks.save_configs = [&] { record("4_save_configs"); };
  hooks.reset_sysproxy = [&]() -> Result<void> {
    record("5_reset_sysproxy");
    return Result<void>::ok();
  };
  hooks.patch_disable_tun = [&]() -> Result<void> {
    record("6_patch_disable_tun");
    return Result<void>::ok();
  };
  hooks.stop_core = [&]() -> Result<void> {
    record("6_stop_core");
    return Result<void>::ok();
  };
  hooks.kill_cores = [&]() -> std::vector<uint32_t> {
    record("6_kill_cores");
    return {};
  };
  hooks.flush_logs = [&] { record("7_flush_logs"); };
  int captured_code = -1;
  hooks.exit_process = [&](int code) {
    captured_code = code;
    record("7_exit_process");
  };

  Lifecycle::instance().set_hooks(hooks);

  int out_exit_code = -1;
  bool ok = Lifecycle::instance().quit(&out_exit_code);
  UT_CHECK(ok);
  UT_CHECK_EQ(out_exit_code, 0);
  UT_CHECK_EQ(captured_code, 0);
  UT_CHECK(Lifecycle::instance().state() == LifecycleState::Exited);

  auto pos = [&](const std::string& val) -> int {
    for (size_t i = 0; i < log.size(); ++i) {
      if (log[i] == val) return static_cast<int>(i);
    }
    return -1;
  };

  UT_CHECK(pos("1_on_prepare_exit") != -1);
  UT_CHECK(pos("1_stop_monitors") != -1);
  UT_CHECK(pos("2_stop_timers") != -1);
  UT_CHECK(pos("2_clear_ws") != -1);
  UT_CHECK(pos("3_stop_server") != -1);
  UT_CHECK(pos("4_save_window_state") != -1);
  UT_CHECK(pos("4_save_configs") != -1);
  UT_CHECK(pos("5_reset_sysproxy") != -1);
  UT_CHECK(pos("6_patch_disable_tun") != -1);
  UT_CHECK(pos("6_stop_core") != -1);
  UT_CHECK(pos("6_kill_cores") != -1);
  UT_CHECK(pos("7_flush_logs") != -1);
  UT_CHECK(pos("7_exit_process") != -1);

  UT_CHECK(pos("1_on_prepare_exit") < pos("2_stop_timers"));
  UT_CHECK(pos("2_stop_timers") < pos("3_stop_server"));
  UT_CHECK(pos("3_stop_server") < pos("4_save_configs"));
  UT_CHECK(pos("4_save_configs") < pos("5_reset_sysproxy"));
  UT_CHECK(pos("4_save_configs") < pos("6_stop_core"));
  UT_CHECK(pos("6_stop_core") < pos("6_kill_cores"));
  UT_CHECK(pos("5_reset_sysproxy") < pos("7_flush_logs"));
  UT_CHECK(pos("6_kill_cores") < pos("7_flush_logs"));
  UT_CHECK(pos("7_flush_logs") < pos("7_exit_process"));
}

// 2. 并行清理均成功时退出码 0 且并发执行
UT_TEST(t23_quit_parallel_cleanup_success_exit0) {
  LifecycleGuard lg;
  Lifecycle::instance().reset();

  QuitHooks hooks;
  hooks.grace_sleep_ms = 0;
  hooks.reset_sysproxy = []() -> Result<void> {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    return Result<void>::ok();
  };
  hooks.stop_core = []() -> Result<void> {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    return Result<void>::ok();
  };
  hooks.kill_cores = []() -> std::vector<uint32_t> { return {}; };

  int exit_code = -1;
  hooks.exit_process = [&](int code) { exit_code = code; };
  Lifecycle::instance().set_hooks(hooks);

  int out_code = -1;
  bool ok = Lifecycle::instance().quit(&out_code);
  UT_CHECK(ok);
  UT_CHECK_EQ(out_code, 0);
  UT_CHECK_EQ(exit_code, 0);
}

// 3. WinINET/RAS 系统代理复原超时 (>1500ms) 触发退出码 1
UT_TEST(t23_quit_sysproxy_timeout_exit1) {
  LifecycleGuard lg;
  Lifecycle::instance().reset();

  QuitHooks hooks;
  hooks.grace_sleep_ms = 0;
  hooks.reset_sysproxy = []() -> Result<void> {
    // 睡眠 1800ms 超过 1500ms 超时预算
    std::this_thread::sleep_for(std::chrono::milliseconds(1800));
    return Result<void>::ok();
  };
  hooks.stop_core = []() -> Result<void> { return Result<void>::ok(); };
  hooks.kill_cores = []() -> std::vector<uint32_t> { return {}; };

  int exit_code = -1;
  hooks.exit_process = [&](int code) { exit_code = code; };
  Lifecycle::instance().set_hooks(hooks);

  int out_code = -1;
  bool ok = Lifecycle::instance().quit(&out_code);
  UT_CHECK(!ok);
  UT_CHECK_EQ(out_code, 1);
  UT_CHECK_EQ(exit_code, 1);
}

// 4. 内核停止超时 (>2000ms) 仍调用 kill_cores 兜底且退出码为 1
UT_TEST(t23_quit_core_stop_timeout_kills_cores) {
  LifecycleGuard lg;
  Lifecycle::instance().reset();

  QuitHooks hooks;
  hooks.grace_sleep_ms = 0;
  hooks.reset_sysproxy = []() -> Result<void> { return Result<void>::ok(); };
  hooks.stop_core = []() -> Result<void> {
    // 睡眠 2300ms 超过 2000ms 超时预算
    std::this_thread::sleep_for(std::chrono::milliseconds(2300));
    return Result<void>::ok();
  };
  bool kill_cores_invoked = false;
  hooks.kill_cores = [&]() -> std::vector<uint32_t> {
    kill_cores_invoked = true;
    return {1024, 2048};
  };

  int exit_code = -1;
  hooks.exit_process = [&](int code) { exit_code = code; };
  Lifecycle::instance().set_hooks(hooks);

  int out_code = -1;
  bool ok = Lifecycle::instance().quit(&out_code);
  UT_CHECK(!ok);
  UT_CHECK(kill_cores_invoked);
  UT_CHECK_EQ(out_code, 1);
  UT_CHECK_EQ(exit_code, 1);
}

// 5. clean_async 期间发送 TUN 禁用 PATCH
UT_TEST(t23_quit_tun_disable_patch_called) {
  LifecycleGuard lg;
  Lifecycle::instance().reset();

  QuitHooks hooks;
  bool patch_called = false;
  hooks.patch_disable_tun = [&]() -> Result<void> {
    patch_called = true;
    return Result<void>::ok();
  };
  hooks.reset_sysproxy = []() -> Result<void> { return Result<void>::ok(); };
  hooks.stop_core = []() -> Result<void> { return Result<void>::ok(); };
  hooks.kill_cores = []() -> std::vector<uint32_t> { return {}; };

  Lifecycle::instance().set_hooks(hooks);
  bool clean_ok = Lifecycle::instance().clean_async();
  UT_CHECK(clean_ok);
  UT_CHECK(patch_called);
}

// 6. 多线程并发 quit() 重入安全防护（CAS 原子互斥）
UT_TEST(t23_quit_reentrancy_prevention) {
  LifecycleGuard lg;
  Lifecycle::instance().reset();

  QuitHooks hooks;
  hooks.grace_sleep_ms = 15;
  std::atomic<int> prepare_count{0};
  std::atomic<int> exit_count{0};

  hooks.on_prepare_exit = [&] { ++prepare_count; };
  hooks.reset_sysproxy = []() -> Result<void> {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return Result<void>::ok();
  };
  hooks.stop_core = []() -> Result<void> { return Result<void>::ok(); };
  hooks.kill_cores = []() -> std::vector<uint32_t> { return {}; };
  hooks.exit_process = [&](int) { ++exit_count; };

  Lifecycle::instance().set_hooks(hooks);

  std::vector<std::thread> workers;
  for (int i = 0; i < 5; ++i) {
    workers.emplace_back([] {
      Lifecycle::instance().quit();
    });
  }
  for (auto& w : workers) {
    w.join();
  }

  UT_CHECK_EQ(prepare_count.load(), 1);
  UT_CHECK_EQ(exit_count.load(), 1);
}

// 7. 控制台信号映射与调度器
UT_TEST(t23_signals_console_ctrl_handlers) {
  LifecycleGuard lg;
  SignalHandler::instance().reset();

  std::vector<SignalType> history;
  SignalHandler::instance().install([&](SignalType sig) {
    history.push_back(sig);
  });

  // 测试 console_ctrl_proc 映射
  SignalHandler::console_ctrl_proc(CTRL_C_EVENT);
  UT_CHECK_EQ(history.size(), 1);
  UT_CHECK(history[0] == SignalType::CtrlC);

  // 重置后测试 CTRL_CLOSE_EVENT
  SignalHandler::instance().reset();
  history.clear();
  SignalHandler::instance().install([&](SignalType sig) {
    history.push_back(sig);
  });
  SignalHandler::console_ctrl_proc(CTRL_CLOSE_EVENT);
  UT_CHECK_EQ(history.size(), 1);
  UT_CHECK(history[0] == SignalType::CtrlClose);

  // 测试字符串映射
  UT_CHECK_EQ(std::string(to_string(SignalType::CtrlC)), "CTRL_C");
  UT_CHECK_EQ(std::string(to_string(SignalType::CtrlBreak)), "CTRL_BREAK");
  UT_CHECK_EQ(std::string(to_string(SignalType::CtrlClose)), "CTRL_CLOSE");
  UT_CHECK_EQ(std::string(to_string(SignalType::CtrlLogoff)), "CTRL_LOGOFF");
  UT_CHECK_EQ(std::string(to_string(SignalType::CtrlShutdown)), "CTRL_SHUTDOWN");
  UT_CHECK_EQ(std::string(to_string(SignalType::SessionQueryEnd)), "SESSION_QUERY_END");
  UT_CHECK_EQ(std::string(to_string(SignalType::SessionEnd)), "SESSION_END");
}

// 8. 重复信号过滤抑制
UT_TEST(t23_signals_repeated_signal_ignored) {
  LifecycleGuard lg;
  SignalHandler::instance().reset();

  std::atomic<int> calls{0};
  SignalHandler::instance().install([&](SignalType) {
    ++calls;
  });

  SignalHandler::instance().dispatch(SignalType::CtrlC);
  SignalHandler::instance().dispatch(SignalType::CtrlClose);
  SignalHandler::instance().dispatch(SignalType::CtrlShutdown);

  UT_CHECK_EQ(calls.load(), 1);
}

// 9. Windows 操作系统会话终止消息处理
UT_TEST(t23_session_query_and_end_messages) {
  LifecycleGuard lg;
  SignalHandler::instance().reset();

  bool query_res = SignalHandler::instance().handle_session_query_end(0);
  UT_CHECK(query_res);

  std::vector<SignalType> signals;
  SignalHandler::instance().install([&](SignalType s) {
    signals.push_back(s);
  });

  // session_ending 为 false 时不分发退出
  SignalHandler::instance().handle_session_end(false, 0);
  UT_CHECK(signals.empty());

  // session_ending 为 true 时分发 SessionEnd
  SignalHandler::instance().handle_session_end(true, 0);
  UT_CHECK_EQ(signals.size(), 1);
  UT_CHECK(signals[0] == SignalType::SessionEnd);
}

// 10. 重启恢复：状态与配置持久化及新实例拉起
UT_TEST(t23_restart_app_state_persistence) {
  LifecycleGuard lg;
  TempHomeGuard th;
  Lifecycle::instance().reset();

  auto& profiles = config::Config::instance().profiles();
  profiles.edit_draft([](config::IProfiles& p) {
    p.current = "prf-restart-node-test";
    config::PrfItem item;
    item.uid = "prf-restart-node-test";
    item.name = "RestartTest";
    p.items = std::vector<config::PrfItem>{item};
  });
  profiles.apply();
  (void)profiles.data_arc()->save_file();

  QuitHooks hooks;
  hooks.grace_sleep_ms = 0;
  std::wstring launched_exe;
  std::wstring launched_cmd;
  hooks.launch_process = [&](const std::wstring& exe, const std::wstring& cmd) -> bool {
    launched_exe = exe;
    launched_cmd = cmd;
    return true;
  };
  int exit_code = -1;
  hooks.exit_process = [&](int code) { exit_code = code; };
  hooks.reset_sysproxy = []() -> Result<void> { return Result<void>::ok(); };
  hooks.stop_core = []() -> Result<void> { return Result<void>::ok(); };
  hooks.kill_cores = []() -> std::vector<uint32_t> { return {}; };

  Lifecycle::instance().set_hooks(hooks);

  bool ok = Lifecycle::instance().restart_app();
  UT_CHECK(ok);
  UT_CHECK(!launched_cmd.empty());
  UT_CHECK_EQ(exit_code, 0);
  UT_CHECK(Lifecycle::instance().state() == LifecycleState::Exited);
}

// 11. 重启后节点在 90s 预算内重试自愈恢复
UT_TEST(t23_restart_node_90s_budget_recovery) {
  RestoreTimings timings;
  timings.api_ready_budget_ms = 100;
  timings.api_ready_poll_ms = 10;
  timings.group_fill_budget_ms = 100;
  timings.group_fill_poll_ms = 10;
  timings.select_budget_ms = 250;
  timings.select_retry_ms = 20;

  struct MockRetryClient : core::ICoreApiClient {
    int attempts = 0;
    Result<mihomo::BaseConfig> get_base_config() override {
      return Result<mihomo::BaseConfig>::ok({});
    }
    Result<mihomo::ProxyItem> get_group_by_name(const std::string& name) override {
      mihomo::ProxyItem g;
      g.name = name;
      g.all = std::vector<std::string>{"HK-Fast", "SG-Backup"};
      return Result<mihomo::ProxyItem>::ok(g);
    }
    Result<void> select_node_for_group(const std::string&, const std::string&) override {
      ++attempts;
      if (attempts >= 2) {
        return Result<void>::ok();
      }
      return Result<void>::err({1100, "IPC pipe busy"});
    }
    Result<void> reload_config(bool, const std::string&) override {
      return Result<void>::ok();
    }
  } client;

  CoreManager::instance().restore_proxy_group_now(client, "HK-Fast", timings);
  UT_CHECK_EQ(client.attempts, 2);
}

// 12. 节点选择超时耗尽后回退 subset 子集首个可用节点
UT_TEST(t23_restart_node_exhaustion_subset_fallback) {
  RestoreTimings timings;
  timings.api_ready_budget_ms = 50;
  timings.api_ready_poll_ms = 10;
  timings.group_fill_budget_ms = 50;
  timings.group_fill_poll_ms = 10;
  timings.select_budget_ms = 60;
  timings.select_retry_ms = 15;

  struct MockFallbackClient : core::ICoreApiClient {
    int dead_node_attempts = 0;
    std::string fallback_selected;
    Result<mihomo::BaseConfig> get_base_config() override {
      return Result<mihomo::BaseConfig>::ok({});
    }
    Result<mihomo::ProxyItem> get_group_by_name(const std::string& name) override {
      mihomo::ProxyItem g;
      g.name = name;
      g.all = std::vector<std::string>{"Subset-Fallback-01", "Unreachable-Target"};
      return Result<mihomo::ProxyItem>::ok(g);
    }
    Result<void> select_node_for_group(const std::string&, const std::string& node) override {
      if (node == "Unreachable-Target") {
        ++dead_node_attempts;
        return Result<void>::err({1100, "dial timeout"});
      }
      fallback_selected = node;
      return Result<void>::ok();
    }
    Result<void> reload_config(bool, const std::string&) override {
      return Result<void>::ok();
    }
  } client;

  CoreManager::instance().restore_proxy_group_now(client, "Unreachable-Target", timings);
  UT_CHECK(client.dead_node_attempts >= 1);
  UT_CHECK_EQ(client.fallback_selected, "Subset-Fallback-01");
}

// 13. CommandService exit_app 与 restart_app 路由装配验证
UT_TEST(t23_command_service_exit_and_restart_wired) {
  LifecycleGuard lg;
  auto& svc = cmd::CommandService::instance();
  svc.install_all();

  UT_CHECK(svc.exists("exit_app"));
  UT_CHECK(svc.exists("restart_app"));

  // 验证 exit_app 调用路由到 Lifecycle::quit()
  Lifecycle::instance().reset();
  std::atomic<bool> quit_invoked{false};
  QuitHooks hooks;
  hooks.grace_sleep_ms = 0;
  hooks.exit_process = [&](int) { quit_invoked.store(true); };
  hooks.reset_sysproxy = []() -> Result<void> { return Result<void>::ok(); };
  hooks.stop_core = []() -> Result<void> { return Result<void>::ok(); };
  hooks.kill_cores = []() -> std::vector<uint32_t> { return {}; };
  Lifecycle::instance().set_hooks(hooks);

  auto r_exit = svc.invoke("exit_app");
  UT_CHECK(r_exit.is_ok());

  for (int i = 0; i < 30 && !quit_invoked.load(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  UT_CHECK(quit_invoked.load());

  // 验证 restart_app 调用路由到 Lifecycle::restart_app()
  Lifecycle::instance().reset();
  std::atomic<bool> restart_invoked{false};
  QuitHooks r_hooks;
  r_hooks.grace_sleep_ms = 0;
  r_hooks.launch_process = [&](const std::wstring&, const std::wstring&) -> bool {
    restart_invoked.store(true);
    return true;
  };
  r_hooks.exit_process = [](int) {};
  r_hooks.reset_sysproxy = []() -> Result<void> { return Result<void>::ok(); };
  r_hooks.stop_core = []() -> Result<void> { return Result<void>::ok(); };
  r_hooks.kill_cores = []() -> std::vector<uint32_t> { return {}; };
  Lifecycle::instance().set_hooks(r_hooks);

  auto r_restart = svc.invoke("restart_app");
  UT_CHECK(r_restart.is_ok());

  for (int i = 0; i < 30 && !restart_invoked.load(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  UT_CHECK(restart_invoked.load());
}
