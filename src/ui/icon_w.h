#pragma once
// IconW（T10）：图标渲染。
// F-D11（2026-11-06）：优先级 1 = 内嵌 @mui/icons-material 真实 SVG path
//   （mui_paths 表，d 属性逐字取自 ClashVerge node_modules；24×24 viewBox
//   扁平化为闭合环后 fill_path 偶奇填充）；优先级 2 = 手绘折线表；优先级 3 =
//   Segoe Fluent/MDL2 字形映射（glyph_for，保留作未覆盖图标的兜底）。
#include <string>
#include <unordered_map>

#include "widget.h"

namespace clashwin::ui {

class IRenderTarget;

// 一条 MUI <path> 子元素：fill-opacity + 扁平化闭合环（24×24 坐标）。
struct MuiLoopSet {
  std::vector<std::vector<PointF>> loops;
  float opacity = 1.0f;
};

class IconW : public Widget {
 public:
  explicit IconW(Widget* parent = nullptr);

  // @mui 图标名 → 字形码点（大小写/下划线不敏感；未知返回 0）
  static wchar_t glyph_for(const std::string& mui_name);
  // 是否有内置矢量版本（MUI path 表或手绘折线表）；有则不落字体字形
  static bool has_vector_path(const std::string& mui_name);
  // F-D11：MUI SVG path 表展开（不缩放，24×24 坐标）；命中返回 true。
  // 供 IconW 与 TitleBarButton（标题栏三键）共用。
  static bool mui_paths_for(const std::string& mui_name,
                            std::vector<MuiLoopSet>& out);

  void set_icon(const std::string& mui_name);  // 按 MUI 名设置
  void set_glyph(wchar_t g);                   // 直接设码点
  // 在既有绘制目标上按绝对矩形画一颗图标（无独立控件树时用：通知条/动态列表）
  static void paint_at(IRenderTarget& rt, const std::string& mui_name,
                       const RectF& box, const Color4f& color,
                       bool flip_y = false);
  // 当前归一化图标名（F-D11：延迟徽章按档位切换前比对用）
  const std::string& name() const { return name_; }

  float size_ = 16;                            // 字号=图标边长（逻辑 px）
  float box_w = 0, box_h = 0;  // 固定命中盒（0=size_；字形仍按 size_ 居中绘制）
  Color4f color_{0, 0, 0, 1};
  // 矢量图标旋转角（顺时针°，绕图标盒中心；Mini CSS transform: rotate(45deg)）
  float rotate_deg = 0.0f;
  // 矢量图标垂直镜像（绕盒中心；Mini CSS transform: scaleY(-1)，logs.tsx:141
  //   SwapVertRounded 降序态翻转）
  bool flip_y = false;
  // 外发光（src: _layout.tsx:1359-1361 drop-shadow rgba(255,59,48,.85)；
  //   以环形偏移重绘等价改写高斯模糊）
  bool glow_ = false;
  Color4f glow_color_ = Color4f::hex(0xFF3B30, 0.85f);
  bool font_fallback_ = false;  // true = Segoe MDL2 Assets（旧系统）
  // 点击支持（T13 快捷条图钉/齿轮；bool hover_/pressed_ 状态）
  std::function<void()> on_click;
  bool hover_ = false, pressed_ = false;

  SizeF on_measure(float max_w, float max_h) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;

  A11yRole a11y_default_role() const override {
    return on_click ? A11yRole::Button : A11yRole::None;
  }
  bool a11y_is_focusable() const override {
    return enabled() && static_cast<bool>(on_click);
  }
  bool a11y_can_invoke() const override {
    return enabled() && static_cast<bool>(on_click);
  }
  bool a11y_invoke() override {
    if (!enabled() || !on_click) return false;
    on_click();
    return true;
  }

 private:
  wchar_t glyph_ = 0;
  std::string name_;  // 归一化 MUI 名（矢量 path 查表用）
};

}  // namespace clashwin::ui
