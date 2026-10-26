// 订阅定时更新服务实现（T15）：语义对照 core/timer.rs（行号见 timer.h）。
#include "timer.h"

#include <windows.h>

#include <algorithm>

#include "switching_lock.h"
#include "util/log.h"

namespace clashwin::core {

// ← feat.rs CURRENT_SWITCHING_PROFILE（T15 上移至 core：cmd 层与 timer 共用）
std::atomic<bool>& profile_switching_lock() {
  static std::atomic<bool> lock{false};
  return lock;
}

ProfileTimer& ProfileTimer::instance() {
  static ProfileTimer t;
  t.now_ms_ = [] { return static_cast<int64_t>(GetTickCount64()); };
  return t;
}

void ProfileTimer::set_update_fn(std::function<bool(const std::string&)> fn) {
  std::lock_guard lk(mutex_);
  update_fn_ = std::move(fn);
}

void ProfileTimer::set_now_ms(std::function<int64_t()> fn) {
  std::lock_guard lk(mutex_);
  now_ms_ = std::move(fn);
}

void ProfileTimer::set_intervals_provider(std::function<
    std::map<std::string, std::pair<uint64_t, int64_t>>()> fn) {
  std::lock_guard lk(mutex_);
  intervals_fn_ = std::move(fn);
}

void ProfileTimer::set_event_emitter(
    std::function<void(const std::string&, bool)> fn) {
  std::lock_guard lk(mutex_);
  event_emitter_ = std::move(fn);
}

// ← init/gen_map/apply_timer_map：interval = option.update_interval 缺省 1440
void ProfileTimer::start() {
  std::lock_guard lk(mutex_);
  if (started_) return;
  started_ = true;
  stop_.store(false);
  if (intervals_fn_) {
    auto m = intervals_fn_();
    for (const auto& [uid, iv] : m) {
      TaskState st;
      st.interval_minutes = iv.first ? iv.first : 1440;
      st.next_run_ms = iv.second;  // 0 = 已过期/从未更新 → 立即补跑
      tasks_[uid] = st;
    }
    // timer-updated 事件（下次更新时间）由 cmd 层 emitter 钩子转发
  }
  worker_ = std::thread([this] { scheduler_loop(); });
  CWLOG_INFO("ProfileTimer: 启动（缺省 1440min；过期项即补跑）");
}

void ProfileTimer::stop() {
  stop_.store(true);
  if (worker_.joinable()) worker_.join();
  std::lock_guard lk(mutex_);
  started_ = false;
  CWLOG_INFO("ProfileTimer: 已停止");
}

// ← run_expired_task/mark_task_running/run_task_now
bool ProfileTimer::run_now(const std::string& uid) {
  std::lock_guard lk(mutex_);
  auto it = tasks_.find(uid);
  if (it == tasks_.end()) return false;
  if (it->second.running) return false;  // ← mark_task_running 防重入
  it->second.running = true;
  it->second.next_run_ms = 0;  // 摘除现有排程（key.take）
  // 同步执行（骨架：生产可改线程池）
  bool ok = execute(uid);
  finish_locked(uid, ok);
  return ok;
}

int ProfileTimer::tick_for_test() {
  std::lock_guard lk(mutex_);
  int64_t now = now_ms_();
  std::vector<std::string> expired;
  for (auto& [uid, st] : tasks_) {
    if (!st.running && now >= st.next_run_ms) {
      st.running = true;
      expired.push_back(uid);
    }
  }
  for (const auto& uid : expired) {
    bool ok = execute(uid);
    finish_locked(uid, ok);
  }
  return static_cast<int>(expired.size());
}

// ← async_task：CURRENT_SWITCHING_PROFILE CAS + 40s 超时 + 事件
bool ProfileTimer::execute(const std::string& uid) {
  auto& lock = profile_switching_lock();
  bool expected = false;
  if (!lock.compare_exchange_strong(expected, true)) {
    // ← timer.rs:388-397：切换/手动更新进行中 → 跳过本次（按失败处理重试）
    CWLOG_INFO("ProfileTimer: 切换进行中，跳过定时更新 uid=" + uid);
    return false;
  }
  bool success = false;
  if (event_emitter_) event_emitter_(uid, true);   // profile-update-started
  success = update_fn_(uid);
  if (event_emitter_) event_emitter_(uid, false);  // profile-update-completed
  lock.store(false);  // ← defer! store(false)
  return success;
}

// ← finish_task：success → 原 interval；失败 → min(interval, 2)
void ProfileTimer::finish_locked(const std::string& uid, bool success) {
  auto it = tasks_.find(uid);
  if (it == tasks_.end()) return;
  it->second.running = false;
  uint64_t interval = success
                          ? it->second.interval_minutes
                          : std::min(it->second.interval_minutes, 2ULL);
  it->second.next_run_ms = now_ms_() + static_cast<int64_t>(interval) * 60000;
}

// ← get_next_update_time
std::optional<int64_t> ProfileTimer::get_next_update_time(
    const std::string& uid) const {
  std::lock_guard lk(mutex_);
  auto it = tasks_.find(uid);
  if (it == tasks_.end()) return std::nullopt;
  // 秒级 Unix（Rust now.timestamp()）；now_ms 为任意毫秒域 → 换算差值
  int64_t now = now_ms_();
  int64_t delta_sec = (it->second.next_run_ms - now) / 1000;
  int64_t unix_now = static_cast<int64_t>(::time(nullptr));
  return unix_now + delta_sec;
}

bool ProfileTimer::running(const std::string& uid) const {
  std::lock_guard lk(mutex_);
  auto it = tasks_.find(uid);
  return it != tasks_.end() && it->second.running;
}

int64_t ProfileTimer::now_ms() const {
  std::lock_guard lk(mutex_);
  return now_ms_();
}

// 调度线程：1s 粒度到期检测（骨架同步执行；与原版 spawn 异步等价语义）
void ProfileTimer::scheduler_loop() {
  while (!stop_.load()) {
    Sleep(1000);
    if (stop_.load()) break;
    std::lock_guard lk(mutex_);
    int64_t now = now_ms_();
    std::vector<std::string> expired;
    for (auto& [uid, st] : tasks_) {
      if (!st.running && now >= st.next_run_ms) {
        st.running = true;
        expired.push_back(uid);
      }
    }
    for (const auto& uid : expired) {
      bool ok = execute(uid);
      finish_locked(uid, ok);
    }
  }
}

}  // namespace clashwin::core
