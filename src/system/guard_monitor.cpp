// T18 代理守护实现（SysproxyGuard 防篡改 + CoreMonitor 守护线程）。
#include "system/guard_monitor.h"

#include <windows.h>

#include "util/log.h"

namespace clashwin::system {

// ---------- SysproxyGuard（← sysproxy-rs GuardMonitor） ----------
SysproxyGuard& SysproxyGuard::instance() {
  static SysproxyGuard s;
  return s;
}

void SysproxyGuard::set_interval(std::chrono::seconds s) {
  interval_secs_.store(static_cast<uint64_t>(s.count()));
  cv_.notify_all();  // interval 变更即时生效
}

uint64_t SysproxyGuard::interval_secs() const {
  return interval_secs_.load();
}

void SysproxyGuard::set_guard_type(GuardType t, const SysProxyState& sys,
                                   const AutoProxyState& auto_proxy) {
  {
    std::lock_guard<std::mutex> lock(m_);
    type_ = t;
    sys_ = sys;
    auto_ = auto_proxy;
  }
  if (t == GuardType::None) {
    // ← sysopt.rs:93,98,159,217：None 即停（不再向 OS 补写代理）
    stop();
  } else if (running_.load()) {
    cv_.notify_all();  // 运行中切类型 → 唤醒立即按新状态重写一次
  }
}

GuardType SysproxyGuard::guard_type() const {
  std::lock_guard<std::mutex> lock(m_);
  return type_;
}

void SysproxyGuard::set_rewrite(std::function<void(
    GuardType, const SysProxyState&, const AutoProxyState&)> fn) {
  std::lock_guard<std::mutex> lock(m_);
  rewrite_ = std::move(fn);
}

void SysproxyGuard::tick_once() {
  // ← GuardMonitor 周期体检：按当前 GuardType 重写 OS 代理（防篡改复原）
  std::function<void(GuardType, const SysProxyState&, const AutoProxyState&)>
      fn;
  GuardType t;
  SysProxyState sys;
  AutoProxyState auto_proxy;
  {
    std::lock_guard<std::mutex> lock(m_);
    fn = rewrite_;
    t = type_;
    sys = sys_;
    auto_proxy = auto_;
  }
  if (!fn || t == GuardType::None) return;
  fn(t, sys, auto_proxy);
}

void SysproxyGuard::start() {
  if (running_.load()) return;
  {
    std::lock_guard<std::mutex> lock(m_);
    if (type_ == GuardType::None) return;  // ← sysopt.rs：None 不启动
  }
  running_.store(true);
  ensure_thread();
}

void SysproxyGuard::stop() {
  if (!running_.exchange(false)) return;
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void SysproxyGuard::ensure_thread() {
  worker_ = std::thread([this] { worker_loop(); });
}

void SysproxyGuard::worker_loop() {
  while (running_.load()) {
    uint64_t interval = interval_secs_.load();
    // 分片等待：interval 内可被 stop/类型切换唤醒
    {
      std::unique_lock<std::mutex> lock(m_);
      cv_.wait_for(lock, std::chrono::seconds(interval),
                   [this] { return !running_.load(); });
    }
    if (!running_.load()) break;
    tick_once();
  }
}

// ---------- CoreMonitor（← module/monitor.rs 语义子集） ----------
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

void CoreMonitor::start() {
  if (running_.exchange(true)) return;
  worker_ = std::thread([this] {
    while (running_.load()) {
      int interval;
      {
        std::lock_guard<std::mutex> lock(m_);
        // ← census §70：正常 15s / 重试 3s / 离线 5s
        interval = api_fail_count_.load() > 0 || node_fail_count_.load() > 0
                       ? params_.retry_interval_secs
                       : params_.normal_interval_secs;
        if (hooks_.network_online && !hooks_.network_online())
          interval = params_.offline_interval_secs;
      }
      // 分片等待（stop 即时退出）
      for (int waited = 0; running_.load() && waited < interval; waited += 1) {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait_for(lock, std::chrono::seconds(1));
      }
      if (!running_.load()) break;
      tick_once();
    }
  });
}

void CoreMonitor::stop() {
  if (!running_.exchange(false)) return;
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
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

std::vector<std::string> CoreMonitor::snapshot_failed_servers() {
  int64_t now = hooks_.now_ms ? hooks_.now_ms()
                              : static_cast<int64_t>(GetTickCount64());
  std::vector<std::string> out;
  std::lock_guard<std::mutex> lock(failed_m_);
  for (auto it = failed_servers_.begin(); it != failed_servers_.end();) {
    if (now - it->second >= params_.failed_server_cooldown_ms) {
      it = failed_servers_.erase(it);  // ← 冷却过期移除（自动续期由再失败触发）
    } else {
      out.push_back(it->first);
      ++it;
    }
  }
  return out;
}

CoreMonitor::Action CoreMonitor::heal(const std::string& uid) {
  // ← monitor.rs self_heal：60s 冷却 + BUSY 不算失败
  int64_t now = hooks_.now_ms ? hooks_.now_ms()
                              : static_cast<int64_t>(GetTickCount64());
  int64_t last = last_heal_ms_.load();
  // last==0 = 从未自愈（首启不冷却；真实时钟 GetTickCount64 非零起）
  if (last != 0 && now - last < params_.heal_cooldown_ms) {
    CWLOG_INFO("CoreMonitor: 自愈冷却中，跳过");
    return Action::Idle;
  }
  last_heal_ms_.store(now);
  if (!hooks_.auto_select_trigger || uid.empty()) return Action::Idle;
  auto r = hooks_.auto_select_trigger();
  if (r.is_ok() && r.value()) {
    // ← 自愈成功：清零判死/失败计数
    api_fail_count_.store(0);
    node_fail_count_.store(0);
    heal_fail_count_.store(0);
    return Action::Healed;
  }
  if (r.is_ok() && !r.value()) {
    // BUSY（AutoSelect 占用）→ 不计入失败、不重置冷却（下轮再试）
    return Action::Idle;
  }
  // ← monitor.rs：自愈失败 → heal_fail++；连续 3 次 → API 不可达则
  //   restart_core()；restart 失败 → 警报（census「连续 5 次失败弹窗」
  //   对应 fire_self_heal_alert 在 restart 失败路径触发）
  int fails = heal_fail_count_.fetch_add(1) + 1;
  if (fails >= params_.heal_fail_threshold) {
    heal_fail_count_.store(0);
    bool api_ok = hooks_.api_alive ? hooks_.api_alive() : true;
    if (api_ok) {
      CWLOG_INFO("CoreMonitor: 自愈连续失败但内核 API 可达（节点侧问题），跳过重启");
      return Action::Idle;
    }
    CWLOG_WARN("CoreMonitor: 自愈连续失败且 API 不可达 → restart_core");
    if (hooks_.restart_core && hooks_.restart_core().is_ok()) {
      return Action::RestartedOnHealFail;
    }
    if (hooks_.alert) {
      hooks_.alert(L"Clash Mini", L"内核守护自愈连续失败，已尝试重启内核仍未恢复，请检查内核状态。");
      return Action::Alerted;
    }
  }
  return Action::Idle;
}

void CoreMonitor::reset_state() {
  api_fail_count_.store(0);
  node_fail_count_.store(0);
  heal_fail_count_.store(0);
  was_offline_.store(false);
  last_heal_ms_.store(0);
  last_tick_ms_.store(0);
  std::lock_guard<std::mutex> lock(failed_m_);
  failed_servers_.clear();
}

CoreMonitor::Action CoreMonitor::tick_once() {
  last_tick_ms_.store(hooks_.now_ms ? hooks_.now_ms()
                                    : static_cast<int64_t>(GetTickCount64()));
  Hooks h;
  Params p;
  {
    std::lock_guard<std::mutex> lock(m_);
    h = hooks_;
    p = params_;
  }

  // ← census §70(b)：进程级直检（本实现差异：立即重启，见头注声明）
  if (h.core_alive && !h.core_alive()) {
    CWLOG_WARN("CoreMonitor: 内核进程静默退出（无退出日志）→ 自动重启");
    if (h.restart_core && h.restart_core().is_ok()) return Action::Restarted;
    return Action::Idle;
  }

  // ← monitor.rs：物理网络离线 → 跳过检测（离线态标记）
  bool online = h.network_online ? h.network_online() : true;
  if (!online) {
    was_offline_.store(true);
    return Action::Idle;
  }
  // 离线 → 在线转移：清零计数（原版 reset 后立即自愈一次）
  if (was_offline_.exchange(false)) {
    api_fail_count_.store(0);
    node_fail_count_.store(0);
    std::string uid = h.current_uid ? h.current_uid() : std::string();
    if (!uid.empty()) return heal(uid);
    return Action::Idle;
  }

  // API 存活：连续 3 次不可达 → 自愈（原版判死主路径）
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

  // 节点健康：连续 2 次判死 → 自愈
  bool node_ok = h.node_healthy ? h.node_healthy() : true;
  if (!node_ok) {
    int fails = node_fail_count_.fetch_add(1) + 1;
    if (fails >= p.node_dead_threshold) {
      node_fail_count_.store(0);
      std::string uid = h.current_uid ? h.current_uid() : std::string();
      return heal(uid);
    }
    return Action::Idle;
  }
  node_fail_count_.store(0);
  return Action::Idle;
}

// ---------- wait_for_clash_ready（T16 留位转正） ----------
bool CoreMonitor::wait_for_clash_ready(const ReadyHooks& h) {
  // 三阶段预算：API 30s → 组列表 20s → 物理 15s（← census 启动等待口径；
  // 简化自原版四阶段，DNS 阶段并入物理网络探测）
  auto poll = [&](std::function<bool()> ready, int budget_ms) {
    if (!ready) return true;  // 钩子缺失 → 该阶段视为立即通过
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

}  // namespace clashwin::system
