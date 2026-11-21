// EditProfileModal 实现（拆分自 utility_modals.cpp）
// src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx:1-192
#include "ui/scenes/dialogs/edit_profile_modal.h"
#include "ui/scenes/dialogs/modal_common.h"

#include <windows.h>
#include <algorithm>

#include "ui/button_draw.h"
#include "ui/card_draw.h"
#include "ui/i18n_text.h"
#include "ui/theme/theme.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

using detail::kPopupActionsGap;
using detail::kPopupActionsPadY;
using detail::kPopupContentPadY;
using detail::kPopupFieldH;
using detail::kPopupLabelLineH;
using detail::kPopupPadX;
using detail::kPopupTitlePadY;
using detail::popup_btn_h;
using theme::ThemeService;

// src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx:1-192
EditProfileModal::EditProfileModal(Widget* parent) : Widget(parent) {
  set_z_order(960);  // 豁免: Mini 无对应物 — 抽屉模态层序（出处: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx:1）

  auto make_field = [&](int idx) -> TextField* {
    auto tf_up = std::make_unique<TextField>(this);
    TextField* tf = tf_up.get();
    // src: node_modules/@mui/material/InputBase/InputBase.js:76-78 MUI size=small 高 40px
    tf->height_ = kPopupFieldH;
    // src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx fontSize 14px, px 10
    tf->set_padding_x(10.0f);
    tf->style_.size = 14.0f;
    tf->apply_theme();
    tf->set_visible(false);
    tf->on_submit = [this] { trigger_save(); };
    tf->on_key_intercept = [this](const KeyEvent& e) {
      if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
        close();
        return true;
      }
      if (e.type == KeyEventType::Down && (e.key == VK_RETURN || e.key == 13)) {
        trigger_save();
        return true;
      }
      return false;
    };
    tf->on_context_menu = [this, tf](PointF anchor) { open_field_menu(tf, anchor); };
    tf->on_text_changed = [this, tf, idx](const std::wstring& w) {
      if (syncing_fields_) return;
      focused_field_ = idx;
      if (idx == 1) {
        name_ = utf::utf16_to_utf8(w);
      } else if (idx == 2) {
        url_ = utf::utf16_to_utf8(w);
      } else if (idx == 3) {
        std::wstring digits;
        digits.reserve(w.size());
        for (wchar_t ch : w) {
          if (ch >= L'0' && ch <= L'9') digits.push_back(ch);
        }
        int val = digits.empty() ? 0 : _wtoi(digits.c_str());
        // src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx 更新间隔上限
        if (val > 100000) val = 100000;
        interval_mins_ = val;
        if (digits != w) {
          syncing_fields_ = true;
          tf->set_text(digits);
          syncing_fields_ = false;
        }
      }
      invalidate();
    };
    add_child(std::move(tf_up));
    return tf;
  };

  name_field_ = make_field(1);
  url_field_ = make_field(2);
  interval_field_ = make_field(3);

  auto fm = std::make_unique<ContextMenu>(this);
  field_menu_ = fm.get();
  field_menu_->set_z_order(1020);  // 豁免: Mini 无对应物 — 弹窗内右键菜单叠在输入框之上（出处: src/utils/popup-styles.ts）
  add_child(std::move(fm));
}

void EditProfileModal::set_name(const std::string& n) {
  name_ = n;
  if (name_field_) {
    syncing_fields_ = true;
    name_field_->set_text(utf::utf8_to_utf16(n));
    syncing_fields_ = false;
  }
  invalidate();
}

void EditProfileModal::set_url(const std::string& u) {
  url_ = u;
  if (url_field_) {
    syncing_fields_ = true;
    url_field_->set_text(utf::utf8_to_utf16(u));
    syncing_fields_ = false;
  }
  invalidate();
}

void EditProfileModal::set_interval_mins(int mins) {
  interval_mins_ = mins;
  if (interval_field_) {
    syncing_fields_ = true;
    interval_field_->set_text(std::to_wstring(mins));
    syncing_fields_ = false;
  }
  invalidate();
}

void EditProfileModal::trigger_save() {
  if (on_save) on_save(name_, url_, interval_mins_);
  close();
}

// src: src/utils/popup-styles.ts
void EditProfileModal::open_field_menu(TextField* tf, PointF anchor) {
  if (!field_menu_ || !tf || !tf->enabled()) return;
  MenuStyle st;
  apply_menu_skin(st);
  // src: src/pages/_layout/utils/style-helpers.tsx:126 菜单行高 32
  st.item_h = 32.0f;
  st.min_w = 160.0f;
  st.font_size = 13.0f;
  st.padding_h = 14.0f;

  std::vector<MenuItem> items;
  items.push_back({tr("shared.actions.cut"), !tf->can_cut(), false, L"", false});
  items.push_back({tr("shared.actions.copy"), !tf->can_copy(), false, L"", false});
  items.push_back({tr("shared.actions.paste"), !tf->can_paste(), false, L"", false});
  items.push_back({tr("shared.actions.selectAll"), !tf->can_select_all(), false, L"", false});
  items.push_back({L"", false, true, L"", false});
  items.push_back({tr("shared.actions.clearInput"), !tf->can_clear(), false, L"", true});

  field_menu_->style_ = st;
  field_menu_->on_select = [tf](int idx) {
    switch (idx) {
      case 0: tf->cut(); break;
      case 1: tf->copy(); break;
      case 2: tf->paste(); break;
      case 3: tf->select_all(); break;
      case 5: tf->clear_text(); break;
      default: break;
    }
  };
  PointF local = anchor;
  if (field_menu_->parent()) {
    local = field_menu_->parent()->to_local(anchor);
  }
  field_menu_->open(items, local);
}

// src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx:1-192
void EditProfileModal::open(const std::string& name, const std::string& url, int interval,
                            bool is_local) {
  open_ = true;
  fade_.show();
  name_ = name;
  url_ = url;
  interval_mins_ = interval;  // src: src/pages/_layout/hooks/use-profile-actions.ts 直接取 option.update_interval（0=禁用）
  is_local_ = is_local;
  hover_cancel_ = false;
  hover_save_ = false;
  focused_field_ = 0;

  syncing_fields_ = true;
  if (name_field_) {
    name_field_->apply_theme();
    name_field_->style_.size = 14.0f;  // src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx fontSize 14px
    name_field_->set_text(utf::utf8_to_utf16(name_));
    name_field_->set_enabled(true);
    name_field_->set_opacity(1.0f);
    name_field_->set_visible(true);
  }
  // src: node_modules/@mui/material/styles opacity 0.38 disabled
  constexpr float kDisabledOpacity = 0.38f;
  if (url_field_) {
    url_field_->apply_theme();
    url_field_->style_.size = 14.0f;  // src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx fontSize 14px
    url_field_->set_text(utf::utf8_to_utf16(url_));
    url_field_->set_enabled(!is_local_);
    url_field_->set_opacity(is_local_ ? kDisabledOpacity : 1.0f);
    url_field_->set_visible(true);
  }
  if (interval_field_) {
    interval_field_->apply_theme();
    interval_field_->style_.size = 14.0f;  // src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx fontSize 14px
    interval_field_->set_text(std::to_wstring(interval_mins_));
    interval_field_->set_enabled(!is_local_);
    interval_field_->set_opacity(is_local_ ? kDisabledOpacity : 1.0f);
    interval_field_->set_visible(true);
  }
  syncing_fields_ = false;

  if (bounds().w > 0.0f && bounds().h > 0.0f) on_arrange(bounds());
  invalidate();
}

void EditProfileModal::close() {
  if (!open_) return;
  open_ = false;
  fade_.hide();
  if (field_menu_ && field_menu_->is_open()) field_menu_->close();
  if (name_field_) name_field_->set_visible(false);
  if (url_field_) url_field_->set_visible(false);
  if (interval_field_) interval_field_->set_visible(false);
  invalidate();
  if (on_dismiss) on_dismiss();
}

// src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx:1-192
void EditProfileModal::on_arrange(const RectF& area) {
  set_bounds(area);
  // src: src/utils/popup-styles.ts:120-185 窄窗弹窗锁定左侧 271px
  const float cw = std::min(theme::kPopupPaneWidth, area.w);
  const float cx = 0.0f;  // src: src/utils/popup-styles.ts:123 left: 0

  constexpr float kTitleTextH = 15.5f * 1.35f;  // src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx
  const float title_h = kPopupTitlePadY * 2.0f + kTitleTextH + 1.0f;
  constexpr float kLabelH = 12.5f * kPopupLabelLineH + 4.0f;  // src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx
  const float group_h = kLabelH + kPopupFieldH;
  const float content_h = kPopupContentPadY * 2.0f + 3.0f * group_h + 2.0f * 12.0f;
  const float save_h = popup_btn_h("primary", 13.0f, 6.8f);
  const float cancel_h = popup_btn_h("default", 12.5f, 5.6f);
  const float actions_h =
      kPopupActionsPadY * 2.0f + save_h + kPopupActionsGap + cancel_h;

  const float need = title_h + content_h + actions_h;
  const float ch = std::min(need, std::max(200.0f, area.h - 8.0f));
  const float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  const float inner_w = std::max(80.0f, cw - kPopupPadX * 2.0f);
  const float content_y = cy + title_h + kPopupContentPadY;
  name_input_rect_ = {cx + kPopupPadX, content_y + kLabelH, inner_w, kPopupFieldH};
  url_input_rect_ = {cx + kPopupPadX, content_y + group_h + 12.0f + kLabelH,
                     inner_w, kPopupFieldH};
  interval_input_rect_ = {cx + kPopupPadX, content_y + 2.0f * (group_h + 12.0f) + kLabelH,
                          inner_w, kPopupFieldH};

  if (name_field_) name_field_->layout_tree(name_input_rect_);
  if (url_field_) url_field_->layout_tree(url_input_rect_);
  if (interval_field_) interval_field_->layout_tree(interval_input_rect_);
  if (field_menu_) field_menu_->layout_tree({0.0f, 0.0f, area.w, area.h});

  // src: src/utils/popup-styles.ts:231-246 全宽纵向堆叠按钮（Save 在上 / Cancel 在下）
  const float actions_y = cy + ch - actions_h;
  btn_save_rect_ = {cx + kPopupPadX, actions_y + kPopupActionsPadY, inner_w, save_h};
  btn_cancel_rect_ = {cx + kPopupPadX, btn_save_rect_.bottom() + kPopupActionsGap,
                      inner_w, cancel_h};
}

// src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx:1-192
void EditProfileModal::on_paint(IRenderTarget& rt) {
  if (fade_.advance()) invalidate();
  set_opacity(fade_.value());
  if (fade_.hidden()) return;

  const auto& svc = ThemeService::instance();
  const auto tokens = svc.popup_tokens();

  // src: src/utils/popup-styles.ts:120
  const float pane_w = std::min(theme::kPopupPaneWidth, bounds().w);
  rt.fill_rect({0, 0, pane_w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});

  ::clashwin::ui::draw_card(rt, card_rect_, svc.popup_card_paint());

  const float pad = kPopupPadX;
  const float inner_w = std::max(80.0f, card_rect_.w - pad * 2.0f);
  constexpr float kTitleTextH = 15.5f * 1.35f;  // src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx
  constexpr float kLabelGapH = 12.5f * kPopupLabelLineH + 4.0f;
  TextStyle title_ts;
  title_ts.size = 15.5f;  // src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = tokens.primary_text;
  title_ts.align_h = AlignH::Center;
  title_ts.align_v = AlignV::Center;
  rt.draw_text(L"⚙️ " + tr("layout.components.dialogs.editProfile.title"),
               {card_rect_.x + pad, card_rect_.y + kPopupTitlePadY, inner_w, kTitleTextH},
               title_ts);
  rt.fill_rect({card_rect_.x, card_rect_.y + kPopupTitlePadY * 2.0f + kTitleTextH,
                card_rect_.w, 1.0f},
               Brush{tokens.divider_color});

  TextStyle label_ts;
  label_ts.size = 12.5f;
  label_ts.weight = static_cast<int>(Weight::Bold);
  label_ts.color = tokens.primary_text;
  label_ts.align_v = AlignV::Center;
  rt.draw_text(tr("layout.components.dialogs.editProfile.nameLabel"),
               {card_rect_.x + pad, name_input_rect_.y - kLabelGapH, inner_w,
                kLabelGapH - 4.0f},
               label_ts);
  rt.draw_text(tr("layout.components.dialogs.editProfile.urlLabel"),
               {card_rect_.x + pad, url_input_rect_.y - kLabelGapH, inner_w,
                kLabelGapH - 4.0f},
               label_ts);
  rt.draw_text(tr("layout.components.dialogs.editProfile.intervalLabel"),
               {card_rect_.x + pad, interval_input_rect_.y - kLabelGapH, inner_w,
                kLabelGapH - 4.0f},
               label_ts);

  // src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx
  if (name_field_) {
    name_field_->apply_theme();
    name_field_->style_.size = 14.0f;
  }
  if (url_field_) {
    url_field_->apply_theme();
    url_field_->style_.size = 14.0f;
  }
  if (interval_field_) {
    interval_field_->apply_theme();
    interval_field_->style_.size = 14.0f;
  }

  TextStyle btn_ts;
  btn_ts.align_h = AlignH::Center;
  btn_ts.align_v = AlignV::Center;

  auto paint_btn = [&](const RectF& r, const char* ctype, bool hover,
                       const std::wstring& label, float fs, bool outlined) {
    const auto bp = ThemeService::instance().button_paint(ctype, outlined);
    draw_button_skin(rt, r, bp, hover ? 1.0f : 0.0f, /*pressed=*/false);
    btn_ts.size = fs;
    btn_ts.weight = bp.weight;
    btn_ts.color = bp.text;
    rt.draw_text(label, r, btn_ts);
  };
  paint_btn(btn_cancel_rect_, "default", hover_cancel_,
            tr("shared.actions.cancel"), 12.5f, /*outlined=*/false);
  paint_btn(btn_save_rect_, "primary", hover_save_,
            tr("shared.actions.save"), 13.0f, /*outlined=*/false);
}

bool EditProfileModal::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    bool hc = btn_cancel_rect_.contains(pt.x, pt.y);
    bool hs = btn_save_rect_.contains(pt.x, pt.y);
    if (hc != hover_cancel_ || hs != hover_save_) {
      hover_cancel_ = hc;
      hover_save_ = hs;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_cancel_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    if (btn_save_rect_.contains(pt.x, pt.y)) {
      trigger_save();
      return true;
    }
    if (name_input_rect_.contains(pt.x, pt.y)) {
      focused_field_ = 1;
      if (name_field_) name_field_->request_focus();
      invalidate();
      return true;
    }
    if (!is_local_ && url_input_rect_.contains(pt.x, pt.y)) {
      focused_field_ = 2;
      if (url_field_) url_field_->request_focus();
      invalidate();
      return true;
    }
    if (!is_local_ && interval_input_rect_.contains(pt.x, pt.y)) {
      focused_field_ = 3;
      if (interval_field_) interval_field_->request_focus();
      invalidate();
      return true;
    }
    if (!card_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    return true;
  }

  return true;
}

bool EditProfileModal::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down) {
    if (e.key == VK_ESCAPE || e.key == 27) {
      close();
      return true;
    }
    if (e.key == VK_RETURN || e.key == 13) {
      trigger_save();
      return true;
    }
  }
  TextField* active_tf = nullptr;
  if (focused_field_ == 1) active_tf = name_field_;
  else if (focused_field_ == 2 && !is_local_) active_tf = url_field_;
  else if (focused_field_ == 3 && !is_local_) active_tf = interval_field_;
  if (active_tf) {
    return active_tf->on_key(e);
  }
  return false;
}

std::wstring EditProfileModal::a11y_default_name() const {
  return tr("layout.components.dialogs.editProfile.title");
}

A11ySubItem EditProfileModal::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  if (idx == 0) {
    sub.role = A11yRole::Edit;
    sub.name = tr("layout.components.dialogs.editProfile.nameLabel");
    sub.local_bounds = name_input_rect_;
    sub.focusable = true;
    sub.can_set_value = true;
    sub.value = utf::utf8_to_utf16(name_);
    return sub;
  }
  if (!is_local_ && idx == 1) {
    sub.role = A11yRole::Edit;
    sub.name = tr("layout.components.dialogs.editProfile.urlLabel");
    sub.local_bounds = url_input_rect_;
    sub.focusable = true;
    sub.can_set_value = true;
    sub.value = utf::utf8_to_utf16(url_);
    return sub;
  }
  if (!is_local_ && idx == 2) {
    sub.role = A11yRole::Edit;
    sub.name = tr("layout.components.dialogs.editProfile.intervalLabel");
    sub.local_bounds = interval_input_rect_;
    sub.focusable = true;
    sub.can_set_value = true;
    sub.value = std::to_wstring(interval_mins_);
    return sub;
  }
  const size_t base = is_local_ ? 1u : 3u;
  const size_t btn_idx = (idx >= base) ? (idx - base) : 99u;
  sub.role = A11yRole::Button;
  sub.can_invoke = enabled();
  if (btn_idx == 0) {
    sub.name = tr("shared.actions.cancel");
    sub.local_bounds = btn_cancel_rect_;
  } else {
    sub.name = tr("shared.actions.save");
    sub.local_bounds = btn_save_rect_;
  }
  return sub;
}

bool EditProfileModal::a11y_invoke_virtual_child(size_t idx) {
  if (!open_ || !enabled()) return false;
  const size_t base = is_local_ ? 1u : 3u;
  if (idx < base) return false;
  const size_t btn_idx = idx - base;
  if (btn_idx == 0) {
    close();
    return true;
  }
  if (btn_idx == 1) {
    if (on_save) on_save(name_, url_, interval_mins_);
    close();
    return true;
  }
  return false;
}

bool EditProfileModal::a11y_set_virtual_child_value(size_t idx, const std::wstring& v) {
  if (!open_ || !enabled()) return false;
  if (idx == 0) {
    set_name(utf::utf16_to_utf8(v));
    return true;
  }
  if (!is_local_ && idx == 1) {
    set_url(utf::utf16_to_utf8(v));
    return true;
  }
  if (!is_local_ && idx == 2) {
    set_interval_mins(_wtoi(v.c_str()));
    return true;
  }
  return false;
}

}  // namespace clashwin::ui::scene
