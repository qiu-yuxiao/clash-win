#pragma once
// 窗口断点状态机（T11）——唯一事实源。
// ← src/constants.ts（MIN_WINDOW/MAX_WINDOW/MINI_*_THRESHOLD）+
//   utils/mini-window.ts（物理域判定 + MINI_PHYS_SLACK）+
//   providers/window/window-provider.tsx（isLargeMode/isDecorationsHidden）
// 常量口径：logical px、**内尺寸/客户区**（原版 shadow(false) outer==inner，
// 我们 WM_NCCALCSIZE 去非客户区，同样 outer==inner，两口径数值一致）。
// 「窗口几何下限」271×126 与「排版断点」285/126 是两件事（见 constants.ts 注释），
// 取值相同语义不同，禁止合并。
#include <cstdint>
#include <functional>
#include <vector>

namespace clashwin::ui::win {

// ---- 窗口几何常量（← constants.ts / resolve/window.rs） ----
inline constexpr double kMinWindowW = 271;    // MIN_WINDOW_WIDTH（几何下限）
inline constexpr double kMinWindowH = 126;    // MIN_WINDOW_HEIGHT
inline constexpr double kMaxWindowW = 640;    // MAX_WINDOW_WIDTH
inline constexpr double kMaxWindowH = 860;    // MAX_WINDOW_HEIGHT
inline constexpr double kDefaultWindowW = 271;  // DEFAULT_WIDTH（resolve/window.rs）
inline constexpr double kDefaultWindowH = 680;  // DEFAULT_HEIGHT

// ---- 排版断点（仅前端语义） ----
inline constexpr double kMiniWidthThreshold = 285;   // 窄窗/宽窗分界
inline constexpr double kMiniHeightThreshold = 126;  // 极小窗高度门槛
// 物理域判定容差（← mini-window.ts MINI_PHYS_SLACK：125%/175% 档取整方向）
inline constexpr double kMiniPhysSlack = 1;

// ---- 主场景骨架常量（← _layout.tsx:119-120 / window-provider FEAT-003） ----
inline constexpr int kTrafficPaneWide = 135;     // TRAFFIC_PANE_HEIGHT_WIDE
inline constexpr int kTrafficPaneMinimal = 100;  // TRAFFIC_PANE_HEIGHT_MINIMAL
inline constexpr int kTitleBarH = 30;            // 标题栏高
inline constexpr int kIdleHideDelayMs = 10000;   // FEAT-003 空闲隐藏标题栏

// 大尺寸模式判定容差（逻辑域 ±1，← isLargeSize）
inline constexpr double kLargeSizeTol = 1;

// ---- 状态机 ----
enum class WinState { Normal, Narrow, Mini, Large };

class Breakpoints {
 public:
  // 权威判定入口：窗口**内尺寸物理像素**（onResized payload / GetClientRect×dpi）。
  // 禁止传页面视口类粗估（整数 CSS 像素会吃掉 1 物理像素容差）。
  // 物理域判定：physW ≤ 285×sf + 1（← isMinimalWidthByPhysical 逐条对照）
  void set_physical_size(int phys_w, int phys_h, double scale_factor);
  // 逻辑域便捷入口（阈值直比，无物理容差语义）
  void set_logical_size(double w, double h);

  // 大尺寸模式（toggleMaximize 显式切换；启动恢复 640×860 时自动标记）
  void set_large(bool large);
  static bool is_large_size(double logical_w, double logical_h) {
    return (logical_w - kMaxWindowW) < kLargeSizeTol &&
           (logical_w - kMaxWindowW) > -kLargeSizeTol &&
           (logical_h - kMaxWindowH) < kLargeSizeTol &&
           (logical_h - kMaxWindowH) > -kLargeSizeTol;
  }

  // 派生量（← WindowContext）
  WinState state() const {
    if (large_) return WinState::Large;
    if (mini_) return WinState::Mini;
    if (narrow_) return WinState::Narrow;
    return WinState::Normal;
  }
  bool is_large() const { return large_; }
  bool is_narrow() const { return narrow_; }        // isMinimalWidth
  bool is_mini() const { return mini_; }            // isMiniStatus
  double logical_w() const { return logical_w_; }
  double logical_h() const { return logical_h_; }

  // 转换通知（T13/T14 据此挂载卸载；prev==cur 不通知）
  uint64_t subscribe(std::function<void(WinState prev, WinState cur)> cb);
  void unsubscribe(uint64_t id);

 private:
  void notify(WinState prev);

  bool large_ = false;
  bool narrow_ = false;  // isMinimalWidth
  bool mini_ = false;    // isMiniStatus
  double logical_w_ = kDefaultWindowW, logical_h_ = kDefaultWindowH;
  std::vector<std::pair<uint64_t, std::function<void(WinState, WinState)>>> subs_;
  uint64_t next_id_ = 1;
};

}  // namespace clashwin::ui::win
