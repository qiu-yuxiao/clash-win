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

  SizeF on_measure(float max_w, float max_h) override;
  void on_paint(IRenderTarget& rt) override;
};

}  // namespace clashwin::ui
