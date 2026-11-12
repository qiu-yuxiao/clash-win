#pragma once
// Label（T10）：文本控件——DirectWrite 排版（字体/字号/粗细/颜色/换行/省略号/对齐）。
// 对照 MUI Typography 语义；测量经 ITextEngine（布局两阶段用）。
#include "widget.h"

namespace clashwin::ui {

class Label : public Widget {
 public:
  explicit Label(Widget* parent = nullptr);
  Label(std::wstring t, Widget* parent = nullptr);

  std::wstring text;
  TextStyle style;  // 缺省 Segoe UI 12px 黑色

  void set_text(const std::wstring& t);
  // F-D01：绑定到 Mini locale key，语言切换时由 Widget::paint_all 自动刷新
  // ← MF:pages/_layout.tsx:478-483（verge.language 变化触发重渲染）
  void set_i18n_text(const char* key);

  SizeF on_measure(float max_w, float max_h) override;
  void on_paint(IRenderTarget& rt) override;
  // 文本不吞鼠标：让 Click/悬停落到父级按钮/段选/输入框
  // （否则点段选文字时命中 Label，父控件收不到 Up，表现为“点了没反应”）
  bool hit_self(PointF) const override { return false; }

  A11yRole a11y_default_role() const override { return A11yRole::Text; }
  std::wstring a11y_default_name() const override { return text; }
};

}  // namespace clashwin::ui
