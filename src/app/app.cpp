#include "app/app.h"

#include "app/lifecycle.h"
#include "app/scheme.h"
#include "app/singleton.h"
#include "cmd/command_service.h"
#include "cmd/cmd_support.h"
#include "config/config.h"
#include "config/prfitem.h"
#include "core/auto_select.h"
#include "core/autostart.h"
#include "core/core_manager.h"
#include "core/timer.h"
#include "events/event_bus.h"
#include "events/event_names.h"
#include "system/guard_monitor.h"
#include "system/sysproxy.h"
#include "system/tray.h"
#include "ui/theme/theme.h"
#include "ui/main_window.h"
#include "util/dirs.h"
#include "util/log.h"

#include <map>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <iphlpapi.h>
#include <thread>
#include <atomic>
#include <format>

#pragma comment(lib, "iphlpapi.lib")

namespace clashwin {

namespace {
// 单实例唤醒（与 ui/win/main_window.cpp 的 kAppWakeup 同值 WM_APP+1）
constexpr UINT kAppWakeupMsg = WM_APP + 1;

// T18：启动收敛视图（← sysopt.rs:132-143；clash mixed_port 兜底）
system::Sysopt::VergeView make_startup_sysproxy_view() {
  auto verge = config::Config::instance().verge().latest_arc();
  uint16_t clash_port = 0;
  auto mapping = config::Config::instance().clash().latest_arc()->mapping();
  if (mapping["mixed-port"]) {
    try {
      clash_port = static_cast<uint16_t>(mapping["mixed-port"].as<int>());
    } catch (const std::exception&) {
      clash_port = 0;
    }
  }
  return system::make_verge_view(*verge, clash_port,
                                 config::consts::kSingletonPort);
}
}  // namespace

App::App() = default;

App::~App() {
  // T18：退出清理（← census §74 quit：prepare_exit → reset_sysproxy →
  //   停内核 → 停 Timer/monitor；CoreManager 停止沿既有路径）
  (void)system::Sysopt::instance().reset_sysproxy();
  system::SysproxyGuard::instance().stop();
  system::CoreMonitor::instance().stop();

  if (singleton_server_) {
    singleton_server_->stop();
    singleton_server_.reset();
  }
  app::Lifecycle::instance().reset();
  if (tray_) {
    tray_->destroy();
    tray_.reset();
  }
  delete main_window_;  // WM_DESTROY 内完成几何落盘 + PostQuitMessage
  main_window_ = nullptr;
  if (wsa_inited_) WSACleanup();
  CWLOG_INFO("App 析构：资源已释放");
  Logger::instance().flush();
}

bool App::init() {
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
  wsa_inited_ = true;

  uint16_t port = consts::kSingletonPort;
  if (const char* env = getenv("CLASHWIN_SINGLETON_PORT")) {
    int p = atoi(env);
    if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
  }
  singleton_port_ = port;

  singleton_server_ = std::make_unique<app::SingletonServer>();
  bool bound = singleton_server_->start(
      singleton_port_,
      [this] {
        if (main_window_) {
          PostMessageW(main_window_->hwnd(), kAppWakeupMsg, 0, 0);
        }
      },
      [](const std::string& param) {
        std::thread([param] {
          (void)app::resolve_scheme(param);
        }).detach();
      });

  if (!bound) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    app::SingletonClient::notify_or_exit(singleton_port_, argc, argv, /*do_exit=*/true);
    if (argv) LocalFree(argv);
    return false;
  }

  if (!init_logger()) return false;
  CWLOG_INFO(std::format("App::init：单实例 bind {} 成功，首实例启动",
                         singleton_port_));

  // 判断静默启动（--silent 参数与配置）
  bool has_silent_arg = false;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv) {
    for (int i = 1; i < argc; ++i) {
      if (wcscmp(argv[i], L"--silent") == 0) {
        has_silent_arg = true;
        break;
      }
    }
    LocalFree(argv);
  }
  auto verge = config::Config::instance().verge().latest_arc();
  bool is_silent_start = verge ? verge->enable_silent_start.value_or(false) : false;
  bool should_silent = is_silent_start && has_silent_arg;

  // T12：主题服务初始化（缺省 retro-3d/light；皮肤/亮暗切换 T17 接 verge）
  ui::theme::ThemeService::instance().init();

  // T15：订阅定时更新（默认 1440min/过期补跑/失败 2min 重试/切换互斥）
  {
    auto& timer = core::ProfileTimer::instance();
    timer.set_intervals_provider([] {
      std::map<std::string, std::pair<uint64_t, int64_t>> m;
      auto latest = config::Config::instance().profiles().latest_arc();
      if (latest && latest->items) {
        for (const auto& it : *latest->items) {
          if (!it.uid) continue;
          uint64_t interval = 1440;
          if (it.option && it.option->update_interval)
            interval = *it.option->update_interval;
          int64_t next =
              it.updated ? *it.updated * 1000 +
                                static_cast<int64_t>(interval) * 60000
                         : 0;
          m[*it.uid] = {interval, next};
        }
      }
      return m;
    });
    timer.set_update_fn([](const std::string& uid) {
      auto r = cmd::CommandService::instance().invoke(
          "update_profile", {{"index", uid}});
      return r.is_ok();
    });
    timer.set_event_emitter([](const std::string& uid, bool is_start) {
      events::EventBus::instance().publish(
          is_start ? events::names::kProfileUpdateStarted
                   : events::names::kProfileUpdateCompleted,
          uid);
    });
    timer.start();
  }

  // T11：窗口壳移交 MainWindow（无框窗 + 标题栏 + 钳制 + Widget 树）
  main_window_ = new ui::win::MainWindow();
  if (!main_window_->create(/*show_immediately=*/!should_silent)) {
    CWLOG_ERROR("App::init：主窗口创建失败");
    return false;
  }

  // T19：系统托盘与生命周期管理装配
  tray_ = std::make_unique<system::Tray>();
  tray_->init(main_window_->hwnd());
  main_window_->set_tray(tray_.get());
  app::Lifecycle::instance().set_window(main_window_);
  app::Lifecycle::instance().set_tray(tray_.get());

  tray_->set_on_left_click([] {
    app::Lifecycle::instance().exit_lightweight_mode();
  });
  tray_->set_on_toggle_lite_mode([] {
    if (app::Lifecycle::instance().is_in_lightweight_mode()) {
      app::Lifecycle::instance().exit_lightweight_mode();
    } else {
      app::Lifecycle::instance().entry_lightweight_mode(true);
    }
  });
  tray_->set_on_exit([this] {
    if (main_window_) {
      DestroyWindow(main_window_->hwnd());
    } else {
      PostQuitMessage(0);
    }
  });

  bool tun_enabled = verge ? verge->enable_tun_mode.value_or(false) : false;
  bool sys_enabled = verge ? verge->enable_system_proxy.value_or(false) : false;
  tray_->update_icon(tun_enabled, sys_enabled);

  events::EventBus::instance().subscribe(
      events::names::kRefreshVerge,
      [this](const std::string&) {
        auto v = config::Config::instance().verge().latest_arc();
        if (v && tray_) {
          bool tun_on = v->enable_tun_mode.value_or(false);
          bool sys_on = v->enable_system_proxy.value_or(false);
          tray_->update_icon(tun_on, sys_on);
        }
      });

  if (should_silent) {
    app::Lifecycle::instance().entry_lightweight_mode(true);
  }

  // T22：开机自启对账与历史计划任务清理
  core::AutostartManager::instance().reconcile_on_startup();

  // ---------- T18：系统代理装配 + 启动收敛 + 内核守护 ----------
  {
    auto& sysopt = system::Sysopt::instance();
    sysopt.set_access(system::wininet_access());
    // ← sysproxy-rs GuardMonitor：重写回调（防篡改周期复原）
    system::SysproxyGuard::instance().set_rewrite(
        [](system::GuardType t, const system::SysProxyState& s,
           const system::AutoProxyState& a) {
          auto* acc = system::Sysopt::instance().access();
          if (!acc) return;
          if (t == system::GuardType::Sysproxy) (void)acc->write_sysproxy(s);
          else if (t == system::GuardType::Autoproxy)
            (void)acc->write_autoproxy(a);
        });
    auto view = make_startup_sysproxy_view();
    (void)sysopt.update_sysproxy(view);
    sysopt.refresh_guard(view);

    // ← module/monitor.rs 守护线程
    auto& monitor = system::CoreMonitor::instance();
    monitor.set_hooks({
        /*.api_alive =*/[] {
          return cmd::mihomo_unchecked().get_base_config().is_ok();
        },
        /*.core_alive =*/[] { return core::CoreManager::instance().sidecar_alive(); },
        /*.restart_core =*/
        [] {
          auto& mgr = core::CoreManager::instance();
          core::CoreApiClientAdapter api(&cmd::mihomo_unchecked());
          core::CoreOps ops;
          ops.start = [] {
            return core::CoreManager::instance().start_sidecar(
                util::dirs::clash_mini_path());
          };
          ops.stop = [] { return core::CoreManager::instance().stop(); };
          return mgr.restart_with_restore(api, ops);
        },
        /*.network_online =*/
        [] {
          ULONG size = 16 * 1024;
          std::vector<char> buf(size);
          auto* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
          if (GetAdaptersAddresses(AF_UNSPEC,
                                   GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
                                   nullptr, aa, &size) != NO_ERROR) {
            return false;
          }
          for (auto* a = aa; a; a = a->Next)
            if (a->IfType != IF_TYPE_SOFTWARE_LOOPBACK &&
                a->OperStatus == IfOperStatusUp)
              return true;
          return false;
        },
        /*.node_healthy =*/
        []() -> bool {
          auto& client = cmd::mihomo_unchecked();
          auto grp = client.get_group_by_name("PROXY");
          if (grp.is_err() || !grp.value().now || grp.value().now->empty()) return true;
          const std::string& active = *grp.value().now;
          if (active == "DIRECT" || active == "REJECT") return true;
          auto px = client.get_proxy_by_name(active);
          if (px.is_err()) return false;
          if (!px.value().alive) return false;
          if (!px.value().history.empty()) {
            uint32_t delay = px.value().history.back().delay;
            if (delay < 30 || delay >= 5000) return false;
          }
          return true;
        },
        /*.auto_select_trigger =*/
        []() -> Result<bool> {
          auto latest = config::Config::instance().profiles().latest_arc();
          std::string uid = latest && latest->current ? *latest->current : "";
          if (uid.empty()) return Result<bool>::ok(false);
          auto out = core::AutoSelect::instance().trigger(uid, std::nullopt,
                                                          /*sort_type=*/1,
                                                          /*select=*/true,
                                                          /*skip_wait=*/false);
          if (out.busy) return Result<bool>::ok(false);
          return Result<bool>::ok(out.selected);
        },
        /*.alert =*/
        [](const std::wstring& title, const std::wstring& text) {
          MessageBoxW(nullptr, text.c_str(), title.c_str(),
                      MB_OK | MB_ICONWARNING);
        },
        /*.current_uid =*/
        [] {
          auto latest = config::Config::instance().profiles().latest_arc();
          return latest && latest->current ? *latest->current : std::string();
        },
        /*.now_ms =*/[] { return static_cast<int64_t>(GetTickCount64()); },
    });
    monitor.start();
  }

  CWLOG_INFO("App::init 完成：主窗口就绪");
  return true;
}

int App::run() { return ui::win::MainWindow::run(); }

// ---------- 日志 ----------

bool App::init_logger() {
  wchar_t exe_path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  std::wstring exe_dir(exe_path);
  size_t pos = exe_dir.find_last_of(L"\\/");
  if (pos != std::wstring::npos) exe_dir.resize(pos);
  Logger::instance().init(exe_dir);
  return true;
}

}  // namespace clashwin
