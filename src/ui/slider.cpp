// Slider 实现（T10）。
#include "slider.h"

#include <algorithm>
#include <cmath>

#include "soft_shadow.h"
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

void Slider::apply_theme() {
  const auto p = ThemeService::instance().slider_paint();
  themed_ = true;
  thickness_ = p.thickness;
  thumb_d_ = std::max(p.thumb_w, p.thumb_h);
  track_ = p.rail;
  fill_ = p.track;
  thumb_ = p.thumb;
  gold_3d_ = ThemeService::instance().skin() == theme::SkinId::Retro3d;
  invalidate();
}

void Slider::on_paint(IRenderTarget& rt) {
  // T12：6 套皮肤统一走 ThemeService::slider_paint()（← get3DSliderStyle）。
  //   此前只有 gold_3d_（retro）与 MUI 缺省（单色圆头）两枝，切换皮肤无差异。
  const theme::SliderPaint sp = themed_ ? ThemeService::instance().slider_paint()
                                        : theme::SliderPaint{};
  const float track_h = themed_ ? sp.thickness : thickness_;
  const float tw = themed_ ? sp.thumb_w : thumb_d_;
  const float th = themed_ ? sp.thumb_h : thumb_d_;
  const float cy = bounds().h / 2;
  const float range = bounds().w - tw;
  auto cx_of = [&](float v) {
    return tw / 2 + (v - min_) / (max_ - min_) * range;
  };
  auto thumb_rect = [&](float cx) {
    return RectF{cx - tw / 2, cy - th / 2, tw, th};
  };
  const float thumb_r = themed_ ? sp.thumb_radius : thumb_d_ / 2;

  // ---- 滑轨 ----
  RectF rail{tw / 2, cy - track_h / 2, range, track_h};
  const float rail_r = themed_ ? sp.rail_radius : track_h / 2;
  Brush rail_b{themed_ ? sp.rail : track_};
  const float rail_bw = themed_ ? sp.rail_bw : 0.0f;
  if (themed_ && sp.rail_inset)
    draw_inner_glow(rt, rail, rail_r, sp.rail_inset_color, sp.rail_inset_blur);
  rt.round_rect(rail, rail_r,
                {themed_ ? sp.rail_border : Color4f{0, 0, 0, 0}, rail_bw},
                &rail_b);
  if (themed_ && sp.rail_top_hi.a > 0.004f)
    rt.draw_line({rail.x, rail.y + 0.5f}, {rail.right(), rail.y + 0.5f},
                 Pen{sp.rail_top_hi, 1.0f});

  // ---- 已填充段（track）----
  const float fw = cx_of(value_) - tw / 2;
  const float track_r = themed_ ? sp.track_radius : track_h / 2;
  if (fw > 0) {
    RectF filled{tw / 2, cy - track_h / 2, fw, track_h};
    if (themed_ && sp.track_grad) {
      // retro：linear-gradient(to bottom, #FFE082, #FFC107, #B8860B) = 180°
      rt.fill_round_rect_gradient_angle(filled, track_r,
                                        sp.track_grad_stops, sp.track_grad_n,
                                        180.0f);
    } else {
      Brush fill_b{themed_ ? sp.track : fill_};
      rt.round_rect(filled, track_r, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &fill_b);
    }
  }
  if (value2_) {
    const float x0 = cx_of(value_), x1 = cx_of(value2_.value());
    Brush fill_b{themed_ ? sp.track : fill_};
    rt.round_rect({x0, cy - track_h / 2, x1 - x0, track_h}, track_r,
                  Pen{Color4f{0, 0, 0, 0}, 0.0f}, &fill_b);
  }

  // ---- 滑块（thumb）----
  auto paint_thumb = [&](float cx) {
    RectF br = thumb_rect(cx);
    if (themed_) {
      if (sp.thumb_shadow)
        draw_soft_shadow(rt, br, thumb_r, sp.thumb_shadow_color,
                         sp.thumb_shadow_dy, sp.thumb_shadow_blur);
      if (sp.thumb_glow)
        draw_glow(rt, br, thumb_r, sp.thumb_glow_color, sp.thumb_glow_blur);
    }
    if (themed_ && sp.thumb_grad) {
      // retro：radial-gradient(circle at 35% 35%, ...)
      rt.fill_round_rect_radial(br, thumb_r, sp.thumb_stops, sp.thumb_grad_n,
                                sp.thumb_grad_cx, sp.thumb_grad_cy,
                                tw * 0.65f, th * 0.65f);
    } else {
      Brush tb{themed_ ? sp.thumb : thumb_};
      rt.round_rect(br, thumb_r, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &tb);
    }
    if (themed_ && sp.thumb_bw > 0.01f)
      rt.round_rect(br, thumb_r, Pen{sp.thumb_border, sp.thumb_bw}, nullptr);
  };
  paint_thumb(cx_of(value_));
  if (value2_) paint_thumb(cx_of(value2_.value()));
}

}  // namespace clashwin::ui
