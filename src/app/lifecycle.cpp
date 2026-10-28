#include "app/lifecycle.h"

#include <windows.h>
#include <chrono>
#include <future>
#include <thread>
#include <format>

#include "cmd/cmd_support.h"
#include "config/config.h"
#include "config/prfitem.h"
#include "core/core_manager.h"
#include "core/timer.h"
#include "events/event_bus.h"
#include "system/guard_monitor.h"
#include "system/sysproxy.h"
#include "system/tray.h"
#include "util/log.h"

namespace clashwin::app {

namespace {

template <typename F>
auto run_with_timeout(F&& fn, std::chrono::milliseconds timeout)
    -> std::optional<decltype(fn())> {
  using R = decltype(fn());
  auto prom = std::make_shared<std::promise<R>>();
  auto fut = prom->get_future();
  std::thread([prom, fn = std::forward<F>(fn)]() mutable {
    try {
      prom->set_value(fn());
    } catch (...) {
      prom->set_exception(std::current_exception());
    }
  }).detach();

  if (fut.wait_for(timeout) == std::future_status::ready) {
    return fut.get();
  }
  return std::nullopt;
}

}  // namespace

Lifecycle::Lifecycle() {
  reset_hooks();
}

Lifecycle& Lifecycle::instance() {
  static Lifecycle s_instance;
  return s_instance;
}

void Lifecycle::reset_hooks() {
  QuitHooks h;
  h.on_prepare_exit = [] {
    events::EventBus::instance().publish("prepare-exit", "");
  };
  h.reset_sysproxy = []() -> Result<void> {
    return system::Sysopt::instance().reset_sysproxy();
  };
  h.patch_disable_tun = []() -> Result<void> {
    auto verge = config::Config::instance().verge().latest_arc();
    bool tun_mode = verge ? verge->enable_tun_mode.value_or(false) : false;
    if (!tun_mode) return Result<void>::ok();
    nlohmann::json patch = {{"tun", {{"enable", false}}}};
    return cmd::mihomo_unchecked().patch_base_config(patch, 1000);
  };
  h.stop_core = []() -> Result<void> {
    return core::CoreManager::instance().stop();
  };
  h.kill_cores = []() -> std::vector<uint32_t> {
    return core::kill_all_mini_cores();
  };
  h.save_configs = [] {
    config::Config::instance().apply_all_and_save_file();
  };
  h.save_window_state = [this](bool immediately) {
    if (window_) window_->save_geometry(immediately);
  };
  h.stop_server = nullptr;
  h.stop_timers = [] {
    core::ProfileTimer::instance().stop();
  };
  h.stop_monitors = [] {
    system::CoreMonitor::instance().stop();
    system::SysproxyGuard::instance().stop();
  };
  h.clear_ws = [this] {
    if (window_) window_->disconnect_ws();
  };
  h.flush_logs = [] {
    Logger::instance().flush();
  };
  h.launch_process = [](const std::wstring& exe, const std::wstring& cmd) -> bool {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');
    bool spawned = CreateProcessW(exe.empty() ? nullptr : exe.c_str(),
                                  cmd_buf.data(), nullptr, nullptr, FALSE,
                                  0, nullptr, nullptr, &si, &pi) != 0;
    if (spawned) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    return spawned;
  };
  h.exit_process = [](int code) {
    ::ExitProcess(static_cast<UINT>(code));
  };
  h.grace_sleep_ms = 200;

  hooks_ = std::move(h);
}

bool Lifecycle::entry_lightweight_mode(bool force) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (is_exiting()) {
    CWLOG_INFO("Lifecycle: already exiting, entry_lightweight_mode skipped");
    return false;
  }

  if (!force) {
    auto verge = config::Config::instance().verge().latest_arc();
    bool enable_auto = verge ? verge->enable_auto_light_weight_mode.value_or(false) : false;
    if (!enable_auto) {
      CWLOG_INFO("Lifecycle: enable_auto_light_weight_mode is false, only hiding window");
      if (window_) window_->hide();
      if (tray_) tray_->update_lite_mode_menu(false);
      return true;
    }
  }

  uint8_t expected = static_cast<uint8_t>(LifecycleState::Normal);
  if (!state_.compare_exchange_strong(expected, static_cast<uint8_t>(LifecycleState::InLightweight))) {
    CWLOG_INFO("Lifecycle: not in Normal state, entry skipped");
    if (tray_) tray_->update_lite_mode_menu(is_in_lightweight_mode());
    return false;
  }

  CWLOG_INFO("Lifecycle: entering lightweight mode");

  if (window_) {
    window_->hide();
    window_->release_d2d_resources();
    window_->disconnect_ws();
  }

  events::EventBus::instance().set_lightweight_filter(true);

  if (tray_) {
    tray_->update_lite_mode_menu(true);
  }

  system::CoreMonitor::instance().tick_once();
  return true;
}

bool Lifecycle::exit_lightweight_mode() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (is_exiting()) {
    CWLOG_INFO("Lifecycle: already exiting, exit_lightweight_mode skipped");
    return false;
  }

  if (state() == LifecycleState::Normal) {
    CWLOG_INFO("Lifecycle: in Normal state, showing and activating window");
    if (window_) window_->show_and_activate();
    if (tray_) tray_->update_lite_mode_menu(false);
    return true;
  }

  if (state() != LifecycleState::InLightweight) {
    CWLOG_INFO("Lifecycle: not in InLightweight state");
    return false;
  }

  CWLOG_INFO("Lifecycle: exiting lightweight mode");

  events::EventBus::instance().set_lightweight_filter(false);

  if (window_) {
    window_->restore_d2d_resources();
    window_->show_and_activate();
    window_->reconnect_ws();
  }

  state_.store(static_cast<uint8_t>(LifecycleState::Normal), std::memory_order_release);

  if (tray_) {
    tray_->update_lite_mode_menu(false);
  }

  system::CoreMonitor::instance().tick_once();
  return true;
}

void Lifecycle::prepare_exit() {
  state_.store(static_cast<uint8_t>(LifecycleState::PreparingExit), std::memory_order_release);
  CWLOG_INFO("Lifecycle::prepare_exit: Step 1 to Step 4 starting");

  // Step 1: on_prepare_exit callback and stop monitors
  if (hooks_.on_prepare_exit) hooks_.on_prepare_exit();
  if (hooks_.stop_monitors) hooks_.stop_monitors();

  // Step 2: Stop timers and disconnect WebSocket subscriptions
  if (hooks_.stop_timers) hooks_.stop_timers();
  if (hooks_.clear_ws) hooks_.clear_ws();

  // Step 3: Grace period (200ms) then stop server
  if (hooks_.grace_sleep_ms > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(hooks_.grace_sleep_ms));
  }
  if (hooks_.stop_server) hooks_.stop_server();

  // Step 4: Flush window state and configuration files
  if (hooks_.save_window_state) hooks_.save_window_state(true);
  if (hooks_.save_configs) hooks_.save_configs();
  CWLOG_INFO("Lifecycle::prepare_exit: Step 1 to Step 4 completed");
}

bool Lifecycle::clean_async() {
  state_.store(static_cast<uint8_t>(LifecycleState::CleaningAsync), std::memory_order_release);
  CWLOG_INFO("Lifecycle::clean_async: Step 5 and Step 6 starting in parallel");

  std::atomic<bool> proxy_success{false};
  std::atomic<bool> core_success{false};

  // Step 5: Proxy Task (1500ms timeout budget)
  auto proxy_fut = std::async(std::launch::async, [this, &proxy_success] {
    if (!hooks_.reset_sysproxy) {
      proxy_success.store(true);
      return;
    }
    auto res = run_with_timeout([this] { return hooks_.reset_sysproxy(); },
                                std::chrono::milliseconds(1500));
    if (res.has_value()) {
      proxy_success.store(res->is_ok());
    } else {
      CWLOG_WARN("Lifecycle: reset_sysproxy timed out (>1500ms)");
      proxy_success.store(false);
    }
  });

  // Step 6: Core Task (1000ms TUN disable timeout + 2000ms stop timeout + kill_cores)
  auto core_fut = std::async(std::launch::async, [this, &core_success] {
    if (hooks_.patch_disable_tun) {
      auto tun_res = run_with_timeout([this] { return hooks_.patch_disable_tun(); },
                                      std::chrono::milliseconds(1000));
      if (!tun_res.has_value()) {
        CWLOG_WARN("Lifecycle: patch_disable_tun timed out (>1000ms)");
      }
    }

    bool stopped = false;
    if (hooks_.stop_core) {
      auto stop_res = run_with_timeout([this] { return hooks_.stop_core(); },
                                       std::chrono::milliseconds(2000));
      if (stop_res.has_value()) {
        stopped = stop_res->is_ok();
      } else {
        CWLOG_WARN("Lifecycle: stop_core timed out (>2000ms)");
        stopped = false;
      }
    } else {
      stopped = true;
    }

    if (hooks_.kill_cores) {
      hooks_.kill_cores();
    }
    core_success.store(stopped);
  });

  proxy_fut.wait();
  core_fut.wait();

  bool success = proxy_success.load() && core_success.load();
  CWLOG_INFO(std::format("Lifecycle::clean_async: completed, proxy_ok={}, core_ok={}",
                         proxy_success.load(), core_success.load()));
  return success;
}

bool Lifecycle::quit(int* out_exit_code) {
  // Re-entrancy guard
  bool expected = false;
  if (!is_cleaning_up_.compare_exchange_strong(expected, true)) {
    CWLOG_INFO("Lifecycle::quit: already cleaning up, skipping duplicate call");
    return true;
  }

  CWLOG_INFO("Lifecycle::quit: initiating 7-step shutdown sequence");

  // Step 1 to 4
  prepare_exit();

  // Step 5 and 6
  bool cleanup_success = clean_async();

  int exit_code = cleanup_success ? 0 : 1;
  CWLOG_INFO(std::format("Lifecycle::quit: cleanup finished, exit code: {}", exit_code));
  if (out_exit_code) *out_exit_code = exit_code;

  // Step 7: Flush logs and terminate
  if (hooks_.flush_logs) hooks_.flush_logs();

  state_.store(static_cast<uint8_t>(LifecycleState::Exited), std::memory_order_release);

  if (hooks_.exit_process) {
    hooks_.exit_process(exit_code);
  }

  return cleanup_success;
}

bool Lifecycle::restart_app() {
  bool expected = false;
  if (!is_cleaning_up_.compare_exchange_strong(expected, true)) {
    CWLOG_INFO("Lifecycle::restart_app: already cleaning up, skipping duplicate call");
    return false;
  }

  CWLOG_INFO("Lifecycle::restart_app: snapshotting state and restarting");

  // 1. Snapshot active node and save to profiles.yaml
  auto latest = config::Config::instance().profiles().latest_arc();
  if (latest && latest->current) {
    std::string uid = *latest->current;
    core::CoreApiClientAdapter api(&cmd::mihomo_unchecked());
    auto node = core::CoreManager::instance().snapshot_proxy_group_now(api);
    if (node && !node->empty()) {
      config::Config::instance().profiles().edit_draft([&](config::IProfiles& p) {
        if (!p.items) return;
        for (auto& item : *p.items) {
          if (item.uid && *item.uid == uid) {
            item.selected = std::vector<config::PrfSelected>{
                config::PrfSelected{"PROXY", *node}};
            break;
          }
        }
      });
      (void)config::Config::instance().profiles().data_arc()->save_file();
    }
  }

  // 2. Prepare exit
  prepare_exit();

  // 3. Parallel resource cleanup
  bool cleanup_ok = clean_async();

  // 4. Spawn fresh instance
  wchar_t exe[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring cmd = GetCommandLineW();

  bool spawned = false;
  if (hooks_.launch_process) {
    spawned = hooks_.launch_process(exe, cmd);
  } else {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');
    spawned = CreateProcessW(exe, cmd_buf.data(), nullptr, nullptr, FALSE,
                             0, nullptr, nullptr, &si, &pi) != 0;
    if (spawned) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
  }

  // Step 7 flush logs
  if (hooks_.flush_logs) hooks_.flush_logs();

  state_.store(static_cast<uint8_t>(LifecycleState::Exited), std::memory_order_release);

  // 5. Terminate current process
  if (hooks_.exit_process) {
    hooks_.exit_process(cleanup_ok ? 0 : 1);
  }

  return spawned && cleanup_ok;
}

void Lifecycle::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.store(static_cast<uint8_t>(LifecycleState::Normal), std::memory_order_release);
  is_cleaning_up_.store(false);
  events::EventBus::instance().set_lightweight_filter(false);
  window_ = nullptr;
  tray_ = nullptr;
  reset_hooks();
}

}  // namespace clashwin::app
