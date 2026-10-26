// ContextMenu / MenuList 实现（T10）。
#include "context_menu.h"

#include <algorithm>

namespace clashwin::ui {

// ---------- MenuList ----------
SizeF MenuList::on_measure(float max_w, float max_h) {
  TextStyle ts;
  float w = min_width_;
  if (ITextEngine* e = text_engine()) {
    for (const auto& it : items) {
      SizeF s = e->measure_text(it.label + L"  " + it.shortcut, ts, 0);
      w = std::max(w, s.w + padding_h * 2);
    }
  }
  float h = 4;
  for (const auto& it : items) h += it.separator ? 9 : item_height;
  if (max_w > 0 && w > max_w) w = max_w;
  if (max_h > 0 && h > max_h) h = max_h;
  return {w, h};
}

bool MenuList::on_mouse(const MouseEvent& e) {
  switch (e.type) {
    case MouseEventType::Move: {
      int idx = static_cast<int>(e.pos.y / item_height);
      if (idx < 0 || idx >= static_cast<int>(items.size())) idx = -1;
      if (idx >= 0 && items[static_cast<size_t>(idx)].separator) idx = -1;
      if (hover_index != idx) {
        hover_index = idx;
        invalidate();
      }
      return true;
    }
    case MouseEventType::Up: {
      int idx = static_cast<int>(e.pos.y / item_height);
      if (idx >= 0 && idx < static_cast<int>(items.size()) &&
          !items[static_cast<size_t>(idx)].disabled &&
          !items[static_cast<size_t>(idx)].separator) {
        if (on_select) on_select(idx);
        return true;
      }
      return false;
    }
    default:
      return false;
  }
}

void MenuList::on_paint(IRenderTarget& rt) {
  RectF r{0, 0, bounds().w, bounds().h};
  Brush bg_b{bg_};
  rt.round_rect(r, 4, {border_, 1}, &bg_b);
  float y = 2;
  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    const MenuItem& it = items[static_cast<size_t>(i)];
    if (it.separator) {
      rt.fill_rect({8, y + 4, r.w - 16, 1},
                   Brush{Color4f::hex(0x000000, 0.10f)});
      y += 9;
      continue;
    }
    if (i == hover_index && !it.disabled)
      rt.fill_rect({4, y, r.w - 8, item_height}, Brush{hover_bg_});
    TextStyle ts;
    ts.color = it.disabled ? disabled_text_ : text_;
    ts.align_v = AlignV::Center;
    ts.ellipsis = true;
    rt.draw_text(it.label, {padding_h, y, r.w - padding_h * 2 - 56,
                            item_height}, ts);
    if (!it.shortcut.empty()) {
      ts.color = shortcut_text_;
      ts.align_h = AlignH::Right;
      rt.draw_text(it.shortcut,
                   {r.w - 8 - 56, y, 56, item_height}, ts);
    }
    y += item_height;
  }
}

// ---------- ContextMenu ----------
void ContextMenu::open(const std::vector<MenuItem>& items, PointF anchor) {
  if (menu_) remove_child(menu_);
  auto ml = std::make_unique<MenuList>();
  ml->items = items;
  ml->on_select = [this](int idx) {
    if (on_select) on_select(idx);  // 先回调（menu_ 在回调期间仍存活）
    close();                        // 后关闭（避免在 MenuList::on_mouse 内释放自身）
  };
  menu_ = ml.get();
  add_child(std::move(ml));
  // measure（无引擎时按最小尺寸），定位在 anchor，简单防越界
  SizeF s = menu_->measure(0, 0);
  menu_->set_bounds({anchor.x, anchor.y, s.w, s.h});
  open_ = true;
  invalidate();
}

void ContextMenu::close() {
  if (menu_) remove_child(menu_);  // 返回 unique_ptr 丢弃
  menu_ = nullptr;
  open_ = false;
  invalidate();
}

bool ContextMenu::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  if (e.type == MouseEventType::Down || e.type == MouseEventType::Up) {
    // 落在菜单上 → 不拦截（MenuList 在 hit_test 中更深，Up 送达它）
    if (menu_ && menu_->bounds().contains(e.pos.x, e.pos.y)) return false;
    if (e.type == MouseEventType::Down) {
      close();  // 菜单外点击：关闭并吞掉
      return true;
    }
    return true;
  }
  return false;
}

void ContextMenu::on_paint(IRenderTarget& rt) {
  if (menu_) {
    // 阴影近似：菜单框外圈半透明描边
    RectF m = menu_->bounds();
    Brush shadow{Color4f::hex(0x000000, 0.18f)};
    rt.round_rect({m.x - 3, m.y - 3, m.w + 6, m.h + 6}, 7, {}, &shadow);
  }
}

}  // namespace clashwin::ui
