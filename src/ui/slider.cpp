// Slider 实现（T10）。
#include "slider.h"

#include <algorithm>
#include <cmath>

namespace clashwin::ui {

void Slider::set_value(float v, bool fire) {
  v = std::clamp(v, min_, max_);
  if (step_ > 0) v = min_ + std::round((v - min_) / step_) * step_;
  if (value_ == v) return;
  value_ = v;
  invalidate();
  if (fire && on_change) on_change(value_);
}

SizeF Slider::on_measure(float max_w, float max_h) {
  (void)max_h;
  return {max_w > 0 ? max_w : 120, 24};
}

float Slider::value_at(float local_x) {
  float range = bounds().w - thumb_d_;
  if (range <= 0) return min_;
  float t = (local_x - thumb_d_ / 2) / range;
  return min_ + std::clamp(t, 0.0f, 1.0f) * (max_ - min_);
}

void Slider::update(float local_x) {
  dragging_ = true;
  set_value(value_at(local_x), true);
}

bool Slider::on_mouse(const MouseEvent& e) {
  switch (e.type) {
    case MouseEventType::Down:
      if (!enabled()) return true;
      // 双滑块：就近 thumb
      if (value2_) {
        float t0 = std::fabs(e.pos.x - (thumb_d_ / 2 +
                     (value_ - min_) / (max_ - min_) * (bounds().w - thumb_d_)));
        float t1 = std::fabs(e.pos.x - (thumb_d_ / 2 + (value2_.value() - min_) /
                                        (max_ - min_) * (bounds().w - thumb_d_)));
        active_thumb_ = t1 < t0 ? 1 : 0;
      }
      update(e.pos.x);
      return true;
    case MouseEventType::Move:
      if (dragging_ && enabled()) {
        update(e.pos.x);
        return true;
      }
      return false;
    case MouseEventType::Up:
      dragging_ = false;
      return true;
    default:
      return false;
  }
}

void Slider::on_paint(IRenderTarget& rt) {
  float cy = bounds().h / 2;
  float range = bounds().w - thumb_d_;
  auto cx_of = [&](float v) {
    return thumb_d_ / 2 + (v - min_) / (max_ - min_) * range;
  };
  rt.fill_rect({thumb_d_ / 2, cy - thickness_ / 2, range, thickness_},
               Brush{track_});
  Brush fill_b{fill_};
  rt.fill_rect({thumb_d_ / 2, cy - thickness_ / 2, cx_of(value_) - thumb_d_ / 2,
                thickness_}, fill_b);
  if (value2_) {
    rt.fill_rect({cx_of(value_), cy - thickness_ / 2,
                  cx_of(value2_.value()) - cx_of(value_), thickness_},
                 fill_b);
  }
  Brush thumb_b{thumb_};
  rt.round_rect({cx_of(value_) - thumb_d_ / 2, cy - thumb_d_ / 2, thumb_d_,
                 thumb_d_}, thumb_d_ / 2, {}, &thumb_b);
  if (value2_) {
    rt.round_rect({cx_of(value2_.value()) - thumb_d_ / 2, cy - thumb_d_ / 2,
                   thumb_d_, thumb_d_}, thumb_d_ / 2, {}, &thumb_b);
  }
}

}  // namespace clashwin::ui
