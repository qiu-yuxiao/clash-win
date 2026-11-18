// Button 实现（T10）：状态机 + 缺省 contained 绘制。
#include "button.h"

#include <algorithm>
#include <chrono>

#include "button_draw.h"
#include "card_draw.h"
#include "theme/theme.h"

namespace clashwin::ui {

namespace {
// steady_clock 毫秒（与 quick_bar 同一惯例：避免 GetTickCount 的 49.7 天回绕）
double steady_ms() {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
// smoothstep 逼近 CSS ease（与 quick_bar 图钉过渡同一改写）
float ease_mui(float t) { return t * t * (3.0f - 2.0f * t); }
Color4f mix_color(const Color4f& a, const Color4f& b, float k) {
  return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k,
          a.a + (b.a - a.a) * k};
}
// ← button-styles.ts:188/258/313/399/464  transition: all .15s ease
constexpr double kHoverMs = 150.0;
}  // namespace

Button::Button(Widget* parent) : Widget(parent) {}

Button::Button(std::wstring t, Widget* parent)
    : Widget(parent), text(std::move(t)) {}

void Button::set_disabled(bool d) {
  set_enabled(!d);
  if (d) {
    state_ = State::Normal;
    hover_mix_ = 0.0f;  // 禁用即回到常态底，避免恢复可用时残留悬停色
    hover_anim_start_ms_ = 0.0;
  }
  invalidate();
}

void Button::set_i18n_text(const char* key) {
  // F-D01：文本槽随语言变化自动刷新（Widget::paint_all 的 revision 机制）
  bind_i18n_text(&text, key);
  invalidate();
}

void Button::set_hover_target(bool on) {
  const float target = on ? 1.0f : 0.0f;
  if (hover_anim_start_ms_ <= 0.0 && hover_mix_ == target) return;  // 已到位
  hover_from_ = hover_mix_;                                          // 连续进出不跳变
  hover_to_ = target;
  hover_anim_start_ms_ = steady_ms();
}

void Button::advance_hover() {
  if (hover_anim_start_ms_ <= 0.0) return;  // 无动画 → 不重绘
  const double elapsed = steady_ms() - hover_anim_start_ms_;
  const float t = static_cast<float>(std::min(1.0, elapsed / kHoverMs));
  hover_mix_ = hover_from_ + (hover_to_ - hover_from_) * ease_mui(t);
  if (t >= 1.0f) {
    hover_mix_ = hover_to_;
    hover_anim_start_ms_ = 0.0;  // 动画结束：定时/续帧到此为止
  } else {
    invalidate();  // 过渡期间自续帧
  }
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
    // T12/F-D02：查表 + 统一绘制（常态 / :hover / :active / 禁用态四段）
    auto p = theme::ThemeService::instance().button_paint(theme_color_type_,
                                                          theme_outlined_);
    const bool disabled = (state() == State::Disabled);
    const bool pressed = (state() == State::Pressed);
    draw_button_skin(rt, r, p, hover_mix_, pressed, disabled);
    // last_fill_：外部取「当前底色」（图标反色、分隔线等）
    if (disabled) {
      last_fill_ = p.fill_disabled;
    } else if (p.use_grad) {
      const int gn = p.grad_count > 0 ? p.grad_count : 1;
      last_fill_ = p.grad[gn / 2].color;
    } else {
      last_fill_ = pressed ? p.fill_pressed
                           : mix_color(p.fill, p.fill_hover, hover_mix_);
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
    case State::Hover:
      fill = mix_color(paint_params.fill_normal, paint_params.fill_hover, hover_mix_);
      break;
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
    auto p = theme::ThemeService::instance().button_paint(theme_color_type_, theme_outlined_);
    const bool disabled = (state() == State::Disabled);
    ts.color = disabled ? p.text_disabled : p.text;
    ts.weight = p.weight;  // ← button-styles.ts fontWeight
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    // F-D02：文字阴影（先画偏移阴影层，再画主文字）
    if (!disabled && state() != State::Pressed && p.text_shadow.a > 0) {
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
  advance_hover();  // 0.15s 悬停过渡推进（结束自动停帧）
  RectF r{0, 0, bounds().w, bounds().h};
  // F-D02：translateY 位移（hover 上移 / active 下移；负=上）
  float ty = 0;
  if (themed_) {
    auto p = theme::ThemeService::instance().button_paint(theme_color_type_, theme_outlined_);
    if (state() == State::Hover) ty = p.translate_hover;
    else if (state() == State::Pressed) ty = p.translate_active;
  }
  if (ty != 0) r = {r.x, r.y + ty, r.w, r.h};
  draw_button_bg(rt, r);
  if (!text.empty()) draw_button_text(rt, r);
}

bool Button::on_mouse(const MouseEvent& e) {
  switch (e.type) {
    case MouseEventType::Enter:
      if (enabled() && state_ != State::Pressed) {
        state_ = State::Hover;
        set_hover_target(true);  // 起 0.15s 过渡（禁用态不启动动画）
      }
      invalidate();
      return true;
    case MouseEventType::Leave:
      if (enabled() && state_ != State::Pressed) {
        state_ = State::Normal;
        set_hover_target(false);
      }
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
        set_hover_target(false);  // 移出后松开 → 底色同样缓动回常态
        invalidate();
      }
      return true;
    default:
      return false;
  }
}

}  // namespace clashwin::ui
