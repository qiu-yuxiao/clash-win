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
#include "core/delay_manager.h"
#include "events/event_bus.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/node.h"
#include "util/str_utils.h"
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

std::mutex g_flowing_mu;
int64_t g_last_flowing_traffic_ms = -1;

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
  has_heal_attempt_.store(false);
  last_tick_ms_.store(0);
  last_https_verify_ms_.store(-1);
  reset_flowing_traffic_state();
  manual_switch_epoch_.store(0, std::memory_order_release);
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
    std::lock_guard<std::mutex> lock(switch_rate_m_);
    recent_auto_switches_ms_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(brownout_m_);
    recent_brownout_episodes_ms_.clear();
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
              // 若无窗口可见（托盘启动），先按过滤边界恢复 persisted selected，再执行 Continuity-First 探测/选点
              if (hooks_.is_headless && hooks_.is_headless()) {
                std::string uid =
                    hooks_.current_uid ? hooks_.current_uid() : "";
                if (!uid.empty()) {
                  if (hooks_.restore_selected_nodes) {
                    hooks_.restore_selected_nodes(uid);
                  }
                  const bool keep_persisted =
                      hooks_.persisted_exit_usable &&
                      hooks_.persisted_exit_usable();
                  if (keep_persisted && hooks_.auto_select_with_mode) {
                    hooks_.auto_select_with_mode(/*select=*/false);
                  } else if (!keep_persisted && hooks_.auto_select_trigger) {
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
  manual_switch_epoch_.fetch_add(1, std::memory_order_acq_rel);
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
  {
    std::lock_guard<std::mutex> lock(brownout_m_);
    recent_brownout_episodes_ms_.clear();
  }
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

void CoreMonitor::fire_self_heal_alert(const Hooks& h) const {
  CWLOG_WARN("CoreMonitor: 连续 5 次自愈选点失败，触发系统网络警报");
  if (h.alert) {
    h.alert(
        L"Clash Win - 网络警报",
        L"后台自动优选节点连续 5 次失败，当前所有代理节点均已失效，无法正常连接网络。\n\n请检查您的网络连接或节点订阅状态。");
  }
}

void CoreMonitor::fire_core_restart_failed_alert(const Hooks& h) const {
  CWLOG_ERROR("CoreMonitor: 重启内核失败，立即升级为终态告警并重置本轮计数");
  if (h.alert) {
    h.alert(
        L"Clash Win - 网络警报",
        L"后台检测到代理内核通信异常且自动重启内核失败，当前无法正常恢复代理连接。\n\n请检查系统服务权限或手动重启 Clash Win。");
  }
}

bool CoreMonitor::is_mihomo_pipe_missing_confirmed(const Hooks& h) const {
  if (!h.expect_core_pipe_alive || !h.pipe_missing) return false;
  if (!h.expect_core_pipe_alive() || !h.pipe_missing()) return false;
  if (h.sleep_ms) {
    h.sleep_ms(kPipeMissingConfirmDelayMs);
  } else {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kPipeMissingConfirmDelayMs));
  }
  return h.expect_core_pipe_alive() && h.pipe_missing();
}

CoreMonitor::Action CoreMonitor::execute_core_restart_with_accounting(
    const Hooks& h, const std::string& reason_log, Action ok_action) {
  CWLOG_WARN(reason_log);
  const bool restart_ok = h.restart_core && h.restart_core().is_ok();
  int auto_fails = auto_select_fail_count_.load();
  int api_dead_fails = api_dead_heal_fail_count_.load();
  const SelfHealAction post =
      complete_restart_core_attempt(restart_ok, auto_fails, api_dead_fails);
  auto_select_fail_count_.store(auto_fails);
  api_dead_heal_fail_count_.store(api_dead_fails);
  if (restart_ok) {
    CWLOG_INFO(std::format(
        "CoreMonitor: 重启内核成功，API 通道应已恢复（保留告警计数={}）",
        auto_fails));
    return ok_action;
  }
  if (post == SelfHealAction::FireAlert) {
    fire_core_restart_failed_alert(h);
    return Action::Alerted;
  }
  return Action::Idle;
}

std::optional<CoreMonitor::Action> CoreMonitor::try_fast_restart_missing_pipe(
    const Hooks& h, const Params& p) {
  if (!h.pipe_missing || !h.expect_core_pipe_alive) {
    return std::nullopt;
  }
  const int64_t now =
      h.now_ms ? h.now_ms() : static_cast<int64_t>(GetTickCount64());
  const bool pipe_missing = h.pipe_missing();
  const bool expect_alive = h.expect_core_pipe_alive();
  const int64_t last = last_heal_ms_.load();
  const bool has_last = has_heal_attempt_.load() || (last != 0);
  std::optional<uint64_t> elapsed_secs;
  if (has_last) {
    elapsed_secs =
        (now > last) ? static_cast<uint64_t>((now - last) / 1000) : 0ULL;
  }
  if (!can_fast_restart_missing_pipe(pipe_missing, expect_alive,
                                     elapsed_secs)) {
    return std::nullopt;
  }
  if (!is_mihomo_pipe_missing_confirmed(h)) {
    return std::nullopt;
  }

  api_fail_count_.store(0);
  const int64_t after_confirm_now =
      h.now_ms ? h.now_ms() : static_cast<int64_t>(GetTickCount64());
  last_heal_ms_.store(after_confirm_now);
  has_heal_attempt_.store(true);
  last_tick_ms_.store(after_confirm_now);
  const int auto_fails = auto_select_fail_count_.fetch_add(1) + 1;
  if (auto_fails >= p.alert_fail_threshold) {
    auto_select_fail_count_.store(0);
    api_dead_heal_fail_count_.store(0);
    fire_self_heal_alert(h);
    return Action::Alerted;
  }
  return execute_core_restart_with_accounting(
      h,
      "CoreMonitor: 检测到内核命名管道已不存在（进程退出），跳过轮询空等立即重启内核",
      Action::Restarted);
}

CoreMonitor::Action CoreMonitor::heal(const std::string& uid,
                                      SelfHealOrigin origin,
                                      const std::string& dead_node) {
  Hooks h;
  Params p;
  {
    std::lock_guard<std::mutex> lock(m_);
    h = hooks_;
    p = params_;
  }
  const int64_t now = h.now_ms ? h.now_ms()
                               : static_cast<int64_t>(GetTickCount64());
  const int64_t last = last_heal_ms_.load();
  const bool has_last = has_heal_attempt_.load() || (last != 0);
  if (has_last && now - last < p.heal_cooldown_ms) {
    CWLOG_INFO(std::format(
        "CoreMonitor: 自愈选点在 {} 秒冷却中（已过 {} 秒），跳过本次",
        p.heal_cooldown_ms / 1000, (now - last) / 1000));
    return Action::Idle;
  }

  // S4 原节点确已死门检（ConfirmedDead 适用；NodeMigration / ApiChannelDown 跳过）
  if (self_heal_requires_dead_gate(origin) && !dead_node.empty() &&
      h.confirm_dead_gate && !h.confirm_dead_gate(dead_node)) {
    CWLOG_INFO("CoreMonitor: 自愈门检确认原节点未确死（" + dead_node +
               "），放弃本次切点");
    return Action::Idle;
  }

  // S4 / BUG-388 自愈切换速率限制（ConfirmedDead 豁免；NodeMigration / ApiChannelDown 受限）
  if (self_heal_subject_to_switch_rate_limit(origin)) {
    std::optional<int64_t> last_switch_age_secs;
    size_t recent_count = 0;
    {
      std::lock_guard<std::mutex> lock(switch_rate_m_);
      const int64_t win_ms = kAutoSwitchWindowSecs * 1000;
      recent_auto_switches_ms_.erase(
          std::remove_if(recent_auto_switches_ms_.begin(),
                         recent_auto_switches_ms_.end(),
                         [&](int64_t ts) { return (now - ts) >= win_ms; }),
          recent_auto_switches_ms_.end());
      if (!recent_auto_switches_ms_.empty()) {
        const int64_t delta_ms =
            (now >= recent_auto_switches_ms_.back())
                ? (now - recent_auto_switches_ms_.back())
                : 0;
        last_switch_age_secs = delta_ms / 1000;
      }
      recent_count = recent_auto_switches_ms_.size();
    }
    if (!auto_switch_permitted(last_switch_age_secs, recent_count)) {
      CWLOG_WARN("CoreMonitor: 自愈切换速率保护（间隔 < 120s 或 600s 内满 3 次），跳过本次切点");
      return Action::Idle;
    }
  }

  // BUG-388 API 通道恢复保活门检 / BUG-411 通道仍死直接免等
  bool api_confirmed_down = false;
  if (origin == SelfHealOrigin::ApiChannelDown) {
    if (h.api_channel_precheck) {
      const ApiChannelPrecheck pre = h.api_channel_precheck();
      if (pre == ApiChannelPrecheck::KeepAlive) {
        auto_select_fail_count_.store(0);
        api_dead_heal_fail_count_.store(0);
        emit_network_health_notice("network_health::ok");
        last_tick_ms_.store(now);
        CWLOG_INFO("CoreMonitor: API 通道自愈前复核已恢复且当前节点健康，保活现状并跳过切点");
        return Action::Idle;
      }
      if (pre == ApiChannelPrecheck::ConfirmedDown) {
        api_confirmed_down = true;
      }
    } else if (h.api_recovery_keep_alive && h.api_recovery_keep_alive()) {
      auto_select_fail_count_.store(0);
      api_dead_heal_fail_count_.store(0);
      emit_network_health_notice("network_health::ok");
      last_tick_ms_.store(now);
      CWLOG_INFO("CoreMonitor: API 通道恢复且当前活跃真节点健康，保活现状并跳过切点");
      return Action::Idle;
    }
  }

  const bool prev_has_attempt = has_heal_attempt_.exchange(true);
  last_heal_ms_.store(now);

  bool failed_this_round = false;
  if (api_confirmed_down) {
    CWLOG_WARN("CoreMonitor: API 通道仍不可达，跳过 30 秒就绪空等直接计入自愈失败次数");
    failed_this_round = true;
  } else {
    if ((!h.auto_select_for_heal && !h.auto_select_trigger) || uid.empty()) {
      return Action::Idle;
    }

    auto on_select_success = [&](std::string new_node) -> Action {
      // 自愈成功：清零双计数并广播恢复健康（BUG-318 / BUG-359）
      api_fail_count_.store(0);
      node_fail_count_.store(0);
      auto_select_fail_count_.store(0);
      api_dead_heal_fail_count_.store(0);
      emit_network_health_notice("network_health::ok");
      if (new_node.empty() && h.active_node_name) {
        new_node = h.active_node_name();
      }
      const bool switched =
          dead_node.empty() || new_node.empty() || dead_node != new_node;
      if (switched) {
        std::lock_guard<std::mutex> lock(switch_rate_m_);
        recent_auto_switches_ms_.push_back(now);
      }
      if (origin == SelfHealOrigin::NodeMigration) {
        if (h.on_node_migration_healed) {
          h.on_node_migration_healed();
        }
      } else if (!dead_node.empty() && !new_node.empty() &&
                 dead_node != new_node) {
        if (h.on_dead_node_healed) {
          h.on_dead_node_healed(dead_node);
        }
        CWLOG_INFO(std::format(
            "CoreMonitor: 自愈已切换 {} -> {}（已同步清理原故障节点存量代理连接）",
            dead_node, new_node));
      }
      return Action::Healed;
    };

    bool benign_abort = false;
    if (h.auto_select_for_heal) {
      const bool skip_wait_ready = (origin == SelfHealOrigin::ApiChannelDown);
      auto r = h.auto_select_for_heal(skip_wait_ready);
      if (r.is_ok() && r.value().selected) {
        return on_select_success(std::move(r.value().selected_node));
      }
      if (r.is_err() && is_benign_auto_select_abort(r.error().message)) {
        benign_abort = true;
      } else {
        if (r.is_ok()) {
          CWLOG_WARN("CoreMonitor: 自愈选点未选出可用节点（所有节点不可达）");
        } else {
          CWLOG_WARN("CoreMonitor: 自愈选点失败 (" + r.error().message + ")");
        }
        failed_this_round = true;
      }
    } else {
      auto r = h.auto_select_trigger();
      if (r.is_ok() && r.value()) {
        return on_select_success(std::string());
      }
      if ((r.is_ok() && !r.value()) ||
          (r.is_err() && is_benign_auto_select_abort(r.error().message))) {
        benign_abort = true;
      } else {
        failed_this_round = true;
      }
    }

    if (benign_abort) {
      // BUSY / MANUAL_PREEMPTED → 不计入失败、回退冷却时间戳允许下次重试
      last_heal_ms_.store(last);
      has_heal_attempt_.store(prev_has_attempt);
      return Action::Idle;
    }
  }

  if (!failed_this_round) {
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
    fire_self_heal_alert(h);
    return Action::Alerted;
  }

  if (decision == SelfHealAction::RestartCore) {
    return execute_core_restart_with_accounting(
        h,
        std::format(
            "CoreMonitor: 连续 {} 次自愈选点失败且内核 API 不可达，尝试重启内核（当前告警计数={}）",
            p.heal_fail_threshold, auto_fails),
        Action::RestartedOnHealFail);
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
  const int64_t now_tick = now_fn();
  last_tick_ms_.store(now_tick);

  // BUG-336 Checkpoint #1：任何提前返回（进程未跑 / 物理断网 / API 失败）前先吸收手动切换信号
  absorb_pending_manual_switch(now_tick);

  // 1. 进程级直检：进程死 → 若接入管道探测则走防抖/保护/冷却快重启，否则沿用直连重启
  if (h.core_alive && !h.core_alive()) {
    if (h.pipe_missing && h.expect_core_pipe_alive) {
      if (auto act = try_fast_restart_missing_pipe(h, p)) {
        return *act;
      }
      return Action::Idle;
    }
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
  // 离线 → 在线转移（S3 统一裁决 NetworkRecovered = Wait）：
  // 复位计数并通知 ok，交由本周期后续正常体检路径重新测量，绝不直接切点
  if (was_offline_.exchange(false)) {
    api_fail_count_.store(0);
    node_fail_count_.store(0);
    {
      std::lock_guard<std::mutex> lock(brownout_m_);
      recent_brownout_episodes_ms_.clear();
    }
    emit_network_health_notice("network_health::ok");
    CWLOG_INFO("CoreMonitor: 物理网络恢复，交由常规体检路径重新测量（不立即自愈）");
  }

  // 3. API 存活：优先缺管秒级快重启（BUG-411），否则连续 3 次不可达且复核仍不可达 → 自愈
  bool api_ok = h.api_alive ? h.api_alive() : true;
  if (!api_ok) {
    if (auto act = try_fast_restart_missing_pipe(h, p)) {
      return *act;
    }
    int fails = api_fail_count_.fetch_add(1) + 1;
    if (fails >= p.api_fail_threshold) {
      const bool api_recheck = h.api_alive ? h.api_alive() : false;
      api_fail_count_.store(0);
      if (decide_self_heal(
              {SelfHealTrigger::Kind::ApiUnreachable,
               static_cast<uint32_t>(fails), api_recheck},
              p.node_dead_threshold, p.api_fail_threshold) ==
          SelfHealDecision::Allow) {
        std::string uid = h.current_uid ? h.current_uid() : std::string();
        return heal(uid, SelfHealOrigin::ApiChannelDown, {});
      }
      CWLOG_INFO("CoreMonitor: API 复核已恢复可达，复位计数交下一周期重测");
    }
    return Action::Idle;
  }
  api_fail_count_.store(0);

  // 定期真 HTTPS 验真（每 180s 周期一次；有未决节点才实际发包）
  if (h.periodic_https_verify) {
    const int64_t last_v = last_https_verify_ms_.load();
    if (last_v < 0 || (now_tick - last_v) >= kHttpsVerifyIntervalSecs * 1000) {
      last_https_verify_ms_.store(now_tick);
      h.periodic_https_verify();
    }
  }

  // BUG-336 Checkpoint #2：观测 active_node_name 前再次吸收
  absorb_pending_manual_switch(now_fn());

  // ★ §5.1 活跃节点切换检测（← Mini monitor.rs:1358-1370）
  std::string observed_active;
  if (h.active_node_name) {
    observed_active = h.active_node_name();
    bool changed = false;
    {
      std::lock_guard<std::mutex> lock(last_node_m_);
      if (observed_active != last_active_node_) {
        last_active_node_ = observed_active;
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
      {
        std::lock_guard<std::mutex> lock(brownout_m_);
        recent_brownout_episodes_ms_.clear();
      }
      CWLOG_INFO(std::format(
          "CoreMonitor: 活跃节点发生变化，起算 {}s 切换宽限期",
          p.switch_grace_ms / 1000));
    }
  }

  // ★ §5.4 第 3 道铁锁：假/广告/验真失败迁移节点 → 直接判需迁移（NodeMigration）
  if (h.is_dummy_active) {
    if (h.is_dummy_active()) {
      CWLOG_WARN("CoreMonitor: 活跃节点为伪节点/已确认假节点/待迁移节点，触发不可信迁移");
      node_fail_count_.store(0);
      std::string uid = h.current_uid ? h.current_uid() : std::string();
      if (!uid.empty()) {
        return heal(uid, SelfHealOrigin::NodeMigration, observed_active);
      }
      return Action::Idle;
    }
  }

  // ★ BUG-405：活跃出口已被当前过滤词排除且存在合规候选 → 立即触发迁移（NodeMigration，受 20s 宽限期保护）
  if (h.should_migrate_filtered_out && h.should_migrate_filtered_out()) {
    absorb_pending_manual_switch(now_fn());
    if (in_switch_grace()) {
      CWLOG_INFO("CoreMonitor: 活跃节点越界过滤词迁移处于 20s 切换宽限期内，暂缓切点");
      return Action::Idle;
    }
    CWLOG_WARN("CoreMonitor: 当前活跃节点已被过滤词排除，触发越界迁移 (NodeMigration)");
    node_fail_count_.store(0);
    std::string uid = h.current_uid ? h.current_uid() : std::string();
    if (!uid.empty()) {
      return heal(uid, SelfHealOrigin::NodeMigration, observed_active);
    }
    return Action::Idle;
  }

  // ★ §5.8 测速失真复核（← Mini monitor.rs:1526-1670）：不可用或主 URL 失真 → 本轮不判死
  if (h.probe_metrics_with_fallback) {
    if (!h.probe_metrics_with_fallback()) {
      CWLOG_WARN(
          "CoreMonitor: 测速链路不可用或主测速地址失真，本轮不判定节点死活");
      node_fail_count_.store(0);
      auto_select_fail_count_.store(0);
      api_dead_heal_fail_count_.store(0);
      return Action::Idle;
    }
  }

  // ★ ShouldMigrate：Alive 但真 HTTPS 验真失败 → 切往已验真节点（在失真复核之后且受 20s 宽限期约束）
  if (h.should_migrate_untrusted && h.should_migrate_untrusted()) {
    absorb_pending_manual_switch(now_fn());
    if (in_switch_grace()) {
      CWLOG_INFO("CoreMonitor: 活跃节点 HTTPS 验真迁移处于 20s 切换宽限期内，暂缓切点");
      return Action::Idle;
    }
    CWLOG_WARN("CoreMonitor: 活跃节点真 HTTPS 验真失败，触发不可信迁移 (ShouldMigrate)");
    node_fail_count_.store(0);
    std::string uid = h.current_uid ? h.current_uid() : std::string();
    if (!uid.empty()) {
      return heal(uid, SelfHealOrigin::NodeMigration, observed_active);
    }
    return Action::Idle;
  }

  // 4. 节点健康：连续 2 次判死或 600s 滑窗内 ≥2 次间歇黑洞 → 自愈（ConfirmedDead）
  bool node_ok = h.node_healthy ? h.node_healthy() : true;
  if (!node_ok) {
    // BUG-336 Checkpoint #3：耗时拨测返回后立即吸收拨测期间发生的手动切节点信号
    absorb_pending_manual_switch(now_fn());
    if (in_switch_grace()) {
      CWLOG_INFO(
          "CoreMonitor: 活跃节点检测异常，当前处于切换宽限期，不计入连续失败");
      node_fail_count_.store(0);
      {
        std::lock_guard<std::mutex> lock(brownout_m_);
        recent_brownout_episodes_ms_.clear();
      }
      return Action::Idle;
    }
    const int fails = node_fail_count_.fetch_add(1) + 1;
    const bool is_should_failover =
        h.has_healthy_standby_in_subset ? h.has_healthy_standby_in_subset()
                                        : true;
    const int64_t episode_now_ms = now_fn();
    size_t brownout_episodes = 0;
    {
      std::lock_guard<std::mutex> lock(brownout_m_);
      if (fails == 1 && is_should_failover) {
        brownout_episodes = record_brownout_episode(
            recent_brownout_episodes_ms_, episode_now_ms);
      } else {
        prune_brownout_episodes(recent_brownout_episodes_ms_, episode_now_ms);
        brownout_episodes = recent_brownout_episodes_ms_.size();
      }
    }
    CWLOG_INFO(std::format(
        "CoreMonitor: 活跃节点检测异常，连续失败次数: {}，窗口内间歇故障次数: {}",
        fails, brownout_episodes));
    if (decide_self_heal(
            {SelfHealTrigger::Kind::NodeConfirmedDead,
             static_cast<uint32_t>(fails), false, brownout_episodes},
            p.node_dead_threshold, p.api_fail_threshold) ==
        SelfHealDecision::Allow) {
      node_fail_count_.store(0);
      {
        std::lock_guard<std::mutex> lock(brownout_m_);
        recent_brownout_episodes_ms_.clear();
      }
      std::string dead_node = observed_active;
      if (dead_node.empty() && h.active_node_name) {
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
      return heal(uid, SelfHealOrigin::ConfirmedDead, dead_node);
    }
    return Action::Idle;
  }
  const int prev_fails = node_fail_count_.exchange(0);
  if (prev_fails > 0) {
    CWLOG_INFO(std::format(
        "CoreMonitor: 活跃节点复检恢复正常（此前连续失败 {} 次），解除重试状态",
        prev_fails));
  }
  auto_select_fail_count_.store(0);
  api_dead_heal_fail_count_.store(0);
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
  // BUG-317 / BUG-345 / BUG-403：总预算保底 >= 2s；
  // 首选单路 223.5.5.5:53 (250ms)，失败再并发竞速 119.29.29.29 / 1.1.1.1 (600ms)；
  // 为局域网封禁 53 端口时的 DNS 解析兜底保留 >= 1150ms 充裕预算。
  const int64_t budget_ms = static_cast<int64_t>(
      std::max<uint64_t>(timeout_secs, kMinProbeBudgetSecs) * 1000);
  auto now_fn = [&]() -> int64_t {
    return ops.now_ms ? ops.now_ms() : static_cast<int64_t>(GetTickCount64());
  };
  const int64_t start_ms = now_fn();
  auto remaining_ms = [&]() -> int64_t {
    return budget_ms - (now_fn() - start_ms);
  };

  if (ops.tcp_race_connect) {
    const int64_t rem1 = remaining_ms();
    if (rem1 <= 0) return false;
    const int primary_ms =
        static_cast<int>(std::min<int64_t>(rem1, kPrimaryNetProbeTimeoutMs));
    if (ops.tcp_race_connect({"223.5.5.5"}, 53, primary_ms)) {
      return true;
    }
    const int64_t rem2 = remaining_ms();
    if (rem2 <= 0) return false;
    const int race_ms =
        static_cast<int>(std::min<int64_t>(rem2, kTcp53RaceTimeoutMs));
    if (ops.tcp_race_connect({"119.29.29.29", "1.1.1.1"}, 53, race_ms)) {
      return true;
    }
  } else if (ops.tcp_connect) {
    const int64_t rem1 = remaining_ms();
    if (rem1 <= 0) return false;
    const int primary_ms =
        static_cast<int>(std::min<int64_t>(rem1, kPrimaryNetProbeTimeoutMs));
    if (ops.tcp_connect("223.5.5.5", 53, primary_ms)) {
      return true;
    }
    const int64_t per_ip_cap = std::max<int64_t>(250, budget_ms / 4);
    static constexpr const char* kBackupDnsIps[] = {"119.29.29.29", "1.1.1.1"};
    for (const char* ip : kBackupDnsIps) {
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

bool CoreMonitor::has_recent_flowing_traffic(
    std::optional<int64_t> elapsed_secs) {
  return elapsed_secs.has_value() && *elapsed_secs >= 0 &&
         *elapsed_secs < kOnlineCheckFlowingSkipSecs;
}

void CoreMonitor::record_flowing_traffic_at(int64_t now_ms) {
  std::lock_guard<std::mutex> lock(g_flowing_mu);
  g_last_flowing_traffic_ms = now_ms;
}

bool CoreMonitor::is_recent_flowing_traffic_active(int64_t now_ms) {
  int64_t recorded = -1;
  {
    std::lock_guard<std::mutex> lock(g_flowing_mu);
    recorded = g_last_flowing_traffic_ms;
  }
  if (recorded < 0 || now_ms < recorded) return false;
  return has_recent_flowing_traffic((now_ms - recorded) / 1000);
}

void CoreMonitor::reset_flowing_traffic_state() {
  std::lock_guard<std::mutex> lock(g_flowing_mu);
  g_last_flowing_traffic_ms = -1;
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

bool CoreMonitor::ThrottledNetProbe::was_online() const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mu_));
  return was_online_;
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
  // 阶段 4：等待 DNS 解析链就绪（最多 15s；超时仅告警仍放行由下游兜底，← monitor.rs:1100-1138）
  if (h.dns_ready && !poll(h.dns_ready, 15000)) {
    CWLOG_WARN("CoreMonitor: 阶段 4 超时：DNS 解析链未就绪，放行测速（由下游兜底）");
  }
  return true;
}

bool CoreMonitor::is_manual_switch_preempted(uint64_t start_epoch,
                                             uint64_t current_epoch) {
  return current_epoch != start_epoch;
}

bool CoreMonitor::is_benign_auto_select_abort(const std::string& err_msg) {
  return err_msg == "AUTO_SELECT_BUSY" ||
         err_msg == "AUTO_SELECT_MANUAL_PREEMPTED";
}

CoreMonitor::NodeStatus CoreMonitor::classify_node_status(
    bool alive, std::optional<uint32_t> last_delay,
    std::optional<uint64_t> age_secs) {
  if (!last_delay.has_value()) {
    return {NodeStatusKind::Unknown, 0};
  }
  const uint32_t delay = *last_delay;
  const bool valid_delay =
      (delay >= kNodeDelayMinMs && delay < kNodeDelayMaxMs);
  if (alive && valid_delay) {
    return {NodeStatusKind::Alive, delay};
  }
  if (!alive && valid_delay && age_secs.has_value() &&
      *age_secs < kSuspectWindowSecs) {
    return {NodeStatusKind::Suspect, 0};
  }
  return {NodeStatusKind::Dead, 0};
}

bool CoreMonitor::should_proceed_with_self_heal(const NodeStatus& status) {
  return status.kind == NodeStatusKind::Dead;
}

std::pair<CoreMonitor::ActiveTrafficState,
          std::map<std::string, std::pair<uint64_t, uint64_t>>>
CoreMonitor::classify_active_traffic_delta(
    const std::optional<std::string>& prev_node,
    const std::string& active_node,
    const std::map<std::string, std::pair<uint64_t, uint64_t>>& prev_conns,
    const std::vector<std::tuple<std::string, uint64_t, uint64_t>>&
        current_conns) {
  std::map<std::string, std::pair<uint64_t, uint64_t>> next_map;
  for (const auto& [id, up, down] : current_conns) {
    next_map[id] = {up, down};
  }
  if (!prev_node.has_value() || *prev_node != active_node) {
    return {ActiveTrafficState::Idle, std::move(next_map)};
  }

  uint64_t delta_up = 0;
  uint64_t delta_down = 0;
  for (const auto& [id, up, down] : current_conns) {
    auto it = prev_conns.find(id);
    if (it != prev_conns.end()) {
      const auto [prev_up, prev_down] = it->second;
      if (up > prev_up) delta_up += (up - prev_up);
      if (down > prev_down) delta_down += (down - prev_down);
    } else {
      delta_up += up;
      delta_down += down;
    }
  }
  ActiveTrafficState state = ActiveTrafficState::Idle;
  if (delta_down > 0) {
    state = ActiveTrafficState::Flowing;
  } else if (delta_up > 0) {
    state = ActiveTrafficState::Stalled;
  }
  return {state, std::move(next_map)};
}

std::pair<CoreMonitor::ActiveTrafficState, bool>
CoreMonitor::update_active_traffic_state(
    std::optional<ActiveTrafficTracker>& tracker_slot,
    const std::string& active_node,
    const std::vector<std::tuple<std::string, uint64_t, uint64_t>>&
        current_conns,
    int64_t now_ms) {
  if (!tracker_slot.has_value()) {
    tracker_slot = ActiveTrafficTracker{};
  }
  auto& tracker = *tracker_slot;
  if (!tracker.active_node.has_value() || *tracker.active_node != active_node) {
    tracker.last_stall_probe_ms = -1;
  }
  const bool stall_cooldown_active =
      tracker.last_stall_probe_ms >= 0 &&
      (now_ms - tracker.last_stall_probe_ms) < kStallReprobeCooldownSecs * 1000;

  auto [state, next_map] = classify_active_traffic_delta(
      tracker.active_node, active_node, tracker.conn_bytes, current_conns);
  tracker.active_node = active_node;
  tracker.conn_bytes = std::move(next_map);
  if (state == ActiveTrafficState::Flowing) {
    record_flowing_traffic_at(now_ms);
  }
  return {state, stall_cooldown_active};
}

bool CoreMonitor::should_probe_active_node(const NodeStatus& status,
                                           std::optional<uint64_t> age_secs,
                                           ActiveTrafficState traffic,
                                           bool stall_cooldown_active) {
  if (status.kind != NodeStatusKind::Alive) return true;
  if (!age_secs.has_value()) return true;
  const int64_t age = static_cast<int64_t>(*age_secs);
  switch (traffic) {
    case ActiveTrafficState::Flowing:
      return age >= kPassiveTrafficMaxSkipSecs;
    case ActiveTrafficState::Stalled:
      return !stall_cooldown_active || age >= kIdleHealthFreshSecs;
    case ActiveTrafficState::Idle:
      return age >= kIdleHealthFreshSecs;
  }
  return true;
}

bool CoreMonitor::should_allow_https_migration(ActiveTrafficState traffic) {
  return traffic != ActiveTrafficState::Flowing;
}

bool CoreMonitor::should_run_periodic_https_verify_with(
    const std::vector<std::string>& members,
    const std::optional<std::string>& active_node,
    const std::string& filter_text,
    const std::function<bool(const std::string&)>& is_fake_node,
    const std::function<bool(const std::string&)>& is_alive,
    const std::function<bool(const std::string&)>& is_verified) {
  const std::string trimmed_filter = util::trim(filter_text);
  bool has_alive_unverified = false;
  size_t verified_alive_standbys = 0;

  for (const auto& name : members) {
    if (util::is_dummy_node(name) || (is_fake_node && is_fake_node(name)) ||
        !util::match_node_filter(name, trimmed_filter)) {
      continue;
    }
    if (!is_alive || !is_alive(name)) continue;
    if (is_verified && is_verified(name)) {
      if (!active_node || *active_node != name) {
        ++verified_alive_standbys;
      }
    } else {
      has_alive_unverified = true;
    }
  }

  if (!has_alive_unverified) return false;
  const bool active_verified =
      active_node.has_value() && is_verified && is_verified(*active_node);
  if (active_verified &&
      verified_alive_standbys >= kPeriodicHttpsVerifySufficientStandbys) {
    return false;
  }
  return true;
}

bool CoreMonitor::should_recheck_with_fallback(
    const std::map<std::string, uint32_t>& primary_delays,
    const std::vector<std::string>& expected_nodes) {
  auto valid = [](uint32_t d) {
    return d >= kNodeDelayMinMs && d < kNodeDelayMaxMs;
  };
  if (!expected_nodes.empty()) {
    for (const auto& name : expected_nodes) {
      auto it = primary_delays.find(name);
      if (it != primary_delays.end() && valid(it->second)) return false;
    }
    return true;
  }
  for (const auto& [name, d] : primary_delays) {
    if (!util::is_dummy_node(name) && !util::is_confirmed_fake_node(name) &&
        valid(d)) {
      return false;
    }
  }
  return true;
}

bool CoreMonitor::should_recheck_primary_with_fallback_with(
    const std::map<std::string, uint32_t>& primary_delays,
    const std::vector<std::string>& expected_nodes,
    bool allow_single_target_skip,
    const std::function<bool(const std::string&)>& is_fake_node) {
  if (!should_recheck_with_fallback(primary_delays, expected_nodes)) {
    return false;
  }
  auto valid = [](uint32_t d) {
    return d >= kNodeDelayMinMs && d < kNodeDelayMaxMs;
  };
  auto eq_ci = [](const std::string& a, const char* b) {
    return _stricmp(a.c_str(), b) == 0;
  };
  if (allow_single_target_skip && expected_nodes.size() == 1) {
    size_t healthy_real_peers = 0;
    for (const auto& [name, d] : primary_delays) {
      if (!eq_ci(name, "DIRECT") && !eq_ci(name, "REJECT") &&
          !util::is_dummy_node(name) &&
          !(is_fake_node && is_fake_node(name)) && valid(d)) {
        ++healthy_real_peers;
      }
    }
    if (healthy_real_peers >= kSingleTargetSkipFallbackMinHealthyPeers) {
      return false;
    }
  }
  return true;
}

bool CoreMonitor::should_recheck_primary_with_fallback(
    const std::map<std::string, uint32_t>& primary_delays,
    const std::vector<std::string>& expected_nodes,
    bool allow_single_target_skip) {
  return should_recheck_primary_with_fallback_with(
      primary_delays, expected_nodes, allow_single_target_skip,
      [](const std::string& n) { return util::is_confirmed_fake_node(n); });
}

std::map<std::string, uint32_t> CoreMonitor::merge_probe_delays(
    std::map<std::string, uint32_t> primary,
    const std::map<std::string, uint32_t>& fallback) {
  auto valid = [](uint32_t d) {
    return d >= kNodeDelayMinMs && d < kNodeDelayMaxMs;
  };
  for (const auto& [name, fb_delay] : fallback) {
    if (valid(fb_delay)) {
      primary[name] = fb_delay;
    } else {
      auto it = primary.find(name);
      if (it == primary.end()) {
        primary[name] = fb_delay;
      }
    }
  }
  return primary;
}

std::vector<std::pair<std::string, uint32_t>>
CoreMonitor::build_probe_ui_display_with(
    const std::vector<std::string>& all_nodes,
    const std::map<std::string, uint32_t>& probed_delays,
    bool primary_url_distorted,
    const std::function<bool(const std::string&)>& is_fake_node) {
  std::vector<std::pair<std::string, uint32_t>> out;
  for (const auto& n : all_nodes) {
    if (util::is_dummy_node(n)) continue;
    if (is_fake_node && is_fake_node(n)) continue;
    auto it = probed_delays.find(n);
    if (primary_url_distorted) {
      if (it != probed_delays.end() && it->second >= kNodeDelayMinMs &&
          it->second < kNodeDelayMaxMs) {
        out.emplace_back(n, it->second);
      }
    } else {
      out.emplace_back(n, (it != probed_delays.end()) ? it->second : 0u);
    }
  }
  return out;
}

std::vector<std::pair<std::string, uint32_t>>
CoreMonitor::build_probe_ui_display(
    const std::vector<std::string>& all_nodes,
    const std::map<std::string, uint32_t>& probed_delays,
    bool primary_url_distorted) {
  return build_probe_ui_display_with(
      all_nodes, probed_delays, primary_url_distorted,
      [](const std::string& n) { return util::is_confirmed_fake_node(n); });
}

std::optional<CoreMonitor::UiActiveSyncState>
CoreMonitor::compute_next_ui_active_sync(
    const std::string& active_node,
    const std::vector<std::pair<std::string, uint32_t>>& display,
    const std::optional<std::string>& active_hist_time,
    const UiActiveSyncState* prev) {
  std::optional<uint32_t> delay;
  for (const auto& [n, d] : display) {
    if (n == active_node) {
      delay = d;
      break;
    }
  }
  if (!delay.has_value()) {
    if (prev) return *prev;
    return std::nullopt;
  }
  const bool reported_alive =
      (*delay >= kNodeDelayMinMs && *delay < kNodeDelayMaxMs);
  std::optional<std::string> hist_time;
  if (reported_alive) {
    if (active_hist_time.has_value()) {
      hist_time = active_hist_time;
    } else if (prev && prev->node_name == active_node && prev->reported_alive) {
      hist_time = prev->history_time;
    }
  }
  return UiActiveSyncState{active_node, std::move(hist_time), reported_alive};
}

bool CoreMonitor::should_sync_alive_ui(bool health_not_ok,
                                       const UiActiveSyncState* last_sync,
                                       const std::string& active_node,
                                       const std::string* active_hist_time) {
  if (health_not_ok || !last_sync) return true;
  if (last_sync->node_name != active_node || !last_sync->reported_alive) {
    return true;
  }
  if (!active_hist_time) return false;
  return !last_sync->history_time.has_value() ||
         *last_sync->history_time != *active_hist_time;
}

void CoreMonitor::prune_brownout_episodes(std::vector<int64_t>& episodes_ms,
                                          int64_t now_ms) {
  episodes_ms.erase(
      std::remove_if(episodes_ms.begin(), episodes_ms.end(),
                     [now_ms](int64_t ts) {
                       const int64_t elapsed_secs =
                           (now_ms > ts) ? ((now_ms - ts) / 1000) : 0;
                       return elapsed_secs >= kBrownoutWindowSecs;
                     }),
      episodes_ms.end());
}

size_t CoreMonitor::record_brownout_episode(std::vector<int64_t>& episodes_ms,
                                            int64_t now_ms) {
  prune_brownout_episodes(episodes_ms, now_ms);
  episodes_ms.push_back(now_ms);
  return episodes_ms.size();
}

size_t CoreMonitor::brownout_episode_count_for_test() const {
  std::lock_guard<std::mutex> lock(brownout_m_);
  return recent_brownout_episodes_ms_.size();
}

CoreMonitor::SelfHealDecision CoreMonitor::decide_self_heal(
    const SelfHealTrigger& trigger, int node_dead_threshold,
    int api_fail_threshold, size_t brownout_confirm_fails) {
  switch (trigger.kind) {
    case SelfHealTrigger::Kind::NodeConfirmedDead:
      return (trigger.count >= static_cast<uint32_t>(node_dead_threshold) ||
              trigger.brownout_episodes >= brownout_confirm_fails)
                 ? SelfHealDecision::Allow
                 : SelfHealDecision::Wait;
    case SelfHealTrigger::Kind::NetworkRecovered:
      return SelfHealDecision::Wait;
    case SelfHealTrigger::Kind::ApiUnreachable:
      return (trigger.count >= static_cast<uint32_t>(api_fail_threshold) &&
              !trigger.api_alive_on_recheck)
                 ? SelfHealDecision::Allow
                 : SelfHealDecision::Wait;
  }
  return SelfHealDecision::Wait;
}

bool CoreMonitor::self_heal_requires_dead_gate(SelfHealOrigin origin) {
  return origin == SelfHealOrigin::ConfirmedDead;
}

bool CoreMonitor::self_heal_subject_to_switch_rate_limit(
    SelfHealOrigin origin) {
  return origin != SelfHealOrigin::ConfirmedDead;
}

bool CoreMonitor::auto_switch_permitted(
    std::optional<int64_t> last_switch_age_secs,
    size_t recent_switch_count) {
  if (last_switch_age_secs.has_value() &&
      *last_switch_age_secs < kMinAutoSwitchIntervalSecs) {
    return false;
  }
  return recent_switch_count < kMaxAutoSwitchesPerWindow;
}

bool CoreMonitor::should_keep_alive_on_api_recovery(bool is_valid_real_node,
                                                    const NodeStatus& status) {
  return is_valid_real_node && status.kind == NodeStatusKind::Alive;
}

bool CoreMonitor::persisted_exit_is_usable(
    const std::optional<std::string>& now,
    const std::vector<std::string>* all, const std::string& filter_text,
    bool confirmed_fake) {
  if (!now.has_value() || now->empty()) return false;
  if (*now == "DIRECT" || *now == "REJECT" || util::is_dummy_node(*now) ||
      confirmed_fake) {
    return false;
  }
  if (!util::match_node_filter(*now, util::effective_filter_text(filter_text))) {
    return false;
  }
  if (!all) return false;
  return std::find(all->begin(), all->end(), *now) != all->end();
}

bool CoreMonitor::should_migrate_filtered_out_active(
    const std::string& active_node, const std::vector<std::string>& members,
    const std::string& filter_text,
    const std::function<bool(const std::string&)>& is_fake) {
  const std::string effective = util::effective_filter_text(filter_text);
  if (effective.empty() || util::match_node_filter(active_node, effective)) {
    return false;
  }
  for (const auto& m : members) {
    if (m != active_node && m != "DIRECT" && m != "REJECT" &&
        !util::is_dummy_node(m) && (!is_fake || !is_fake(m)) &&
        util::match_node_filter(m, effective)) {
      return true;
    }
  }
  return false;
}

bool CoreMonitor::is_https_verify_backoff_active(
    std::optional<int64_t> elapsed_secs) {
  return elapsed_secs.has_value() &&
         *elapsed_secs < kHttpsVerifyAllFailedBackoffSecs;
}

namespace {
struct MetricsProbeState {
  std::mutex state_mu;
  int64_t completed_at_ms = -1;
  std::map<std::string, uint32_t> delays;
  bool reported_live_to_ui = false;
};

MetricsProbeState& shared_metrics_probe_state() {
  static MetricsProbeState s;
  return s;
}
}  // namespace

std::mutex& CoreMonitor::metrics_probe_mutex() {
  static std::mutex m;
  return m;
}

std::optional<std::map<std::string, uint32_t>>
CoreMonitor::try_reuse_metrics_probe(
    int64_t now_ms, int64_t max_age_ms,
    const std::vector<std::string>& expected_nodes, bool check_fallback) {
  auto& st = shared_metrics_probe_state();
  std::lock_guard<std::mutex> lock(st.state_mu);
  if (st.completed_at_ms < 0 || st.delays.empty() || now_ms < st.completed_at_ms ||
      (now_ms - st.completed_at_ms) >= max_age_ms) {
    return std::nullopt;
  }
  if (check_fallback &&
      should_recheck_with_fallback(st.delays, expected_nodes)) {
    return std::nullopt;
  }
  return st.delays;
}

void CoreMonitor::record_metrics_probe(int64_t now_ms,
                                       std::map<std::string, uint32_t> delays,
                                       bool reported_live_to_ui) {
  auto& st = shared_metrics_probe_state();
  std::lock_guard<std::mutex> lock(st.state_mu);
  if (delays.empty()) {
    st.completed_at_ms = -1;
    st.delays.clear();
    st.reported_live_to_ui = false;
    return;
  }
  st.completed_at_ms = now_ms;
  st.delays = std::move(delays);
  st.reported_live_to_ui = reported_live_to_ui;
}

void CoreMonitor::invalidate_metrics_probe() {
  auto& st = shared_metrics_probe_state();
  std::lock_guard<std::mutex> lock(st.state_mu);
  st.completed_at_ms = -1;
  st.delays.clear();
  st.reported_live_to_ui = false;
}

bool CoreMonitor::mark_metrics_probe_reported_if_unreported() {
  auto& st = shared_metrics_probe_state();
  std::lock_guard<std::mutex> lock(st.state_mu);
  if (st.completed_at_ms < 0 || st.delays.empty() || st.reported_live_to_ui) {
    return true;
  }
  st.reported_live_to_ui = true;
  return false;
}

}  // namespace clashwin::core
