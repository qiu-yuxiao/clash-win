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

  void set_icon(const std::string& mui_name);  // 按 MUI 名设置
  void set_glyph(wchar_t g);                   // 直接设码点

  float size_ = 16;                            // 字号=图标边长（逻辑 px）
  Color4f color_{0, 0, 0, 1};
  bool font_fallback_ = false;  // true = Segoe MDL2 Assets（旧系统）
  // 点击支持（T13 快捷条图钉/齿轮；bool hover_/pressed_ 状态）
  std::function<void()> on_click;
  bool hover_ = false, pressed_ = false;

  SizeF on_measure(float max_w, float max_h) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;

 private:
  wchar_t glyph_ = 0;
};

}  // namespace clashwin::ui
