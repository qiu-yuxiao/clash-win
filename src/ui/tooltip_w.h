#pragma once
// TooltipW（T10）：悬停提示——悬停 500ms 后显示（MUI Tooltip 语义）。
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

  static constexpr int kShowDelayMs = 500;

  // 鼠标悬停在带 tooltip_text() 的 widget 上时由宿主调用（全局坐标）
  void hover(Widget* target, PointF global);
  void leave();                      // 离开/移出目标
  void tick_now(int64_t now_ms);     // 时间推进：到 500ms 显示
  bool showing() const { return visible(); }
  const std::wstring& text() const { return text_; }
  RectF tip_bounds() const { return bounds(); }

  void on_paint(IRenderTarget& rt) override;

  // 绘制参数（MUI tooltip：深灰底白字）
  Color4f bg_ = Color4f::hex(0x616161, 0.95f);
  Color4f text_color_ = Color4f::hex(0xFFFFFF);
  float font_size = 11;
  float pad_h = 8, pad_v = 4;
  float radius_ = 4;

 private:
  void place(PointF anchor);
  std::wstring text_;
  int64_t hover_start_ = -1;
  int64_t now_ = 0;
  PointF anchor_{};
};

}  // namespace clashwin::ui
