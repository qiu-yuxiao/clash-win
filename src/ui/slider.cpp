// Slider 实现（T10）。
#include "slider.h"

#include <algorithm>
#include <cmath>

#include "theme/theme.h"

namespace clashwin::ui {

using theme::ThemeService;

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
  if (gold_3d_) {
    // src: style-helpers.tsx:251-283 get3DSliderStyle retro-3d
    //   rail：rgba(212,175,55,.25/.15) + 1px #B8860B(light)/#8A6D00(dark)（:255-263）
    RectF rail{thumb_d_ / 2, cy - thickness_ / 2, range, thickness_};
    Brush rail_b{track_};
    rt.round_rect(rail, thickness_ / 2,
                  Pen{theme::ThemeService::instance().is_light()
                          ? Color4f::hex(0xB8860B)
                          : Color4f::hex(0x8A6D00),
                      1.0f},
                  &rail_b);
    // track（已填充段）：linear-gradient(to bottom,#FFE082,#FFC107,#B8860B)（:265-273）
    float fw = cx_of(value_) - thumb_d_ / 2;
    if (fw > 0) {
      GradientStop track[3] = {{0.0f, Color4f::hex(0xFFE082)},
                               {0.50f, Color4f::hex(0xFFC107)},
                               {1.00f, Color4f::hex(0xB8860B)}};
      RectF filled{thumb_d_ / 2, cy - thickness_ / 2, fw, thickness_};
      rt.fill_round_rect_gradient_angle(filled, thickness_ / 2, track, 3, 180.0f);
    }
    // thumb：radial-gradient(circle at 35% 35%)（:280-283）
    float d = thumb_d_;
    RectF ball_rect{cx_of(value_) - d / 2, cy - d / 2, d, d};
    GradientStop ball[4] = {
        {0.00f, Color4f::hex(theme::ThemeService::instance().is_light() ? 0xFFFDE7
                                                                       : 0xFFF9C4)},
        {0.45f, Color4f::hex(theme::ThemeService::instance().is_light() ? 0xFFD54F
                                                                       : 0xFFC107)},
        {0.80f, Color4f::hex(0xFFA000)},
        {1.00f, Color4f::hex(theme::ThemeService::instance().is_light() ? 0xB8860B
                                                                       : 0x8A6D00)}};
    rt.fill_round_rect_radial(ball_rect, d / 2, ball, 4, 0.35f, 0.35f, d * 0.65f,
                              d * 0.65f);
    // src: :297 border 1px #8A6D00(light)/#634E00(dark)
    rt.round_rect(ball_rect, d / 2,
                  Pen{theme::ThemeService::instance().is_light()
                          ? Color4f::hex(0x8A6D00)
                          : Color4f::hex(0x634E00),
                      1.0f},
                  nullptr);
    return;
  }
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
