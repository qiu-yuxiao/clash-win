#pragma once
// ContextMenu（T10）：右键菜单——作为 overlay 挂根 Stack 顶层使用。
// MenuList：菜单列表本体（hover 高亮/禁用项/分隔线），Dropdown 弹出列表复用。
// ContextMenu：全屏透明遮罩（吞掉菜单外点击）+ 定位 MenuList。
#include <functional>
#include <string>
#include <vector>

#include "widget.h"

namespace clashwin::ui {

struct MenuItem {
  std::wstring label;
  bool disabled = false;
  bool separator = false;
  std::wstring shortcut;  // 右侧灰字（T20 连接表用）
};

// 菜单列表本体（也可被 Dropdown 复用）
class MenuList : public Widget {
 public:
  explicit MenuList(Widget* parent = nullptr) : Widget(parent) {}

  std::vector<MenuItem> items;
  int hover_index = -1;
  std::function<void(int)> on_select;  // 禁用项不触发
  float item_height = 28;

  SizeF on_measure(float max_w, float max_h) override;
  bool on_mouse(const MouseEvent& e) override;
  void on_paint(IRenderTarget& rt) override;

  // 绘制参数
  Color4f bg_ = Color4f::hex(0xFFFFFF);
  Color4f border_ = Color4f::hex(0x000000, 0.12f);
  Color4f hover_bg_ = Color4f::hex(0x000000, 0.06f);
  Color4f text_ = Color4f::hex(0x000000, 0.87f);
  Color4f disabled_text_ = Color4f::hex(0x000000, 0.38f);
  Color4f shortcut_text_ = Color4f::hex(0x000000, 0.45f);
  float min_width_ = 140;
  float padding_h = 12;
};

// 右键菜单 overlay
class ContextMenu : public Widget {
 public:
  explicit ContextMenu(Widget* parent = nullptr) : Widget(parent) {
    set_z_order(900);
  }

  std::function<void(int)> on_select;

  bool is_open() const { return open_; }
  // anchor：全局坐标；打开后 menu 由宿主 arrange（通常 measure 后定位在
  // anchor，超界翻转归宿主/T13）
  void open(const std::vector<MenuItem>& items, PointF anchor);
  void close();
  RectF menu_rect() const { return menu_ ? menu_->bounds() : RectF{}; }
  MenuList* menu() const { return menu_; }

  // 遮罩：菜单外点击 → close 并吞掉
  bool on_mouse(const MouseEvent& e) override;
  void on_paint(IRenderTarget& rt) override;
  // 未打开时不参与命中
  bool hit_self(PointF local) const override {
    return open_ && Widget::hit_self(local);
  }

 private:
  MenuList* menu_ = nullptr;  // add_child 持有
  bool open_ = false;
};

}  // namespace clashwin::ui
