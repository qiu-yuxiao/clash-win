// Label 实现（T10）。
#include "label.h"

namespace clashwin::ui {

Label::Label(Widget* parent) : Widget(parent) {}

Label::Label(std::wstring t, Widget* parent)
    : Widget(parent), text(std::move(t)) {}

void Label::set_text(const std::wstring& t) {
  if (text == t) return;
  text = t;
  invalidate();
}

void Label::set_i18n_text(const char* key) {
  // F-D01：文本槽随语言变化自动刷新（Widget::paint_all 的 revision 机制）
  bind_i18n_text(&text, key);
  invalidate();
}

SizeF Label::on_measure(float max_w, float max_h) {
  ITextEngine* e = text_engine();
  if (!e) return {};
  SizeF s = e->measure_text(text, style, style.wrap ? max_w : 0);
  // nowrap+ellipsis：宽度钳到可用宽（绘制时 trimming 出省略号）
  if (!style.wrap && max_w > 0 && s.w > max_w && style.ellipsis) s.w = max_w;
  if (max_h > 0 && s.h > max_h) s.h = max_h;
  return s;
}

void Label::on_paint(IRenderTarget& rt) {
  rt.draw_text(text, {0, 0, bounds().w, bounds().h}, style);
}

}  // namespace clashwin::ui
