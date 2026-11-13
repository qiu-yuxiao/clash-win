#include "core/monitor.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <format>
#include <fstream>
#include <future>

#include "core/core_manager.h"
#include "events/event_bus.h"
#include "util/log.h"
#include "util/dirs.h"
#include "yaml-cpp/yaml.h"

#pragma comment(lib, "ws2_32.lib")

namespace clashwin::core {

namespace {

bool ensure_wsa_started() {
  static bool wsa_ok = [] {
    WSADATA w{};
    return WSAStartup(MAKEWORD(2, 2), &w) == 0;
  }();
  return wsa_ok;
}

bool tcp_connect_ipv4_ms(const char* ip, uint16_t port, int timeout_ms) {
  if (!ip || timeout_ms <= 0) return false;
  (void)ensure_wsa_started();

  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;

  u_long nonblock = 1;
  ::ioctlsocket(s, FIONBIO, &nonblock);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (::inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
    ::closesocket(s);
    return false;
  }

  if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
    ::closesocket(s);
    return true;
  }

  const int wsa_err = ::WSAGetLastError();
  if (wsa_err != WSAEWOULDBLOCK) {
    ::closesocket(s);
    return false;
  }

  fd_set wset, eset;
  FD_ZERO(&wset);
  FD_SET(s, &wset);
  FD_ZERO(&eset);
  FD_SET(s, &eset);
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  const int ready = ::select(0, nullptr, &wset, &eset, &tv);
  if (ready <= 0 || FD_ISSET(s, &eset) || !FD_ISSET(s, &wset)) {
    ::closesocket(s);
    return false;
  }

  int so_err = 0;
  int len = sizeof(so_err);
  const bool ok =
      (::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_err),
                    &len) == 0 &&
       so_err == 0);
  ::closesocket(s);
  return ok;
}

bool dns_lookup_with_timeout_ms(const std::string& host, uint16_t port,
                                int timeout_ms) {
  if (host.empty() || timeout_ms <= 0) return false;
  (void)ensure_wsa_started();

  auto prom = std::make_shared<std::promise<bool>>();
  auto fut = prom->get_future();
  const std::string port_str = std::to_string(port);
  std::thread([prom, host, port_str] {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const bool ok =
        (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) == 0 &&
         res != nullptr);
    if (res) ::freeaddrinfo(res);
    try {
      prom->set_value(ok);
    } catch (...) {
    }
  }).detach();

  if (fut.wait_for(std::chrono::milliseconds(timeout_ms)) ==
      std::future_status::ready) {
    return fut.get();
  }
  return false;
}

}  // namespace

bool CoreMonitor::tcp_race_connect_ipv4(const std::vector<const char*>& ips,
                                        uint16_t port, int timeout_ms) {
  if (ips.empty() || timeout_ms <= 0) return false;
  (void)ensure_wsa_started();

  std::vector<SOCKET> pending;
  pending.reserve(ips.size());
  struct SocketCleanup {
    std::vector<SOCKET>& socks;
    ~SocketCleanup() {
      for (SOCKET s : socks) {
        if (s != INVALID_SOCKET) ::closesocket(s);
      }
    }
  } cleanup{pending};

  for (const char* ip : ips) {
    if (!ip || *ip == '\0') continue;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip, &addr.sin_addr) != 1) continue;

    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) continue;

    u_long nonblock = 1;
    ::ioctlsocket(s, FIONBIO, &nonblock);

    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
      ::closesocket(s);
      return true;
    }
    if (::WSAGetLastError() == WSAEWOULDBLOCK) {
      pending.push_back(s);
    } else {
      ::closesocket(s);
    }
  }

  if (pending.empty()) return false;

  const uint64_t deadline = ::GetTickCount64() + static_cast<uint64_t>(timeout_ms);
  while (!pending.empty()) {
    const uint64_t now = ::GetTickCount64();
    if (now >= deadline) break;
    const uint64_t rem_ms = deadline - now;

    fd_set wset, eset;
    FD_ZERO(&wset);
    FD_ZERO(&eset);
    for (SOCKET s : pending) {
      FD_SET(s, &wset);
      FD_SET(s, &eset);
    }
    timeval tv{};
    tv.tv_sec = static_cast<long>(rem_ms / 1000);
    tv.tv_usec = static_cast<long>((rem_ms % 1000) * 1000);

    const int ready = ::select(0, nullptr, &wset, &eset, &tv);
    if (ready <= 0) break;

    for (auto it = pending.begin(); it != pending.end();) {
      SOCKET s = *it;
      const bool writable = FD_ISSET(s, &wset) != 0;
      const bool errored = FD_ISSET(s, &eset) != 0;
      if (!writable && !errored) {
        ++it;
        continue;
      }
      int so_err = 0;
      int len = sizeof(so_err);
      const bool ok =
          writable && !errored &&
          (::getsockopt(s, SOL_SOCKET, SO_ERROR,
                        reinterpret_cast<char*>(&so_err), &len) == 0 &&
           so_err == 0);
      ::closesocket(s);
      it = pending.erase(it);
      if (ok) return true;
    }
  }
  return false;
}

CoreMonitor& CoreMonitor::instance() {
  static CoreMonitor s;
  return s;
}

void CoreMonitor::set_hooks(Hooks h) {
  std::lock_guard<std::mutex> lock(m_);
  hooks_ = std::move(h);
}

void CoreMonitor::set_params(Params p) {
  std::lock_guard<std::mutex> lock(m_);
  params_ = p;
}

int CoreMonitor::current_interval_secs() const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_));
  if (hooks_.network_online && !hooks_.network_online()) {
    return params_.offline_interval_secs;
  }
  if (api_fail_count_.load() > 0 || node_fail_count_.load() > 0) {
    return params_.retry_interval_secs;
  }
  return params_.normal_interval_secs;
}

void CoreMonitor::run_supervised_daemon(
    const char* task_name, const std::function<bool()>& is_stopping,
    const std::function<void(uint32_t)>& backoff_sleep_ms,
    const std::function<void()>& on_reset,
    const std::function<void()>& daemon_body) {
  const char* label = (task_name && *task_name) ? task_name : "CoreMonitor";
  while (!is_stopping || !is_stopping()) {
    try {
      if (daemon_body) daemon_body();
      break;
    } catch (const std::exception& ex) {
      CWLOG_ERROR(std::format(
          "{}: [后台监测] 守护循环未预期异常（{}），重置状态并于 {}ms 后拉起恢复",
          label, ex.what(), kSupervisorRestartDelayMs));
    } catch (...) {
      CWLOG_ERROR(std::format(
          "{}: [后台监测] 守护循环未知异常，重置状态并于 {}ms 后拉起恢复",
          label, kSupervisorRestartDelayMs));
    }
    if (on_reset) on_reset();
    if (is_stopping && is_stopping()) break;
    if (backoff_sleep_ms) {
      backoff_sleep_ms(kSupervisorRestartDelayMs);
    } else {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(kSupervisorRestartDelayMs));
    }
  }
}

void CoreMonitor::reset_runtime_loop_state() {
  api_fail_count_.store(0);
  node_fail_count_.store(0);
  auto_select_fail_count_.store(0);
  api_dead_heal_fail_count_.store(0);
  was_offline_.store(false);
  last_heal_ms_.store(0);
  last_tick_ms_.store(0);
  last_node_change_ms_.store(0);
  has_node_change_.store(false);
  manual_switch_wake_.store(false);
  manual_grace_unmatched_.store(false);
  {
    std::lock_guard<std::mutex> lock(manual_m_);
    pending_manual_switch_ms_ = -1;
  }
  {
    std::lock_guard<std::mutex> lock(last_node_m_);
    last_active_node_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(health_m_);
    last_notified_health_ = "network_health::ok";
  }
}

void CoreMonitor::start() {
  if (running_.exchange(true)) return;
  worker_ = std::thread([this] {
    bool is_first_run = true;
    run_supervised_daemon(
        "CoreMonitor",
        [this] { return !running_.load(); },
        [this](uint32_t ms) {
          std::unique_lock<std::mutex> lock(m_);
          cv_.wait_for(lock, std::chrono::milliseconds(ms),
                       [this] { return !running_.load(); });
        },
        [this] { reset_runtime_loop_state(); },
        [this, &is_first_run] {
          while (running_.load()) {
            if (is_first_run) {
              is_first_run = false;
              // 首次启动时等待内核就绪
              for (int i = 0; i < 50 && running_.load(); ++i) {
                if (hooks_.api_alive && hooks_.api_alive()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
              }
              // 若无窗口可见（托盘启动），执行一次初始化自动选点
              // （← monitor.rs:1236-1247 v3.2.5 BUG-316：若系看门狗无感挂载存活服务内核，
              //  跳过启动自动重选，避免打断用户正在进行的下载/通话）
              if (hooks_.is_headless && hooks_.is_headless()) {
                if (CoreManager::instance().take_reattached_existing_core()) {
                  CWLOG_INFO(
                      "CoreMonitor: [后台监测] 伴生看门狗已无感挂载运行中内核，延续现有出口节点健康守护");
                } else {
                  std::string uid =
                      hooks_.current_uid ? hooks_.current_uid() : "";
                  if (!uid.empty() && hooks_.auto_select_trigger) {
                    hooks_.auto_select_trigger();
                  }
                }
              }
            }
            int interval = current_interval_secs();
            // 分片等待（stop/kick 即时退出；manual_switch_wake 仅吸收宽限期时间戳后继续休眠）
            for (int waited = 0; running_.load() && waited < interval;
                 waited += 1) {
              std::unique_lock<std::mutex> lock(m_);
              cv_.wait_for(lock, std::chrono::seconds(1), [this] {
                return !running_.load() || kick_.load() ||
                       manual_switch_wake_.load();
              });
              if (!running_.load()) break;
              if (manual_switch_wake_.exchange(false)) {
                const int64_t now =
                    hooks_.now_ms ? hooks_.now_ms()
                                  : static_cast<int64_t>(GetTickCount64());
                absorb_pending_manual_switch(now);
              }
              if (kick_.exchange(false)) break;
            }
            if (!running_.load()) break;
            if (hooks_.on_guardian_tick) hooks_.on_guardian_tick();
            tick_once();
          }
        });
  });
}

void CoreMonitor::stop() {
  if (!running_.exchange(false)) return;
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void CoreMonitor::kick() {
  kick_.store(true);
  cv_.notify_all();
}

void CoreMonitor::notify_manual_node_switched() {
  int64_t now = 0;
  {
    std::lock_guard<std::mutex> lock(m_);
    now = hooks_.now_ms ? hooks_.now_ms()
                        : static_cast<int64_t>(GetTickCount64());
  }
  {
    std::lock_guard<std::mutex> lock(manual_m_);
    pending_manual_switch_ms_ = now;
  }
  manual_switch_wake_.store(true);
  cv_.notify_all();
}

void CoreMonitor::absorb_pending_manual_switch(int64_t fallback_now_ms) {
  int64_t ts = -1;
  {
    std::lock_guard<std::mutex> lock(manual_m_);
    ts = pending_manual_switch_ms_;
    pending_manual_switch_ms_ = -1;
  }
  if (ts < 0) return;
  const int64_t effective_ts = (ts >= 0) ? ts : fallback_now_ms;
  last_node_change_ms_.store(effective_ts);
  has_node_change_.store(true);
  manual_grace_unmatched_.store(true);
  node_fail_count_.store(0);
  CWLOG_INFO("CoreMonitor: 吸收前端手动节点切换事件，立即刷新 20s 切换宽限期");
}

void CoreMonitor::emit_network_health_notice(const std::string& health_msg) {
  {
    std::lock_guard<std::mutex> lock(health_m_);
    if (last_notified_health_ == health_msg) return;
    last_notified_health_ = health_msg;
  }
  const char* status =
      (health_msg == "network_health::ok") ? "info" : "warning";
  events::EventBus::notice_message(status, health_msg);
}

std::string CoreMonitor::last_notified_health_for_test() const {
  std::lock_guard<std::mutex> lock(health_m_);
  return last_notified_health_;
}

void CoreMonitor::record_server_failure(const std::string& server) {
  int64_t now = hooks_.now_ms ? hooks_.now_ms()
                              : static_cast<int64_t>(GetTickCount64());
  std::lock_guard<std::mutex> lock(failed_m_);
  failed_servers_[server] = now;
}

void CoreMonitor::clear_server_failures() {
  std::lock_guard<std::mutex> lock(failed_m_);
  failed_servers_.clear();
}

std::map<std::string, std::string> CoreMonitor::load_node_server_map() {
  std::map<std::string, std::string> server_map;
  std::wstring path = util::dirs::clash_mini_path();
  std::ifstream fin(path, std::ios::binary);
  if (!fin.is_open()) return server_map;
  std::string content((std::istreambuf_iterator<char>(fin)),
                       std::istreambuf_iterator<char>());
  try {
    YAML::Node root = YAML::Load(content);
    if (root && root["proxies"] && root["proxies"].IsSequence()) {
      for (const auto& px : root["proxies"]) {
        if (px.IsMap() && px["name"] && px["server"]) {
          std::string name = px["name"].as<std::string>("");
          std::string server = px["server"].as<std::string>("");
          if (!name.empty() && !server.empty()) {
            server_map[name] = server;
          }
        }
      }
    }
  } catch (...) {
  }
  return server_map;
}

std::vector<std::string> CoreMonitor::snapshot_failed_servers() {
  int64_t now = hooks_.now_ms ? hooks_.now_ms()
                              : static_cast<int64_t>(GetTickCount64());
  std::vector<std::string> out;
  std::lock_guard<std::mutex> lock(failed_m_);
  for (auto it = failed_servers_.begin(); it != failed_servers_.end();) {
    if (now - it->second >= params_.failed_server_cooldown_ms) {
      it = failed_servers_.erase(it);  // 冷却过期移除
    } else {
      out.push_back(it->first);
      ++it;
    }
  }
  return out;
}

CoreMonitor::SelfHealAction CoreMonitor::evaluate_self_heal_counters(
    int& auto_select_fail_count, int& api_dead_heal_fail_count,
    bool api_alive, int restart_threshold, int alert_threshold) {
  if (auto_select_fail_count >= alert_threshold) {
    auto_select_fail_count = 0;
    api_dead_heal_fail_count = 0;
    return SelfHealAction::FireAlert;
  }
  if (api_dead_heal_fail_count >= restart_threshold) {
    api_dead_heal_fail_count = 0;
    if (!api_alive) {
      return SelfHealAction::RestartCore;
    }
  }
  return SelfHealAction::None;
}

CoreMonitor::SelfHealAction CoreMonitor::complete_restart_core_attempt(
    bool restart_ok, int& auto_select_fail_count,
    int& api_dead_heal_fail_count) {
  if (restart_ok) {
    return SelfHealAction::None;
  }
  auto_select_fail_count = 0;
  api_dead_heal_fail_count = 0;
  return SelfHealAction::FireAlert;
}

CoreMonitor::Action CoreMonitor::heal(const std::string& uid) {
  Hooks h;
  Params p;
  {
    std::lock_guard<std::mutex> lock(m_);
    h = hooks_;
    p = params_;
  }
  int64_t now = h.now_ms ? h.now_ms()
                         : static_cast<int64_t>(GetTickCount64());
  int64_t last = last_heal_ms_.load();
  if (last != 0 && now - last < p.heal_cooldown_ms) {
    CWLOG_INFO(std::format(
        "CoreMonitor: 自愈选点在 {} 秒冷却中（已过 {} 秒），跳过本次",
        p.heal_cooldown_ms / 1000, (now - last) / 1000));
    return Action::Idle;
  }
  last_heal_ms_.store(now);
  if (!h.auto_select_trigger || uid.empty()) return Action::Idle;

  auto r = h.auto_select_trigger();
  if (r.is_ok() && r.value()) {
    // 自愈成功：清零双计数并广播恢复健康（BUG-318 / BUG-359）
    api_fail_count_.store(0);
    node_fail_count_.store(0);
    auto_select_fail_count_.store(0);
    api_dead_heal_fail_count_.store(0);
    emit_network_health_notice("network_health::ok");
    return Action::Healed;
  }
  if ((r.is_ok() && !r.value()) ||
      (r.is_err() && r.error().message == "AUTO_SELECT_BUSY")) {
    // BUSY（AutoSelect 占用）→ 不计入失败、回退冷却时间戳允许下次重试
    last_heal_ms_.store(last);
    return Action::Idle;
  }

  // 自愈失败：递增双计数器 + 发射全节点不可达通知（BUG-318 / BUG-359）
  int auto_fails = auto_select_fail_count_.fetch_add(1) + 1;
  int api_dead_fails = api_dead_heal_fail_count_.fetch_add(1) + 1;
  emit_network_health_notice("network_health::all_dead");

  const bool need_api_check =
      (auto_fails < p.alert_fail_threshold &&
       api_dead_fails >= p.heal_fail_threshold);
  const bool api_ok =
      need_api_check ? (h.api_alive ? h.api_alive() : true) : true;
  if (need_api_check && api_ok) {
    CWLOG_WARN(std::format(
        "CoreMonitor: 连续 {} 次自愈选点失败但内核 API 可达（保留告警计数={}），跳过内核重启",
        api_dead_fails, auto_fails));
  }

  const SelfHealAction decision = evaluate_self_heal_counters(
      auto_fails, api_dead_fails, api_ok, p.heal_fail_threshold,
      p.alert_fail_threshold);
  auto_select_fail_count_.store(auto_fails);
  api_dead_heal_fail_count_.store(api_dead_fails);

  if (decision == SelfHealAction::FireAlert) {
    CWLOG_WARN("CoreMonitor: 连续 5 次自愈选点失败，触发系统网络警报");
    if (h.alert) {
      h.alert(
          L"Clash Win - 网络警报",
          L"后台自动优选节点连续 5 次失败，当前所有代理节点均已失效，无法正常连接网络。\n\n请检查您的网络连接或节点订阅状态。");
    }
    return Action::Alerted;
  }

  if (decision == SelfHealAction::RestartCore) {
    CWLOG_WARN(std::format(
        "CoreMonitor: 连续 {} 次自愈选点失败且内核 API 不可达，尝试重启内核（当前告警计数={}）",
        p.heal_fail_threshold, auto_fails));
    const bool restart_ok = h.restart_core && h.restart_core().is_ok();
    const SelfHealAction post = complete_restart_core_attempt(
        restart_ok, auto_fails, api_dead_fails);
    auto_select_fail_count_.store(auto_fails);
    api_dead_heal_fail_count_.store(api_dead_fails);
    if (restart_ok) {
      return Action::RestartedOnHealFail;
    }
    if (post == SelfHealAction::FireAlert) {
      CWLOG_ERROR("CoreMonitor: 重启内核失败，立即升级为终态告警并重置本轮计数");
      if (h.alert) {
        h.alert(
            L"Clash Win - 网络警报",
            L"后台检测到代理内核通信异常且自动重启内核失败，当前无法正常恢复代理连接。\n\n请检查系统服务权限或手动重启 Clash Win。");
      }
      return Action::Alerted;
    }
  }

  return Action::Idle;
}

void CoreMonitor::reset_state() {
  reset_runtime_loop_state();
  std::lock_guard<std::mutex> lock(failed_m_);
  failed_servers_.clear();
}

// ★ §5.1 切换宽限期判定（← Mini monitor.rs:1383-1396，v3.2.4 BUG-315 = 20s）
//   切换后 20s 内：失败判定只记日志、不计入连续失败次数。
bool CoreMonitor::in_switch_grace() const {
  if (!has_node_change_.load()) return false;
  int64_t changed = last_node_change_ms_.load();
  int64_t now = hooks_.now_ms ? hooks_.now_ms()
                              : static_cast<int64_t>(GetTickCount64());
  int64_t grace;
  {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_));
    grace = params_.switch_grace_ms;
  }
  return (now - changed) < grace;
}

CoreMonitor::Action CoreMonitor::tick_once() {
  Hooks h;
  Params p;
  {
    std::lock_guard<std::mutex> lock(m_);
    h = hooks_;
    p = params_;
  }
  const auto now_fn = [&]() -> int64_t {
    return h.now_ms ? h.now_ms() : static_cast<int64_t>(GetTickCount64());
  };
  last_tick_ms_.store(now_fn());

  // BUG-336 Checkpoint #1：任何提前返回（进程未跑 / 物理断网 / API 失败）前先吸收手动切换信号
  absorb_pending_manual_switch(now_fn());

  // 1. 进程级直检：进程死 → 立即重启
  if (h.core_alive && !h.core_alive()) {
    CWLOG_WARN("CoreMonitor: 内核进程静默退出（无退出日志）→ 自动重启");
    if (h.restart_core && h.restart_core().is_ok()) return Action::Restarted;
    return Action::Idle;
  }

  // 2. 物理网络离线：跳过检测并通知前端离线横幅（BUG-359）
  bool online = h.network_online ? h.network_online() : true;
  if (!online) {
    was_offline_.store(true);
    emit_network_health_notice("network_health::offline");
    return Action::Idle;
  }
  // 离线 → 在线转移：清零计数并自愈
  if (was_offline_.exchange(false)) {
    api_fail_count_.store(0);
    node_fail_count_.store(0);
    std::string uid = h.current_uid ? h.current_uid() : std::string();
    if (!uid.empty()) return heal(uid);
    emit_network_health_notice("network_health::ok");
    return Action::Idle;
  }

  // 3. API 存活：连续 3 次不可达 → 自愈
  bool api_ok = h.api_alive ? h.api_alive() : true;
  if (!api_ok) {
    int fails = api_fail_count_.fetch_add(1) + 1;
    if (fails >= p.api_fail_threshold) {
      api_fail_count_.store(0);
      std::string uid = h.current_uid ? h.current_uid() : std::string();
      return heal(uid);
    }
    return Action::Idle;
  }
  api_fail_count_.store(0);

  // BUG-336 Checkpoint #2：观测 active_node_name 前再次吸收，若已被手动切换精确标记，
  // 后续观测到节点名变化时保留真实点击时间戳而不二次顺延宽限期。
  absorb_pending_manual_switch(now_fn());

  // ★ §5.1 活跃节点切换检测（← Mini monitor.rs:1358-1370）
  if (h.active_node_name) {
    const std::string active = h.active_node_name();
    bool changed = false;
    {
      std::lock_guard<std::mutex> lock(last_node_m_);
      if (active != last_active_node_) {
        last_active_node_ = active;
        changed = true;
      }
    }
    if (changed) {
      const bool had_manual_grace =
          manual_grace_unmatched_.exchange(false) && in_switch_grace();
      if (!had_manual_grace) {
        last_node_change_ms_.store(now_fn());
        has_node_change_.store(true);
      }
      node_fail_count_.store(0);
      CWLOG_INFO(std::format(
          "CoreMonitor: 活跃节点发生变化，起算 {}s 切换宽限期",
          p.switch_grace_ms / 1000));
    }
  }

  // ★ §5.4 第 3 道铁锁：假/广告节点 → 直接判需重选（← Mini monitor.rs:663-666）
  if (h.is_dummy_active) {
    if (h.is_dummy_active()) {
      CWLOG_WARN("CoreMonitor: 活跃节点是伪节点（广告/公告节点），触发重选");
      node_fail_count_.store(0);
      std::string uid = h.current_uid ? h.current_uid() : std::string();
      if (!uid.empty()) return heal(uid);
      return Action::Idle;
    }
  }

  // ★ §5.8 测速失真复核（← Mini monitor.rs:685-703）
  if (h.probe_metrics_with_fallback) {
    if (!h.probe_metrics_with_fallback()) {
      CWLOG_WARN(
          "CoreMonitor: 测速链路不可用（主/备测速地址均整体失败），本轮不判定节点死活");
      node_fail_count_.store(0);
      return Action::Idle;
    }
  }

  // 4. 节点健康：连续 2 次判死 → 自愈
  bool node_ok = h.node_healthy ? h.node_healthy() : true;
  if (!node_ok) {
    // BUG-336 Checkpoint #3：耗时拨测返回后立即吸收拨测期间发生的手动切节点信号
    absorb_pending_manual_switch(now_fn());
    if (in_switch_grace()) {
      CWLOG_INFO(
          "CoreMonitor: 活跃节点检测异常，当前处于切换宽限期，不计入连续失败");
      node_fail_count_.store(0);
      return Action::Idle;
    }
    int fails = node_fail_count_.fetch_add(1) + 1;
    if (fails >= p.node_dead_threshold) {
      node_fail_count_.store(0);
      std::string dead_node;
      if (h.active_node_name) {
        dead_node = h.active_node_name();
      }
      if (!dead_node.empty()) {
        auto server_map = load_node_server_map();
        auto it = server_map.find(dead_node);
        if (it != server_map.end() && !it->second.empty()) {
          record_server_failure(it->second);
          CWLOG_INFO("CoreMonitor: 活跃节点连续判死，服务器记入冷却避让: " + it->second);
        }
      }
      std::string uid = h.current_uid ? h.current_uid() : std::string();
      return heal(uid);
    }
    return Action::Idle;
  }
  const int prev_fails = node_fail_count_.exchange(0);
  if (prev_fails > 0) {
    CWLOG_INFO(std::format(
        "CoreMonitor: 活跃节点复检恢复正常（此前连续失败 {} 次），解除重试状态",
        prev_fails));
  }
  emit_network_health_notice("network_health::ok");
  return Action::Idle;
}

std::pair<std::string, uint16_t> CoreMonitor::resolve_probe_target(
    const std::string& test_url) {
  const auto fallback = []() -> std::pair<std::string, uint16_t> {
    return {"cp.cloudflare.com", 443};
  };

  size_t scheme_end = test_url.find("://");
  if (scheme_end == std::string::npos || scheme_end == 0) return fallback();

  std::string scheme = test_url.substr(0, scheme_end);
  for (char& ch : scheme) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  uint16_t default_port = 443;
  if (scheme == "http" || scheme == "ws") {
    default_port = 80;
  } else if (scheme == "https" || scheme == "wss") {
    default_port = 443;
  } else if (scheme == "ftp") {
    default_port = 21;
  }

  size_t auth_start = scheme_end + 3;
  size_t auth_end = test_url.find_first_of("/?#", auth_start);
  std::string authority = (auth_end == std::string::npos)
                              ? test_url.substr(auth_start)
                              : test_url.substr(auth_start, auth_end - auth_start);
  if (authority.empty()) return fallback();

  // Strip optional userinfo@
  size_t at_pos = authority.rfind('@');
  if (at_pos != std::string::npos) {
    authority = authority.substr(at_pos + 1);
  }
  if (authority.empty()) return fallback();

  auto parse_port = [&](const std::string& s, uint16_t& out_port) -> bool {
    if (s.empty()) return false;
    int val = 0;
    for (char c : s) {
      if (c < '0' || c > '9') return false;
      val = val * 10 + (c - '0');
      if (val > 65535) return false;
    }
    if (val <= 0) return false;
    out_port = static_cast<uint16_t>(val);
    return true;
  };

  // IPv6 literal: [::1]:8443 or [2606:4700::1]
  if (authority.front() == '[') {
    size_t close_br = authority.find(']');
    if (close_br == std::string::npos || close_br <= 1) return fallback();
    std::string ipv6 = authority.substr(1, close_br - 1);
    uint16_t port = default_port;
    if (close_br + 1 < authority.size()) {
      if (authority[close_br + 1] != ':') return fallback();
      if (!parse_port(authority.substr(close_br + 2), port)) return fallback();
    }
    return {ipv6, port};
  }

  // IPv4 or domain: host[:port]
  size_t colon = authority.find(':');
  if (colon != std::string::npos) {
    // Reject unbracketed multi-colon IPv6
    if (authority.find(':', colon + 1) != std::string::npos) return fallback();
    std::string host = authority.substr(0, colon);
    if (host.empty()) return fallback();
    uint16_t port = default_port;
    std::string port_part = authority.substr(colon + 1);
    if (!port_part.empty() && !parse_port(port_part, port)) return fallback();
    return {host, port};
  }

  return {authority, default_port};
}

bool CoreMonitor::probe_physical_network_with(uint64_t timeout_secs,
                                              const std::string& test_url,
                                              const PhysicalNetProbeOps& ops) {
  // BUG-317 / BUG-345：总预算保底 >= 2s；优先 3 路 TCP:53 并发竞速（上限 600ms），
  // 为局域网封禁 53 端口时的 DNS 解析兜底保留 >= 1400ms 充裕预算。
  const int64_t budget_ms = static_cast<int64_t>(
      std::max<uint64_t>(timeout_secs, kMinProbeBudgetSecs) * 1000);
  auto now_fn = [&]() -> int64_t {
    return ops.now_ms ? ops.now_ms() : static_cast<int64_t>(GetTickCount64());
  };
  const int64_t start_ms = now_fn();
  auto remaining_ms = [&]() -> int64_t {
    return budget_ms - (now_fn() - start_ms);
  };

  static constexpr const char* kPublicDnsIps[] = {
      "223.5.5.5",
      "119.29.29.29",
      "1.1.1.1",
  };
  if (ops.tcp_race_connect) {
    const int64_t rem = remaining_ms();
    if (rem <= 0) return false;
    const int race_ms =
        static_cast<int>(std::min<int64_t>(rem, kTcp53RaceTimeoutMs));
    const std::vector<const char*> ips(std::begin(kPublicDnsIps),
                                       std::end(kPublicDnsIps));
    if (ops.tcp_race_connect(ips, 53, race_ms)) {
      return true;
    }
  } else if (ops.tcp_connect) {
    const int64_t per_ip_cap = std::max<int64_t>(250, budget_ms / 4);
    for (const char* ip : kPublicDnsIps) {
      const int64_t rem = remaining_ms();
      if (rem <= 0) return false;
      const int step_ms = static_cast<int>(std::min<int64_t>(rem, per_ip_cap));
      if (ops.tcp_connect(ip, 53, step_ms)) {
        return true;
      }
    }
  }

  // ← monitor.rs:302-307：兜底——若特定局域网封禁出站 53 端口，回退至域名 DNS 查询
  const int64_t rem = remaining_ms();
  if (rem <= 0 || !ops.dns_lookup) return false;
  const auto [host, port] = resolve_probe_target(test_url);
  return ops.dns_lookup(host, port, static_cast<int>(rem));
}

bool CoreMonitor::is_physical_network_online(uint64_t timeout_secs,
                                             const std::string& test_url) {
  PhysicalNetProbeOps ops;
  ops.tcp_race_connect = tcp_race_connect_ipv4;
  ops.tcp_connect = tcp_connect_ipv4_ms;
  ops.dns_lookup = dns_lookup_with_timeout_ms;
  ops.now_ms = [] { return static_cast<int64_t>(GetTickCount64()); };
  return probe_physical_network_with(timeout_secs, test_url, ops);
}

bool CoreMonitor::ThrottledNetProbe::check(
    int64_t now_ms, const std::function<bool()>& probe_fn) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (has_checked_) {
      const int64_t interval =
          was_online_ ? kOnlineProbeIntervalMs : kOfflineProbeIntervalMs;
      if ((now_ms - last_check_ms_) < interval) {
        return was_online_;
      }
    }
  }
  const bool online = probe_fn ? probe_fn() : true;
  {
    std::lock_guard<std::mutex> lock(mu_);
    has_checked_ = true;
    was_online_ = online;
    last_check_ms_ = now_ms;
  }
  return online;
}

void CoreMonitor::ThrottledNetProbe::reset() {
  std::lock_guard<std::mutex> lock(mu_);
  has_checked_ = false;
  was_online_ = true;
  last_check_ms_ = 0;
}

bool CoreMonitor::wait_for_clash_ready(const ReadyHooks& h) {
  auto poll = [&](std::function<bool()> ready, int budget_ms) {
    if (!ready) return true;
    int64_t start = h.now_ms ? h.now_ms() : 0;
    for (;;) {
      if (ready()) return true;
      int64_t now = h.now_ms ? h.now_ms() : 0;
      if (now - start >= budget_ms) return false;
      if (h.sleep_ms) h.sleep_ms(300);
      else Sleep(300);
    }
  };
  if (!poll(h.api_alive, 30000)) return false;
  if (!poll(h.group_ready, 20000)) return false;
  if (!poll(h.network_online, 15000)) return false;
  return true;
}

}  // namespace clashwin::core
