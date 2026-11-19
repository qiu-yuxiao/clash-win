// ContextMenu / MenuList 实现（T10）。
#include "context_menu.h"

#include <algorithm>

#include "icon_w.h"
#include "soft_shadow.h"
#include "theme/theme.h"

namespace clashwin::ui {

// ---------- MenuList ----------
float MenuList::content_height() const {
  float h = pad_top_ + pad_bottom_;
  for (const auto& it : items) h += it.separator ? sep_height_ : item_height;
  return h;
}

size_t MenuList::a11y_virtual_child_count() const {
  size_t n = 0;
  for (const auto& it : items) {
    if (!it.separator) ++n;
  }
  return n;
}

A11ySubItem MenuList::a11y_virtual_child(size_t index) const {
  A11ySubItem sub;
  sub.role = A11yRole::MenuItem;
  size_t non_sep_total = a11y_virtual_child_count();
  size_t cur = 0;
  float cy = pad_top_ - scroll_y_;
  for (size_t i = 0; i < items.size(); ++i) {
    const auto& it = items[i];
    const float ih = it.separator ? sep_height_ : item_height;
    if (!it.separator) {
      if (cur == index) {
        sub.name = it.label;
        if (!it.shortcut.empty()) sub.help_text = it.shortcut;
        sub.local_bounds = {0.0f, cy, bounds().w, ih};
        sub.enabled = enabled() && !it.disabled;
        sub.focusable = sub.enabled;
        sub.can_invoke = sub.enabled;
        if (it.selected) sub.selected = true;
        sub.pos_in_set = static_cast<int>(cur) + 1;
        sub.size_of_set = static_cast<int>(non_sep_total);
        return sub;
      }
      ++cur;
    }
    cy += ih;
  }
  return sub;
}

bool MenuList::a11y_invoke_virtual_child(size_t index) {
  size_t cur = 0;
  for (size_t i = 0; i < items.size(); ++i) {
    const auto& it = items[i];
    if (it.separator) continue;
    if (cur == index) {
      if (it.disabled || !on_select) return false;
      on_select(static_cast<int>(i));
      return true;
    }
    ++cur;
  }
  return false;
}

void MenuList::set_scroll_y(float sy) {
  const float max_s = std::max(0.0f, content_height() - bounds().h);
  const float clamped = std::clamp(sy, 0.0f, max_s);
  if (clamped != scroll_y_) {
    scroll_y_ = clamped;
    invalidate();
  }
}

void MenuList::scroll_to_selected() {
  const float view_h = bounds().h;
  if (view_h <= 0.0f || content_height() <= view_h) {
    scroll_y_ = 0.0f;
    return;
  }
  float cy = pad_top_;
  for (const auto& it : items) {
    const float ih = it.separator ? sep_height_ : item_height;
    if (it.selected && !it.separator) {
      if (cy < scroll_y_) {
        set_scroll_y(std::max(0.0f, cy - pad_top_));
      } else if (cy + ih > scroll_y_ + view_h) {
        set_scroll_y(cy + ih - view_h + pad_bottom_);
      }
      return;
    }
    cy += ih;
  }
}

SizeF MenuList::on_measure(float max_w, float max_h) {
  TextStyle ts;
  if (font_size_ > 0) ts.size = font_size_;
  if (font_weight_ > 0) ts.weight = font_weight_;
  float w = min_width_;
  if (ITextEngine* e = text_engine()) {
    for (const auto& it : items) {
      SizeF s = e->measure_text(it.label + L"  " + it.shortcut, ts, 0);
      // src: connection-table.tsx:567 fontSize 18 + popup-styles.ts:283 gap 1.25(10)
      const float icon_reserve = it.icon.empty() ? 0.0f : 28.0f;
      w = std::max(w, s.w + padding_h * 2 + icon_reserve);
    }
  }
  float h = content_height();
  if (max_w > 0 && w > max_w) w = max_w;
  if (max_h > 0 && h > max_h) h = max_h;
  return {w, h};
}

bool MenuList::on_mouse(const MouseEvent& e) {
  // 逐项累加命中（叠加纵向滚动偏移 scroll_y_）
  auto hit_index = [this](float local_y) -> int {
    const float y = local_y + scroll_y_;
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
    case MouseEventType::Wheel: {
      if (content_height() <= bounds().h) return true;
      // src: MUI Menu paper overflowY 'auto' 滚轮滚动
      const float notches = static_cast<float>(e.wheel_delta) / 120.0f;
      const float step = std::max(24.0f, item_height);
      set_scroll_y(scroll_y_ - notches * step);
      hover_index = hit_index(e.pos.y);
      return true;
    }
    case MouseEventType::Leave: {
      if (hover_index != -1) {
        hover_index = -1;
        invalidate();
      }
      return true;
    }
    case MouseEventType::Down: {
      hover_index = hit_index(e.pos.y);
      invalidate();
      return true;
    }
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
        auto cb = on_select;
        if (cb) cb(idx);
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
  // 面板底：6 套皮肤走 apply_menu_skin 填好的参数（← index.scss:109-114
  //   .MuiMenu-paper.theme-panel + 各皮肤 .theme-panel override）
  if (panel_grad_on_) {
    rt.fill_round_rect_gradient_angle(r, radius_, panel_stops_, panel_n_,
                                      panel_angle_);
  } else {
    Brush bg_b{panel_bg_.a > 0.001f ? panel_bg_ : bg_};
    rt.round_rect(r, radius_, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &bg_b);
  }

  rt.push_clip({1.0f, 1.0f, std::max(0.0f, r.w - 2.0f), std::max(0.0f, r.h - 2.0f)});
  float y = pad_top_ - scroll_y_;
  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    const MenuItem& it = items[static_cast<size_t>(i)];
    if (it.separator) {
      // src: src/utils/popup-styles.ts:364-367 Divider my 4px & borderColor tokens.dividerColor
      const Color4f div_c = border_.a > 0.001f ? border_ : Color4f::hex(0x000000, 0.12f);
      const RectF line = gradient_panel_
                             ? RectF{0, y + (sep_height_ - 1.0f) / 2, r.w, 1}
                             : RectF{8, y + (sep_height_ - 1.0f) / 2, r.w - 16, 1};
      rt.fill_rect(line, Brush{div_c});
      y += sep_height_;
      continue;
    }
    if (y + item_height < 0.0f || y > r.h) {
      y += item_height;
      continue;
    }
    const RectF item_rect{hover_inset_, y, r.w - hover_inset_ * 2, item_height};
    const bool hot = (i == hover_index) && !it.disabled;
    // src: src/utils/popup-styles.ts:300-337 选中底色
    if (it.selected && !it.disabled)
      rt.fill_rect(item_rect, Brush{selected_bg_});
    if (hot) {
      // hover：← style-helpers.tsx:109-198 getMenuItemHoverStyle（6 套皮肤）
      if (hover_glow_)
        draw_glow(rt, item_rect, 0.0f, hover_glow_color_, hover_glow_blur_);
      if (hover_grad_ && hover_n_ > 0) {
        rt.fill_round_rect_gradient_angle(item_rect, 0.0f, hover_stops_,
                                          hover_n_, hover_angle_);
      } else {
        Brush hb{hover_bg_};
        rt.round_rect(item_rect, hover_radius_, Pen{Color4f{0, 0, 0, 0}, 0.0f},
                      &hb);
      }
      if (hover_border_w_ > 0.01f && hover_border_.a > 0.001f)
        rt.round_rect(item_rect, hover_radius_,
                      Pen{hover_border_, hover_border_w_}, nullptr);
    }
    TextStyle ts;
    if (font_size_ > 0) ts.size = font_size_;
    if (font_weight_ > 0) ts.weight = font_weight_;
    ts.color = it.disabled ? disabled_text_
                           : (it.danger ? danger_text_
                                        : (it.selected ? selected_text_ : text_));
    if (hot && hover_text_.a > 0.001f) {
      // src: src/utils/popup-styles.ts:298,333 danger 悬停保持/加深错误红（除 solid 高饱和底）
      if (!it.danger || hover_bg_.a >= 0.5f) {
        ts.color = hover_text_;
      }
    }
    ts.align_v = AlignV::Center;
    ts.ellipsis = true;
    // src: connection-table.tsx:567 fontSize 18 + popup-styles.ts:283 gap 1.25(10)
    float text_x = padding_h;
    if (!it.icon.empty()) {
      Color4f ic_col = (it.icon_color.a > 0.001f) ? it.icon_color : ts.color;
      if (it.disabled) ic_col.a *= 0.55f;
      if (hot && hover_text_.a > 0.001f && (!it.danger || hover_bg_.a >= 0.5f)) {
        ic_col = hover_text_;
      }
      constexpr float kIconSz = 18.0f;  // src: connection-table.tsx:567
      constexpr float kIconGap = 10.0f;  // src: popup-styles.ts:283
      IconW::paint_at(
          rt, it.icon,
          {padding_h, y + (item_height - kIconSz) * 0.5f, kIconSz, kIconSz},
          ic_col);
      text_x += kIconSz + kIconGap;
    }
    // 仅在确有快捷键时才为右侧留 56（否则无快捷键的项会被无谓截断）
    const float shortcut_reserve = it.shortcut.empty() ? 0.0f : 56.0f;
    rt.draw_text(it.label, {text_x, y,
                            std::max(0.0f, r.w - text_x - padding_h - shortcut_reserve),
                            item_height},
                 ts);
    if (!it.shortcut.empty()) {
      ts.color = shortcut_text_;
      ts.align_h = AlignH::Right;
      rt.draw_text(it.shortcut,
                   {r.w - 8 - 56, y, 56, item_height}, ts);
    }
    y += item_height;
  }
  const float total_h = content_height();
  if (total_h > r.h + 1.0f && r.h > 16.0f) {
    // src: MUI Menu paper 纵向溢出滚动条滑块
    const float track_h = r.h - 8.0f;
    const float thumb_h = std::max(18.0f, track_h * (r.h / total_h));
    const float max_s = std::max(1.0f, total_h - r.h);
    const float thumb_y = 4.0f + (track_h - thumb_h) * (scroll_y_ / max_s);
    Color4f sc = text_;
    sc.a = 0.28f;
    Brush sb{sc};
    rt.round_rect({r.w - 5.0f, thumb_y, 3.0f, thumb_h}, 1.5f,
                  Pen{Color4f{0, 0, 0, 0}, 0.0f}, &sb);
  }
  rt.pop_clip();

  if (panel_border_w_ > 0.01f) {
    const Color4f bc = panel_border_.a > 0.001f ? panel_border_ : border_;
    if (bc.a > 0.001f) rt.round_rect(r, radius_, Pen{bc, panel_border_w_}, nullptr);
  }
}

// ---------- ContextMenu ----------
void ContextMenu::open(const std::vector<MenuItem>& items, PointF anchor) {
  retiring_menu_.reset();
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
  ml->hover_grad_ = style_.hover_grad;
  ml->hover_bg_ = style_.hover_bg;
  ml->hover_radius_ = style_.hover_radius;
  for (int i = 0; i < 4; ++i) ml->hover_stops_[i] = style_.hover_stops[i];
  ml->hover_n_ = style_.hover_n;
  ml->hover_angle_ = style_.hover_angle;
  ml->hover_text_ = style_.hover_text;
  ml->hover_border_ = style_.hover_border;
  ml->hover_border_w_ = style_.hover_border_w;
  ml->hover_glow_ = style_.hover_glow;
  ml->hover_glow_color_ = style_.hover_glow_color;
  ml->hover_glow_blur_ = style_.hover_glow_blur;
  ml->panel_grad_on_ = style_.panel_grad_on;
  for (int i = 0; i < 4; ++i) ml->panel_stops_[i] = style_.panel_stops[i];
  ml->panel_n_ = style_.panel_n;
  ml->panel_angle_ = style_.panel_angle;
  ml->panel_bg_ = style_.panel_bg;
  ml->panel_border_ = style_.panel_border;
  ml->panel_border_w_ = style_.panel_border_w;
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
  for (int i = 0; i < 4; ++i) ml->panel_grad_[i] = style_.grad[i];
  ml->on_select = [this](int idx) {
    if (on_select) on_select(idx);  // 先回调（menu_ 在回调期间仍存活）
    close();                        // 后关闭（避免在 MenuList::on_mouse 内释放自身）
  };
  menu_ = ml.get();
  add_child(std::move(ml));
  // measure（无引擎时按最小尺寸），定位在 anchor；按宿主可用区域双向钳制宽高与坐标
  // src: MUI Popover marginThreshold = 16 / paper maxHeight 'calc(100% - 32px)'
  SizeF s = menu_->measure(0, 0);
  float w = s.w, h = s.h;
  if (style_.max_h > 0.0f) h = std::min(h, style_.max_h);
  float x = anchor.x, y = anchor.y;
  const float avail_w =
      bounds().w > 0.0f ? bounds().w : (parent() ? parent()->bounds().w : 0.0f);
  const float avail_h =
      bounds().h > 0.0f ? bounds().h : (parent() ? parent()->bounds().h : 0.0f);
  if (avail_w > 16.0f && avail_h > 16.0f) {
    w = std::min(w, avail_w - 8.0f);
    h = std::min(h, avail_h - 8.0f);
    x = std::clamp(x, 4.0f, std::max(4.0f, avail_w - w - 4.0f));
    y = std::clamp(y, 4.0f, std::max(4.0f, avail_h - h - 4.0f));
  }
  menu_->set_bounds({x, y, w, h});
  menu_->scroll_to_selected();
  open_ = true;
  invalidate();
}

void ContextMenu::close() {
  if (menu_) retiring_menu_ = remove_child(menu_);  // 延后析构，防止 on_mouse 执行中销毁自身
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


// ---------- 按皮肤填写菜单外观 ----------
// src: src/utils/popup-styles.ts:90-117 getPopupCardStyle & :266-346 getPopupMenuItemStyle
void apply_menu_skin(MenuStyle& st) {
  using theme::ThemeService;
  auto& svc = ThemeService::instance();
  const bool light = svc.is_light();
  const auto card = svc.popup_card_paint();
  const auto tokens = svc.popup_tokens();

  // src: src/utils/popup-styles.ts:349-360 菜单容器统一复用 getPopupCardStyle(theme)
  st.panel_grad_on = card.grad_bg && card.grad_count >= 2;
  st.panel_n = std::min(card.grad_count, 4);
  for (int i = 0; i < st.panel_n; ++i) {
    st.panel_stops[i] = card.grad[i];
  }
  st.panel_angle = card.grad_angle;
  st.panel_bg = card.bg;
  st.panel_border = card.border;
  st.panel_border_w = card.border_w;
  st.radius = card.radius;
  st.gradient_panel = true;

  st.text = tokens.primary_text;
  st.disabled_text = tokens.disabled_text;
  st.danger_text = tokens.error_color;
  st.selected_text = tokens.primary_text;
  st.border = tokens.divider_color;

  st.hover_grad = false;
  st.hover_n = 0;
  st.hover_text = Color4f{0, 0, 0, 0};
  st.hover_border = Color4f{0, 0, 0, 0};
  st.hover_border_w = 0;
  st.hover_glow = false;
  st.hover_bg = Color4f{0, 0, 0, 0};
  // src: src/utils/popup-styles.ts:277 MUI MenuItem radius
  st.hover_radius = 4.0f;
  st.gold_hover = false;

  switch (svc.skin()) {
    case theme::SkinId::Retro3d:
      // src: src/utils/popup-styles.ts:293-303 retro-3d 金卡半透明 hover + #1F1500
      st.selected_bg = light ? Color4f::hex(0xB8860B, 0.28f)
                             : Color4f::hex(0x2C1F03, 0.22f);
      st.hover_bg = light ? Color4f::hex(0xB8860B, 0.22f)
                          : Color4f::hex(0x2C1F03, 0.16f);
      st.hover_text = Color4f::hex(0x1F1500);
      st.font_weight = 600;
      break;
    case theme::SkinId::Cyberpunk:
      // src: src/utils/popup-styles.ts:304-316 neon #39ff14 hover & #00F0FF selected
      st.selected_bg = Color4f::hex(0x00F0FF, 0.18f);
      st.hover_bg = Color4f::hex(0x39FF14);
      st.hover_text = Color4f::hex(0x000000);
      st.hover_border = Color4f::hex(0x39FF14);
      st.hover_border_w = 1.0f;
      st.hover_radius = 0.0f;
      st.hover_glow = true;
      st.hover_glow_color = Color4f::hex(0x39FF14, 0.75f);
      st.hover_glow_blur = 8.0f;
      st.font_weight = 700;
      break;
    case theme::SkinId::Monochrome:
      // src: src/utils/popup-styles.ts:317-327 黑白反色 hover & 0.12/0.18 selected
      st.selected_bg = light ? Color4f::hex(0x000000, 0.12f)
                             : Color4f::hex(0xFFFFFF, 0.18f);
      st.hover_bg = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      st.hover_text = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x000000);
      st.hover_radius = 0.0f;
      st.font_weight = static_cast<int>(Weight::Normal);
      break;
    case theme::SkinId::Original:
    case theme::SkinId::ModernFlat:
    case theme::SkinId::FrostedGlass:
    default: {
      // src: src/utils/popup-styles.ts:328-339 alpha(primary, .10|.18) hover & .14|.22 selected
      const Color4f c = svc.primary();
      st.selected_bg = Color4f{c.r, c.g, c.b, light ? 0.14f : 0.22f};
      st.hover_bg = Color4f{c.r, c.g, c.b, light ? 0.10f : 0.18f};
      st.hover_text = c;
      st.font_weight = 500;
      break;
    }
  }
}

}  // namespace clashwin::ui
