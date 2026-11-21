// CoreUpdateModal 实现（拆分自 utility_modals.cpp）
// src: src/pages/_layout/components/dialogs/core-update-dialog.tsx:1-180
#include "ui/scenes/dialogs/core_update_modal.h"
#include "ui/scenes/dialogs/modal_common.h"

#include <windows.h>
#include <algorithm>

#include "events/event_bus.h"
#include "events/notice.h"
#include "ui/button_draw.h"
#include "ui/card_draw.h"
#include "ui/i18n_text.h"
#include "ui/theme/theme.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

using detail::format_core_version;
using detail::kChgPad;
using detail::kPopupActionsGap;
using detail::kPopupActionsPadY;
using detail::kPopupBody2H;
using detail::kPopupContentPadY;
using detail::kPopupPadX;
using detail::kPopupTitleBlockH;
using detail::kPopupTitlePadY;
using detail::kPopupTitleTextH;
using detail::popup_btn_h;
using detail::versions_equal;
using theme::ThemeService;

// src: src/pages/_layout/components/dialogs/core-update-dialog.tsx:1-180
CoreUpdateModal::CoreUpdateModal(Widget* parent) : Widget(parent) {
  set_z_order(960);
  subscribe_events();
}

CoreUpdateModal::~CoreUpdateModal() {
  unsubscribe_events();
}

void CoreUpdateModal::set_external_event_driver(bool ext) {
  external_event_driver_ = ext;
  if (ext) {
    unsubscribe_events();
  } else if (listener_id_ == 0) {
    subscribe_events();
  }
}

void CoreUpdateModal::open(const std::string& current_ver, const std::string& latest_ver) {
  open_ = true;
  fade_.show();
  current_version_ = current_ver;
  latest_version_ = latest_ver;
  progress_ = 0;
  status_ = "idle";
  is_upgrading_ = false;
  message_.clear();
  hover_close_x_ = false;
  hover_close_ = false;
  hover_upgrade_ = false;
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  invalidate();
}

void CoreUpdateModal::close() {
  if (!open_) return;
  open_ = false;
  fade_.hide();
  invalidate();
  if (on_dismiss) on_dismiss();
}

void CoreUpdateModal::subscribe_events() {
  listener_id_ = events::EventBus::instance().subscribe(
      "core-upgrade-progress", [this](const events::Payload& payload) {
        if (!payload.is_object()) return;
        std::string st = payload.value("status", "checking");
        int pr = payload.value("progress", 0);
        std::string msg = payload.value("message", "");
        this->handle_progress_event(st, pr, msg);
      });
}

void CoreUpdateModal::unsubscribe_events() {
  if (listener_id_ != 0) {
    events::EventBus::instance().unsubscribe(listener_id_);
    listener_id_ = 0;
  }
}

void CoreUpdateModal::handle_progress_event(const std::string& status, int progress,
                                            const std::string& msg) {
  status_ = status;
  progress_ = std::clamp(progress, 0, 100);
  message_ = msg;
  if (status_ == "done") {
    is_upgrading_ = false;
    current_version_ = latest_version_;
    // src: src/pages/_layout/hooks/use-core-update.ts:44-46
    events::NoticeService::instance().push("success", tr8("layout.notices.coreUpdateSuccess"));
  } else if (status_ == "error") {
    is_upgrading_ = false;
    // src: src/pages/_layout/hooks/use-core-update.ts:47-51
    events::NoticeService::instance().push(
        "error", tr8("layout.notices.coreUpdateFailed", {{"message", msg}}));
  }
  invalidate();
}

// src: src/pages/_layout/components/dialogs/core-update-dialog.tsx:1-180
void CoreUpdateModal::on_arrange(const RectF& area) {
  set_bounds(area);
  // src: src/utils/popup-styles.ts:120-185 窄窗弹窗锁定左侧 271px
  const float cw = std::min(theme::kPopupPaneWidth, area.w);
  const float cx = 0.0f;
  const float inner_w = std::max(80.0f, cw - kPopupPadX * 2.0f);

  const float upgrade_h = popup_btn_h("primary", 13.0f, 6.8f);
  const float close_h = popup_btn_h("default", 12.5f, 5.6f);
  const float actions_h =
      kPopupActionsPadY * 2.0f + upgrade_h + kPopupActionsGap + close_h;
  constexpr float kMetaBoxH = kChgPad * 2.0f + kPopupBody2H + 4.8f + kPopupBody2H + 2.0f;
  const float extra_h =
      (status_ != "idle") ? 12.0f + 8.0f + 6.0f + kPopupBody2H : 0.0f;
  const float content_h = kPopupContentPadY * 2.0f + kMetaBoxH + extra_h;
  const float need = kPopupTitleBlockH + content_h + actions_h;
  const float ch = std::min(need, std::max(200.0f, area.h - 8.0f));
  const float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  btn_close_x_rect_ = {cx + cw - 32.0f, cy + 6.0f, 26.0f, 24.0f};

  const float content_y = cy + kPopupTitleBlockH + kPopupContentPadY;
  meta_box_rect_ = {cx + kPopupPadX, content_y, inner_w, kMetaBoxH};
  progress_bar_rect_ = {cx + kPopupPadX, meta_box_rect_.bottom() + 12.0f, inner_w, 8.0f};

  // src: src/utils/popup-styles.ts:231-246
  const float actions_y = cy + ch - actions_h;
  btn_upgrade_rect_ = {cx + kPopupPadX, actions_y + kPopupActionsPadY, inner_w,
                       upgrade_h};
  btn_close_rect_ = {cx + kPopupPadX, btn_upgrade_rect_.bottom() + kPopupActionsGap,
                     inner_w, close_h};
}

// src: src/pages/_layout/components/dialogs/core-update-dialog.tsx:1-180
void CoreUpdateModal::on_paint(IRenderTarget& rt) {
  if (fade_.advance()) invalidate();
  set_opacity(fade_.value());
  if (fade_.hidden()) return;

  const auto& svc = ThemeService::instance();
  const auto tokens = svc.popup_tokens();

  // src: src/utils/popup-styles.ts:120-168
  const float pane_w = std::min(theme::kPopupPaneWidth, bounds().w);
  rt.fill_rect({0, 0, pane_w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});

  ::clashwin::ui::draw_card(rt, card_rect_, svc.popup_card_paint());

  const float pad = kPopupPadX;
  const float inner_w = std::max(80.0f, card_rect_.w - pad * 2.0f);
  TextStyle title_ts;
  title_ts.size = 15.5f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = tokens.primary_text;
  title_ts.align_h = AlignH::Center;
  title_ts.align_v = AlignV::Center;
  title_ts.ellipsis = true;
  // src: locales/zh/layout.json:82
  rt.draw_text(L"⚙️ " + tr("layout.components.dialogs.core.title"),
               {card_rect_.x + pad, card_rect_.y + kPopupTitlePadY, inner_w,
                kPopupTitleTextH},
               title_ts);
  rt.fill_rect({card_rect_.x, card_rect_.y + kPopupTitleBlockH - 1.0f, card_rect_.w,
                1.0f},
               Brush{tokens.divider_color});

  Brush mb_bg{tokens.subtle_bg};
  rt.round_rect(meta_box_rect_, 6.0f, {tokens.subtle_border, 1.0f}, &mb_bg);
  TextStyle ver_ts;
  ver_ts.size = 12.5f;
  ver_ts.color = tokens.secondary_text;
  ver_ts.align_v = AlignV::Center;
  ver_ts.ellipsis = true;
  rt.draw_text(tr("layout.components.dialogs.update.currentVersion",
                  {{"version", format_core_version(current_version_)}}),
               {meta_box_rect_.x + kChgPad, meta_box_rect_.y + kChgPad,
                meta_box_rect_.w - kChgPad * 2.0f, kPopupBody2H},
               ver_ts);
  // src: locales/zh/layout.json:69
  TextStyle ver_bold = ver_ts;
  ver_bold.weight = static_cast<int>(Weight::Bold);
  ver_bold.color = tokens.primary_text;
  rt.draw_text(tr("layout.components.dialogs.update.latestVersion",
                  {{"version", latest_version_.empty()
                                   ? utf::utf16_to_utf8(
                                         tr("layout.components.dialogs.core.fetching"))
                                   : format_core_version(latest_version_)}}),
               {meta_box_rect_.x + kChgPad,
                meta_box_rect_.y + kChgPad + kPopupBody2H + 4.8f,
                meta_box_rect_.w - kChgPad * 2.0f, kPopupBody2H},
               ver_bold);

  if (status_ != "idle") {
    Brush bar_bg{tokens.subtle_bg};
    rt.round_rect(progress_bar_rect_, 4.0f, {tokens.subtle_border, 1.0f}, &bar_bg);
    float fill_w = progress_bar_rect_.w * (static_cast<float>(progress_) / 100.0f);
    Brush fill_b{tokens.accent_primary};
    rt.round_rect({progress_bar_rect_.x, progress_bar_rect_.y, fill_w, progress_bar_rect_.h},
                  4.0f, {}, &fill_b);

    TextStyle msg_ts;
    msg_ts.size = 12.0f;
    msg_ts.color = tokens.secondary_text;
    std::wstring display_msg = utf::utf8_to_utf16(message_);
    rt.draw_text(display_msg, {progress_bar_rect_.x, progress_bar_rect_.y + 8.0f + 6.0f,
                               progress_bar_rect_.w, kPopupBody2H},
                 msg_ts);
  }

  // src: src/utils/popup-styles.ts:243 borderTop 1px
  rt.fill_rect({card_rect_.x, btn_upgrade_rect_.y - kPopupActionsPadY, card_rect_.w,
                1.0f},
               Brush{tokens.divider_color});

  const bool busy = is_upgrading_ || status_ == "checking" || status_ == "downloading" ||
                    status_ == "extracting";
  const bool same_ver =
      !latest_version_.empty() && versions_equal(current_version_, latest_version_);
  const bool upd_disabled = busy || status_ == "done" || same_ver;
  auto paint_themed_btn = [&](IRenderTarget& rt2, const RectF& r, bool hovered,
                              bool disabled, const char* color_type, bool outlined,
                              const std::wstring& label) {
    auto p = svc.button_paint(color_type, outlined);
    draw_button_skin(rt2, r, p, hovered ? 1.0f : 0.0f, /*pressed=*/false, disabled);
    TextStyle ts;
    ts.size = 12.5f;
    ts.weight = p.weight;
    ts.color = disabled ? p.text_disabled : p.text;
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt2.draw_text(label, r, ts);
  };

  std::wstring upd_label;
  if (status_ == "done")
    upd_label = tr("layout.components.dialogs.core.updateDone");
  else if (busy)
    upd_label = tr("layout.components.dialogs.update.updating");
  else if (same_ver)
    upd_label = tr("layout.components.dialogs.update.upToDate");
  else
    upd_label = tr("layout.components.dialogs.update.updateNow");
  paint_themed_btn(rt, btn_upgrade_rect_, hover_upgrade_ && !upd_disabled, upd_disabled,
                   "primary", false, upd_label);
  paint_themed_btn(rt, btn_close_rect_, hover_close_ && !busy, busy, "default", true,
                   tr("shared.actions.close"));
}

bool CoreUpdateModal::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    bool hc = btn_close_rect_.contains(pt.x, pt.y);
    bool hu = btn_upgrade_rect_.contains(pt.x, pt.y);
    if (hc != hover_close_ || hu != hover_upgrade_) {
      hover_close_ = hc;
      hover_upgrade_ = hu;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_close_rect_.contains(pt.x, pt.y)) {
      if (!(is_upgrading_ || status_ == "checking" || status_ == "downloading" ||
            status_ == "extracting")) {
        close();
      }
      return true;
    }
    if (btn_upgrade_rect_.contains(pt.x, pt.y)) {
      const bool busy = is_upgrading_ || status_ == "checking" || status_ == "downloading" ||
                        status_ == "extracting";
      const bool same_ver =
          !latest_version_.empty() && versions_equal(current_version_, latest_version_);
      if (!busy && status_ != "done" && !same_ver) {
        is_upgrading_ = true;
        // src: src/pages/_layout/hooks/use-core-update.ts:98-100
        handle_progress_event("checking", 5,
                              utf::utf16_to_utf8(
                                  tr("layout.components.dialogs.progress.coreStarting")));
        if (on_start_upgrade) on_start_upgrade();
      }
      return true;
    }
    if (!card_rect_.contains(pt.x, pt.y)) {
      if (!(is_upgrading_ || status_ == "checking" || status_ == "downloading" ||
            status_ == "extracting")) {
        close();
      }
      return true;
    }
    return true;
  }

  return true;
}

bool CoreUpdateModal::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
    if (!(is_upgrading_ || status_ == "checking" || status_ == "downloading" ||
          status_ == "extracting")) {
      close();
    }
    return true;
  }
  return false;
}

std::wstring CoreUpdateModal::a11y_default_name() const {
  return tr("layout.components.dialogs.core.title");
}

A11ySubItem CoreUpdateModal::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  const bool busy = is_upgrading_ || status_ == "checking" ||
                    status_ == "downloading" || status_ == "extracting";
  const bool same_ver =
      !latest_version_.empty() && versions_equal(current_version_, latest_version_);
  if (idx == 0) {
    sub.role = A11yRole::Text;
    sub.name = tr("layout.components.dialogs.update.currentVersion",
                  {{"version", format_core_version(current_version_)}}) +
               L", " +
               tr("layout.components.dialogs.update.latestVersion",
                  {{"version", latest_version_.empty()
                                   ? utf::utf16_to_utf8(
                                         tr("layout.components.dialogs.core.fetching"))
                                   : format_core_version(latest_version_)}});
    sub.local_bounds = card_rect_;
    return sub;
  }
  sub.role = A11yRole::Button;
  if (idx == 1) {
    sub.enabled = !busy;
    sub.can_invoke = !busy;
    sub.name = tr("shared.actions.close");
    sub.local_bounds = btn_close_rect_;
  } else {
    const bool upd_disabled = busy || status_ == "done" || same_ver;
    sub.enabled = !upd_disabled;
    sub.can_invoke = !upd_disabled;
    if (status_ == "done")
      sub.name = tr("layout.components.dialogs.core.updateDone");
    else if (busy)
      sub.name = tr("layout.components.dialogs.update.updating");
    else if (same_ver)
      sub.name = tr("layout.components.dialogs.update.upToDate");
    else
      sub.name = tr("layout.components.dialogs.update.updateNow");
    sub.local_bounds = btn_upgrade_rect_;
  }
  return sub;
}

bool CoreUpdateModal::a11y_invoke_virtual_child(size_t idx) {
  if (!open_ || !enabled()) return false;
  const bool busy = is_upgrading_ || status_ == "checking" ||
                    status_ == "downloading" || status_ == "extracting";
  const bool same_ver =
      !latest_version_.empty() && versions_equal(current_version_, latest_version_);
  if (idx == 1) {
    if (busy) return false;
    close();
    return true;
  }
  if (idx == 2) {
    if (busy || status_ == "done" || same_ver) return false;
    is_upgrading_ = true;
    handle_progress_event("checking", 5,
                          utf::utf16_to_utf8(
                              tr("layout.components.dialogs.progress.coreStarting")));
    if (on_start_upgrade) on_start_upgrade();
    return true;
  }
  return false;
}

}  // namespace clashwin::ui::scene
