#pragma once
// Button（T10）：状态机 normal/hover/pressed/disabled + 点击回调。
// 对照 MUI Button（contained 风格缺省色板）。绘制留虚函数 draw_button_bg /
// draw_button_text 给 T12 皮肤系统覆盖（6 套皮肤参数注入 + 3D 内阴影）。
#include "widget.h"

namespace clashwin::ui {

class Button : public Widget {
 public:
  explicit Button(Widget* parent = nullptr);
  Button(std::wstring t, Widget* parent = nullptr);

  enum class State { Normal, Hover, Pressed, Disabled };

  std::wstring text;
  TextStyle text_style;
  // 缺省绘制参数（MUI contained：light primary #007AFF；T12 覆盖）
  struct Paint {
    Color4f fill_normal = Color4f::hex(0x007AFF);  // ← 主色缺省（主题注入前）
    Color4f fill_hover = Color4f::hex(0x007AFF, 0.85f);  // ← 悬停 85% 主色
    Color4f fill_pressed = Color4f::hex(0x0062CC);  // ← 按下深主色
    Color4f fill_disabled = Color4f::hex(0x000000, 0.12f);  // ← 禁用 12% 黑
    Color4f text_normal = Color4f::hex(0xFFFFFF);  // ← MUI contained contrastText
    Color4f text_disabled = Color4f::hex(0x000000, 0.26f);  // ← 禁用文字 26% 黑
    Pen border{Color4f(0, 0, 0, 0), 0};  // 豁免: 缺省无边框（透明色）
    float radius = 4;  // ← MUI shape.borderRadius 4
    // 可选渐变底：grad_count>0 时非禁用态改用 135° 多停渐变绘制
    //   （retro-3d contained primary 六停，src: button-styles.ts:38-39）
    GradientStop grad[6];
    int grad_count = 0;
    float grad_angle = 135.0f;
  };
  Paint paint_params;

  std::function<void()> on_click;

  // T12：主题接入开关（true 时 draw_button_bg 走 ThemeService 查表；
  // 默认 false 保持 paint_params 直绘。T13 场景挂载时统一开启）
  bool themed_ = false;
  const char* theme_color_type_ = "default";  // "primary" | "error" | "default"

  State state() const { return enabled() ? state_ : State::Disabled; }
  void set_disabled(bool d);
  // F-D01：绑定到 Mini locale key，语言切换时由 Widget::paint_all 自动刷新
  void set_i18n_text(const char* key);

  SizeF on_measure(float max_w, float max_h) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;

  A11yRole a11y_default_role() const override { return A11yRole::Button; }
  std::wstring a11y_default_name() const override { return text; }
  bool a11y_is_focusable() const override { return enabled(); }
  bool a11y_can_invoke() const override {
    return enabled() && static_cast<bool>(on_click);
  }
  bool a11y_invoke() override {
    if (!enabled() || !on_click) return false;
    on_click();
    return true;
  }

 protected:
  // T12 皮肤系统重写点：按钮底（3D 内阴影 ×depth×vibrancy 在此挂）
  virtual void draw_button_bg(IRenderTarget& rt, const RectF& r);
  virtual void draw_button_text(IRenderTarget& rt, const RectF& r);
  // 自绘底色的子类（如 SkinCell）按此进度插值常态/悬停两套色
  float hover_mix() const { return hover_mix_; }

 private:
  State state_ = State::Normal;
  Color4f last_fill_{};
  // ---- 悬停过渡（← button-styles.ts:188/258/313/399/464 transition: all .15s ease）----
  // hover_mix_: 0 = 常态底，1 = 悬停底；绘制时在两种底色之间插值。
  // 动画只在 Enter/Leave 时启动，结束即把 hover_anim_start_ms_ 归零，
  // on_paint 不再 invalidate —— 不会引入持续重绘。
  void set_hover_target(bool on);
  void advance_hover();
  float hover_mix_ = 0.0f;
  float hover_from_ = 0.0f;
  float hover_to_ = 0.0f;
  double hover_anim_start_ms_ = 0.0;  // 0 = 无动画进行中
};

}  // namespace clashwin::ui
