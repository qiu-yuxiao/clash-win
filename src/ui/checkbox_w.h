#pragma once
// CheckboxW（T10）：复选——方框 + Check 字形 + 可选标签。
// 对照 MUI Checkbox 语义（点击切换；标签同点可切）。
#include <functional>

#include "widget.h"

namespace clashwin::ui {

class CheckboxW : public Widget {
 public:
  explicit CheckboxW(Widget* parent = nullptr);

  bool checked() const { return checked_; }
  bool set_checked(bool v, bool fire = true);
  void set_label(const std::wstring& t);
  const std::wstring& label() const { return label_; }

  std::function<void(bool)> on_change;

  // 绘制参数（T12 覆盖）
  Color4f check_color_ = Color4f::hex(0x007AFF);  // ← 选中主色缺省（主题注入前）
  TextStyle label_style_;
  float box_ = 16;

  SizeF on_measure(float max_w, float max_h) override;
  bool on_mouse(const MouseEvent& e) override;
  void on_paint(IRenderTarget& rt) override;

 private:
  bool checked_ = false;
  std::wstring label_;
};

}  // namespace clashwin::ui
