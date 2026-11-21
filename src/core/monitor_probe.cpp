// CoreMonitor 物理网络连通性探测、流量活跃状态分类、metrics 拨测缓存与失败服务器表
// （拆分自 src/core/monitor.cpp；← ClashVerge src-tauri/src/module/monitor/probe.rs）
#include "core/monitor.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <future>

#include "clashwin/core/constants.h"
#include "core/delay_manager.h"
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
    int64_t now_ms, const std::function<bool()>& probe_fn, bool force) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!force && has_checked_) {
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
                                           bool stall_cooldown_active,
                                           bool kernel_reprobe_ready) {
  if (status.kind != NodeStatusKind::Alive) return true;
  if (!age_secs.has_value()) return true;
  // BUG-415 / BUG-416：建连失败突发或 TUN 默认网卡切换 → 冷却外立即破缓存实拨
  if (kernel_reprobe_ready) return true;
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
