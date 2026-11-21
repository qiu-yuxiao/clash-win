// ClientUpdateModal 实现（拆分自 utility_modals.cpp）
// src: src/pages/_layout/components/dialogs/client-update-dialog.tsx:1-200
#include "ui/scenes/dialogs/client_update_modal.h"
#include "ui/scenes/dialogs/modal_common.h"

#include <windows.h>
#include <algorithm>

#include "events/event_bus.h"
#include "ui/button_draw.h"
#include "ui/card_draw.h"
#include "ui/i18n_text.h"
#include "ui/theme/theme.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

using detail::ensure_v_prefix;
using detail::kChgLineH;
using detail::kChgMaxH;
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

// src: src/pages/_layout/components/dialogs/client-update-dialog.tsx:1-200
ClientUpdateModal::ClientUpdateModal(Widget* parent) : Widget(parent) {
  set_z_order(960);
  subscribe_events();
}

ClientUpdateModal::~ClientUpdateModal() {
  unsubscribe_events();
}

void ClientUpdateModal::subscribe_events() {
  listener_id_ = events::EventBus::instance().subscribe(
      "client-update-progress", [this](const events::Payload& payload) {
        if (!payload.is_object()) return;
        std::string st = payload.value("status", "downloading");
        int pr = payload.value("progress", 0);
        std::string msg = payload.value("message", "");
        this->handle_progress_event(st, pr, msg);
      });
}

void ClientUpdateModal::unsubscribe_events() {
  if (listener_id_ != 0) {
    events::EventBus::instance().unsubscribe(listener_id_);
    listener_id_ = 0;
  }
}

void ClientUpdateModal::handle_progress_event(const std::string& status, int progress,
                                              const std::string& msg) {
  status_ = status;
  progress_ = std::clamp(progress, 0, 100);
  if (!msg.empty()) status_msg_ = msg;
  if (status_ == "done") {
    is_downloading_ = false;
  } else if (status_ == "error") {
    is_downloading_ = false;
  } else {
    is_downloading_ = true;
  }
  invalidate();
}

void ClientUpdateModal::open(const std::string& current_ver, const std::string& latest_ver,
                             const std::string& changelog, const std::string& download_url) {
  open_ = true;
  fade_.show();
  current_version_ = current_ver;
  latest_version_ = latest_ver;
  changelog_ = changelog;
  download_url_ = download_url;
  changelog_scroll_y_ = 0.0f;
  changelog_text_h_ = 0.0f;
  progress_ = 0;
  status_ = "idle";
  is_downloading_ = false;
  status_msg_.clear();
  hover_close_x_ = false;
  hover_close_ = false;
  hover_update_ = false;
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  invalidate();
}

void ClientUpdateModal::close() {
  if (!open_) return;
  open_ = false;
  fade_.hide();
  invalidate();
  if (on_dismiss) on_dismiss();
}

void ClientUpdateModal::set_progress(int p, const std::string& msg) {
  progress_ = std::clamp(p, 0, 100);
  if (!msg.empty()) status_msg_ = msg;
  invalidate();
}

// src: src/pages/_layout/components/dialogs/client-update-dialog.tsx:1-200
void ClientUpdateModal::on_arrange(const RectF& area) {
  set_bounds(area);
  // src: src/utils/popup-styles.ts:120-185 窄窗弹窗锁定左侧 271px
  const float cw = std::min(theme::kPopupPaneWidth, area.w);
  const float cx = 0.0f;
  const float inner_w = std::max(80.0f, cw - kPopupPadX * 2.0f);

  const float update_h = popup_btn_h("primary", 13.0f, 6.8f);
  const float close_h = popup_btn_h("default", 12.5f, 5.6f);
  const float actions_h =
      kPopupActionsPadY * 2.0f + update_h + kPopupActionsGap + close_h;

  TextStyle chg_ts;
  chg_ts.size = 12.0f;
  chg_ts.wrap = true;
  chg_ts.line_h = kChgLineH;
  float text_h = kChgLineH;
  if (ITextEngine* e = text_engine()) {
    const std::wstring w =
        changelog_.empty()
            ? tr("layout.components.dialogs.client.changelogEmpty")
            : utf::utf8_to_utf16(changelog_);
    text_h = e->measure_text(w, chg_ts, inner_w - kChgPad * 2.0f).h;
  }
  changelog_text_h_ = text_h;
  // src: src/pages/_layout/components/dialogs/client-update-dialog.tsx maxHeight '160px'
  float chg_box_h = std::min(kChgMaxH, text_h + kChgPad * 2.0f + 2.0f);

  const float meta_h = kPopupBody2H + 4.0f + kPopupBody2H;
  const float extra_h =
      (status_ != "idle") ? 12.0f + 8.0f + 6.0f + kPopupBody2H : 0.0f;
  float need = kPopupTitleBlockH + kPopupContentPadY * 2.0f + meta_h + 10.0f +
               kPopupBody2H + 6.0f + chg_box_h + extra_h + actions_h;
  const float max_h = std::max(200.0f, area.h - 8.0f);
  if (need > max_h) {
    chg_box_h = std::max(56.0f, chg_box_h - (need - max_h));
    need = max_h;
  }
  const float ch = need;
  const float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  btn_close_x_rect_ = {cx + cw - 32.0f, cy + 6.0f, 26.0f, 24.0f};

  const float content_y = cy + kPopupTitleBlockH + kPopupContentPadY;
  changelog_box_rect_ = {cx + kPopupPadX,
                         content_y + meta_h + 10.0f + kPopupBody2H + 6.0f,
                         inner_w, chg_box_h};
  const float max_chg_scroll = std::max(
      0.0f, changelog_text_h_ - std::max(0.0f, changelog_box_rect_.h - kChgPad * 2.0f));
  changelog_scroll_y_ = std::clamp(changelog_scroll_y_, 0.0f, max_chg_scroll);
  progress_bar_rect_ = {cx + kPopupPadX, changelog_box_rect_.bottom() + 12.0f,
                        inner_w, 8.0f};

  // src: src/utils/popup-styles.ts:231-246
  const float actions_y = cy + ch - actions_h;
  btn_update_rect_ = {cx + kPopupPadX, actions_y + kPopupActionsPadY, inner_w, update_h};
  btn_close_rect_ = {cx + kPopupPadX, btn_update_rect_.bottom() + kPopupActionsGap,
                     inner_w, close_h};
}

// src: src/pages/_layout/components/dialogs/client-update-dialog.tsx:1-200
void ClientUpdateModal::on_paint(IRenderTarget& rt) {
  if (fade_.advance()) invalidate();
  set_opacity(fade_.value());
  if (fade_.hidden()) return;

  const auto& svc = ThemeService::instance();
  const auto tokens = svc.popup_tokens();

  // src: src/utils/popup-styles.ts:120-168 左侧 271px 窄窗 Backdrop
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
  // src: locales/zh/layout.json:76
  rt.draw_text(L"🚀 " + tr("layout.components.dialogs.client.title"),
               {card_rect_.x + pad, card_rect_.y + kPopupTitlePadY, inner_w,
                kPopupTitleTextH},
               title_ts);
  rt.fill_rect({card_rect_.x, card_rect_.y + kPopupTitleBlockH - 1.0f, card_rect_.w,
                1.0f},
               Brush{tokens.divider_color});

  const float content_y = card_rect_.y + kPopupTitleBlockH + kPopupContentPadY;
  const float meta_h = kPopupBody2H + 4.0f + kPopupBody2H;
  const std::wstring unknown = tr("layout.components.dialogs.update.unknown");
  const std::string cur_disp = current_version_.empty()
                                   ? utf::utf16_to_utf8(unknown)
                                   : ensure_v_prefix(current_version_);
  const std::string lat_disp = latest_version_.empty()
                                   ? utf::utf16_to_utf8(unknown)
                                   : ensure_v_prefix(latest_version_);
  TextStyle ver_ts;
  ver_ts.size = 12.5f;
  ver_ts.color = tokens.secondary_text;
  ver_ts.align_v = AlignV::Center;
  ver_ts.ellipsis = true;
  rt.draw_text(tr("layout.components.dialogs.update.currentVersion",
                  {{"version", cur_disp}}),
               {card_rect_.x + pad, content_y, inner_w, kPopupBody2H}, ver_ts);
  TextStyle ver_bold = ver_ts;
  ver_bold.weight = static_cast<int>(Weight::Bold);
  ver_bold.color = tokens.primary_text;
  rt.draw_text(tr("layout.components.dialogs.update.latestVersion",
                  {{"version", lat_disp}}),
               {card_rect_.x + pad, content_y + kPopupBody2H + 4.0f, inner_w,
                kPopupBody2H},
               ver_bold);

  TextStyle chg_label = ver_bold;
  // src: locales/zh/layout.json:77
  rt.draw_text(tr("layout.components.dialogs.client.changelog"),
               {card_rect_.x + pad, content_y + meta_h + 10.0f, inner_w,
                kPopupBody2H},
               chg_label);

  Brush cl_bg{tokens.subtle_bg};
  rt.round_rect(changelog_box_rect_, 6.0f, {tokens.subtle_border, 1.0f}, &cl_bg);

  TextStyle cl_ts;
  cl_ts.size = 12.0f;
  cl_ts.color = tokens.primary_text;
  cl_ts.wrap = true;
  cl_ts.line_h = kChgLineH;
  // src: locales/zh/layout.json:78
  std::wstring chg_w = changelog_.empty() ? tr("layout.components.dialogs.client.changelogEmpty") : utf::utf8_to_utf16(changelog_);
  const RectF chg_inner{changelog_box_rect_.x + kChgPad,
                        changelog_box_rect_.y + kChgPad,
                        std::max(10.0f, changelog_box_rect_.w - kChgPad * 2.0f),
                        std::max(10.0f, changelog_box_rect_.h - kChgPad * 2.0f)};
  rt.push_clip(chg_inner);
  rt.draw_text(chg_w,
               {chg_inner.x, chg_inner.y - changelog_scroll_y_, chg_inner.w,
                std::max(changelog_text_h_, chg_inner.h)},
               cl_ts);
  rt.pop_clip();

  if (status_ != "idle") {
    Brush bar_bg{tokens.subtle_bg};
    rt.round_rect(progress_bar_rect_, 4.0f, {tokens.subtle_border, 1.0f}, &bar_bg);
    float fill_w = progress_bar_rect_.w * (static_cast<float>(progress_) / 100.0f);
    Brush fill_b{tokens.accent_primary};
    rt.round_rect({progress_bar_rect_.x, progress_bar_rect_.y, fill_w, progress_bar_rect_.h},
                  4.0f, {}, &fill_b);

    TextStyle stat_ts;
    stat_ts.size = 12.0f;
    stat_ts.color = tokens.secondary_text;
    std::wstring stat_str = status_msg_.empty()
                                ? tr("layout.components.dialogs.progress.clientDownloading")
                                : utf::utf8_to_utf16(status_msg_);
    rt.draw_text(stat_str, {progress_bar_rect_.x, progress_bar_rect_.y + 8.0f + 6.0f,
                            progress_bar_rect_.w, kPopupBody2H},
                 stat_ts);
  }

  // src: src/utils/popup-styles.ts:243 borderTop 1px
  rt.fill_rect({card_rect_.x, btn_update_rect_.y - kPopupActionsPadY, card_rect_.w,
                1.0f},
               Brush{tokens.divider_color});

  const bool busy = is_downloading_;
  const bool same_ver = versions_equal(latest_version_, current_version_);
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
    upd_label = tr("layout.components.dialogs.client.prepareRestart");
  else if (busy)
    upd_label = tr("layout.components.dialogs.update.updating");
  else if (same_ver)
    upd_label = tr("layout.components.dialogs.update.upToDate");
  else
    upd_label = tr("layout.components.dialogs.update.updateNow");
  paint_themed_btn(rt, btn_update_rect_, hover_update_ && !upd_disabled, upd_disabled,
                   "primary", false, upd_label);
  paint_themed_btn(rt, btn_close_rect_, hover_close_ && !busy, busy, "default", true,
                   tr("shared.actions.close"));
}

bool ClientUpdateModal::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    bool hc = btn_close_rect_.contains(pt.x, pt.y);
    bool hu = btn_update_rect_.contains(pt.x, pt.y);
    if (hc != hover_close_ || hu != hover_update_) {
      hover_close_ = hc;
      hover_update_ = hu;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Wheel) {
    if (changelog_box_rect_.contains(pt.x, pt.y)) {
      const float max_scroll = std::max(
          0.0f, changelog_text_h_ - std::max(0.0f, changelog_box_rect_.h - kChgPad * 2.0f));
      if (max_scroll > 0.0f) {
        // 豁免: Mini 无对应物 — Win32 滚轮 120 刻度映射 36px（出处: src/pages/_layout/components/dialogs/client-update-dialog.tsx）
        const float delta = static_cast<float>(e.wheel_delta) / 120.0f * 36.0f;
        changelog_scroll_y_ = std::clamp(changelog_scroll_y_ - delta, 0.0f, max_scroll);
        invalidate();
      }
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_close_rect_.contains(pt.x, pt.y)) {
      if (!is_downloading_) close();
      return true;
    }
    if (btn_update_rect_.contains(pt.x, pt.y)) {
      // src: src/pages/_layout/hooks/use-client-update.ts:56-67
      if (download_url_.empty() || is_downloading_ || status_ == "done" ||
          progress_ >= 100 || versions_equal(latest_version_, current_version_)) {
        return true;
      }
      handle_progress_event("downloading", 0,
                            utf::utf16_to_utf8(
                                tr("layout.components.dialogs.progress.clientDownloading")));
      if (on_start_update) on_start_update();
      return true;
    }
    if (!card_rect_.contains(pt.x, pt.y)) {
      if (!is_downloading_) close();
      return true;
    }
    return true;
  }

  return true;
}

bool ClientUpdateModal::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
    if (!is_downloading_) close();
    return true;
  }
  return false;
}

std::wstring ClientUpdateModal::a11y_default_name() const {
  return tr("layout.components.dialogs.client.title");
}

A11ySubItem ClientUpdateModal::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  const bool busy = is_downloading_;
  const bool same_ver = versions_equal(latest_version_, current_version_);
  if (idx == 0) {
    sub.role = A11yRole::Text;
    const std::wstring unknown = tr("layout.components.dialogs.update.unknown");
    const std::string cur_disp = current_version_.empty()
                                     ? utf::utf16_to_utf8(unknown)
                                     : ensure_v_prefix(current_version_);
    const std::string lat_disp = latest_version_.empty()
                                     ? utf::utf16_to_utf8(unknown)
                                     : ensure_v_prefix(latest_version_);
    sub.name = tr("layout.components.dialogs.update.currentVersion",
                  {{"version", cur_disp}}) +
               L", " +
               tr("layout.components.dialogs.update.latestVersion",
                  {{"version", lat_disp}});
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
    sub.can_invoke = !upd_disabled && !download_url_.empty() && progress_ < 100;
    if (status_ == "done")
      sub.name = tr("layout.components.dialogs.client.prepareRestart");
    else if (busy)
      sub.name = tr("layout.components.dialogs.update.updating");
    else if (same_ver)
      sub.name = tr("layout.components.dialogs.update.upToDate");
    else
      sub.name = tr("layout.components.dialogs.update.updateNow");
    sub.local_bounds = btn_update_rect_;
  }
  return sub;
}

bool ClientUpdateModal::a11y_invoke_virtual_child(size_t idx) {
  if (!open_ || !enabled()) return false;
  if (idx == 1) {
    if (is_downloading_) return false;
    close();
    return true;
  }
  if (idx == 2) {
    if (download_url_.empty() || is_downloading_ || status_ == "done" ||
        progress_ >= 100 || versions_equal(latest_version_, current_version_)) {
      return false;
    }
    handle_progress_event("downloading", 0,
                          utf::utf16_to_utf8(
                              tr("layout.components.dialogs.progress.clientDownloading")));
    if (on_start_update) on_start_update();
    return true;
  }
  return false;
}

}  // namespace clashwin::ui::scene
