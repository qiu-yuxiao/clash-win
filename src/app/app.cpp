#include "app/app.h"

#include <windows.h>

#include "app/lifecycle.h"
#include "app/scheme.h"
#include "app/singleton.h"
#include "cmd/command_service.h"
#include "cmd/cmd_support.h"
#include "config/config.h"
#include "config/prfitem.h"
#include "config/verge.h"
#include "core/auto_select.h"
#include "core/autostart.h"
#include "core/core_manager.h"
#include "core/delay_manager.h"
#include "core/timer.h"
#include "events/event_bus.h"
#include "events/event_names.h"
#include "i18n/i18n.h"
#include "system/guard_monitor.h"
#include "system/sysproxy.h"
#include "system/tray.h"
#include "system/resources.h"
#include "system/signals.h"
#include "ui/theme/theme.h"
#include "ui/main_window.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/node.h"

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
// pac_port：PAC URL 端口，必须传单实例服务器运行时实际绑定端口
system::Sysopt::VergeView make_startup_sysproxy_view(uint16_t pac_port) {
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
  return system::make_verge_view(*verge, clash_port, pac_port);
}
}  // namespace

App::App() = default;

App::~App() {
  system::SignalHandler::instance().uninstall();
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

  uint16_t port = config::consts::kSingletonPort;
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

  bool port_taken_by_other = false;
  if (!bound) {
    // bind 失败：先探测端口上是否确实是 Clash Win 前代实例（/commands/visible
    // 返回 200 才算），确认后才唤醒并退出；若是其它程序占用则继续启动，
    // 绝不把别人的端口当成自己人、绝不静默退出。
    int probe = app::SingletonClient::send_request(singleton_port_, "/commands/visible");
    if (probe != 200) {
      port_taken_by_other = true;
    } else {
      int argc = 0;
      LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
      app::SingletonClient::notify_or_exit(singleton_port_, argc, argv, /*do_exit=*/true);
      if (argv) LocalFree(argv);
      return false;
    }
  }

  if (!init_logger()) return false;
  if (port_taken_by_other) {
    CWLOG_WARN(std::format(
        "App::init：端口 {} 被非 Clash Win 进程占用，跳过单实例服务继续启动",
        singleton_port_));
  } else {
    CWLOG_INFO(std::format("App::init：单实例 bind {} 成功，首实例启动",
                           singleton_port_));
  }

  // T25: 预置离线资源包释放（Country.mmdb, geosite.dat, ruleset/gfwlist.yaml）
  (void)system::init_resources(util::dirs::app_home_dir(),
                               util::dirs::app_resources_dir());

  // T23: 注册控制台信号与操作系统会话终止处理器
  system::SignalHandler::instance().install([](system::SignalType sig) {
    CWLOG_INFO(std::format("App: 捕获系统信号 {}，执行优雅退出序列", system::to_string(sig)));
    app::Lifecycle::instance().quit();
  });

  // T23: 装配内嵌服务器停止钩子
  {
    auto hooks = app::Lifecycle::instance().hooks();
    hooks.stop_server = [this] {
      if (singleton_server_) {
        singleton_server_->stop();
      }
    };
    app::Lifecycle::instance().set_hooks(hooks);
  }

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

  // F-D01 ← MF:services/preload.ts:62-97 preloadLanguage / MF:main.tsx:105-110
  //   bootstrap：初始语言优先级「缓存 → verge.language → 浏览器语言 → zh」，
  //   并在 createRoot/render 之前完成 initializeLanguage。
  //   Win 无 localStorage；verge.language 的出厂模板值即 system_language()
  //   （verge.cpp:31-48），等价覆盖后两级；下拉切换经 on_patch_verge 持久化。
  //   必须在 MainWindow::create（build_widget_tree）之前，否则首屏取不到译文。
  {
    const std::string lang =
        (verge && verge->language && !verge->language->empty())
            ? *verge->language
            : config::verge::system_language();
    if (!i18n::I18n::instance().init(
            util::dirs::exe_dir() + L"\\locales", lang)) {
      CWLOG_WARN("App::init：locales 未完整加载，缺失 key 将显示 key 原文");
    }
  }

  // B-D26 ← Mini: src-tauri/src/utils/dirs.rs:146-151 clash_latest_log()
  //   Service 模式内核日志在 <logs>/service，其余在 <logs>/sidecar。
  util::dirs::set_clash_log_service_provider([] {
    return core::CoreManager::instance().running_mode() ==
           core::RunningMode::Service;
  });

  // T12：主题服务初始化（缺省 retro-3d/light；皮肤/亮暗切换 T17 接 verge）
  ui::theme::ThemeService::instance().init();

  // T09：装配命令注册表（必须在任何 invoke 之前——抽屉/定时器/托盘命令链依赖）
  cmd::CommandService::instance().install_all();

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
  tray_->set_on_exit([] {
    app::Lifecycle::instance().quit();
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
    auto view = make_startup_sysproxy_view(app::current_singleton_port());
    (void)sysopt.update_sysproxy(view);
    sysopt.refresh_guard(view);

    // ← module/monitor.rs 守护线程
    auto& monitor = system::CoreMonitor::instance();
    monitor.set_hooks({
        /*.api_alive =*/[] {
          return cmd::mihomo_unchecked().get_base_config().is_ok();
        },
        /*.core_alive =*/[] { return core::CoreManager::instance().core_alive(); },
        /*.restart_core =*/
        [] {
          // ← Verge start_core_by_sidecar：先 generate_file(Run) 重生成运行时
          //   产物再起内核（档案增删改后守护拉起才不会用陈旧配置）
          auto outcome = cmd::run_enhance_pipeline();
          if (!outcome.is_valid()) {
            // 失败必须留痕：此前这里只有 WARN 且后续启动失败静默，
            // 导致“内核反复静默退出→自动重启”却查不到 enhance 的真实错误
            CWLOG_WARN("restart_core: enhance pipeline invalid: " + outcome.message);
          } else {
            CWLOG_INFO("restart_core: enhance pipeline ok（clash-win.yaml 已重生成）");
          }
          auto& mgr = core::CoreManager::instance();
          core::CoreApiClientAdapter api(&cmd::mihomo_unchecked());
          core::CoreOps ops;
          ops.start = [] {
            return core::CoreManager::instance().start_auto(
                util::dirs::clash_mini_path());
          };
          ops.stop = [] { return core::CoreManager::instance().stop(); };
          auto rr = mgr.restart_with_restore(api, ops);
          if (!rr.is_ok()) {
            CWLOG_WARN("restart_core: 内核重启失败: " + rr.error().message);
          }
          // ← Verge：内核起来后 handle.refresh_clash()，UI 据此重取 PROXY 组
          //   （否则启动期 fetch 失败后主页列表永远空）
          events::EventBus::refresh_clash();
          return rr;
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
          if (px.value().history.empty()) return false;
          uint32_t delay = px.value().history.back().delay;
          if (delay < core::kNodeDelayMinMs || delay >= core::kNodeDelayMaxMs) return false;
          return true;
        },
        /*.active_node_name =*/
        []() -> std::string {
          // ★ §5.1 切换宽限期的起算依据（← Mini monitor.rs:1358 读 active_node_name）
          auto grp = cmd::mihomo_unchecked().get_group_by_name("PROXY");
          if (grp.is_err() || !grp.value().now) return std::string();
          return *grp.value().now;
        },
        /*.is_dummy_active =*/
        []() -> bool {
          // ★ §5.4 第 3 道铁锁（← Mini monitor.rs:663-666）
          auto grp = cmd::mihomo_unchecked().get_group_by_name("PROXY");
          if (grp.is_err() || !grp.value().now || grp.value().now->empty()) return false;
          return util::is_dummy_node(*grp.value().now);
        },
        /*.probe_metrics_with_fallback =*/
        []() -> bool {
          // ★ §5.8 测速失真复核（← Mini monitor.rs:188-223 probe_metrics_group）
          //   主测速地址整体失败 → 备用地址复核；主备双双整体失败 → 链路不可用。
          auto& client = cmd::mihomo_unchecked();
          std::string main_url = "https://cp.cloudflare.com/generate_204";
          try {
            const auto& verge = *config::Config::instance().verge().latest_arc();
            main_url = verge.default_latency_test.value_or(main_url);
          } catch (...) {
          }
          auto r = client.delay_group("PROXY__METRICS", main_url,
                                      core::kNodeDelayMaxMs, 5000);
          if (r.is_ok()) {
            std::vector<std::pair<std::string, uint32_t>> vec;
            for (const auto& [name, ms] : r.value()) vec.emplace_back(name, ms);
            events::EventBus::backend_delay_results("PROXY", vec);
            return true;
          }
          CWLOG_WARN(
              "CoreMonitor: 主测速地址整体失败，改用备用地址复核: " + main_url);
          auto r2 = client.delay_group("PROXY__METRICS",
                                       "https://www.gstatic.com/generate_204",
                                       core::kNodeDelayMaxMs, 5000);
          if (r2.is_ok()) {
            CWLOG_INFO(
                "CoreMonitor: 备用测速地址可用 → 判定为主测速地址失真，节点侧正常");
            std::vector<std::pair<std::string, uint32_t>> vec;
            for (const auto& [name, ms] : r2.value()) vec.emplace_back(name, ms);
            events::EventBus::backend_delay_results("PROXY", vec);
            return true;
          }
          CWLOG_WARN("CoreMonitor: 主/备测速地址均整体失败 → 测速链路不可用");
          return false;
        },
        /*.auto_select_trigger =*/
        []() -> Result<bool> {
          auto latest = config::Config::instance().profiles().latest_arc();
          std::string uid = latest && latest->current ? *latest->current : "";
          if (uid.empty()) return Result<bool>::err(Error{1, "no active profile"});
          auto out = core::AutoSelect::instance().trigger(uid, std::nullopt,
                                                          /*sort_type=*/1,
                                                          /*select=*/true,
                                                          /*skip_wait=*/false);
          if (out.busy) return Result<bool>::ok(false);
          if (!out.selected) return Result<bool>::err(Error{2, "auto_select failed"});
          return Result<bool>::ok(true);
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
        /*.is_headless =*/
        [this] {
          return !main_window_ || !main_window_->hwnd() ||
                 !IsWindowVisible(main_window_->hwnd());
        },
        /*.now_ms =*/[] { return static_cast<int64_t>(GetTickCount64()); },
    });

    // ← clash.rs::start_core（启动收敛）：主动重生成运行时配置并拉起内核。
    //   此前这里只装配 CoreMonitor hooks，从不主动 start_core，只能等守护
    //   空转重试。一旦内核没在跑（首次启动/上次异常退出），clash-win.yaml
    //   就停留在旧内容（mixed-port: 7890、/proxies 无 PROXY 组），10811 永远
    //   不通。start_core 内部 = enhance pipeline → start_sidecar → refresh_clash。
    auto startup_core = cmd::CommandService::instance().invoke("start_core");
    if (!startup_core.is_ok()) {
      CWLOG_WARN("App::init：start_core 失败（内核未启动）: " +
                 startup_core.error().message);
    } else {
      CWLOG_INFO("App::init：start_core 完成（运行时配置已重生成、内核已拉起）");
    }

    monitor.start();
  }

  CWLOG_INFO("App::init 完成：主窗口就绪");
  return true;
}

int App::run() { return ui::win::MainWindow::run(); }

// ---------- 日志 ----------

bool App::init_logger() {
  // 【INSTALLFIX】perMachine 安装目录（Program Files）对非管理员不可写：
  // 日志必须落在 app_home_dir（%APPDATA%\<APPID>，← dirs.rs::app_logs_dir），
  // 否则 CreateFileW 静默失败 → 装机版完全没有主日志。
  Logger::instance().init(util::dirs::app_home_dir());
  return true;
}

}  // namespace clashwin
