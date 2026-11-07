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
  bool danger = false;    // 红字项（Mini 删除项 color:error.main，_layout.tsx:1852）
  bool selected = false;  // 选中态（MUI selected：bg action.selected + text primary）
};

// 菜单列表本体（也可被 Dropdown 复用）
class MenuList : public Widget {
 public:
  explicit MenuList(Widget* parent = nullptr) : Widget(parent) {}

  std::vector<MenuItem> items;
  int hover_index = -1;
  std::function<void(int)> on_select;  // 禁用项不触发
  float item_height = 28;
  float sep_height_ = 9;  // 分隔线高度（Mini Divider：1px + 上下 margin 8 = 17）

  SizeF on_measure(float max_w, float max_h) override;
  bool on_mouse(const MouseEvent& e) override;
  void on_paint(IRenderTarget& rt) override;

  // 绘制参数
  Color4f bg_ = Color4f::hex(0xFFFFFF);  // src: MUI background.paper #fff
  Color4f border_ = Color4f::hex(0x000000, 0.12f);  // src: MUI palette.divider
  Color4f hover_bg_ = Color4f::hex(0x000000, 0.06f);  // src: MUI action.hover rgba(0,0,0,0.06)
  Color4f text_ = Color4f::hex(0x000000, 0.87f);  // src: MUI text.primary
  Color4f disabled_text_ = Color4f::hex(0x000000, 0.38f);  // src: MUI text.disabled
  Color4f shortcut_text_ = Color4f::hex(0x000000, 0.45f);  // src: MUI text.secondary
  Color4f danger_text_ = Color4f::hex(0xD32F2F);  // src: MUI error.main（light）
  // src: MUI MenuItem selected → bg alpha(primary,.08) + text.primary
  //   （_layout.tsx:1550-1558 语言 Select 的 MenuItem 自动带 selected）
  Color4f selected_bg_ = Color4f::hex(0x007AFF, 0.08f);
  Color4f selected_text_ = Color4f::hex(0x000000, 0.87f);
  float min_width_ = 140;
  float padding_h = 12;

  // 以下为 Mini 订阅右键菜单（_layout.tsx:1806-1856 + style-helpers.tsx:115-145）
  // 所需的扩展；缺省值保持旧观感（语言/连接表菜单不受影响）。
  float radius_ = 4;
  float font_size_ = 0;      // 0 = 引擎缺省
  int font_weight_ = 0;      // 0 normal / 1 bold
  bool gold_hover_ = false;  // retro-3d hover 金渐变 + 深金文字
  bool gradient_panel_ = false;
  GradientStop panel_grad_[3];
  float pad_top_ = 2;
  float pad_bottom_ = 2;
  float hover_inset_ = 4;
  float hover_radius_ = 0;  // hover 底圆角（cyberpunk/monochrome 为 0，其余同 MUI 缺省）

  // 6 套皮肤扩展（← apply_menu_skin）
  bool hover_grad_ = false;
  GradientStop hover_stops_[4] = {};
  int hover_n_ = 0;
  float hover_angle_ = 135.0f;
  Color4f hover_text_{0, 0, 0, 0};
  Color4f hover_border_{0, 0, 0, 0};
  float hover_border_w_ = 0;
  bool hover_glow_ = false;
  Color4f hover_glow_color_;
  float hover_glow_blur_ = 0;
  bool panel_grad_on_ = false;
  GradientStop panel_stops_[3] = {};
  int panel_n_ = 0;
  float panel_angle_ = 135.0f;
  Color4f panel_bg_{0, 0, 0, 0};
  Color4f panel_border_{0, 0, 0, 0};
  float panel_border_w_ = 1;
};

// 菜单外观（Mini 订阅右键菜单用；缺省 = 旧观感）
struct MenuStyle {
  float item_h = 28;
  float sep_h = 9;
  float min_w = 140;
  float radius = 4;
  float font_size = 0;
  int font_weight = 0;
  bool gold_hover = false;
  bool gradient_panel = false;
  float pad_v = 2;
  // src: style-helpers.tsx:126-145 MenuItem padding '8px 16px' → 水平 16
  float padding_h = 12;
  float hover_inset = 4;
  // MUI Menu 缺省（dark/light paper 由宿主覆盖；src: _layout.tsx:1806-1818）
  Color4f bg = Color4f::hex(0xFFFFFF);                   // src: MUI palette.background.paper
  Color4f border = Color4f::hex(0x000000, 0.12f);        // src: MUI palette.divider
  Color4f text = Color4f::hex(0x000000, 0.87f);          // src: MUI text.primary
  Color4f disabled_text = Color4f::hex(0x000000, 0.38f); // src: MUI disabled.opacity .38
  Color4f danger_text = Color4f::hex(0xD32F2F);           // src: MUI error.main
  Color4f selected_bg = Color4f::hex(0x007AFF, 0.08f);   // src: MUI action.selected
  Color4f selected_text = Color4f::hex(0x000000, 0.87f);  // src: text.primary
  GradientStop grad[3] = {};

  // ---- 6 套皮肤（← style-helpers.tsx:109-198 getMenuItemHoverStyle +
  //      index.scss:109-114 .MuiMenu-paper.theme-panel）----
  // hover 渐变（retro-3d 135° 四停）；hover_n=0 时退回 hover_bg 平涂
  bool hover_grad = false;
  GradientStop hover_stops[4] = {};
  int hover_n = 0;
  float hover_angle = 135.0f;
  Color4f hover_bg{0, 0, 0, 0};        // a=0 → 用缺省 hover_bg
  float hover_radius = 0;              // hover 底圆角
  Color4f hover_text{0, 0, 0, 0};      // a=0 → 沿用 text
  Color4f hover_border{0, 0, 0, 0};    // a=0 → 不画 hover 描边
  float hover_border_w = 0;
  bool hover_glow = false;             // cyberpunk 0 0 8px #39ff14
  Color4f hover_glow_color;
  float hover_glow_blur = 0;

  // 面板（.theme-panel 在 Menu paper 上的等价物）
  bool panel_grad_on = false;          // true → panel_stops 渐变底
  GradientStop panel_stops[3] = {};
  int panel_n = 0;
  float panel_angle = 135.0f;
  Color4f panel_bg{0, 0, 0, 0};        // a=0 → 用 bg
  Color4f panel_border{0, 0, 0, 0};    // a=0 → 用 border
  float panel_border_w = 1;
};

// 按当前皮肤/亮暗填写菜单外观（6 套皮肤查表；调用点不再手写颜色）
void apply_menu_skin(MenuStyle& st);

// 右键菜单 overlay
class ContextMenu : public Widget {
 public:
  explicit ContextMenu(Widget* parent = nullptr) : Widget(parent) {
    set_z_order(900);
  }

  std::function<void(int)> on_select;
  MenuStyle style_;  // open() 时应用到 MenuList

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
