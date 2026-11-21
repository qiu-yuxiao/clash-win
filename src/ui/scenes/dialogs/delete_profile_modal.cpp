// DeleteProfileConfirmModal 实现（BUG-390，拆分自 utility_modals.cpp）
// src: src/pages/_layout/components/dialogs/delete-profile-dialog.tsx:1-102
#include "ui/scenes/dialogs/delete_profile_modal.h"
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
using detail::kPopupPadX;
using detail::kPopupTitleBlockH;
using detail::kPopupTitlePadY;
using detail::kPopupTitleTextH;
using detail::popup_btn_h;
using theme::ThemeService;

// src: src/pages/_layout/components/dialogs/delete-profile-dialog.tsx:1-102
DeleteProfileConfirmModal::DeleteProfileConfirmModal(Widget* parent)
    : Widget(parent) {
  set_z_order(980);
}

void DeleteProfileConfirmModal::open(const std::string& uid,
                                     const std::string& profile_name) {
  uid_ = uid;
  profile_name_ = profile_name;
  open_ = true;
  fade_.show();
  hover_cancel_ = false;
  hover_delete_ = false;
  pressed_cancel_ = false;
  pressed_delete_ = false;
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  invalidate();
}

void DeleteProfileConfirmModal::close() {
  if (!open_) return;
  open_ = false;
  fade_.hide();
  invalidate();
  if (on_dismiss) on_dismiss();
}

// src: src/pages/_layout/components/dialogs/delete-profile-dialog.tsx:1-102
void DeleteProfileConfirmModal::on_arrange(const RectF& area) {
  set_bounds(area);
  // src: src/utils/popup-styles.ts:120-246 窄窗 271px 左锚定
  const float cw = std::min(theme::kPopupPaneWidth, area.w);
  const float pad = kPopupPadX;
  const float inner_w = std::max(80.0f, cw - pad * 2.0f);
  const float title_h = kPopupTitleBlockH;
  float name_h = 0.0f;
  float msg_h = 22.0f;
  if (ITextEngine* te = text_engine()) {
    if (!profile_name_.empty()) {
      TextStyle nts;
      nts.size = 13.0f;
      nts.weight = static_cast<int>(Weight::Bold);
      nts.wrap = true;
      name_h = std::max(20.0f,
                        te->measure_text(utf::utf8_to_utf16(profile_name_), nts, inner_w).h + 4.0f);
    }
    TextStyle mts;
    mts.size = 13.0f;
    mts.wrap = true;
    msg_h = std::max(20.0f,
                     te->measure_text(tr("profiles.modals.confirmDelete.message"), mts, inner_w).h + 4.0f);
  } else if (!profile_name_.empty()) {
    name_h = 22.0f;
  }
  const float content_h =
      kPopupContentPadY * 2.0f + name_h + (name_h > 0.0f ? 8.0f : 0.0f) + msg_h;
  const float cancel_h = popup_btn_h("default", 12.5f, 5.6f);
  const float delete_h = popup_btn_h("error", 13.0f, 6.8f);
  const float acts_h =
      kPopupActionsPadY * 2.0f + cancel_h + kPopupActionsGap + delete_h;
  const float ch = std::min(title_h + content_h + acts_h, std::max(140.0f, area.h - 16.0f));
  const float cx = 0.0f;
  const float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  const float acts_top = cy + ch - acts_h;
  btn_cancel_rect_ = {cx + pad, acts_top + kPopupActionsPadY, inner_w, cancel_h};
  btn_delete_rect_ = {cx + pad, btn_cancel_rect_.bottom() + kPopupActionsGap, inner_w, delete_h};
}

// src: src/pages/_layout/components/dialogs/delete-profile-dialog.tsx:1-102
void DeleteProfileConfirmModal::on_paint(IRenderTarget& rt) {
  if (fade_.advance()) invalidate();
  set_opacity(fade_.value());
  if (fade_.hidden()) return;

  if (card_rect_.w <= 0.0f && bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }

  const auto& svc = ThemeService::instance();
  const auto tokens = svc.popup_tokens();

  // src: src/utils/popup-styles.ts:120
  const float pane_w = std::min(theme::kPopupPaneWidth, bounds().w);
  rt.fill_rect({0.0f, 0.0f, pane_w, bounds().h},
               Brush{Color4f(0.0f, 0.0f, 0.0f, 0.5f)});
  ::clashwin::ui::draw_card(rt, card_rect_, svc.popup_card_paint());

  const float pad = kPopupPadX;
  const float inner_w = std::max(80.0f, card_rect_.w - pad * 2.0f);
  TextStyle title_ts;
  title_ts.size = 15.5f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = tokens.primary_text;
  title_ts.align_h = AlignH::Center;
  title_ts.align_v = AlignV::Center;
  rt.draw_text(tr("profiles.modals.confirmDelete.title"),
               {card_rect_.x + pad, card_rect_.y + kPopupTitlePadY, inner_w, kPopupTitleTextH},
               title_ts);
  rt.fill_rect({card_rect_.x, card_rect_.y + kPopupTitleBlockH - 1.0f, card_rect_.w, 1.0f},
               Brush{tokens.divider_color});

  float text_y = card_rect_.y + kPopupTitleBlockH + kPopupContentPadY;
  if (!profile_name_.empty()) {
    TextStyle nts;
    nts.size = 13.0f;
    nts.weight = static_cast<int>(Weight::Bold);
    nts.color = tokens.primary_text;
    nts.wrap = true;
    const float max_h =
        std::max(20.0f, btn_cancel_rect_.y - kPopupActionsPadY - text_y - 24.0f);
    rt.draw_text(utf::utf8_to_utf16(profile_name_),
                 {card_rect_.x + pad, text_y, inner_w, max_h}, nts);
    if (ITextEngine* te = text_engine()) {
      text_y += std::max(20.0f,
                         te->measure_text(utf::utf8_to_utf16(profile_name_), nts, inner_w).h + 4.0f) + 8.0f;
    } else {
      text_y += 30.0f;
    }
  }
  TextStyle mts;
  mts.size = 13.0f;
  mts.color = tokens.secondary_text;
  mts.wrap = true;
  rt.draw_text(tr("profiles.modals.confirmDelete.message"),
               {card_rect_.x + pad, text_y, inner_w,
                std::max(18.0f, btn_cancel_rect_.y - kPopupActionsPadY - kPopupContentPadY - text_y)},
               mts);

  rt.fill_rect({card_rect_.x, btn_cancel_rect_.y - kPopupActionsPadY, card_rect_.w, 1.0f},
               Brush{tokens.divider_color});

  auto paint_btn = [&](const RectF& orig_r, const char* ctype, bool outlined,
                       float fs, bool hover, bool pressed,
                       const std::wstring& label) {
    const auto bp = svc.button_paint(ctype, outlined);
    RectF r = orig_r;
    if (pressed) r.y += bp.translate_active;
    draw_button_skin(rt, r, bp, hover ? 1.0f : 0.0f, pressed);
    TextStyle bts;
    bts.size = fs;
    bts.weight = bp.weight;
    bts.color = bp.text;
    bts.align_h = AlignH::Center;
    bts.align_v = AlignV::Center;
    rt.draw_text(label, r, bts);
  };
  paint_btn(btn_cancel_rect_, "default", true, 12.5f, hover_cancel_, pressed_cancel_,
            tr("shared.actions.cancel"));
  paint_btn(btn_delete_rect_, "error", false, 13.0f, hover_delete_, pressed_delete_,
            tr("shared.actions.delete"));
}

bool DeleteProfileConfirmModal::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  const PointF pt = e.pos;

  if (e.type == MouseEventType::Leave) {
    hover_cancel_ = hover_delete_ = false;
    pressed_cancel_ = pressed_delete_ = false;
    invalidate();
    return true;
  }
  if (e.type == MouseEventType::Move) {
    const bool hc = btn_cancel_rect_.contains(pt.x, pt.y);
    const bool hd = btn_delete_rect_.contains(pt.x, pt.y);
    if (hc != hover_cancel_ || hd != hover_delete_) {
      hover_cancel_ = hc;
      hover_delete_ = hd;
      invalidate();
    }
    return true;
  }
  if (e.type == MouseEventType::Down) {
    pressed_cancel_ = btn_cancel_rect_.contains(pt.x, pt.y);
    pressed_delete_ = btn_delete_rect_.contains(pt.x, pt.y);
    invalidate();
    return true;
  }
  if (e.type == MouseEventType::Up) {
    const bool was_c = pressed_cancel_;
    const bool was_d = pressed_delete_;
    pressed_cancel_ = pressed_delete_ = false;
    invalidate();
    if (was_c && btn_cancel_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    if (was_d && btn_delete_rect_.contains(pt.x, pt.y)) {
      const std::string target_uid = uid_;
      close();
      if (on_confirm && !target_uid.empty()) on_confirm(target_uid);
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

bool DeleteProfileConfirmModal::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down) {
    if (e.key == VK_ESCAPE || e.key == 27) {
      close();
      return true;
    }
    if (e.key == VK_RETURN || e.key == 13) {
      const std::string target_uid = uid_;
      close();
      if (on_confirm && !target_uid.empty()) on_confirm(target_uid);
      return true;
    }
  }
  return false;
}

std::wstring DeleteProfileConfirmModal::a11y_default_name() const {
  return tr("profiles.modals.confirmDelete.title");
}

A11ySubItem DeleteProfileConfirmModal::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  if (idx == 0) {
    sub.role = A11yRole::Text;
    std::wstring msg = tr("profiles.modals.confirmDelete.message");
    if (!profile_name_.empty()) {
      msg = utf::utf8_to_utf16(profile_name_) + L": " + msg;
    }
    sub.name = msg;
    sub.local_bounds = card_rect_;
    return sub;
  }
  sub.role = A11yRole::Button;
  sub.can_invoke = enabled();
  if (idx == 1) {
    sub.name = tr("shared.actions.cancel");
    sub.local_bounds = btn_cancel_rect_;
  } else {
    sub.name = tr("shared.actions.delete");
    sub.local_bounds = btn_delete_rect_;
  }
  return sub;
}

bool DeleteProfileConfirmModal::a11y_invoke_virtual_child(size_t idx) {
  if (!open_ || !enabled()) return false;
  if (idx == 1) {
    close();
    return true;
  }
  if (idx == 2) {
    const std::string target_uid = uid_;
    close();
    if (on_confirm && !target_uid.empty()) on_confirm(target_uid);
    return true;
  }
  return false;
}

}  // namespace clashwin::ui::scene
