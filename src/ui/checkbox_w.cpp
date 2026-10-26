// CheckboxW 实现（T10）。
#include "checkbox_w.h"

#include <algorithm>

namespace clashwin::ui {

CheckboxW::CheckboxW(Widget* parent) : Widget(parent) {}

bool CheckboxW::set_checked(bool v, bool fire) {
  if (checked_ == v) return false;
  checked_ = v;
  invalidate();
  if (fire && on_change) on_change(checked_);
  return true;
}

void CheckboxW::set_label(const std::wstring& t) {
  label_ = t;
  invalidate();
}

SizeF CheckboxW::on_measure(float max_w, float max_h) {
  (void)max_w; (void)max_h;
  float w = box_ + 8;
  if (ITextEngine* e = text_engine(); e && !label_.empty()) {
    SizeF s = e->measure_text(label_, label_style_, 0);
    w += s.w;
  }
  return {w, std::max(box_, 18.0f)};
}

bool CheckboxW::on_mouse(const MouseEvent& e) {
  if (e.type == MouseEventType::Down) {
    if (!enabled()) return true;
    set_checked(!checked_, true);
    return true;
  }
  return false;
}

void CheckboxW::on_paint(IRenderTarget& rt) {
  float box = box_;
  RectF bb{0, (bounds().h - box) / 2, box, box};
  Pen border{checked_ ? check_color_ : Color4f::hex(0x000000, 0.54f), 1.5f};
  Brush fill{checked_ ? check_color_ : Color4f::hex(0xFFFFFF)};
  rt.round_rect(bb, 3, border, &fill);
  if (checked_) {
    TextStyle ts;
    ts.font_family = L"Segoe MDL2 Assets";
    ts.size = box - 4;
    ts.color = Color4f::hex(0xFFFFFF);
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt.draw_text(std::wstring(1, L'\uE73E'), bb, ts);  // Check
  }
  if (!label_.empty()) {
    TextStyle ts = label_style_;
    ts.align_v = AlignV::Center;
    ts.ellipsis = true;
    rt.draw_text(label_, {box + 8, 0, bounds().w - box - 8, bounds().h}, ts);
  }
}

}  // namespace clashwin::ui
