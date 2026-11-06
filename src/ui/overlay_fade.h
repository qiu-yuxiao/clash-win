#pragma once
// OverlayFade —— MUI <Fade> 过渡进度（Dialog / 模态层的统一淡入淡出）。
//
// 为什么需要：Mini 所有 <Dialog> 都用 MUI 缺省的 Fade 过渡
// （theme.transitions.duration.enteringScreen = 225ms / leavingScreen = 195ms），
// 展开/收起是整体不透明度的渐变，不是瞬时出现。Win 侧此前全部瞬时切换，
// 与「所有用户可见画面一模一样」的标准不符。
//
// 用法（宿主控件）：
//   OverlayFade fade_;
//   open()  → fade_.show();            // 起 225ms 淡入
//   close() → fade_.hide();            // 起 195ms 淡出（宿主 open_ 语义立刻失效）
//   on_paint 开头：
//       if (fade_.advance()) { set_opacity(fade_.value()); invalidate(); }
//       if (fade_.value() <= 0.0f) return;   // 完全隐藏 → 不画
//
// 注意：不透明度作用于宿主整棵子树（Widget::paint 的 opacity 层），
// 遮罩与纸面因此一起渐变，与 MUI「<Fade> 包住 Dialog（含 Backdrop）」同构。

#include <cstdint>

namespace clashwin::ui {

class OverlayFade {
 public:
  // ← theme.transitions.duration.enteringScreen / leavingScreen
  static constexpr int kEnterMs = 225;
  static constexpr int kLeaveMs = 195;

  void show() { start(1.0f, kEnterMs, now_ms()); }
  void hide() { start(0.0f, kLeaveMs, now_ms()); }
  // 注入起点时钟（测试用：避免依赖真实时间而断言抖动）
  void show_at(int64_t now) { start(1.0f, kEnterMs, now); }
  void hide_at(int64_t now) { start(0.0f, kLeaveMs, now); }
  void set_shown(bool shown) { shown ? show() : hide(); }
  // 跳过过渡（首帧直出 / 测试）
  void jump_to(float v);

  float value() const { return value_; }
  bool animating() const { return from_ != to_; }
  bool hidden() const { return value_ <= 0.0f; }

  // 推进到当前时刻。返回 true 表示仍在过渡中（调用方应 invalidate 自续帧）。
  bool advance();
  // 同 advance，但用注入的时钟（测试用，避免依赖真实时间）
  bool advance_at(int64_t now_ms);

 private:
  void start(float to, int dur_ms, int64_t now);
  static int64_t now_ms();

  float value_ = 0.0f;
  float from_ = 0.0f;
  float to_ = 0.0f;
  int64_t start_ms_ = 0;
  int64_t dur_ms_ = 0;
};

}  // namespace clashwin::ui
