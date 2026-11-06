// Button 实现（T10）：状态机 + 缺省 contained 绘制。
#include "button.h"

#include <algorithm>

#include "theme/theme.h"

namespace clashwin::ui {

Button::Button(Widget* parent) : Widget(parent) {}

Button::Button(std::wstring t, Widget* parent)
    : Widget(parent), text(std::move(t)) {}

void Button::set_disabled(bool d) {
  set_enabled(!d);
  if (d) state_ = State::Normal;
  invalidate();
}

void Button::set_i18n_text(const char* key) {
  // F-D01：文本槽随语言变化自动刷新（Widget::paint_all 的 revision 机制）
  bind_i18n_text(&text, key);
  invalidate();
}

SizeF Button::on_measure(float max_w, float max_h) {
  (void)max_w; (void)max_h;
  SizeF s{};
  if (ITextEngine* e = text_engine(); e && !text.empty())
    s = e->measure_text(text, text_style, 0);
  // MUI 最小触达高度 24（Mini 紧凑）；左右 padding 12
  return {s.w + 24 + padding().h(), std::max(24.0f, s.h + 8) + padding().v()};
}

void Button::draw_button_bg(IRenderTarget& rt, const RectF& r) {
  if (themed_) {
    // T12/F-D02：ThemeService 查表（皮肤 × 亮暗 × 状态）；
    // 渐变 + 浮雕 + 外发光 + translateY 位移。
    auto p = theme::ThemeService::instance().button_paint(theme_color_type_);
    const bool disabled = (state() == State::Disabled);
    const float vf = theme::ThemeService::instance().vibrancy_factor();
    // 外发光（先于主体绘制形成光晕；vibrancy 调制）。D2D 无模糊滤镜，
    // 用略大一圈的同色填充圆角矩形近似 halo（真实 glow=多层 boxShadow blur）。
    if (p.glow.a > 0 && !disabled) {
      float ga = std::min(1.0f, p.glow.a * (0.5f + 0.5f * vf));
      if (ga > 0.01f) {
        RectF gr{r.x - 3, r.y - 3, r.w + 6, r.h + 6};
        Brush gb{Color4f{p.glow.r, p.glow.g, p.glow.b, ga}};
        rt.round_rect(gr, p.radius + 3, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &gb);
      }
    }
    // 主体：135° 多停渐变（retro）或单色填充（其余皮肤）
    if (p.use_grad && !disabled) {
      const GradientStop* gs = (state() == State::Hover) ? p.grad_hover : p.grad;
      rt.fill_round_rect_gradient_angle(r, p.radius, gs, p.grad_count, p.grad_angle);
      last_fill_ = gs[p.grad_count / 2].color;
    } else {
      Color4f fill;
      switch (state()) {
        case State::Hover: fill = p.fill_hover; break;
        case State::Pressed: fill = p.fill_pressed; break;
        case State::Disabled: fill = p.fill_disabled; break;
        default: fill = p.fill; break;
      }
      Brush b{fill};
      rt.round_rect(r, p.radius, {p.border, p.border_w}, &b);
      last_fill_ = fill;
    }
    // 3D 浮雕（retro 渐变皮肤）：顶部高光 + 底部暗边
    if (p.use_grad && !disabled && p.bevel_dark.a > 0) {
      rt.draw_line(PointF{r.x + p.radius, r.y + 1.0f},
                   PointF{r.right() - p.radius, r.y + 1.0f},
                   Pen{p.bevel_hi, 1.0f});
      rt.draw_line(PointF{r.x + p.radius, r.bottom() - 1.0f},
                   PointF{r.right() - p.radius, r.bottom() - 1.0f},
                   Pen{p.bevel_dark, 1.0f});
    }
    return;
  }
  if (state() != State::Disabled && paint_params.grad_count > 0) {
    // src: button-styles.ts:38-39 retro-3d contained primary 135° 六停渐变
    rt.fill_round_rect_gradient_angle(r, paint_params.radius, paint_params.grad,
                                      paint_params.grad_count, paint_params.grad_angle);
    if (paint_params.border.width > 0)
      rt.round_rect(r, paint_params.radius, paint_params.border, nullptr);
    last_fill_ = paint_params.grad[paint_params.grad_count / 2].color;
    return;
  }
  Color4f fill;
  switch (state()) {
    case State::Hover: fill = paint_params.fill_hover; break;
    case State::Pressed: fill = paint_params.fill_pressed; break;
    case State::Disabled: fill = paint_params.fill_disabled; break;
    default: fill = paint_params.fill_normal; break;
  }
  Brush b{fill};
  rt.round_rect(r, paint_params.radius, paint_params.border, &b);
  last_fill_ = fill;
}

void Button::draw_button_text(IRenderTarget& rt, const RectF& r) {
  TextStyle ts = text_style;
  if (themed_) {
    auto p = theme::ThemeService::instance().button_paint(theme_color_type_);
    const bool disabled = (state() == State::Disabled);
    ts.color = disabled ? p.text_disabled : p.text;
    ts.weight = p.weight;  // ← button-styles.ts fontWeight
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    // F-D02：文字阴影（先画偏移阴影层，再画主文字）
    if (!disabled && p.text_shadow.a > 0) {
      TextStyle ts2 = ts;
      ts2.color = p.text_shadow;
      RectF sr{r.x, r.y + p.text_shadow_dy, r.w, r.h};
      rt.draw_text(text, sr, ts2);
    }
    rt.draw_text(text, r, ts);
    return;
  }
  ts.color = state() == State::Disabled ? paint_params.text_disabled
                                        : paint_params.text_normal;
  ts.align_h = AlignH::Center;
  ts.align_v = AlignV::Center;
  rt.draw_text(text, r, ts);
}

void Button::on_paint(IRenderTarget& rt) {
  RectF r{0, 0, bounds().w, bounds().h};
  // F-D02：translateY 位移（hover 上移 / active 下移；负=上）
  float ty = 0;
  if (themed_) {
    auto p = theme::ThemeService::instance().button_paint(theme_color_type_);
    if (state() == State::Hover) ty = p.translate_hover;
    else if (state() == State::Pressed) ty = p.translate_active;
  }
  if (ty != 0) r = {0, r.y + ty, r.w, r.h};
  draw_button_bg(rt, r);
  if (!text.empty()) draw_button_text(rt, r);
}

bool Button::on_mouse(const MouseEvent& e) {
  switch (e.type) {
    case MouseEventType::Enter:
      if (enabled() && state_ != State::Pressed) state_ = State::Hover;
      invalidate();
      return true;
    case MouseEventType::Leave:
      if (enabled() && state_ != State::Pressed) state_ = State::Normal;
      invalidate();
      return true;
    case MouseEventType::Down:
      if (!enabled()) return true;  // disabled 吞事件
      state_ = State::Pressed;
      invalidate();
      return true;
    case MouseEventType::Up:
      if (!enabled()) return true;
      // capture 语义保证 Up 送达；移出按钮松开不触发点击
      // 【CLICKFIX】e.pos 局部坐标 vs 局部矩形（原 bounds() 父坐标致点击永不触发）
      if (state_ == State::Pressed &&
          RectF{0, 0, bounds().w, bounds().h}.contains(e.pos.x, e.pos.y)) {
        state_ = State::Hover;
        invalidate();
        if (on_click) on_click();
      } else {
        state_ = State::Normal;
        invalidate();
      }
      return true;
    default:
      return false;
  }
}

}  // namespace clashwin::ui
