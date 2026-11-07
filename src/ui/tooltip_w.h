#pragma once
// TooltipW（T10）：悬停提示——悬停 100ms 后显示。
// src: node_modules/@mui/material/Tooltip/Tooltip.js:331（enterDelay 缺省 100）
// 作为 overlay 挂根 Stack（z_order 1200）；宿主（route 层/T13）在鼠标 Move
// 时调 hover()/leave()；时间推进经 tick_now()（窗口层帧驱动，测试可注入）。
#include <string>

#include "widget.h"

namespace clashwin::ui {

class TooltipHost : public Widget {
 public:
  explicit TooltipHost(Widget* parent = nullptr) : Widget(parent) {
    set_z_order(1200);
    set_visible(false);
  }

  static constexpr int kShowDelayMs = 100;  // src: node_modules/@mui/material/Tooltip/Tooltip.js:331 enterDelay = 100

  // 鼠标悬停在带 tooltip_text() 的 widget 上时由宿主调用（全局坐标）
  void hover(Widget* target, PointF global);
  void leave();                      // 离开/移出目标
  void tick_now(int64_t now_ms);     // 时间推进：到 100ms 显示
  bool showing() const { return visible(); }
  const std::wstring& text() const { return text_; }
  RectF tip_bounds() const { return bounds(); }

  void on_paint(IRenderTarget& rt) override;
  // 提示层不拦截命中（否则显示期间鼠标事件全被它吃掉）
  bool hit_self(PointF) const override { return false; }

  // 绘制参数（MUI tooltip：深灰底白字）
  Color4f bg_ = Color4f::hex(0x616161, 0.95f);  // src: MUI grey[700] rgba(97,97,97,.95)
  Color4f text_color_ = Color4f::hex(0xFFFFFF);  // src: MUI tooltip 文字白
  float font_size = 11;  // ← MUI tooltip caption 字号
  float pad_h = 8, pad_v = 4;  // ← MUI tooltip 内距 8/4
  float radius_ = 4;

 private:
  void place(PointF anchor);
  std::wstring text_;
  int64_t hover_start_ = -1;
  int64_t now_ = 0;
  PointF anchor_{};
};

}  // namespace clashwin::ui
