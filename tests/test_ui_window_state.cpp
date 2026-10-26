// T11 测试：断点状态机 + WM_SIZING 钳制 + stealth + 大尺寸模式（纯逻辑层）。
#include <windows.h>

#include "test_util.h"
#include "ui/main_window.h"

using namespace clashwin::ui::win;
using Bp = Breakpoints;

UT_TEST(t11_bp_transition_matrix) {
  // 271×680 默认 → Narrow（宽 271 ≤ 285 断点；窗口最小时必然落在窄窗模式
  // ← constants.ts「它比最小宽度大，是有意为之」）
  Bp bp;
  bp.set_logical_size(271, 680);
  UT_CHECK(bp.state() == WinState::Narrow);
  UT_CHECK(bp.is_narrow() && !bp.is_mini() && !bp.is_large());
  // Normal：宽 > 285
  bp.set_logical_size(300, 680);
  UT_CHECK(bp.state() == WinState::Normal);
  UT_CHECK(!bp.is_narrow() && !bp.is_mini() && !bp.is_large());

  // 285 边界（含）→ Narrow；286 → Normal
  bp.set_logical_size(285, 680);
  UT_CHECK(bp.state() == WinState::Narrow);
  bp.set_logical_size(286, 680);
  UT_CHECK(bp.state() == WinState::Normal);
  bp.set_logical_size(285, 680);
  UT_CHECK(bp.state() == WinState::Narrow);

  // 126 高边界（含）→ Mini；127 → Narrow
  bp.set_logical_size(271, 126);
  UT_CHECK(bp.state() == WinState::Mini);
  UT_CHECK(bp.is_mini() && bp.is_narrow());
  bp.set_logical_size(271, 127);
  UT_CHECK(bp.state() == WinState::Narrow);

  // 大尺寸显式：640×860 → Large（压制 narrow 判定）
  bp.set_logical_size(640, 860);
  UT_CHECK(bp.state() == WinState::Normal);  // 640 > 285，非 narrow
  bp.set_large(true);
  UT_CHECK(bp.state() == WinState::Large);
  bp.set_large(false);
  UT_CHECK(bp.state() == WinState::Normal);

  // 物理域判定（150%：285*1.5=427.5；+1 slack）
  bp.set_physical_size(427, 500, 1.5);  // 427 ≤ 428.5 → Narrow（284.67 逻辑）
  UT_CHECK(bp.state() == WinState::Narrow);
  bp.set_physical_size(430, 500, 1.5);  // 430 > 428.5 → Normal
  UT_CHECK(bp.state() == WinState::Normal);
  bp.set_physical_size(400, 189, 1.5);  // 高 126 逻辑 → Mini
  UT_CHECK(bp.state() == WinState::Mini);
  // 容差：物理 428（=285.33 逻辑 > 285）仍在容差内 → Narrow
  bp.set_physical_size(428, 500, 1.5);
  UT_CHECK(bp.state() == WinState::Narrow);
}

UT_TEST(t11_bp_transition_notifications) {
  Bp bp;
  std::vector<std::pair<WinState, WinState>> seen;
  uint64_t id = bp.subscribe([&](WinState p, WinState c) { seen.push_back({p, c}); });
  bp.set_logical_size(280, 680);   // Normal→Narrow
  bp.set_logical_size(280, 120);   // Narrow→Mini
  bp.set_logical_size(400, 680);   // Mini→Normal
  bp.set_logical_size(400, 680);   // 无变化不通知
  bp.set_large(true);              // Normal→Large
  bp.unsubscribe(id);
  bp.set_logical_size(280, 680);   // 已退订
  UT_CHECK_EQ(seen.size(), 4u);
  UT_CHECK(seen[0] == std::make_pair(WinState::Normal, WinState::Narrow));
  UT_CHECK(seen[1] == std::make_pair(WinState::Narrow, WinState::Mini));
  UT_CHECK(seen[2] == std::make_pair(WinState::Mini, WinState::Normal));
  UT_CHECK(seen[3] == std::make_pair(WinState::Normal, WinState::Large));
}

UT_TEST(t11_sizing_top_clamp) {
  // ← lib.rs sizing_wndproc：WMSZ_TOP* 提议高度 < min → top=bottom-minH
  const int min_h = 126;
  RECT rc{100, 0, 371, 100};  // 高 100 < 126
  UT_CHECK(clamp_sizing_top(WMSZ_TOP, rc, min_h));
  UT_CHECK_EQ(rc.top, 100 - 126);      // top = bottom - minH（防下移）
  UT_CHECK_EQ(rc.bottom, 100);
  // 左上角
  RECT rc2{100, 50, 371, 100};
  UT_CHECK(clamp_sizing_top(WMSZ_TOPLEFT, rc2, min_h));
  UT_CHECK_EQ(rc2.top, -26);
  // 右上角
  RECT rc3{100, 50, 371, 100};
  UT_CHECK(clamp_sizing_top(WMSZ_TOPRIGHT, rc3, min_h));
  UT_CHECK_EQ(rc3.top, -26);
  // 高度已足 → 不修正
  RECT rc4{100, 0, 371, 500};
  UT_CHECK(!clamp_sizing_top(WMSZ_TOP, rc4, min_h));
  // 非上缘方向不管（下缘缩放由 MINMAXINFO 钳制）
  RECT rc5{100, 0, 371, 100};
  UT_CHECK(!clamp_sizing_top(WMSZ_BOTTOM, rc5, min_h));
  UT_CHECK_EQ(rc5.top, 0);
}

UT_TEST(t11_stealth_idle_hide) {
  StealthController s;
  int changes = 0;
  s.subscribe([&](bool) { ++changes; });
  int64_t t = 1000;
  s.set_mini(true);
  s.feed_activity(t);
  s.tick(t + 9999);
  UT_CHECK(!s.hidden());           // 未满 10s
  s.tick(t + kIdleHideDelayMs);
  UT_CHECK(s.hidden());            // 10s 空闲 → 隐藏
  UT_CHECK_EQ(changes, 1);
  // 自续期：继续 tick 不再通知（保持隐藏）
  s.tick(t + kIdleHideDelayMs + 5000);
  UT_CHECK(s.hidden());
  UT_CHECK_EQ(changes, 1);
  // 交互续期：feed 后 10s 内 tick 不隐藏（已隐藏保持）
  s.restore_chrome(t + kIdleHideDelayMs + 6000);
  UT_CHECK(!s.hidden());
  UT_CHECK_EQ(changes, 2);
  s.tick(t + kIdleHideDelayMs + 6000 + 9999);
  UT_CHECK(!s.hidden());
  s.tick(t + kIdleHideDelayMs + 6000 + kIdleHideDelayMs);
  UT_CHECK(s.hidden());
  UT_CHECK_EQ(changes, 3);
}

UT_TEST(t11_stealth_leave_mini_restores) {
  StealthController s;
  s.set_mini(true);
  s.feed_activity(0);
  s.tick(kIdleHideDelayMs);
  UT_CHECK(s.hidden());
  // 离开极小窗尺寸（拉高/变宽）→ 立即恢复（← applyGeometry）
  s.set_mini(false);
  UT_CHECK(!s.hidden());
}

UT_TEST(t11_large_mode_toggle_and_persist) {
  LargeModeController lm;
  // 非大尺寸下拖动 → 记忆
  lm.on_resized(300, 500);
  UT_CHECK(lm.last_w() == 300 && lm.last_h() == 500);
  // 进入大尺寸
  UT_CHECK(lm.enter());
  UT_CHECK(lm.is_large());
  // toggleMaximize 触发的 640×860 onResized：保持 large，记忆不动
  lm.on_resized(640, 860);
  UT_CHECK(lm.is_large());
  UT_CHECK(lm.last_w() == 300);
  // 从大尺寸拖小 → 自动退出 + 记忆更新
  lm.on_resized(320, 600);
  UT_CHECK(!lm.is_large());
  UT_CHECK(lm.last_w() == 320 && lm.last_h() == 600);
  // 退出恢复
  lm.enter();
  lm.exit();
  UT_CHECK(!lm.is_large());
  UT_CHECK(Bp::is_large_size(640, 860));
  UT_CHECK(Bp::is_large_size(640.9, 860.5));  // ±1 容差
  UT_CHECK(!Bp::is_large_size(642, 860));
}
