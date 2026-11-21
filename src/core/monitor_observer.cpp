// CoreMonitor 内核日志观测（← ClashVerge src-tauri/src/module/monitor/observer.rs，
// BUG-415 代理建连失败突发 / BUG-416 TUN 默认网卡事件 / BUG-417 事件窗口与 TTL 解耦）
// 以及 BUG-417 探测有效性 / 失真豁免 / 首次观测宽限 / 冷启动预置失败纯函数。
#include "core/monitor.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>

#include "clashwin/core/constants.h"
#include "core/delay_manager.h"
#include "util/log.h"
#include "util/node.h"

namespace clashwin::core {

namespace {

std::mutex g_dial_tracker_mu;
std::optional<CoreMonitor::ProxyDialFailureTracker> g_dial_tracker;
std::atomic<bool> g_force_online_recheck{false};

std::mutex g_health_obs_mu;
std::optional<CoreMonitor::ActiveHealthObservation> g_last_health_obs;

int64_t mono_now_ms() { return static_cast<int64_t>(::GetTickCount64()); }

int64_t utc_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string ascii_lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool contains(std::string_view hay, std::string_view needle) {
  return hay.find(needle) != std::string_view::npos;
}

bool parse_fixed_int(std::string_view s, size_t pos, size_t len, int& out) {
  if (pos + len > s.size()) return false;
  int v = 0;
  for (size_t i = pos; i < pos + len; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    v = v * 10 + (s[i] - '0');
  }
  out = v;
  return true;
}

}  // namespace

// ← observer.rs is_proxy_dial_failure_log_line（BUG-415）
bool CoreMonitor::is_proxy_dial_failure_log_line(std::string_view line) {
  if (!contains(line, "dial PROXY ")) return false;
  const size_t pos = line.rfind(" error: ");
  if (pos == std::string_view::npos) return false;
  const std::string err = ascii_lower(line.substr(pos + 8));
  if (contains(err, "operation was canceled") || contains(err, "context canceled") ||
      contains(err, "dns resolve failed") || contains(err, "couldn't find ip") ||
      contains(err, "no such host")) {
    return false;
  }
  return contains(err, "i/o timeout") || contains(err, "context deadline exceeded") ||
         contains(err, "failed to create session") || contains(err, "handshake") ||
         contains(err, "connection refused") || contains(err, "actively refused") ||
         contains(err, "connection reset") || contains(err, "forcibly closed") ||
         contains(err, "broken pipe") || contains(err, "eof") ||
         contains(err, "network is unreachable") || contains(err, "no route to host");
}

// ← observer.rs classify_kernel_log_event（BUG-416）
std::optional<CoreMonitor::KernelLogEvent> CoreMonitor::classify_kernel_log_event(
    std::string_view line) {
  if (contains(line, "[TUN] default interface lost by monitor")) {
    return KernelLogEvent::TunInterfaceLost;
  }
  if (contains(line, "[TUN] default interface changed by monitor")) {
    return KernelLogEvent::TunInterfaceChanged;
  }
  if (is_proxy_dial_failure_log_line(line)) return KernelLogEvent::ProxyDialFailure;
  return std::nullopt;
}

// ← observer.rs parse_mihomo_log_timestamp：time="YYYY-MM-DDTHH:MM:SS[.frac](Z|±HH:MM)"
std::optional<int64_t> CoreMonitor::parse_mihomo_log_timestamp_ms(std::string_view line) {
  const size_t start = line.find("time=\"");
  if (start == std::string_view::npos) return std::nullopt;
  const size_t ts_begin = start + 6;
  const size_t ts_end = line.find('"', ts_begin);
  if (ts_end == std::string_view::npos) return std::nullopt;
  const std::string_view ts = line.substr(ts_begin, ts_end - ts_begin);
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
  if (ts.size() < 20 || !parse_fixed_int(ts, 0, 4, y) || ts[4] != '-' ||
      !parse_fixed_int(ts, 5, 2, mo) || ts[7] != '-' || !parse_fixed_int(ts, 8, 2, d) ||
      (ts[10] != 'T' && ts[10] != 't' && ts[10] != ' ') || !parse_fixed_int(ts, 11, 2, h) ||
      ts[13] != ':' || !parse_fixed_int(ts, 14, 2, mi) || ts[16] != ':' ||
      !parse_fixed_int(ts, 17, 2, se)) {
    return std::nullopt;
  }
  size_t pos = 19;
  int64_t frac_ms = 0;
  if (pos < ts.size() && ts[pos] == '.') {
    ++pos;
    int digits = 0;
    while (pos < ts.size() && std::isdigit(static_cast<unsigned char>(ts[pos]))) {
      if (digits < 3) frac_ms = frac_ms * 10 + (ts[pos] - '0');
      ++digits;
      ++pos;
    }
    if (digits == 0) return std::nullopt;
    for (int i = digits; i < 3; ++i) frac_ms *= 10;
  }
  int64_t offset_secs = 0;
  if (pos >= ts.size()) return std::nullopt;
  if (ts[pos] == 'Z' || ts[pos] == 'z') {
    ++pos;
  } else if (ts[pos] == '+' || ts[pos] == '-') {
    const int sign = ts[pos] == '-' ? -1 : 1;
    int oh = 0, om = 0;
    if (!parse_fixed_int(ts, pos + 1, 2, oh) || pos + 3 >= ts.size() || ts[pos + 3] != ':' ||
        !parse_fixed_int(ts, pos + 4, 2, om)) {
      return std::nullopt;
    }
    offset_secs = sign * (oh * 3600 + om * 60);
    pos += 6;
  } else {
    return std::nullopt;
  }
  if (pos != ts.size()) return std::nullopt;
  std::tm tm{};
  tm.tm_year = y - 1900;
  tm.tm_mon = mo - 1;
  tm.tm_mday = d;
  tm.tm_hour = h;
  tm.tm_min = mi;
  tm.tm_sec = se;
  const time_t t = _mkgmtime(&tm);
  if (t == static_cast<time_t>(-1)) return std::nullopt;
  return (static_cast<int64_t>(t) - offset_secs) * 1000 + frac_ms;
}

// ← observer.rs read_incremental_log_lines（BUG-415）
std::vector<std::string> CoreMonitor::read_incremental_log_lines(
    const std::wstring& path, IncrementalLogCursor& cursor) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) return {};
  const uint64_t file_len = static_cast<uint64_t>(size);

  if (cursor.reanchor_to_end || !cursor.path.has_value() || *cursor.path != path) {
    cursor.path = path;
    cursor.offset = file_len;
    cursor.reanchor_to_end = false;
    return {};
  }
  if (file_len < cursor.offset) cursor.offset = 0;
  if (file_len == cursor.offset) return {};

  const uint64_t floor_start =
      file_len > kMaxKernelLogIncrementalBytes ? file_len - kMaxKernelLogIncrementalBytes : 0;
  const uint64_t start = (std::max)(cursor.offset, floor_start);
  std::ifstream in(std::filesystem::path(path), std::ios::binary);
  if (!in.is_open()) return {};
  in.seekg(static_cast<std::streamoff>(start), std::ios::beg);
  if (!in) return {};
  const uint64_t want = (std::min)(file_len - start, kMaxKernelLogIncrementalBytes);
  std::string buf(static_cast<size_t>(want), '\0');
  in.read(buf.data(), static_cast<std::streamsize>(want));
  buf.resize(static_cast<size_t>(in.gcount()));
  if (buf.empty()) return {};

  const size_t last_nl = buf.rfind('\n');
  if (last_nl == std::string::npos) return {};
  const size_t consumed = last_nl + 1;
  cursor.offset = start + consumed;

  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos < consumed) {
    size_t nl = buf.find('\n', pos);
    if (nl == std::string::npos || nl >= consumed) nl = consumed - 1;
    std::string line = buf.substr(pos, nl - pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(std::move(line));
    pos = nl + 1;
  }
  return lines;
}

// ---------- ProxyDialFailureTracker ----------

void CoreMonitor::ProxyDialFailureTracker::on_node_checked(const std::string& active_node,
                                                           int64_t now_utc_ms) {
  if (!active_node_.has_value() || *active_node_ != active_node) {
    active_node_ = active_node;
    active_since_utc_ms_ = now_utc_ms;
    failure_ms_.clear();
    interface_changed_at_ms_.reset();
    last_probe_at_ms_.reset();
    cursor.reanchor_to_end = true;
  }
}

void CoreMonitor::ProxyDialFailureTracker::reset_for_manual_switch(int64_t now_utc_ms) {
  active_since_utc_ms_ = now_utc_ms;
  failure_ms_.clear();
  interface_changed_at_ms_.reset();
  last_probe_at_ms_.reset();
  cursor.reanchor_to_end = true;
}

void CoreMonitor::ProxyDialFailureTracker::prune(int64_t now_ms) {
  failure_ms_.erase(std::remove_if(failure_ms_.begin(), failure_ms_.end(),
                                   [&](int64_t t) {
                                     return (now_ms - t) / 1000 >= kDialFailWindowSecs;
                                   }),
                    failure_ms_.end());
  if (interface_changed_at_ms_.has_value() &&
      (now_ms - *interface_changed_at_ms_) / 1000 >= kTunInterfaceChangedTtlSecs) {
    interface_changed_at_ms_.reset();
  }
}

void CoreMonitor::ProxyDialFailureTracker::ingest_log_line(std::string_view line,
                                                           int64_t now_ms,
                                                           int64_t now_utc_ms) {
  const auto event = classify_kernel_log_event(line);
  if (!event.has_value()) return;
  int64_t event_ms = now_ms;
  if (const auto ts = parse_mihomo_log_timestamp_ms(line)) {
    if (*event != KernelLogEvent::TunInterfaceLost && active_since_utc_ms_.has_value() &&
        *ts < *active_since_utc_ms_) {
      return;
    }
    // 与 chrono num_seconds 一致：向零截断
    const int64_t age_secs = (now_utc_ms - *ts) / 1000;
    const int64_t max_age = (*event == KernelLogEvent::ProxyDialFailure)
                                ? kDialFailWindowSecs
                                : kTunEventMaxAgeSecs;
    if (age_secs >= max_age) return;
    event_ms = now_ms - (std::max<int64_t>)(age_secs, 0) * 1000;
  }
  switch (*event) {
    case KernelLogEvent::TunInterfaceLost:
      reset_flowing_traffic_state();
      request_force_online_recheck();
      break;
    case KernelLogEvent::TunInterfaceChanged:
      reset_flowing_traffic_state();
      request_force_online_recheck();
      interface_changed_at_ms_ = now_ms;
      break;
    case KernelLogEvent::ProxyDialFailure:
      prune(now_ms);
      if (failure_ms_.size() >= 64) failure_ms_.erase(failure_ms_.begin());
      failure_ms_.push_back(event_ms);
      break;
  }
}

std::pair<size_t, bool> CoreMonitor::ProxyDialFailureTracker::evaluate_burst(int64_t now_ms) {
  prune(now_ms);
  const size_t count = failure_ms_.size();
  const bool cooldown_active = last_probe_at_ms_.has_value() &&
                               (now_ms - *last_probe_at_ms_) < kDialFailReprobeCooldownSecs * 1000;
  return {count, count >= kDialFailReprobeThreshold && !cooldown_active};
}

std::optional<CoreMonitor::KernelReprobeSignal>
CoreMonitor::ProxyDialFailureTracker::evaluate_reprobe_signal(int64_t now_ms) {
  prune(now_ms);
  const bool cooldown_active = last_probe_at_ms_.has_value() &&
                               (now_ms - *last_probe_at_ms_) < kDialFailReprobeCooldownSecs * 1000;
  if (cooldown_active) return std::nullopt;
  if (failure_ms_.size() >= kDialFailReprobeThreshold) {
    return KernelReprobeSignal{KernelReprobeReason::DialFailureBurst, failure_ms_.size()};
  }
  if (interface_changed_at_ms_.has_value()) {
    return KernelReprobeSignal{KernelReprobeReason::TunInterfaceChanged, 0};
  }
  return std::nullopt;
}

void CoreMonitor::ProxyDialFailureTracker::consume_burst_for_probe(int64_t now_ms) {
  failure_ms_.clear();
  interface_changed_at_ms_.reset();
  last_probe_at_ms_ = now_ms;
}

// ---------- 全局追踪器入口 ----------

void CoreMonitor::record_kernel_log_line(std::string_view line) {
  if (!contains(line, "dial PROXY ") && !contains(line, "[TUN] default interface ")) return;
  const int64_t now_ms = mono_now_ms();
  const int64_t now_utc = utc_now_ms();
  std::lock_guard<std::mutex> lock(g_dial_tracker_mu);
  if (!g_dial_tracker.has_value()) g_dial_tracker.emplace();
  g_dial_tracker->ingest_log_line(line, now_ms, now_utc);
}

void CoreMonitor::sync_service_kernel_logs(const std::wstring& log_path,
                                           const std::optional<std::string>& expected_active) {
  if (log_path.empty()) return;
  IncrementalLogCursor cursor;
  {
    std::lock_guard<std::mutex> lock(g_dial_tracker_mu);
    if (!g_dial_tracker.has_value()) g_dial_tracker.emplace();
    cursor = g_dial_tracker->cursor;
  }
  auto lines = read_incremental_log_lines(log_path, cursor);
  const int64_t now_ms = mono_now_ms();
  const int64_t now_utc = utc_now_ms();
  std::lock_guard<std::mutex> lock(g_dial_tracker_mu);
  if (!g_dial_tracker.has_value()) g_dial_tracker.emplace();
  if (!expected_active.has_value() ||
      (g_dial_tracker->active_node().has_value() &&
       *g_dial_tracker->active_node() == *expected_active)) {
    g_dial_tracker->cursor = cursor;
    for (const auto& l : lines) g_dial_tracker->ingest_log_line(l, now_ms, now_utc);
  }
}

std::optional<CoreMonitor::KernelReprobeSignal> CoreMonitor::poll_active_node_dial_failures(
    const std::string& active_node, const std::wstring& service_log_path) {
  {
    std::lock_guard<std::mutex> lock(g_dial_tracker_mu);
    if (!g_dial_tracker.has_value()) g_dial_tracker.emplace();
    g_dial_tracker->on_node_checked(active_node, utc_now_ms());
  }
  if (!service_log_path.empty()) sync_service_kernel_logs(service_log_path, active_node);
  std::lock_guard<std::mutex> lock(g_dial_tracker_mu);
  if (!g_dial_tracker.has_value()) g_dial_tracker.emplace();
  return g_dial_tracker->evaluate_reprobe_signal(mono_now_ms());
}

void CoreMonitor::consume_kernel_reprobe_signal() {
  std::lock_guard<std::mutex> lock(g_dial_tracker_mu);
  if (g_dial_tracker.has_value()) g_dial_tracker->consume_burst_for_probe(mono_now_ms());
}

void CoreMonitor::reset_kernel_log_observer() {
  {
    std::lock_guard<std::mutex> lock(g_dial_tracker_mu);
    g_dial_tracker.reset();
  }
  {
    std::lock_guard<std::mutex> lock(g_health_obs_mu);
    g_last_health_obs.reset();
  }
  g_force_online_recheck.store(false, std::memory_order_release);
}

void CoreMonitor::reset_kernel_observer_for_manual_switch() {
  std::lock_guard<std::mutex> lock(g_dial_tracker_mu);
  if (g_dial_tracker.has_value()) g_dial_tracker->reset_for_manual_switch(utc_now_ms());
}

void CoreMonitor::request_force_online_recheck() {
  g_force_online_recheck.store(true, std::memory_order_release);
}

bool CoreMonitor::take_force_online_recheck() {
  return g_force_online_recheck.exchange(false, std::memory_order_acq_rel);
}

bool CoreMonitor::peek_force_online_recheck() {
  return g_force_online_recheck.load(std::memory_order_acquire);
}

// ---------- BUG-417 纯函数 ----------

// ← probe.rs is_valid_probe_delay：[NODE_DELAY_MIN_MS, NODE_DELAY_MAX_MS)
bool CoreMonitor::is_valid_probe_delay(uint32_t delay) {
  return delay >= kNodeDelayMinMs && delay < kNodeDelayMaxMs;
}

bool CoreMonitor::has_any_valid_real_probe_delay_with(
    const std::map<std::string, uint32_t>& delays,
    const std::function<bool(const std::string&)>& is_fake_node) {
  for (const auto& [name, d] : delays) {
    const std::string lower = ascii_lower(name);
    if (lower == "direct" || lower == "reject") continue;
    if (util::is_dummy_node(name)) continue;
    if (is_fake_node && is_fake_node(name)) continue;
    if (is_valid_probe_delay(d)) return true;
  }
  return false;
}

bool CoreMonitor::has_any_valid_real_probe_delay(const std::map<std::string, uint32_t>& delays) {
  return has_any_valid_real_probe_delay_with(
      delays, [](const std::string& n) { return util::is_confirmed_fake_node(n); });
}

// ← verdict.rs is_active_excused_by_primary_distortion：禁止其它节点「替考」免死
bool CoreMonitor::is_active_excused_by_primary_distortion(
    bool primary_url_distorted, const std::string& active_node,
    const std::map<std::string, uint32_t>& probed_delays) {
  if (!primary_url_distorted) return false;
  const auto it = probed_delays.find(active_node);
  return it != probed_delays.end() && is_valid_probe_delay(it->second);
}

// ← daemon.rs should_arm_switch_grace_on_node_change：首次观测（None -> Some）不开伪宽限期
bool CoreMonitor::should_arm_switch_grace_on_node_change(
    const std::optional<std::string>& prev_active, const std::string& new_active,
    bool existing_grace_active) {
  return prev_active.has_value() && *prev_active != new_active && !existing_grace_active;
}

// ← daemon.rs should_seed_boot_probe_failure
bool CoreMonitor::should_seed_boot_probe_failure(
    const std::optional<std::string>& active_node, bool selected, bool live_probe,
    const std::vector<std::pair<std::string, uint32_t>>& display) {
  if (selected || !live_probe) return false;
  if (!active_node.has_value() || active_node->empty()) return false;
  const std::string& active = *active_node;
  for (const auto& [name, d] : display) {
    if (name == active && is_valid_probe_delay(d)) return false;
  }
  for (const auto& [name, d] : display) {
    if (name != active && is_valid_probe_delay(d)) return true;
  }
  return false;
}

// ---------- BUG-415 稳态摘要 ----------

void CoreMonitor::record_health_observation(ActiveHealthObservation obs) {
  std::lock_guard<std::mutex> lock(g_health_obs_mu);
  g_last_health_obs = std::move(obs);
}

std::optional<CoreMonitor::ActiveHealthObservation> CoreMonitor::last_health_observation() {
  std::lock_guard<std::mutex> lock(g_health_obs_mu);
  return g_last_health_obs;
}

const char* CoreMonitor::node_status_name(NodeStatusKind k) {
  switch (k) {
    case NodeStatusKind::Alive: return "Alive";
    case NodeStatusKind::Dead: return "Dead";
    case NodeStatusKind::Suspect: return "Suspect";
    default: return "Unknown";
  }
}

const char* CoreMonitor::traffic_state_name(ActiveTrafficState s) {
  switch (s) {
    case ActiveTrafficState::Flowing: return "Flowing";
    case ActiveTrafficState::Stalled: return "Stalled";
    default: return "Idle";
  }
}

void CoreMonitor::maybe_log_steady_state_summary(int64_t now_ms, bool online) {
  int64_t start = daemon_start_ms_.load();
  if (start < 0) {
    daemon_start_ms_.store(now_ms);
    start = now_ms;
  }
  int64_t last = last_summary_log_ms_.load();
  if (last < 0) {
    last_summary_log_ms_.store(now_ms);
    return;
  }
  if (now_ms - last < kMonitorSummaryIntervalSecs * 1000) return;
  last_summary_log_ms_.store(now_ms);
  const int64_t uptime = (now_ms - start) / 1000;
  std::string active;
  {
    std::lock_guard<std::mutex> lock(last_node_m_);
    active = last_active_node_;
  }
  size_t brownouts = 0;
  {
    std::lock_guard<std::mutex> lock(brownout_m_);
    brownouts = recent_brownout_episodes_ms_.size();
  }
  if (!online) {
    CWLOG_INFO(std::format("[后台监测] 周期状态摘要: online=false, active={}, uptime={}s",
                           active.empty() ? "None" : active, uptime));
    return;
  }
  if (const auto obs = last_health_observation()) {
    CWLOG_INFO(std::format(
        "[后台监测] 周期状态摘要: node={}, status={}, traffic={}, last_probe_age={}s, "
        "probed={}, fails={}, brownouts={}, uptime={}s",
        obs->active_node, node_status_name(obs->status.kind),
        traffic_state_name(obs->traffic_state),
        obs->history_age_secs ? std::to_string(*obs->history_age_secs) : std::string("None"),
        obs->probed_this_round, node_fail_count_.load(), brownouts, uptime));
  } else {
    CWLOG_INFO(std::format(
        "[后台监测] 周期状态摘要: active={}, fails={}, api_errors={}, online={}, uptime={}s",
        active.empty() ? "None" : active, node_fail_count_.load(), api_fail_count_.load(),
        online, uptime));
  }
}

}  // namespace clashwin::core
