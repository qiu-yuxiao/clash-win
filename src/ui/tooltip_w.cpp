// TooltipHost 实现（T10）。
#include "tooltip_w.h"

#include <algorithm>

namespace clashwin::ui {

void TooltipHost::hover(Widget* target, PointF global) {
  // 命中控件无提示时沿父链上溯（Label 等不吞鼠标，提示挂在父容器上）
  std::wstring t;
  for (Widget* w = target; w && t.empty(); w = w->parent()) t = w->tooltip_text();
  if (t.empty()) {
    leave();
    return;
  }
  if (text_ != t) {
    text_ = t;
    hover_start_ = now_;  // 新目标重新计时
  } else if (hover_start_ < 0) {
    hover_start_ = now_;
  }
  anchor_ = global;
  place(anchor_);
}

void TooltipHost::leave() {
  hover_start_ = -1;
  text_.clear();
  if (visible()) {
    set_visible(false);
    invalidate();
  }
}

void TooltipHost::tick_now(int64_t now_ms) {
  now_ = now_ms;
  if (hover_start_ >= 0) place(anchor_);  // 父级重排可能覆盖 bounds，每帧重贴
  if (hover_start_ >= 0 && now_ - hover_start_ >= kShowDelayMs) {
    if (!visible()) {
      place(anchor_);
      set_visible(true);
      invalidate();
    }
  }
}

void TooltipHost::place(PointF anchor) {
  SizeF s{};
  if (ITextEngine* e = text_engine()) {
    TextStyle ts;
    ts.size = font_size;
    s = e->measure_text(text_, ts, 0);
  }
  // 右下偏移 (12, 16)；不越界钳制留给宿主（窗口尺寸已知时）
  set_bounds({anchor.x + 12, anchor.y + 16, s.w + pad_h * 2,
              s.h + pad_v * 2});
}

void TooltipHost::on_paint(IRenderTarget& rt) {
  RectF r{0, 0, bounds().w, bounds().h};
  Brush bg_b{bg_};
  rt.round_rect(r, radius_, {}, &bg_b);
  TextStyle ts;
  ts.size = font_size;
  ts.color = text_color_;
  ts.align_h = AlignH::Center;
  ts.align_v = AlignV::Center;
  rt.draw_text(text_, r, ts);
}

}  // namespace clashwin::ui
