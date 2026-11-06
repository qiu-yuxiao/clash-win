#pragma once
// IconW（T10）：图标字形渲染。
// 原版用 @mui/icons-material（Rounded 系）；C++ 侧以系统字体 Segoe Fluent
// Icons（Win11）/ Segoe MDL2 Assets 等价字形映射表渲染（glyph_for）。
// 映射表覆盖原版实际引用清单 + T13+ 常用项；无精确对应者取近似字形。
#include <string>
#include <unordered_map>

#include "widget.h"

namespace clashwin::ui {

class IconW : public Widget {
 public:
  explicit IconW(Widget* parent = nullptr);

  // @mui 图标名 → 字形码点（大小写/下划线不敏感；未知返回 0）
  static wchar_t glyph_for(const std::string& mui_name);
  // 是否有内置 SVG path（折线）版本；有则优先矢量绘制，不依赖系统图标字体
  static bool has_vector_path(const std::string& mui_name);

  void set_icon(const std::string& mui_name);  // 按 MUI 名设置
  void set_glyph(wchar_t g);                   // 直接设码点

  float size_ = 16;                            // 字号=图标边长（逻辑 px）
  float box_w = 0, box_h = 0;  // 固定命中盒（0=size_；字形仍按 size_ 居中绘制）
  Color4f color_{0, 0, 0, 1};
  // 矢量图标旋转角（顺时针°，绕图标盒中心；Mini CSS transform: rotate(45deg)）
  float rotate_deg = 0.0f;
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

 private:
  wchar_t glyph_ = 0;
  std::string name_;  // 归一化 MUI 名（矢量 path 查表用）
};

}  // namespace clashwin::ui
