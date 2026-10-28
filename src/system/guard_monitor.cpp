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

}  // namespace clashwin::system
