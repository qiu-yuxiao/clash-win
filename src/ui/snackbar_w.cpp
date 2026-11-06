// SnackbarW 实现：MUI Alert(filled) 堆叠通知条。
// ← src/components/layout/notice-manager.tsx:157-216 + MUI Alert 样式常量
#include "snackbar_w.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "events/notice.h"
#include "icon_w.h"
#include "theme/theme.h"
#include "ui_postbox.h"
#include "util/utf.h"

namespace clashwin::ui {

namespace {

float srgb_to_linear(float c) {
  return c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

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
  if (attached_) events::NoticeService::instance().unsubscribe();
}

void SnackbarW::attach(events::NoticeService& svc) {
  svc.subscribe([this]() { refresh(); });
  attached_ = true;
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
  // action：padding 4px 0 0 16px / marginRight -8px；IconButton size=small
  //   （padding 5 + icon 20）= 30×30
  constexpr float kBtn = 30.0f;
  return {item.x + item.w - 8.0f - kBtn, item.y + 10.0f, kBtn, kBtn};
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
  for (int i = 0; i < n; ++i) {
    float y;
    if (vertical_top) {
      y = kInset + static_cast<float>(i) * (kItemH + kGapPx);
    } else {
      // bottom 锚点：最新一条贴底，较早的向上叠
      const int from_bottom = n - 1 - i;
      y = bounds().h - kInset - kItemH -
          static_cast<float>(from_bottom) * (kItemH + kGapPx);
    }
    item_rects_.push_back({x, y, width, kItemH});
  }
}

void SnackbarW::on_paint(IRenderTarget& rt) {
  if (item_rects_.size() != items_.size()) layout_items();
  const int n = static_cast<int>(std::min(item_rects_.size(), items_.size()));
  for (int i = 0; i < n; ++i) {
    const RectF r = item_rects_[static_cast<size_t>(i)];
    const Color4f bg = severity_color(items_[static_cast<size_t>(i)].type);
    Brush b{bg};
    rt.round_rect(r, kRadius, Pen{Color4f(0, 0, 0, 0), 0.0f}, &b);

    // 图标：x=16，y=6+7=13，22×22（.MuiAlert-icon marginRight 12 / padding 7px 0）
    IconW::paint_at(rt, severity_icon(items_[static_cast<size_t>(i)].type),
                    {r.x + 16, r.y + 13, kIconSize, kIconSize}, contrast_text(bg));

    // 消息：x=16+22+12=50 → 关闭按钮左侧；padding 8px 0；14px/1.43
    const RectF btn = close_rect(r);
    const float msg_x = r.x + 50;
    const float msg_w = std::max(0.0f, btn.x - 16 - msg_x);
    TextStyle ts;
    ts.size = 14;
    ts.color = contrast_text(bg);
    ts.align_v = AlignV::Center;
    ts.ellipsis = true;
    rt.draw_text(utf::utf8_to_utf16(items_[static_cast<size_t>(i)].message),
                 {msg_x, r.y + 6, msg_w, r.h - 12}, ts);

    // 关闭按钮：透明 30×30 + CloseRounded fontSize inherit(=20)
    IconW::paint_at(rt, "close", {btn.x + 5, btn.y + 5, 20, 20}, contrast_text(bg));
  }
}

bool SnackbarW::on_mouse(const MouseEvent& e) {
  if (e.type != MouseEventType::Down && e.type != MouseEventType::Up) return false;
  const PointF p = e.pos;
  for (size_t i = 0; i < item_rects_.size() && i < items_.size(); ++i) {
    const RectF btn = close_rect(item_rects_[i]);
    if (btn.contains(p.x, p.y)) {
      if (e.type == MouseEventType::Up) {
        events::NoticeService::instance().hide(items_[i].id);  // ← :205 handleClose
      }
      return true;
    }
    if (item_rects_[i].contains(p.x, p.y)) return true;  // 卡片吞掉点击
  }
  return false;
}

}  // namespace clashwin::ui
