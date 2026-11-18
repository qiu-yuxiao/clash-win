#include "app/app.h"

#include <windows.h>

#include "app/lifecycle.h"
#include "app/scheme.h"
#include "app/singleton.h"
#include "cmd/command_service.h"
#include "cmd/cmd_support.h"
#include "config/config.h"
#include "config/prfitem.h"
#include "config/proxy_head_state.h"
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
#include "system/coexist.h"
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
#include <ctime>
#include <sstream>
#include <iomanip>

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

  // ★ 互斥共存方针（用户 2026-11-09 改定，最高优先级；总纲 §5.2）：可同装不可同跑。
  // 启动即按 exe 安装目录路径强杀 Clash Mini 全部进程（含对方路径的服务运行实例），
  // 不删除对方磁盘上任何东西；本方自家进程（含前代实例）永不进入候选。
  // 放在单实例 bind 之前：Mini 在跑时必占 33335，先互斥再绑定。
  {
    const int killed = system::coexist::kill_other_processes();
    const bool svc_stopped = system::coexist::stop_foreign_service_if_running();
    // ★ 杀对方收尾：清掉对方遗留的 Wintun/Mihomo 孤儿网卡，避免本方开 TUN 时同名冲突断网。
    system::coexist::remove_orphan_wintun_adapters();
    if (killed > 0 || svc_stopped) {
      CWLOG_INFO(std::format(
          "App::init：互斥共存——终止 Clash Mini 进程 {} 个，服务停止 {}",
          killed, svc_stopped ? "是" : "否"));
    }
  }

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

  // ★ 单实例锁（1:1 对齐 Clash Mini utils/server.rs:check_singleton）：
  // 33335 是 Clash 专用单实例端口。bind 失败 = 端口已被占用 = 已有 Clash Win
  // （或 Mini 残留）实例持有。此时无论探测 /commands/visible 是否返回 200，
  // 都尽力唤醒对方并退出，绝不启动第二个实例。
  // 原实现在「探测非 200」（首实例重负载下 500ms 内未响应 / 端口被无响应进程占用）
  // 时误判为「其它程序占用」而继续启动，导致可双开 Clash Win 自身——与 Mini 行为相悖。
  // BUG-394：探测超时已对齐 Mini v3.5.2 的 1500ms（singleton.cpp:288）。
  if (!bound) {
    CWLOG_WARN(std::format(
        "App::init：单实例端口 {} 已被占用，判定存在其它实例，尝试唤醒后退出",
        singleton_port_));
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    // notify_or_exit 在端口可达时发送唤醒并 exit(0)；不可达时仅返回 false，
    // 故其后以 return false 兜底，确保本进程绝不成为第二个实例。
    app::SingletonClient::notify_or_exit(singleton_port_, argc, argv, /*do_exit=*/true);
    if (argv) LocalFree(argv);
    return false;
  }

  if (!init_logger()) return false;
  CWLOG_INFO(std::format("App::init：单实例 bind {} 成功，首实例启动",
                         singleton_port_));

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
  // BUG-395（Mini v3.5.2 lightweight.rs:has_real_profile_items /
  //   should_enter_silent_tray_on_launch）：只要开启静默启动且已存在至少一条
  //   remote/local 真实配置，无论 --silent 开机自启还是手动双击冷启动，一律
  //   静默进托盘不弹主窗；纯新用户（无任何真实订阅）仍弹窗展示首次引导。
  auto has_real_profile_items = []() -> bool {
    auto p = config::Config::instance().profiles().latest_arc();
    if (!p || !p->items) return false;
    for (const auto& it : *p->items) {
      if (it.itype && (*it.itype == "remote" || *it.itype == "local")) return true;
    }
    return false;
  };
  // has_silent_arg 区分开机自启(--silent)与手动冷启动：Mini v3.5.2 对后者额外弹
  // NIF_INFO 托盘气泡（notify_silent_running）；Win 当前统一静默驻留不弹窗，
  // 气泡提示属后续增量（见对齐遗留）。保留该标志供该增量接线。
  (void)has_silent_arg;
  bool should_silent = is_silent_start && has_real_profile_items();

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
          "update_profile", {{"index", uid}, {"auto_trigger", true}});
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
  if (!main_window_->create(/*show_immediately=*/!should_silent,
                            /*defer_render=*/should_silent)) {
    CWLOG_ERROR("App::init：主窗口创建失败");
    return false;
  }

  // T19：系统托盘与生命周期管理装配
  tray_ = std::make_unique<system::Tray>();
  tray_->init(main_window_->hwnd());
  main_window_->set_tray(tray_.get());
  app::Lifecycle::instance().set_window(main_window_);
  app::Lifecycle::instance().set_tray(tray_.get());

  tray_->set_on_left_click([this] {
    if (main_window_ && main_window_->should_tray_click_hide(
            app::Lifecycle::instance().is_in_lightweight_mode())) {
      main_window_->close_to_lightweight();
    } else {
      app::Lifecycle::instance().exit_lightweight_mode();
    }
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

    // ← module/monitor.rs 守护线程（v3.2.4 BUG-315：双频 TCP:53 + DNS 兜底物理网络探测）
    auto net_probe = std::make_shared<core::CoreMonitor::ThrottledNetProbe>();
    auto& monitor = system::CoreMonitor::instance();
    core::CoreMonitor::Hooks mh;
    mh.api_alive = [] {
      return cmd::mihomo_unchecked().get_base_config().is_ok();
    };
    mh.core_alive = [] { return core::CoreManager::instance().core_alive(); };
    mh.restart_core = [] {
      auto outcome = cmd::run_enhance_pipeline();
      if (!outcome.is_valid()) {
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
      events::EventBus::refresh_clash();
      return rr;
    };
    mh.network_online = [net_probe] {
      const int64_t now = static_cast<int64_t>(GetTickCount64());
      return net_probe->check(now, [] {
        std::string test_url = "https://cp.cloudflare.com/generate_204";
        try {
          const auto verge = config::Config::instance().verge().latest_arc();
          if (verge && verge->default_latency_test &&
              !verge->default_latency_test->empty()) {
            test_url = *verge->default_latency_test;
          }
        } catch (...) {
        }
        return core::CoreMonitor::is_physical_network_online(2, test_url);
      });
    };
    struct MonitorRuntimeState {
      std::mutex probe_mu;
      std::optional<core::CoreMonitor::ActiveTrafficTracker> traffic_tracker;
      std::optional<core::CoreMonitor::UiActiveSyncState> ui_sync;
      core::CoreMonitor::ActiveTrafficState last_traffic =
          core::CoreMonitor::ActiveTrafficState::Idle;
      int64_t last_active_verify_ms = -1;
      int64_t last_https_all_failed_ms = -1;
    };
    auto mstate = std::make_shared<MonitorRuntimeState>();

    auto parse_proxy_delay_age = [](const mihomo::ProxyItem& p)
        -> std::pair<std::optional<uint32_t>, std::optional<uint64_t>> {
      if (p.history.empty()) return {std::nullopt, std::nullopt};
      const uint32_t delay = p.history.back().delay;
      const std::string& t = p.history.back().time;
      std::optional<uint64_t> age;
      if (t.size() >= 19) {
        std::tm tm{};
        std::istringstream iss(t.substr(0, 19));
        iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (!iss.fail()) {
          const int64_t rec_sec = static_cast<int64_t>(_mkgmtime(&tm));
          const int64_t now_sec = static_cast<int64_t>(std::time(nullptr));
          age = static_cast<uint64_t>(now_sec > rec_sec ? (now_sec - rec_sec) : 0);
        }
      }
      return {delay, age};
    };

    auto read_current_filter_text = []() -> std::string {
      try {
        auto latest = config::Config::instance().profiles().latest_arc();
        if (!latest || !latest->current) return {};
        auto st = config::proxy_head_state::load();
        if (auto gs = config::proxy_head_state::get_group_state(
                st, *latest->current, "PROXY")) {
          return gs->filter_text;
        }
      } catch (...) {
      }
      return {};
    };

    auto run_https_verify_pass = [mstate](const std::vector<std::string>& candidates,
                                          const char* reason) {
      if (!util::has_pending_unverified_nodes(candidates)) return;
      const int64_t now_ms = static_cast<int64_t>(GetTickCount64());
      if (mstate->last_https_all_failed_ms >= 0) {
        const int64_t elapsed_secs =
            (now_ms - mstate->last_https_all_failed_ms) / 1000;
        if (core::CoreMonitor::is_https_verify_backoff_active(elapsed_secs)) {
          return;
        }
      }
      if (!core::CoreMonitor::is_physical_network_online(
              2, "https://cp.cloudflare.com/generate_204")) {
        return;
      }
      auto& client = cmd::mihomo_unchecked();
      std::map<std::string, uint32_t> delays;
      {
        std::lock_guard<std::mutex> lock(mstate->probe_mu);
        auto r1 = client.delay_group(
            "PROXY__METRICS", core::CoreMonitor::kVerifyHttpsUrlPrimary,
            core::kNodeDelayMaxMs, 5000);
        if (r1.is_ok() && !r1.value().empty()) {
          delays = std::move(r1.value());
        } else {
          auto r2 = client.delay_group(
              "PROXY__METRICS", core::CoreMonitor::kVerifyHttpsUrlFallback,
              core::kNodeDelayMaxMs, 5000);
          if (r2.is_err()) {
            mstate->last_https_all_failed_ms = now_ms;
            return;
          }
          delays = std::move(r2.value());
        }
      }
      std::vector<std::string> passed;
      for (const auto& [name, d] : delays) {
        if (d >= core::kNodeDelayMinMs && d < core::kNodeDelayMaxMs) {
          passed.push_back(name);
        }
      }
      if (passed.empty()) {
        mstate->last_https_all_failed_ms = now_ms;
        return;
      }
      mstate->last_https_all_failed_ms = -1;
      std::vector<std::string> failed;
      for (const auto& c : candidates) {
        if (std::find(passed.begin(), passed.end(), c) == passed.end() &&
            !util::is_dummy_node(c) && !util::is_confirmed_fake_node(c)) {
          failed.push_back(c);
        }
      }
      auto outcome = util::record_https_verify_results(passed, failed);
      if (outcome.newly_verified > 0 || outcome.newly_confirmed_fake > 0) {
        CWLOG_INFO(std::format(
            "[HTTPS验真/{}] 通过 {} (新验真 {}), 未通过 {}, 新确认软隔离(24h) {}",
            reason, passed.size(), outcome.newly_verified, failed.size(),
            outcome.newly_confirmed_fake));
      }
      if (outcome.newly_confirmed_fake > 0 &&
          !events::EventBus::instance().lightweight_filter()) {
        events::EventBus::refresh_proxies();
      }
    };

    mh.node_healthy = [parse_proxy_delay_age]() -> bool {
      auto& client = cmd::mihomo_unchecked();
      auto grp = client.get_group_by_name("PROXY");
      if (grp.is_err() || !grp.value().now || grp.value().now->empty())
        return true;
      const std::string& active = *grp.value().now;
      if (active == "DIRECT" || active == "REJECT") return true;
      auto px = client.get_proxy_by_name(active);
      if (px.is_err()) return false;
      auto [delay, age] = parse_proxy_delay_age(px.value());
      auto st = core::CoreMonitor::classify_node_status(px.value().alive, delay, age);
      return st.kind != core::CoreMonitor::NodeStatusKind::Dead;
    };
    mh.active_node_name = []() -> std::string {
      auto grp = cmd::mihomo_unchecked().get_group_by_name("PROXY");
      if (grp.is_err() || !grp.value().now) return std::string();
      return *grp.value().now;
    };
    mh.periodic_https_verify = [run_https_verify_pass]() {
      auto grp = cmd::mihomo_unchecked().get_group_by_name("PROXY");
      if (grp.is_err() || !grp.value().all) return;
      std::vector<std::string> members;
      for (const auto& n : *grp.value().all) {
        if (!util::is_dummy_node(n) && !util::is_confirmed_fake_node(n)) {
          members.push_back(n);
        }
      }
      run_https_verify_pass(members, "后台定期验真");
    };
    mh.persisted_exit_usable = [read_current_filter_text]() -> bool {
      auto grp = cmd::mihomo_unchecked().get_group_by_name("PROXY");
      if (grp.is_err()) return false;
      const auto& now = grp.value().now;
      const bool fake = now.has_value() && !now->empty() &&
                        util::is_confirmed_fake_node(*now);
      const std::vector<std::string>* all_ptr =
          grp.value().all ? &(*grp.value().all) : nullptr;
      return core::CoreMonitor::persisted_exit_is_usable(
          now, all_ptr, read_current_filter_text(), fake);
    };
    mh.auto_select_with_mode = [](bool select) -> Result<bool> {
      auto latest = config::Config::instance().profiles().latest_arc();
      std::string uid = latest && latest->current ? *latest->current : "";
      if (uid.empty()) return Result<bool>::err(Error{1, "no active profile"});
      auto out = core::AutoSelect::instance().trigger(
          uid, std::nullopt, /*sort_type=*/0, select, /*skip_wait_ready=*/false);
      if (out.busy) return Result<bool>::err(Error{-2, "AUTO_SELECT_BUSY"});
      if (out.preempted) {
        return Result<bool>::err(Error{-3, "AUTO_SELECT_MANUAL_PREEMPTED"});
      }
      return Result<bool>::ok(out.selected);
    };
    mh.confirm_dead_gate = [parse_proxy_delay_age](const std::string& dead_node) -> bool {
      auto px = cmd::mihomo_unchecked().get_proxy_by_name(dead_node);
      if (px.is_err()) return false;
      auto [delay, age] = parse_proxy_delay_age(px.value());
      auto st = core::CoreMonitor::classify_node_status(px.value().alive, delay, age);
      return core::CoreMonitor::should_proceed_with_self_heal(st);
    };
    mh.api_recovery_keep_alive = [parse_proxy_delay_age]() -> bool {
      auto& client = cmd::mihomo_unchecked();
      auto grp = client.get_group_by_name("PROXY");
      if (grp.is_err() || !grp.value().now || grp.value().now->empty()) {
        return false;
      }
      const std::string& active = *grp.value().now;
      if (active == "DIRECT" || active == "REJECT" ||
          util::is_dummy_node(active) || util::is_confirmed_fake_node(active)) {
        return false;
      }
      auto px = client.get_proxy_by_name(active);
      if (px.is_err()) return false;
      auto [delay, age] = parse_proxy_delay_age(px.value());
      auto st = core::CoreMonitor::classify_node_status(px.value().alive, delay, age);
      return core::CoreMonitor::should_keep_alive_on_api_recovery(true, st);
    };
    mh.is_dummy_active = [mstate, parse_proxy_delay_age, read_current_filter_text,
                          run_https_verify_pass]() -> bool {
      auto& client = cmd::mihomo_unchecked();
      auto grp = client.get_group_by_name("PROXY");
      if (grp.is_err() || !grp.value().now || grp.value().now->empty())
        return false;
      const std::string& active = *grp.value().now;
      if (util::is_dummy_node(active) || util::is_confirmed_fake_node(active)) {
        return true;
      }
      if (!core::CoreMonitor::should_allow_https_migration(mstate->last_traffic) ||
          util::is_verified_true_node(active)) {
        return false;
      }
      const int64_t now_ms = static_cast<int64_t>(GetTickCount64());
      if (mstate->last_active_verify_ms < 0 ||
          (now_ms - mstate->last_active_verify_ms) >=
              core::CoreMonitor::kActiveVerifyCooldownSecs * 1000) {
        mstate->last_active_verify_ms = now_ms;
        if (grp.value().all) {
          std::vector<std::string> members;
          for (const auto& n : *grp.value().all) {
            if (!util::is_dummy_node(n) && !util::is_confirmed_fake_node(n)) {
              members.push_back(n);
            }
          }
          run_https_verify_pass(members, "活跃节点即时验真");
        }
      }
      if (!util::has_verify_failure_node(active) || !grp.value().all) return false;
      auto proxies_res = client.get_proxies();
      if (proxies_res.is_err()) return false;
      const std::string filter = read_current_filter_text();
      for (const auto& n : *grp.value().all) {
        if (n == active || util::is_dummy_node(n) ||
            util::is_confirmed_fake_node(n) || !util::is_verified_true_node(n) ||
            !util::match_node_filter(n, filter)) {
          continue;
        }
        auto it = proxies_res.value().proxies.find(n);
        if (it == proxies_res.value().proxies.end()) continue;
        auto [d, age] = parse_proxy_delay_age(it->second);
        auto st = core::CoreMonitor::classify_node_status(it->second.alive, d, age);
        if (st.kind == core::CoreMonitor::NodeStatusKind::Alive) {
          CWLOG_INFO("CoreMonitor: 当前节点 " + active +
                     " 验真失败且子集内存在已验真健康节点，触发迁移");
          return true;
        }
      }
      return false;
    };
    mh.probe_metrics_with_fallback = [mstate, parse_proxy_delay_age]() -> bool {
      auto& client = cmd::mihomo_unchecked();
      auto grp = client.get_group_by_name("PROXY");
      if (grp.is_err() || !grp.value().now || grp.value().now->empty()) {
        return true;
      }
      const std::string& active = *grp.value().now;
      if (active == "DIRECT" || active == "REJECT") return true;

      const int64_t now_ms = static_cast<int64_t>(GetTickCount64());
      bool stall_cd = false;
      core::CoreMonitor::ActiveTrafficState traffic =
          core::CoreMonitor::ActiveTrafficState::Idle;
      if (auto conns_res = client.get_connections(3000); conns_res.is_ok()) {
        std::vector<std::tuple<std::string, uint64_t, uint64_t>> cur_conns;
        for (const auto& c : conns_res.value().connections) {
          if (std::find(c.chains.begin(), c.chains.end(), active) != c.chains.end()) {
            cur_conns.emplace_back(c.id, c.upload, c.download);
          }
        }
        auto [ts, scd] = core::CoreMonitor::update_active_traffic_state(
            mstate->traffic_tracker, active, cur_conns, now_ms);
        traffic = ts;
        stall_cd = scd;
        mstate->last_traffic = traffic;
      }

      auto proxies_res = client.get_proxies();
      if (proxies_res.is_err()) return false;
      auto active_it = proxies_res.value().proxies.find(active);
      if (active_it == proxies_res.value().proxies.end()) return true;

      auto [delay, age] = parse_proxy_delay_age(active_it->second);
      const auto active_status = core::CoreMonitor::classify_node_status(
          active_it->second.alive, delay, age);
      const bool need_probe = core::CoreMonitor::should_probe_active_node(
          active_status, age, traffic, stall_cd);

      if (!need_probe) {
        if (active_status.kind == core::CoreMonitor::NodeStatusKind::Alive &&
            !events::EventBus::instance().lightweight_filter()) {
          const std::string hist_time = active_it->second.history.empty()
                                            ? std::string()
                                            : active_it->second.history.back().time;
          const std::string* hist_ptr = hist_time.empty() ? nullptr : &hist_time;
          const bool health_not_ok =
              core::CoreMonitor::instance().last_notified_health_for_test() !=
              "network_health::ok";
          if (core::CoreMonitor::should_sync_alive_ui(
                  health_not_ok,
                  mstate->ui_sync ? &(*mstate->ui_sync) : nullptr, active,
                  hist_ptr) &&
              grp.value().all) {
            std::vector<std::pair<std::string, uint32_t>> fresh_alive;
            for (const auto& n : *grp.value().all) {
              if (util::is_dummy_node(n) || util::is_confirmed_fake_node(n)) continue;
              auto pit = proxies_res.value().proxies.find(n);
              if (pit == proxies_res.value().proxies.end()) continue;
              auto [nd, nage] = parse_proxy_delay_age(pit->second);
              auto nst = core::CoreMonitor::classify_node_status(
                  pit->second.alive, nd, nage);
              if (nst.kind == core::CoreMonitor::NodeStatusKind::Alive &&
                  nage.has_value() &&
                  *nage < static_cast<uint64_t>(
                              core::CoreMonitor::kPassiveTrafficMaxSkipSecs)) {
                fresh_alive.emplace_back(n, nst.delay_ms);
              }
            }
            if (!fresh_alive.empty()) {
              events::EventBus::backend_delay_results("PROXY", fresh_alive, true);
              events::EventBus::refresh_proxies();
              mstate->ui_sync = core::CoreMonitor::UiActiveSyncState{
                  active, hist_time.empty() ? std::nullopt : std::optional(hist_time),
                  true};
            }
          }
        }
        return true;
      }

      if (traffic == core::CoreMonitor::ActiveTrafficState::Stalled &&
          mstate->traffic_tracker.has_value()) {
        mstate->traffic_tracker->last_stall_probe_ms = now_ms;
      }

      std::string main_url = "https://cp.cloudflare.com/generate_204";
      try {
        const auto& verge = *config::Config::instance().verge().latest_arc();
        main_url = verge.default_latency_test.value_or(main_url);
      } catch (...) {
      }
      const std::string fallback_url = "https://www.gstatic.com/generate_204";
      std::map<std::string, uint32_t> probed_delays;
      bool distorted = false;
      bool link_ok = false;
      {
        std::lock_guard<std::mutex> lock(mstate->probe_mu);
        auto r = client.delay_group("PROXY__METRICS", main_url,
                                    core::kNodeDelayMaxMs, 5000);
        if (r.is_ok()) {
          probed_delays = std::move(r.value());
          link_ok = true;
          if (core::CoreMonitor::should_recheck_with_fallback(
                  probed_delays, {active})) {
            auto rfb = client.delay_group("PROXY__METRICS", fallback_url,
                                          core::kNodeDelayMaxMs, 5000);
            if (rfb.is_ok()) {
              probed_delays = core::CoreMonitor::merge_probe_delays(
                  std::move(probed_delays), rfb.value());
            } else if (probed_delays.empty()) {
              link_ok = false;
            }
          }
        } else {
          CWLOG_WARN("CoreMonitor: 主测速地址整体失败，改用备用地址复核: " + main_url);
          auto r2 = client.delay_group("PROXY__METRICS", fallback_url,
                                       core::kNodeDelayMaxMs, 5000);
          if (r2.is_ok()) {
            CWLOG_INFO("CoreMonitor: 备用测速地址可用 → 判定为主测速地址失真，本轮不判死");
            probed_delays = std::move(r2.value());
            distorted = true;
            link_ok = true;
          }
        }
      }

      if (!link_ok) {
        CWLOG_WARN("CoreMonitor: 主/备测速地址均整体失败 → 测速链路不可用");
        if (traffic == core::CoreMonitor::ActiveTrafficState::Stalled &&
            !events::EventBus::instance().lightweight_filter()) {
          events::EventBus::backend_delay_results(
              "PROXY", {{active, 0}}, /*from_probe=*/false);
          events::EventBus::refresh_proxies();
          mstate->ui_sync = core::CoreMonitor::UiActiveSyncState{
              active, std::nullopt, false};
        }
        return false;
      }

      const std::vector<std::string> all_members =
          grp.value().all.value_or(std::vector<std::string>{});
      auto display = core::CoreMonitor::build_probe_ui_display(
          all_members, probed_delays, distorted);
      if (!display.empty() && !events::EventBus::instance().lightweight_filter()) {
        events::EventBus::backend_delay_results("PROXY", display, true);
        events::EventBus::refresh_proxies();
        std::optional<std::string> active_hist_time;
        if (auto post_px = client.get_proxy_by_name(active);
            post_px.is_ok() && !post_px.value().history.empty()) {
          active_hist_time = post_px.value().history.back().time;
        }
        mstate->ui_sync = core::CoreMonitor::compute_next_ui_active_sync(
            active, display, active_hist_time,
            mstate->ui_sync ? &(*mstate->ui_sync) : nullptr);
      }
      return !distorted;
    };
    mh.auto_select_trigger = []() -> Result<bool> {
      auto latest = config::Config::instance().profiles().latest_arc();
      std::string uid = latest && latest->current ? *latest->current : "";
      if (uid.empty()) return Result<bool>::err(Error{1, "no active profile"});
      auto out = core::AutoSelect::instance().trigger(uid, std::nullopt,
                                                      /*sort_type=*/0,
                                                      /*select=*/true,
                                                      /*skip_wait_ready=*/false);
      if (out.busy) return Result<bool>::err(Error{-2, "AUTO_SELECT_BUSY"});
      if (out.preempted) {
        return Result<bool>::err(Error{-3, "AUTO_SELECT_MANUAL_PREEMPTED"});
      }
      if (!out.selected)
        return Result<bool>::err(Error{2, "auto_select failed"});
      return Result<bool>::ok(true);
    };
    mh.alert = [](const std::wstring& title, const std::wstring& text) {
      static std::atomic<bool> s_alert_dialog_open{false};
      bool expected = false;
      if (!s_alert_dialog_open.compare_exchange_strong(expected, true)) {
        CWLOG_INFO("CoreMonitor: 系统网络警报弹窗已在显示中，跳过重复弹窗");
        return;
      }
      std::thread([title, text] {
        MessageBoxW(nullptr, text.c_str(), title.c_str(),
                    MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        s_alert_dialog_open.store(false);
      }).detach();
    };
    mh.current_uid = [] {
      auto latest = config::Config::instance().profiles().latest_arc();
      return latest && latest->current ? *latest->current : std::string();
    };
    mh.is_headless = [this] {
      return !main_window_ || !main_window_->hwnd() ||
             !IsWindowVisible(main_window_->hwnd());
    };
    mh.now_ms = [] { return static_cast<int64_t>(GetTickCount64()); };
    monitor.set_hooks(std::move(mh));

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
