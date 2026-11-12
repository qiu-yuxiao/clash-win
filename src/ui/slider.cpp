// Slider 实现（T10）。
#include "slider.h"

#include <windows.h>

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
  // ★ 值变 → thumb 位置变 → 悬停判定需重算（← `.MuiSlider-thumb:hover`）
  if (has_mouse_) update_hover();
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

// ★ 悬停判定（← Mini `.MuiSlider-thumb:hover`）
//   鼠标落在 thumb 命中盒内即视为悬停（Mini 的 thumb 视觉体是 12–14px，
//   命中盒 42px 是 ::after 的无障碍热区；视觉悬停看 thumb 本体）。
void Slider::update_hover() {
  const theme::SliderPaint sp = ThemeService::instance().slider_paint();
  bool hover = false;
  if (themed_ && sp.thumb_hover && has_mouse_) {
    const float tw = sp.thumb_w, th = sp.thumb_h;
    const float range = bounds().w - tw;
    auto cx_of = [&](float v) {
      return tw / 2 + (v - min_) / (max_ - min_) * range;
    };
    const float hit = std::max(tw, th) * 0.5f + 2.0f;
    if (std::fabs(last_mouse_x_ - cx_of(value_)) <= hit) hover = true;
    if (!hover && value2_) {
      if (std::fabs(last_mouse_x_ - cx_of(value2_.value())) <= hit) hover = true;
    }
  }
  if (hover != thumb_hovered_) {
    thumb_hovered_ = hover;
    invalidate();
  }
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
      // ★ 键盘焦点（← MUI Slider 点击后获得焦点，可方向键调节）
      request_focus();
      return true;
    case MouseEventType::Move:
      // ★ 记录鼠标位置供 hover 判定（← `.MuiSlider-thumb:hover`）
      last_mouse_x_ = e.pos.x;
      has_mouse_ = true;
      update_hover();
      if (dragging_ && enabled()) {
        update(e.pos.x);
        return true;
      }
      return false;
    case MouseEventType::Up: {
      const bool was_dragging = dragging_;
      dragging_ = false;
      if (was_dragging && enabled() && on_commit) on_commit(value_);
      return true;
    }
    case MouseEventType::Leave:
      has_mouse_ = false;
      thumb_hovered_ = false;
      invalidate();
      return true;
    default:
      return false;
  }
}

// ★ 键盘调节（← MUI useSlider.js:326-370 `createHandleHiddenInputKeyDown`）
//   ArrowUp/ArrowRight   +step（Shift 时 +shiftStep=10）
//   ArrowDown/ArrowLeft   -step（Shift 时 -shiftStep=10）
//   PageUp/PageDown       ±shiftStep(=10)
//   Home                  → min
//   End                   → max
//   取值语义 `getNewValue`：direction===1 ? min(v+step,max) : max(v-step,min)
//   （不做 step 对齐 —— Mini 侧同样直接加减，由 MUI 内部 roundValue 兜底）
bool Slider::on_key(const KeyEvent& e) {
  if (e.type != KeyEventType::Down) return false;
  if (!enabled()) return false;
  const float step = step_ > 0 ? step_ : 0.1f;
  const float shift_step = 10.0f;   // ← MUI shiftStep 缺省 10（useSlider.js:182）
  const float size = e.shift ? shift_step : step;
  float target = value_;
  switch (e.key) {
    case VK_UP:
    case VK_RIGHT:
      target = std::min(value_ + size, max_);
      break;
    case VK_DOWN:
    case VK_LEFT:
      target = std::max(value_ - size, min_);
      break;
    case VK_PRIOR:            // PageUp
      target = std::min(value_ + shift_step, max_);
      break;
    case VK_NEXT:             // PageDown
      target = std::max(value_ - shift_step, min_);
      break;
    case VK_HOME:
      target = min_;
      break;
    case VK_END:
      target = max_;
      break;
    default:
      return false;
  }
  key_focused_ = true;
  set_value(target, true);
  if (on_commit) on_commit(value_);
  invalidate();
  return true;
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
  // ★ thumb 尺寸随皮肤变 → 悬停命中盒变 → 重算（← `.MuiSlider-thumb:hover`）
  if (has_mouse_) update_hover();
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
  // ★ 悬停态（← Mini `.MuiSlider-thumb:hover`）：状态由 update_hover() 维护。
  // ★ 键盘焦点态（MUI `.Mui-focusVisible` 与 `:hover` 共用同一组样式）
  const bool hover = thumb_hovered_ ||
                     (themed_ && sp.thumb_hover && key_focused_);

  auto paint_thumb = [&](float cx) {
    // ★ hover 缩放（← `transform: scale(1.2)` / `scale(1.15)`）
    const float scale = hover ? sp.thumb_hover_scale : 1.0f;
    const float sw = tw * scale;
    const float sh = th * scale;
    RectF br{cx - sw / 2, cy - sh / 2, sw, sh};
    const float s_radius = thumb_r * scale;
    if (themed_) {
      // ★ hover 阴影覆盖（← `&:hover` 的 boxShadow 覆盖常态）
      const bool use_hs = hover && sp.thumb_hover_shadow;
      if (use_hs || sp.thumb_shadow)
        draw_soft_shadow(rt, br, s_radius,
                         use_hs ? sp.thumb_hover_shadow_color
                                : sp.thumb_shadow_color,
                         use_hs ? sp.thumb_hover_shadow_dy
                                : sp.thumb_shadow_dy,
                         use_hs ? sp.thumb_hover_shadow_blur
                                : sp.thumb_shadow_blur);
      // ★ hover 辉光覆盖
      const bool use_hg = hover && sp.thumb_hover_glow;
      if (use_hg || sp.thumb_glow)
        draw_glow(rt, br, s_radius,
                  use_hg ? sp.thumb_hover_glow_color : sp.thumb_glow_color,
                  use_hg ? sp.thumb_hover_glow_blur : sp.thumb_glow_blur);
    }
    if (themed_ && sp.thumb_grad) {
      // retro：radial-gradient(circle at 35% 35%, ...)
      rt.fill_round_rect_radial(br, s_radius, sp.thumb_stops, sp.thumb_grad_n,
                                sp.thumb_grad_cx, sp.thumb_grad_cy,
                                sw * 0.65f, sh * 0.65f);
    } else {
      // ★ hover 底色覆盖（← frosted-glass `:hover` bgcolor rgba(255,255,255,1.0)）
      Color4f tc = themed_ ? sp.thumb : thumb_;
      if (hover && themed_ && sp.thumb_hover_fill) tc = sp.thumb_hover_fill_color;
      // ★ hover 亮度（← `filter: brightness(1.15)`）：仅 retro-3d 定义，
      //   作用于旋钮自身底色（不含阴影/辉光）。
      if (hover && themed_ && sp.thumb_hover_brightness != 1.0f) {
        const float b = sp.thumb_hover_brightness;
        tc.r = std::min(1.0f, tc.r * b);
        tc.g = std::min(1.0f, tc.g * b);
        tc.b = std::min(1.0f, tc.b * b);
      }
      Brush tb{tc};
      rt.round_rect(br, s_radius, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &tb);
    }
    // ★ thumb 内凹顶高光（← style-helpers.tsx:285/288）
    //   `inset 0 calc(1px * var(--depth-factor)) 0 rgba(255,255,255,.8|.4)`
    //   在 thumb 本体之上画一圈向内收缩的浅色描边 → 顶部有立体高光。
    const bool use_hih = hover && sp.thumb_hover_inset_hi;
    if (themed_ && (use_hih || sp.thumb_inset_hi)) {
      const float dy = use_hih ? sp.thumb_hover_inset_hi_dy
                               : sp.thumb_inset_hi_dy;
      const Color4f base = use_hih ? sp.thumb_hover_inset_hi_color
                                   : sp.thumb_inset_hi_color;
      RectF ir{br.x, br.y + dy, br.w, br.h - dy};
      // 只保留上缘：用一层贴顶的横向渐变近似 inset 顶部高光
      const int layers = 4;
      for (int i = 0; i < layers; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(layers);
        Color4f c = base;
        c.a = base.a * (1.0f - t) / layers * 2.0f;
        if (c.a <= 0.004f) continue;
        const float inset = 1.0f + t * 1.2f;
        RectF rr{ir.x + inset, ir.y + t * 1.0f, ir.w - inset * 2.0f,
                 std::max(0.0f, 1.4f)};
        if (rr.w <= 0 || rr.h <= 0) continue;
        rt.round_rect(rr, s_radius > inset ? s_radius - inset : 0.0f, Pen{c, 1.2f},
                      nullptr);
      }
    }
    if (themed_ && sp.thumb_bw > 0.01f)
      rt.round_rect(br, s_radius, Pen{sp.thumb_border, sp.thumb_bw}, nullptr);
    // ★ thumb 按下内凹（← style-helpers.tsx:306-307 `:active`）
    //   `inset calc(1px*d) calc(1px*d) calc(2px*d) rgba(0,0,0,.15|.5)`
    //   拖拽中才画：内凹的深色描边 + 左上偏移，模拟被按下去。
    if (themed_ && sp.thumb_active_inset && dragging_) {
      const float dx = sp.thumb_active_inset_dx;
      const float dy = sp.thumb_active_inset_dy;
      RectF ar{br.x + dx, br.y + dy, br.w - dx, br.h - dy};
      draw_inner_glow(rt, ar, s_radius, sp.thumb_active_inset_color,
                      sp.thumb_active_inset_blur, 4);
    }
  };
  paint_thumb(cx_of(value_));
  if (value2_) paint_thumb(cx_of(value2_.value()));
}

}  // namespace clashwin::ui
