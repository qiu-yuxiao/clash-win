// SwitchW 实现（T10）。
#include "switch_w.h"

#include <algorithm>

namespace clashwin::ui {

bool SwitchW::set_checked(bool v, bool fire) {
  if (checked_ == v) return false;
  checked_ = v;
  anim_ = v ? 1.0f : 0.0f;  // 即时落位（拖拽进度由 move 驱动）
  invalidate();
  if (fire && on_change) on_change(checked_);
  return true;
}

SizeF SwitchW::on_measure(float max_w, float max_h) {
  (void)max_w; (void)max_h;
  return {width_, height_};
}

void SwitchW::update_drag(float local_x) {
  float range = width_ - height_;
  float t = range > 0 ? (local_x - height_ / 2) / range : 0.0f;
  anim_ = t < 0 ? 0 : (t > 1 ? 1 : t);
  dragging_ = true;
  drag_moved_ = true;
  invalidate();
}

bool SwitchW::on_mouse(const MouseEvent& e) {
  switch (e.type) {
    case MouseEventType::Down:
      if (!enabled()) return true;
      press_x_ = e.pos.x;
      drag_moved_ = false;
      return true;
    case MouseEventType::Move:
      if (!enabled()) return false;
      if ((e.buttons & kMouseLeft) &&
          (drag_moved_ || std::fabs(e.pos.x - press_x_) > 3.0f)) {
        update_drag(e.pos.x);  // 超过阈值才算拖动（单击不动）
        return true;
      }
      return drag_moved_;
    case MouseEventType::Up:
      if (!enabled()) return true;
      if (drag_moved_) {
        dragging_ = false;
        drag_moved_ = false;
        set_checked(anim_ >= 0.5f, true);  // <0.5 回弹
      } else {
        set_checked(!checked_, true);  // 单击切换（无拖动位移）
      }
      return true;
    default:
      return false;
  }
}

void SwitchW::on_paint(IRenderTarget& rt) {
  float h = bounds().h, w = bounds().w;
  float thumb_d = h - 4;
  Brush track{Color4f::lerp(track_off_, track_on_, anim_)};
  if (!enabled()) track.color.a *= 0.4f;
  rt.round_rect({0, 0, w, h}, h / 2, {}, &track);
  float tx = 2 + anim_ * (w - thumb_d - 4);
  Brush thumb_b{thumb_};
  rt.round_rect({tx, 2, thumb_d, thumb_d}, thumb_d / 2, {}, &thumb_b);
}

}  // namespace clashwin::ui
