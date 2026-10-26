// Dropdown 实现（T10）。
#include "dropdown.h"

#include <algorithm>

namespace clashwin::ui {

void Dropdown::set_options(std::vector<std::wstring> opts, int sel) {
  options = std::move(opts);
  selected_ = sel;
  invalidate();
}

void Dropdown::set_selected(int i, bool fire) {
  if (i < 0 || i >= static_cast<int>(options.size()) || i == selected_) return;
  selected_ = i;
  invalidate();
  if (fire && on_change) on_change(selected_);
}

void Dropdown::open() {
  if (open_ || options.empty()) return;
  auto ml = std::make_unique<MenuList>();
  for (const auto& o : options) ml->items.push_back({o, false, false, L""});
  ml->hover_index = selected_;
  MenuList* p = ml.get();
  p->on_select = [this](int idx) {
    close();
    set_selected(idx, true);
  };
  popup_ = p;
  open_ = true;
  // popup 期望尺寸：宿主按 anchor 决定最终 bounds（防越界归宿主）
  SizeF s = p->measure(220, 0);
  p->set_bounds({global_bounds().x, global_bounds().bottom(), s.w, s.h});
  if (popup_host) popup_host(p, global_bounds(), true);
  invalidate();
}

void Dropdown::close() {
  if (!open_) return;
  if (popup_host) popup_host(popup_, global_bounds(), false);
  popup_ = nullptr;
  open_ = false;
  invalidate();
}

SizeF Dropdown::on_measure(float max_w, float max_h) {
  (void)max_h;
  return {max_w > 0 ? max_w : 120, height_};
}

bool Dropdown::on_mouse(const MouseEvent& e) {
  if (e.type == MouseEventType::Down) {
    if (!enabled()) return true;
    open_ ? close() : open();
    return true;
  }
  return false;
}

void Dropdown::on_paint(IRenderTarget& rt) {
  RectF r{0, 0, bounds().w, bounds().h};
  Brush bg_b{bg_};
  rt.round_rect(r, radius_, {open_ ? border_focused_ : border_, 1}, &bg_b);
  TextStyle ts = style_;
  ts.align_v = AlignV::Center;
  ts.ellipsis = true;
  std::wstring cur = selected_ >= 0 && selected_ < static_cast<int>(options.size())
                        ? options[static_cast<size_t>(selected_)]
                        : L"";
  rt.draw_text(cur, {8, 0, r.w - 32, r.h}, ts);
  // ChevronDown 字形
  ts.align_h = AlignH::Right;
  ts.font_family = L"Segoe Fluent Icons";
  ts.size = 10;
  ts.color = chevron_;
  rt.draw_text(std::wstring(1, L'\uE70D'), {r.w - 20, 0, 14, r.h}, ts);
}

}  // namespace clashwin::ui
