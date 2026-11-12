#pragma once
// SwitchW（T10）：开关——开/关/拖动中三态 + 动画进度（anim_ 0..1）。
// 对照 MUI Switch：单击切换；拖动时 thumb 跟随 x，松开按进度≥0.5 判定。
#include <functional>

#include "widget.h"

namespace clashwin::ui {

class SwitchW : public Widget {
 public:
  explicit SwitchW(Widget* parent = nullptr) : Widget(parent) {}

  bool checked() const { return checked_; }
  bool dragging() const { return dragging_; }
  float anim() const { return anim_; }  // 测试检查点
  // ★ 旋钮悬停态（← Mini `&:hover .MuiSwitch-thumb`）——测试检查点
  bool thumb_hovered() const { return thumb_hovered_; }
  // fire=false 时只改状态不触发回调（恢复内核实际值用）
  bool set_checked(bool v, bool fire = true);

  std::function<void(bool)> on_change;

  // 绘制参数（T12 皮肤覆盖；缺省 = retro-3d 源码值，on_paint 内按主题重算）
  Color4f track_on_ = Color4f::hex(0xFFC107);   // src: base-switch.tsx:99-103
  Color4f track_off_ = Color4f::hex(0xD4AF37);  // src: base-switch.tsx:104-109
  Color4f thumb_ = Color4f::hex(0xFFD54F);      // src: base-switch.tsx:84-97
  float width_ = 28, height_ = 14;  // ← base-switch.tsx（small 同值强制，零内衬）

  SizeF on_measure(float max_w, float max_h) override;
  bool on_mouse(const MouseEvent& e) override;
  void on_paint(IRenderTarget& rt) override;

  A11yRole a11y_default_role() const override { return A11yRole::CheckBox; }
  bool a11y_is_focusable() const override { return enabled(); }
  std::optional<bool> a11y_toggle_state() const override { return checked_; }
  bool a11y_toggle() override {
    if (!enabled()) return false;
    set_checked(!checked_, true);
    return true;
  }

 private:
  void update_drag(float local_x);
  bool checked_ = false;
  bool dragging_ = false;
  bool drag_moved_ = false;  // Down 后有超过阈值的位移 = 拖动（否则单击）
  float press_x_ = 0;
  float anim_ = 0;
  // ★ 悬停态（Mini 的 hover 挂在开关根上：`&:hover .MuiSwitch-thumb`）
  bool thumb_hovered_ = false;
};

}  // namespace clashwin::ui
