#pragma once
// Lifecycle（T19）：应用生命周期与轻量模式管理
// - 原子状态跃迁：Normal (0) -> InLightweight (1) -> Exiting (2) -> Normal (0)
// - entry_lightweight_mode: 校验配置/force、隐藏窗口、释放 D2D、断开 WS、静音 EventBus、更新托盘、唤醒 CoreMonitor
// - exit_lightweight_mode: 恢复 EventBus、重建 D2D、显示激活窗口、重连 WS、更新托盘、唤醒 CoreMonitor

#include <cstdint>
#include <atomic>
#include <mutex>

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
};

enum class LifecycleState : uint8_t {
  Normal = 0,
  InLightweight = 1,
  Exiting = 2
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

  // force=true (比如从菜单/命令显式触发) 忽略 enable_auto_light_weight_mode 配置直接进入轻量模式
  // force=false (比如点窗口 X 按钮) 则先读配置，若未开则只 hide 窗口
  bool entry_lightweight_mode(bool force = false);

  // 退出轻量模式
  bool exit_lightweight_mode();

  // 重置状态（测试用）
  void reset();

 private:
  Lifecycle() = default;
  std::mutex mutex_;
  std::atomic<uint8_t> state_{static_cast<uint8_t>(LifecycleState::Normal)};
  ILifecycleWindow* window_ = nullptr;
  system::Tray* tray_ = nullptr;
};

}  // namespace clashwin::app
