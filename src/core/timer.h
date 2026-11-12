#pragma once
// 订阅定时更新服务（T15）——← core/timer.rs 全语义移植：
// - 每 profile 的 update_interval（option.update_interval 分钟；缺省 1440）
// - 启动加载：已过期项**立即补跑**（run_expired_task）
// - running 标志防重入（mark_task_running）
// - 失败 → 下次重试间隔 = min(interval, 2) 分钟（finish_task）
// - async_task：profile_switching_lock CAS false→true，已锁定 → 跳过本次
//   （返回 false，同样按失败 2min 重试）
// - 事件：verge://profile-update-started / profile-update-completed（uid）
//   + timer-updated（下次更新时间，秒级 Unix）事件
// 时钟/执行注入（测试确定性）：now_ms/update_fn 可替换；生产绑定
// update_profile 命令链路（feat::update_profile 等价 = CommandService::invoke）。
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace clashwin::core {

class ProfileTimer {
 public:
  static ProfileTimer& instance();

  // update_fn(uid) → 成功与否（生产 = update_profile 命令等价链路；测试注入）
  void set_update_fn(std::function<bool(const std::string& uid)> fn);
  // 事件发射钩子（is_start；cmd 层接 EventBus 发
  // profile-update-started/completed——core 层不反向依赖 cmd/events）
  void set_event_emitter(std::function<void(const std::string& uid, bool is_start)> fn);
  // 时钟注入（毫秒；缺省 GetTickCount64）
  void set_now_ms(std::function<int64_t()> fn);

  // ← init/gen_map/apply_timer_map：provider 返回每 uid 的
  //   (interval_min, next_run_unix_ms)；next_run=0 → 立即补跑（加载时已过期/
  //   从无 updated 的项）；cmd 层按 updated*1000+interval*60k 计算
  void set_intervals_provider(std::function<
      std::map<std::string, std::pair<uint64_t, int64_t>>()> fn);

  void start();   // 启动调度线程（1s tick）；幂等
  void stop();    // 停止（退出序列）

  // 立即触发一次更新（手动更新/测试）；running 中跳过
  bool run_now(const std::string& uid);

  // ← get_next_update_time：下次更新 Unix 秒；无任务 → nullopt
  std::optional<int64_t> get_next_update_time(const std::string& uid) const;

  // 测试驱动：推进时钟并执行到期任务（同步调用 update_fn）
  // 返回本次补跑的 uid 数
  int tick_for_test();

  // 测试观察
  bool running(const std::string& uid) const;
  int64_t now_ms() const;
  void reset_for_test();

 private:
  ProfileTimer();

  struct TaskState {
    uint64_t interval_minutes = 1440;
    int64_t next_run_ms = 0;   // 时钟域 = now_ms
    bool running = false;
  };

  void scheduler_loop();
  void run_expired_locked(const std::string& uid);
  void finish_locked(const std::string& uid, bool success);
  bool execute(const std::string& uid);  // 锁 CAS + update_fn + 事件

  mutable std::mutex mutex_;
  std::map<std::string, TaskState> tasks_;
  std::function<bool(const std::string&)> update_fn_ =
      [](const std::string&) { return false; };
  std::function<int64_t()> now_ms_;  // 缺省 GetTickCount64（.cpp 构造赋值）
  std::function<std::map<std::string, std::pair<uint64_t, int64_t>>()>
      intervals_fn_;
  std::function<void(const std::string&, bool)> event_emitter_;
  std::thread worker_;
  bool started_ = false;
  std::atomic<bool> stop_{false};
};

}  // namespace clashwin::core
