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
    Color4f fill_normal = Color4f::hex(0x007AFF);
    Color4f fill_hover = Color4f::hex(0x007AFF, 0.85f);
    Color4f fill_pressed = Color4f::hex(0x0062CC);
    Color4f fill_disabled = Color4f::hex(0x000000, 0.12f);
    Color4f text_normal = Color4f::hex(0xFFFFFF);
    Color4f text_disabled = Color4f::hex(0x000000, 0.26f);
    Pen border{Color4f(0, 0, 0, 0), 0};
    float radius = 4;
  };
  Paint paint_params;

  std::function<void()> on_click;

  // T12：主题接入开关（true 时 draw_button_bg 走 ThemeService 查表；
  // 默认 false 保持 paint_params 直绘。T13 场景挂载时统一开启）
  bool themed_ = false;
  const char* theme_color_type_ = "default";  // "primary" | "error" | "default"

  State state() const { return enabled() ? state_ : State::Disabled; }
  void set_disabled(bool d);

  SizeF on_measure(float max_w, float max_h) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;

 protected:
  // T12 皮肤系统重写点：按钮底（3D 内阴影 ×depth×vibrancy 在此挂）
  virtual void draw_button_bg(IRenderTarget& rt, const RectF& r);
  virtual void draw_button_text(IRenderTarget& rt, const RectF& r);

 public:
  Color4f debug_last_fill() const { return last_fill_; }  // 测试检查点

 private:
  State state_ = State::Normal;
  Color4f last_fill_{};
};

}  // namespace clashwin::ui
