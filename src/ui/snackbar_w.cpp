// SnackbarW 实现：MUI Alert(filled) 堆叠通知条。
// ← src/components/layout/notice-manager.tsx:157-216 + MUI Alert 样式常量
#include "snackbar_w.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "cmd/cmd_support.h"  // copy_text_to_clipboard
#include "events/notice.h"
#include "icon_w.h"
#include "theme/theme.h"
#include "ui/i18n_text.h"
#include "ui_postbox.h"
#include "util/utf.h"

namespace clashwin::ui {

namespace {

float srgb_to_linear(float c) {
  return c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// src: node_modules/@mui/material/styles/createPalette.js:27（dark.text.primary = rgba(0,0,0,.87)）
//      :211（contrastThreshold = 3）、:230（getContrastText 取 dark/light.text.primary）
// MUI getContrastText：相对亮度 < contrastThreshold(3 对应的 0.5 判据) → 白字
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

// MUI Alert severity → 图标（SuccessOutlined / ErrorOutline / InfoOutlined）
const char* severity_icon(const std::string& type) {
  if (type == "success") return "successoutline";
  if (type == "error") return "erroroutline";
  if (type == "warning") return "erroroutline";  // WarningAmberOutlined 近似
  return "infooutline";
}

constexpr size_t kMaxVisible = 8;  // 同屏最多绘制条数（Mini 上限 50，窗口内实际受限）

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
  // 通知来源可能是后台线程（命令超时、更新检查、内核事件）：widget 树只能在
  // UI 线程改，非 UI 线程一律转投。（未建窗口的测试场景 on_ui_thread() 恒真）
  if (!UiPostbox::instance().on_ui_thread()) {
    UiPostbox::instance().post([this] { refresh(); });
    return;
  }
  items_ = events::NoticeService::instance().snapshot();
  layout_items();
  invalidate();
}

void SnackbarW::set_position(const std::string& pos) {
  const bool ok = pos == "top-left" || pos == "top-right" ||
                  pos == "bottom-left" || pos == "bottom-right";
  const std::string next = ok ? pos : "top-right";  // ← resolvePosition 回落
  if (next == position_) return;
  position_ = next;
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
  // src: components/layout/notice-manager.tsx:206-212（action 用 IconButton size="small"）
  //      + node_modules/@mui/material/IconButton/IconButton.js:136（sizeSmall padding 5）
  //   命中盒 30×30（MUI small IconButton 的图标 20 + 上下各 padding 5）
  constexpr float kBtn = 30.0f;
  return {item.x + item.w - 8.0f - kBtn, item.y + 10.0f, kBtn, kBtn};
}

// 消息块左右可用宽：item_w − 左右 padding 32 − 图标(22+12) − action(30+16−8)
float SnackbarW::message_width(float item_w) {
  return std::max(0.0f, item_w - 32.0f - 34.0f - 38.0f);
}

float SnackbarW::item_height(const std::string& message, float item_w) {
  // MUI Alert root 为 flex + alignItems:center，高度取三者最大 +
  // 上下 padding 6×2：图标块 22+7×2=36、消息 行数×20.02+16、action 4+30=34。
  float msg_h = kLineH + 2 * kMsgPadY;
  if (ITextEngine* te = text_engine()) {
    TextStyle ts;
    ts.size = 14;
    ts.wrap = true;
    const SizeF s =
        te->measure_text(utf::utf8_to_utf16(message), ts, message_width(item_w));
    if (s.h > 0) msg_h = s.h + 2 * kMsgPadY;
  }
  const float content = std::max({36.0f, msg_h, 34.0f});
  return std::round(content + 2 * kPadY);
}

void SnackbarW::layout_items() {
  item_rects_.clear();
  const float w_avail = bounds().w > 0 ? bounds().w : kMaxWidth;
  const float width = std::min(kMaxWidth, std::max(0.0f, w_avail - 2 * kInset));
  const bool vertical_top = position_.rfind("top", 0) == 0;
  const bool horizontal_left = position_.find("left") != std::string::npos;

  const float x = horizontal_left ? kInset : w_avail - kInset - width;
  const int n = static_cast<int>(std::min(items_.size(), kMaxVisible));
  item_rects_.reserve(static_cast<size_t>(std::max(n, 0)));

  // 逐条先算高度（消息折行 → 条目变高，Mini 未截断）
  std::vector<float> hs(static_cast<size_t>(std::max(n, 0)), kItemH);
  for (int i = 0; i < n; ++i) {
    const float h = item_height(items_[static_cast<size_t>(i)].message, width);
    hs[static_cast<size_t>(i)] = h > 0 ? h : kItemH;
  }

  if (vertical_top) {
    float y = kInset;
    for (int i = 0; i < n; ++i) {
      const float h = hs[static_cast<size_t>(i)];
      item_rects_.push_back({x, y, width, h});
      y += h + kGapPx;
    }
  } else {
    // bottom 锚点：最新一条贴底，较早的向上叠
    float y = bounds().h - kInset;
    for (int i = n - 1; i >= 0; --i) {
      const float h = hs[static_cast<size_t>(i)];
      y -= h;
      item_rects_.push_back({x, y, width, h});
      y -= kGapPx;
    }
    std::reverse(item_rects_.begin(), item_rects_.end());
  }
}

void SnackbarW::on_paint(IRenderTarget& rt) {
  if (item_rects_.size() != items_.size()) layout_items();
  const int n = static_cast<int>(std::min(item_rects_.size(), items_.size()));
  for (int i = 0; i < n; ++i) {
    const RectF r = item_rects_[static_cast<size_t>(i)];
    const Color4f bg = severity_color(items_[static_cast<size_t>(i)].type);
    Brush b{bg};
    // 豁免: Mini 无对应物 — D2D 无边框填充须传透明 Pen（出处: 无）
    rt.round_rect(r, kRadius, Pen{Color4f(0, 0, 0, 0), 0.0f}, &b);

    // Alert root 为 flex + alignItems:center：图标/消息/action 均垂直居中
    // 图标：22×22，自身 marginRight 12；块高 22+7×2=36
    IconW::paint_at(rt, severity_icon(items_[static_cast<size_t>(i)].type),
                    {r.x + 16, r.y + (r.h - 36.0f) / 2 + 7.0f, kIconSize, kIconSize},
                    contrast_text(bg));

    // 消息：x=16+22+12=50 → action 左缘；padding 8px 0；14px/1.43；
    //   minWidth:0 + 块级 → 长文案自动折行（不省略）
    const RectF btn = close_rect(r);
    const float msg_x = r.x + 50;
    const float msg_w = message_width(r.w);
    TextStyle ts;
    ts.size = 14;
    ts.color = contrast_text(bg);
    ts.align_v = AlignV::Center;
    ts.wrap = true;
    rt.draw_text(utf::utf8_to_utf16(items_[static_cast<size_t>(i)].message),
                 {msg_x, r.y + kPadY + kMsgPadY, msg_w, r.h - 2 * (kPadY + kMsgPadY)}, ts);

    // 关闭按钮：透明 30×30 + CloseRounded fontSize inherit(=20)；
    //   action padding 4px 0 0 16px / marginRight -8
    IconW::paint_at(rt, "close", {btn.x + 5, btn.y + 5, 20, 20}, contrast_text(bg));
  }
}

void SnackbarW::copy_notice(const std::string& text) {
  // ← handleNoticeCopy :178-193：写剪贴板成功后 copySuccess 1000ms；
  //   失败只 warn，不弹错（Mini 同款）。
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
    // 右键 → 复制该条全文（← :196-200 onContextMenu，位于 Alert 根，
    //   关闭按钮上的右键同样冒泡到这里）
    if (e.buttons & kMouseRight) {
      if (e.type == MouseEventType::Down) {
        // 先拷贝：copy_notice 里的 push 会重建 items_，引用不可留
        const std::string text = items_[i].message;
        copy_notice(text);
      }
      return true;
    }
    const RectF btn = close_rect(item_rects_[i]);
    if (btn.contains(p.x, p.y)) {
      if (e.type == MouseEventType::Up) {
        events::NoticeService::instance().hide(items_[i].id);  // ← :205 handleClose
      }
      return true;
    }
    return true;  // 卡片吞掉点击
  }
  return false;
}

}  // namespace clashwin::ui
