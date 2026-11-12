#pragma once
// Lifecycle（T19/T23）：应用生命周期、轻量模式与7步优雅退出管理
// - 原子状态：Normal (0), InLightweight (1), PreparingExit (2), CleaningAsync (3), Exited (4)
// - entry_lightweight_mode: 校验配置/force、隐藏窗口、释放 D2D、断开 WS、静音 EventBus、更新托盘、唤醒 CoreMonitor
// - exit_lightweight_mode: 恢复 EventBus、重建 D2D、显示激活窗口、重连 WS、更新托盘、唤醒 CoreMonitor
// - 7步退出 quit():
//   Step 1: on_prepare_exit 回调 + 停止 monitors (CoreMonitor, SysproxyGuard)
//   Step 2: 停止 timers (ProfileTimer) + 断开 WebSocket 订阅 (clear_ws)
//   Step 3: 200ms grace period + 关闭内嵌服务 (stop_server)
//   Step 4: 刷新窗口几何 (save_geometry) + 提交并保存配置 (save_configs)
//   Step 5 & 6 (并行 clean_async):
//     Step 5: WinINET/RAS 系统代理复原 (reset_sysproxy，1500ms 超时预算)
//     Step 6: TUN 禁用 PATCH (1000ms 超时) + 停止内核 (stop_core，2000ms 超时) + 清理残留内核 (kill_cores)
//   Step 7: 刷新日志 (flush_logs) + 进程退出 (exit_process，清理均成功则退出码 0，否则 1)
// - restart_app():
//   快照当前代理节点并落盘 profiles.yaml，执行 prepare_exit + clean_async，拉起新进程并退出当前进程

#include <cstdint>
#include <atomic>
#include <mutex>
#include <functional>
#include <string>
#include <vector>
#include <optional>

#include "util/result.h"

namespace clashwin::system {
class Tray;
}

namespace clashwin::app {

struct ILifecycleWindow {
  virtual ~ILifecycleWindow() = default;
  virtual void hide() = 0;
  virtual void show_and_activate() = 0;
  virtual void release_d2d_resources() = 0;
  virtual void restore_d2d_resources() = 0;
  virtual void disconnect_ws() = 0;
  virtual void reconnect_ws() = 0;
  virtual void save_geometry(bool immediately) = 0;
};

enum class LifecycleState : uint8_t {
  Normal = 0,
  InLightweight = 1,
  PreparingExit = 2,
  CleaningAsync = 3,
  Exited = 4
};

// 依赖注入钩子结构：用于 100% 隔离测试，保证零实机污染
struct QuitHooks {
  std::function<void()> on_prepare_exit = nullptr;
  std::function<Result<void>()> reset_sysproxy = nullptr;
  std::function<Result<void>()> patch_disable_tun = nullptr;
  std::function<Result<void>()> stop_core = nullptr;
  std::function<std::vector<uint32_t>()> kill_cores = nullptr;
  std::function<Result<void>()> stop_service = nullptr;
  std::function<void()> save_configs = nullptr;
  std::function<void(bool immediately)> save_window_state = nullptr;
  std::function<void()> stop_server = nullptr;
  std::function<void()> stop_timers = nullptr;
  std::function<void()> stop_monitors = nullptr;
  std::function<void()> clear_ws = nullptr;
  std::function<void()> flush_logs = nullptr;
  std::function<bool(const std::wstring& exe, const std::wstring& cmd)> launch_process = nullptr;
  std::function<void(int exit_code)> exit_process = nullptr;
  // BUG-331: 无锁服务优先停核与模式漂移兜底钩子（reset_hooks 装配生产实现；
  // 旧测试默认 QuitHooks{} 保持 nullptr 以沿用传统 stop_core 2000ms 分支）
  std::function<bool()> is_service_or_tun_mode = nullptr;
  std::function<Result<void>()> stop_core_by_service_direct = nullptr;
  std::function<bool(uint32_t max_wait_ms)> wait_cores_exit = nullptr;
  std::function<bool()> are_cores_running = nullptr;
  std::function<bool()> is_service_installed = nullptr;
  std::function<void()> cleanup_after_service_stop = nullptr;
  uint32_t grace_sleep_ms = 200;
};

class Lifecycle {
 public:
  static Lifecycle& instance();

  void set_window(ILifecycleWindow* win) { window_ = win; }
  ILifecycleWindow* window() const { return window_; }

  void set_tray(system::Tray* tray) { tray_ = tray; }
  system::Tray* tray() const { return tray_; }

  LifecycleState state() const {
    return static_cast<LifecycleState>(state_.load(std::memory_order_acquire));
  }

  bool is_in_lightweight_mode() const {
    return state() == LifecycleState::InLightweight;
  }

  bool is_exiting() const {
    auto s = state();
    return s == LifecycleState::PreparingExit ||
           s == LifecycleState::CleaningAsync ||
           s == LifecycleState::Exited;
  }

  // force=true (比如从菜单/命令显式触发) 忽略 enable_auto_light_weight_mode 配置直接进入轻量模式
  // force=false (比如点窗口 X 按钮) 则先读配置，若未开则只 hide 窗口
  bool entry_lightweight_mode(bool force = false);

  // 退出轻量模式
  bool exit_lightweight_mode();

  // T23: 7步退出流程
  // cleanup_success 为 true 时退出码 0，否则退出码 1
  bool quit(int* out_exit_code = nullptr);

  // T23: 重启应用程序（保留活动节点与窗口几何）
  bool restart_app();

  // 步骤 1-4：准备退出
  void prepare_exit();

  // 步骤 5-6：并行资源清理与超时预算控制
  bool clean_async();

  // 依赖注入钩子（供单元/集成测试 mock 隔离）
  void set_hooks(const QuitHooks& hooks) { hooks_ = hooks; }
  const QuitHooks& hooks() const { return hooks_; }
  void reset_hooks();

  // 重置状态（测试用）
  void reset();

 private:
  Lifecycle();
  ~Lifecycle() = default;

  std::mutex mutex_;
  std::atomic<uint8_t> state_{static_cast<uint8_t>(LifecycleState::Normal)};
  std::atomic<bool> is_cleaning_up_{false};
  ILifecycleWindow* window_ = nullptr;
  system::Tray* tray_ = nullptr;
  QuitHooks hooks_;
};

}  // namespace clashwin::app
