// SnackbarW 实现：src: components/layout/notice-manager.tsx:108-231 + MUI Alert(filled)
#include "snackbar_w.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "cmd/cmd_support.h"  // copy_text_to_clipboard
#include "events/notice.h"
#include "icon_w.h"
#include "soft_shadow.h"
#include "theme/theme.h"
#include "ui/i18n_text.h"
#include "ui/uia_provider.h"
#include "ui_postbox.h"
#include "util/utf.h"

namespace clashwin::ui {

namespace {

float srgb_to_linear(float c) {
  return c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// src: node_modules/@mui/material/styles/createPalette.js:27,211,230
Color4f contrast_text(const Color4f& bg) {
  const float lum = 0.2126f * srgb_to_linear(bg.r) +
                    0.7152f * srgb_to_linear(bg.g) +
                    0.0722f * srgb_to_linear(bg.b);
  return lum < 0.5f ? Color4f(1, 1, 1, 1) : Color4f(0, 0, 0, 0.87f);
}

Color4f severity_color(const std::string& type) {
  const auto& p = theme::ThemeService::instance().palette();
  if (type == "success") return p.success_color;
  if (type == "error") return p.error_color;
  if (type == "warning") return p.warning_color;
  return p.info_color;
}

// MUI Alert severity 图标
const char* severity_icon(const std::string& type) {
  if (type == "success") return "successoutline";
  if (type == "error") return "erroroutline";
  if (type == "warning") return "warningamberoutlined";
  return "infooutline";
}

constexpr size_t kMaxVisible = 50;  // src: services/notice-service.ts:42 MAX_NOTICES

}  // namespace

SnackbarW::~SnackbarW() {
  if (svc_ && sub_id_ != 0) {
    svc_->unsubscribe(sub_id_);
    sub_id_ = 0;
  }
}

void SnackbarW::attach(events::NoticeService& svc) {
  if (svc_ && sub_id_ != 0) {
    svc_->unsubscribe(sub_id_);
    sub_id_ = 0;
  }
  svc_ = &svc;
  sub_id_ = svc.subscribe([this]() { refresh(); });
  refresh();
}

void SnackbarW::set_items(std::vector<events::NoticeItem> items) {
  items_ = std::move(items);
  layout_items();
  invalidate();
}

void SnackbarW::refresh() {
  if (!UiPostbox::instance().on_ui_thread()) {
    UiPostbox::instance().post([this] { refresh(); });
    return;
  }
  items_ = events::NoticeService::instance().snapshot();
  layout_items();
  for (const auto& it : items_) {
    if (it.id > last_announced_id_) {
      last_announced_id_ = it.id;
      if (!it.message.empty()) {
        uia::notify_live_announcement(utf::utf8_to_utf16(it.message), it.id);
      }
    }
  }
  invalidate();
}

std::wstring SnackbarW::a11y_default_name() const {
  std::wstring out;
  for (const auto& it : items_) {
    if (it.message.empty()) continue;
    if (!out.empty()) out += L"; ";
    out += utf::utf8_to_utf16(it.message);
  }
  return out;
}

A11ySubItem SnackbarW::a11y_virtual_child(size_t index) const {
  A11ySubItem sub;
  if (index >= items_.size() || index >= item_rects_.size()) return sub;
  sub.role = A11yRole::Button;
  sub.name = utf::utf8_to_utf16(items_[index].message);
  sub.help_text = tr("shared.actions.close");
  sub.local_bounds = item_rects_[index];
  sub.enabled = enabled();
  sub.focusable = enabled();
  sub.can_invoke = enabled();
  return sub;
}

bool SnackbarW::a11y_invoke_virtual_child(size_t index) {
  if (index >= items_.size()) return false;
  events::NoticeService::instance().hide(items_[index].id);
  return true;
}

void SnackbarW::set_position(const std::string& pos) {
  const bool ok = pos == "top-left" || pos == "top-right" ||
                  pos == "bottom-left" || pos == "bottom-right";
  const std::string next = ok ? pos : "top-left";
  if (next == position_) return;
  position_ = next;
  layout_items();
  invalidate();
}

void SnackbarW::set_top_offset(float top_offset) {
  const float clamped = (std::max)(0.0f, top_offset);
  if (std::abs(top_offset_ - clamped) < 0.01f) return;
  top_offset_ = clamped;
  layout_items();
  invalidate();
}

SizeF SnackbarW::on_measure(float max_w, float max_h) {
  (void)max_w;
  (void)max_h;
  return {kMaxWidth, kItemH};
}

void SnackbarW::on_arrange(const RectF& area) {
  set_bounds(area);
  layout_items();
}

RectF SnackbarW::close_rect(const RectF& item) {
  // src: components/layout/notice-manager.tsx:203-222 action small IconButton (24x24)
  constexpr float kBtn = 24.0f;
  constexpr float kRightInset = 6.0f;
  return {item.x + item.w - kRightInset - kBtn, item.y + 4.0f, kBtn, kBtn};
}

// 消息可用宽：src: components/layout/notice-manager.tsx:185-208
// item_w - (px 10*2) - (icon 18 + mr 8) - (action pl 4 + btn 24 - mr 4)
float SnackbarW::message_width(float item_w) {
  return std::max(0.0f, item_w - 20.0f - 26.0f - 24.0f);
}

float SnackbarW::item_height(const std::string& message, float item_w) {
  // src: components/layout/notice-manager.tsx:186-197
  float msg_h = kLineH + 2.0f * kMsgPadY;
  if (ITextEngine* te = text_engine()) {
    TextStyle ts;
    ts.size = kFontSize;
    ts.line_h = kLineH;
    ts.wrap = true;
    const SizeF s =
        te->measure_text(utf::utf8_to_utf16(message), ts, message_width(item_w));
    if (s.h > 0.0f) msg_h = s.h + 2.0f * kMsgPadY;
  }
  constexpr float kMinInnerH = 24.0f;  // icon 18 + py 3*2
  const float content = (std::max)(kMinInnerH, msg_h);
  return std::round(content + 2.0f * kPadY);
}

void SnackbarW::layout_items() {
  item_rects_.clear();
  // src: components/layout/notice-manager.tsx:148-163
  // 固定 top: topOffset, left: 0, width: 271px, px: 6px, pt: 4px, gap: 4px
  const float w_avail = bounds().w > 0.0f ? bounds().w : kMaxWidth;
  const float box_w = (std::min)(kMaxWidth, w_avail);
  const float width = (std::max)(0.0f, box_w - 2.0f * kOuterPadX);
  const float x = kOuterPadX;
  const int n = static_cast<int>((std::min)(items_.size(), kMaxVisible));
  item_rects_.reserve(static_cast<size_t>((std::max)(n, 0)));

  float y = top_offset_ + kOuterPadTop;
  for (int i = 0; i < n; ++i) {
    const float h = item_height(items_[static_cast<size_t>(i)].message, width);
    const float rh = h > 0.0f ? h : kItemH;
    item_rects_.push_back({x, y, width, rh});
    y += rh + kGapPx;
  }
}

void SnackbarW::on_paint(IRenderTarget& rt) {
  if (item_rects_.size() != items_.size()) layout_items();
  const auto skin = theme::ThemeService::instance().skin();
  // src: components/layout/notice-manager.tsx:115-116
  const float rad = (skin == theme::SkinId::Cyberpunk ||
                     skin == theme::SkinId::Monochrome)
                        ? 0.0f
                        : kRadius;
  const int n = static_cast<int>((std::min)(item_rects_.size(), items_.size()));
  for (int i = 0; i < n; ++i) {
    const RectF r = item_rects_[static_cast<size_t>(i)];
    const Color4f bg = severity_color(items_[static_cast<size_t>(i)].type);
    // src: components/layout/notice-manager.tsx:190 boxShadow '0 4px 12px rgba(0, 0, 0, 0.28)'
    draw_soft_shadow(rt, r, rad, Color4f(0.0f, 0.0f, 0.0f, 0.28f), 4.0f, 12.0f);
    Brush b{bg};
    // 豁免: Mini 无对应物 — D2D 无边框填充须传透明 Pen（出处: 无）
    rt.round_rect(r, rad, Pen{Color4f(0, 0, 0, 0), 0.0f}, &b);

    // src: components/layout/notice-manager.tsx:191-195 18px icon + mr 8px
    const Color4f fg = contrast_text(bg);
    IconW::paint_at(rt, severity_icon(items_[static_cast<size_t>(i)].type),
                    {r.x + kPadX, r.y + (r.h - kIconSize) * 0.5f, kIconSize,
                     kIconSize},
                    fg);

    // src: components/layout/notice-manager.tsx:187-202 12px / 1.45 / wrap
    const RectF btn = close_rect(r);
    const float msg_x = r.x + kPadX + kIconSize + 8.0f;
    const float msg_w = message_width(r.w);
    TextStyle ts;
    ts.size = kFontSize;
    ts.line_h = kLineH;
    ts.color = fg;
    ts.align_v = AlignV::Center;
    ts.wrap = true;
    rt.draw_text(utf::utf8_to_utf16(items_[static_cast<size_t>(i)].message),
                 {msg_x, r.y + kPadY + kMsgPadY, msg_w,
                  r.h - 2.0f * (kPadY + kMsgPadY)},
                 ts);

    // src: components/layout/notice-manager.tsx:216-222 CloseRounded 18px
    IconW::paint_at(rt, "close", {btn.x + 3.0f, btn.y + 3.0f, 18.0f, 18.0f},
                    fg);
  }
}

void SnackbarW::copy_notice(const std::string& text) {
  if (text.empty()) return;
  if (!cmd::copy_text_to_clipboard(text)) return;
  events::NoticeService::instance().push(
      "success", tr8("shared.feedback.notifications.common.copySuccess"), 1000);
}

bool SnackbarW::on_mouse(const MouseEvent& e) {
  if (e.type != MouseEventType::Down && e.type != MouseEventType::Up) return false;
  const PointF p = e.pos;
  for (size_t i = 0; i < item_rects_.size() && i < items_.size(); ++i) {
    if (!item_rects_[i].contains(p.x, p.y)) continue;
    if (e.buttons & kMouseRight) {
      if (e.type == MouseEventType::Down) {
        const std::string text = items_[i].message;
        copy_notice(text);
      }
      return true;
    }
    const RectF btn = close_rect(item_rects_[i]);
    if (btn.contains(p.x, p.y)) {
      if (e.type == MouseEventType::Up) {
        events::NoticeService::instance().hide(items_[i].id);
      }
      return true;
    }
    return true;
  }
  return false;
}

}  // namespace clashwin::ui
