// ContextMenu / MenuList 实现（T10）。
#include "context_menu.h"

#include <algorithm>

#include "soft_shadow.h"
#include "theme/theme.h"

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
  {
    const Color4f bc = panel_border_.a > 0.001f ? panel_border_ : border_;
    rt.round_rect(r, radius_, Pen{bc, panel_border_w_}, nullptr);
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
    if (hot && hover_text_.a > 0.001f) ts.color = hover_text_;  // ← :141 等各皮肤
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
  for (int i = 0; i < 3; ++i) ml->panel_stops_[i] = style_.panel_stops[i];
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
// 面板：index.scss:109-114 让 .MuiMenu-paper 复用 .theme-panel 规则，
//   因此面板底/边框/圆角直接取 ThemeService::panel_paint()。
// hover：style-helpers.tsx:109-198 getMenuItemHoverStyle 六套皮肤逐条落地。
void apply_menu_skin(MenuStyle& st) {
  using theme::ThemeService;
  auto& svc = ThemeService::instance();
  const bool light = svc.is_light();
  const auto pp = svc.panel_paint();

  // —— 面板 ——
  // 【关键】index.scss:109-115：`.MuiMenu-paper.theme-panel` 被强制为
  //   `background: rgb(var(--theme-panel-base-rgb))` + opacity 1 + 无毛玻璃，
  //   即菜单底永远是「纯色面板基色」，不吃各皮肤的渐变/半透明底
  //   （只有 4px double 描边与圆角仍按皮肤走）。
  st.panel_grad_on = false;
  st.panel_bg = svc.vars().theme_panel_bg;
  if (pp.border != theme::PanelPaint::BorderKind::None) {
    st.panel_border = pp.border_color;
    st.panel_border_w = 1.34f;  // 与节点区容器同一 DPI 补偿
  }
  st.radius = pp.radius > 0.0f ? pp.radius : 6.0f;

  // —— hover（默认清零，避免沿用旧的金渐变）——
  st.hover_grad = false;
  st.hover_n = 0;
  st.hover_text = Color4f{0, 0, 0, 0};
  st.hover_border = Color4f{0, 0, 0, 0};
  st.hover_border_w = 0;
  st.hover_glow = false;
  st.hover_bg = Color4f{0, 0, 0, 0};
  st.gold_hover = false;

  switch (svc.skin()) {
    case theme::SkinId::Retro3d:
      // :136-143 135° #FFE082/#FFD54F 35%/#FFB300 70%/#B8860B + 文字 #2C1F03
      st.hover_grad = true;
      st.hover_stops[0] = {0.00f, Color4f::hex(0xFFE082)};
      st.hover_stops[1] = {0.35f, Color4f::hex(0xFFD54F)};
      st.hover_stops[2] = {0.70f, Color4f::hex(0xFFB300)};
      st.hover_stops[3] = {1.00f, Color4f::hex(0xB8860B)};
      st.hover_n = 4;
      st.hover_angle = 135.0f;
      st.hover_text = Color4f::hex(0x2C1F03);
      st.font_weight = static_cast<int>(Weight::Bold);
      break;
    case theme::SkinId::Original:
      // :144-153 rgba(91,92,157,.08) + 文字 --primary-main
      st.hover_bg = Color4f::hex(0x5B5C9D, 0.08f);
      st.hover_text = svc.primary();
      st.font_weight = 500;
      break;
    case theme::SkinId::ModernFlat: {
      // :154-163 alpha(primary,.1) + 文字 primary
      const Color4f c = svc.primary();
      st.hover_bg = Color4f{c.r, c.g, c.b, 0.10f};
      st.hover_text = c;
      st.font_weight = static_cast<int>(Weight::Bold);
      break;
    }
    case theme::SkinId::FrostedGlass:
      // :164-173 rgba(255,255,255,.12) + 文字 primary
      st.hover_bg = Color4f::hex(0xFFFFFF, 0.12f);
      st.hover_text = svc.primary();
      st.font_weight = static_cast<int>(Weight::Normal);
      break;
    case theme::SkinId::Cyberpunk:
      // :175-188 #39ff14 底 + 黑字 + 同色描边 + 0 0 8px 同色发光
      st.hover_bg = Color4f::hex(0x39FF14);
      st.hover_text = Color4f::hex(0x000000);
      st.hover_border = Color4f::hex(0x39FF14);
      st.hover_border_w = 1.0f;
      st.hover_glow = true;
      st.hover_glow_color = Color4f::hex(0x39FF14, 0.75f);
      st.hover_glow_blur = 8.0f;
      st.font_weight = static_cast<int>(Weight::Bold);
      break;
    case theme::SkinId::Monochrome:
      // :189-197 黑白对调
      st.hover_bg = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      st.hover_text = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x000000);
      st.font_weight = static_cast<int>(Weight::Normal);
      break;
  }
}

}  // namespace clashwin::ui
