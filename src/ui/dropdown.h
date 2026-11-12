#pragma once
// Dropdown（T10）：下拉选择 + 弹出列表（MenuList 复用）。
// 弹出层 overlay 机制：open() 时构造 MenuList 并经 popup_host 回调交给宿主
// （Stack 顶层）；close()/选择后回调 nullptr 收回。
#include <functional>
#include <string>
#include <vector>

#include "context_menu.h"
#include "widget.h"

namespace clashwin::ui {

class Dropdown : public Widget {
 public:
  explicit Dropdown(Widget* parent = nullptr) : Widget(parent) {}

  std::vector<std::wstring> options;
  int selected_ = 0;
  bool open_ = false;

  std::function<void(int)> on_change;
  // popup_host(popup, anchor_global, opening)：宿主把 popup 放 Stack 顶层；
  // opening=false 表示收回
  std::function<void(MenuList* popup, const RectF& anchor, bool opening)> popup_host;

  int selected() const { return selected_; }
  bool is_open() const { return open_; }
  MenuList* popup() const { return popup_; }

  void set_options(std::vector<std::wstring> opts, int sel = 0);
  void set_selected(int i, bool fire = true);
  void open();
  void close();

  SizeF on_measure(float max_w, float max_h) override;
  bool on_mouse(const MouseEvent& e) override;
  void on_paint(IRenderTarget& rt) override;

  A11yRole a11y_default_role() const override { return A11yRole::ComboBox; }
  bool a11y_is_focusable() const override { return enabled(); }
  std::wstring a11y_value() const override {
    return (selected_ >= 0 && selected_ < static_cast<int>(options.size()))
               ? options[static_cast<size_t>(selected_)]
               : std::wstring();
  }
  std::optional<bool> a11y_expanded() const override { return open_; }
  bool a11y_expand_collapse(bool expand) override {
    if (!enabled()) return false;
    expand ? open() : close();
    return true;
  }
  bool a11y_can_invoke() const override { return enabled(); }
  bool a11y_invoke() override {
    if (!enabled()) return false;
    open_ ? close() : open();
    return true;
  }

  // 绘制参数
  TextStyle style_;
  Color4f bg_ = Color4f::hex(0xFFFFFF);  // src: MUI background.paper #fff
  Color4f border_ = Color4f::hex(0x000000, 0.23f);  // src: MUI OutlinedInput 未聚焦描边 23%
  Color4f border_focused_ = Color4f::hex(0x007AFF);  // ← 聚焦主色缺省（主题注入前）
  Color4f text_ = Color4f::hex(0x000000, 0.87f);  // src: MUI text.primary
  Color4f chevron_ = Color4f::hex(0x000000, 0.54f);  // src: MUI text.secondary/icon 54%
  float height_ = 28;
  float radius_ = 4;

 private:
  MenuList* popup_ = nullptr;
};

}  // namespace clashwin::ui
