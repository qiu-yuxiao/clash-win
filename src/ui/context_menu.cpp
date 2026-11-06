// ContextMenu / MenuList 实现（T10）。
#include "context_menu.h"

#include <algorithm>

namespace clashwin::ui {

// ---------- MenuList ----------
SizeF MenuList::on_measure(float max_w, float max_h) {
  TextStyle ts;
  if (font_size_ > 0) ts.size = font_size_;
  if (font_weight_ > 0) ts.weight = font_weight_;
  float w = min_width_;
  if (ITextEngine* e = text_engine()) {
    for (const auto& it : items) {
      SizeF s = e->measure_text(it.label + L"  " + it.shortcut, ts, 0);
      w = std::max(w, s.w + padding_h * 2);
    }
  }
  float h = pad_top_ + pad_bottom_;
  for (const auto& it : items) h += it.separator ? sep_height_ : item_height;
  if (max_w > 0 && w > max_w) w = max_w;
  if (max_h > 0 && h > max_h) h = max_h;
  return {w, h};
}

bool MenuList::on_mouse(const MouseEvent& e) {
  // 逐项累加命中：分隔线占 9px，不能再用 y/item_height 整除（多分隔线会错位）
  auto hit_index = [this](float y) -> int {
    float cy = pad_top_;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
      const float h = items[static_cast<size_t>(i)].separator ? sep_height_ : item_height;
      if (y >= cy && y < cy + h)
        return items[static_cast<size_t>(i)].separator ? -1 : i;
      cy += h;
    }
    return -1;
  };
  switch (e.type) {
    case MouseEventType::Move: {
      int idx = hit_index(e.pos.y);
      if (hover_index != idx) {
        hover_index = idx;
        invalidate();
      }
      return true;
    }
    case MouseEventType::Up: {
      int idx = hit_index(e.pos.y);
      if (idx >= 0 && !items[static_cast<size_t>(idx)].disabled) {
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
  if (gradient_panel_) {
    // src: _layout.tsx:1806-1818 paper theme-panel + radius 6 + 1px 白 .12
    rt.fill_round_rect_gradient_angle(r, radius_, panel_grad_, 3, 135.0f);
    rt.round_rect(r, radius_, {border_, 1.0f}, nullptr);
  } else {
    Brush bg_b{bg_};
    rt.round_rect(r, radius_, {border_, 1.0f}, &bg_b);
  }
  float y = pad_top_;
  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    const MenuItem& it = items[static_cast<size_t>(i)];
    if (it.separator) {
      // src: _layout.tsx:1832 Divider my4px / rgba(255,255,255,.12)（全宽）
      const RectF line = gradient_panel_
                             ? RectF{0, y + (sep_height_ - 1.0f) / 2, r.w, 1}
                             : RectF{8, y + (sep_height_ - 1.0f) / 2, r.w - 16, 1};
      rt.fill_rect(line, Brush{gradient_panel_ ? border_
                                               : Color4f::hex(0x000000, 0.10f)});
      y += sep_height_;
      continue;
    }
    const RectF item_rect{hover_inset_, y, r.w - hover_inset_ * 2, item_height};
    const bool hot = (i == hover_index) && !it.disabled;
    // src: MUI MenuItem.selected → bg action.selected（语言菜单当前项）
    if (it.selected && !it.disabled)
      rt.fill_rect(item_rect, Brush{selected_bg_});
    if (hot) {
      if (gold_hover_) {
        // src: style-helpers.tsx:138-141 135° #FFE082/#FFD54F 35%/#FFB300 70%/#B8860B
        GradientStop hg[4] = {{0.00f, Color4f::hex(0xFFE082)},
                              {0.35f, Color4f::hex(0xFFD54F)},
                              {0.70f, Color4f::hex(0xFFB300)},
                              {1.00f, Color4f::hex(0xB8860B)}};
        rt.fill_round_rect_gradient_angle(item_rect, 0.0f, hg, 4, 135.0f);
      } else {
        rt.fill_rect(item_rect, Brush{hover_bg_});
      }
    }
    TextStyle ts;
    if (font_size_ > 0) ts.size = font_size_;
    if (font_weight_ > 0) ts.weight = font_weight_;
    ts.color = it.disabled ? disabled_text_
                           : (it.danger ? danger_text_
                                        : (it.selected ? selected_text_ : text_));
    if (hot && gold_hover_) ts.color = Color4f::hex(0x2C1F03);  // src: :141
    ts.align_v = AlignV::Center;
    ts.ellipsis = true;
    // 仅在确有快捷键时才为右侧留 56（否则无快捷键的项会被无谓截断）
    const float shortcut_reserve = it.shortcut.empty() ? 0.0f : 56.0f;
    rt.draw_text(it.label, {padding_h, y, r.w - padding_h * 2 - shortcut_reserve,
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
  // 外观注入（缺省值 = 旧观感）
  ml->item_height = style_.item_h;
  ml->sep_height_ = style_.sep_h;
  ml->min_width_ = style_.min_w;
  ml->radius_ = style_.radius;
  ml->font_size_ = style_.font_size;
  ml->font_weight_ = style_.font_weight;
  ml->gold_hover_ = style_.gold_hover;
  ml->gradient_panel_ = style_.gradient_panel;
  ml->pad_top_ = ml->pad_bottom_ = style_.pad_v;
  ml->padding_h = style_.padding_h;  // src: style-helpers.tsx:126-145 '8px 16px'
  ml->hover_inset_ = style_.hover_inset;
  ml->bg_ = style_.bg;
  ml->border_ = style_.border;
  ml->text_ = style_.text;
  ml->disabled_text_ = style_.disabled_text;
  ml->danger_text_ = style_.danger_text;
  ml->selected_bg_ = style_.selected_bg;
  ml->selected_text_ = style_.selected_text;
  for (int i = 0; i < 3; ++i) ml->panel_grad_[i] = style_.grad[i];
  ml->on_select = [this](int idx) {
    if (on_select) on_select(idx);  // 先回调（menu_ 在回调期间仍存活）
    close();                        // 后关闭（避免在 MenuList::on_mouse 内释放自身）
  };
  menu_ = ml.get();
  add_child(std::move(ml));
  // measure（无引擎时按最小尺寸），定位在 anchor；越界向内收 4px
  SizeF s = menu_->measure(0, 0);
  float x = anchor.x, y = anchor.y;
  if (Widget* p = parent(); p && p->bounds().w > 0) {
    if (x + s.w > p->bounds().w) x = std::max(0.0f, p->bounds().w - s.w - 4.0f);
    if (y + s.h > p->bounds().h) y = std::max(0.0f, p->bounds().h - s.h - 4.0f);
  }
  menu_->set_bounds({x, y, s.w, s.h});
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
    // src: MUI Popover elevation 阴影（D2D 无 box-shadow，偏移层模拟）
    RectF m = menu_->bounds();
    Brush shadow{Color4f::hex(0x000000, 0.18f)};  // 豁免: Mini 无对应物 — D2D 无 box-shadow，18% 偏移层模拟（出处: MUI elevation 并非色值字面量）
    rt.round_rect({m.x - 3, m.y - 3, m.w + 6, m.h + 6}, 7, {}, &shadow);
  }
}

}  // namespace clashwin::ui
