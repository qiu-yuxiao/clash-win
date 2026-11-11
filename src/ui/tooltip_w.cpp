// TooltipHost 实现（T10）。
#include "tooltip_w.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "d2d_context.h"

namespace clashwin::ui {

namespace {

constexpr wchar_t kTooltipPopupClass[] = L"ClashWinTooltipPopupWnd";
// src: node_modules/@mui/material/Tooltip/Tooltip.js:126-140 arrow 几何尺寸
constexpr float kArrowW = 10.0f;
constexpr float kArrowH = 5.0f;
constexpr float kGapDip = 6.0f;

LRESULT CALLBACK tooltip_popup_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_ERASEBKGND:
      return 1;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

void ensure_popup_class_registered() {
  static bool registered = false;
  if (registered) return;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = tooltip_popup_wndproc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kTooltipPopupClass;
  if (RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    registered = true;
  }
}

}  // namespace

TooltipHost::TooltipHost(Widget* parent) : Widget(parent) {
  set_z_order(1200);
  set_visible(false);
}

TooltipHost::~TooltipHost() {
  destroy_popup();
}

void TooltipHost::bind_owner_hwnd(HWND owner_hwnd) {
  if (owner_hwnd_ == owner_hwnd) return;
  destroy_popup();
  owner_hwnd_ = owner_hwnd;
}

bool TooltipHost::can_use_popup() const {
  return owner_hwnd_ != nullptr && IsWindow(owner_hwnd_) != FALSE &&
         IsWindowVisible(owner_hwnd_) != FALSE && IsIconic(owner_hwnd_) == FALSE;
}

bool TooltipHost::ensure_popup_hwnd() {
  if (popup_hwnd_ && IsWindow(popup_hwnd_)) return true;
  popup_hwnd_ = nullptr;
  ensure_popup_class_registered();
  popup_hwnd_ = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
          WS_EX_TRANSPARENT | WS_EX_TOPMOST,
      kTooltipPopupClass, L"", WS_POPUP, 0, 0, 1, 1, owner_hwnd_, nullptr,
      GetModuleHandleW(nullptr), nullptr);
  return popup_hwnd_ != nullptr;
}

void TooltipHost::hide_popup() {
  popup_active_ = false;
  popup_screen_rect_ = {};
  if (popup_hwnd_ && IsWindow(popup_hwnd_)) {
    ShowWindow(popup_hwnd_, SW_HIDE);
  }
}

void TooltipHost::destroy_popup() {
  hide_popup();
  if (popup_rt_) {
    popup_rt_->destroy();
    popup_rt_.reset();
  }
  if (popup_ctx_) {
    popup_ctx_->shutdown();
    popup_ctx_.reset();
  }
  if (popup_hwnd_ && IsWindow(popup_hwnd_)) {
    DestroyWindow(popup_hwnd_);
  }
  popup_hwnd_ = nullptr;
}

TooltipHost::MeasuredTip TooltipHost::measure_tip(ITextEngine* eng,
                                                  float max_box_w) const {
  const float max_w = std::max(48.0f, max_box_w);
  const float max_text_w = std::max(24.0f, max_w - pad_h * 2.0f);
  if (!eng || text_.empty()) {
    const float fallback_w =
        std::min(max_w, static_cast<float>(text_.size()) * 7.0f + pad_h * 2.0f);
    return {std::max(24.0f, fallback_w), 22.0f, false};
  }
  TextStyle ts;
  ts.size = font_size;
  ts.wrap = false;
  const SizeF single = eng->measure_text(text_, ts, 0.0f);
  if (single.w <= max_text_w && text_.find(L'\n') == std::wstring::npos) {
    return {std::ceil(single.w) + pad_h * 2.0f,
            std::max(22.0f, std::ceil(single.h) + pad_v * 2.0f), false};
  }
  ts.wrap = true;
  const SizeF wrapped = eng->measure_text(text_, ts, max_text_w);
  const float used_w = std::min(max_text_w, std::max(single.w, wrapped.w));
  return {std::ceil(used_w) + pad_h * 2.0f,
          std::max(22.0f, std::ceil(wrapped.h) + pad_v * 2.0f), true};
}

void TooltipHost::paint_tip_body(IRenderTarget& rt, float box_w, float box_h,
                                 bool multiline, bool with_arrow,
                                 bool arrow_at_top, float arrow_x) const {
  const float body_y = (with_arrow && arrow_at_top) ? kArrowH : 0.0f;
  const RectF body{0.0f, body_y, box_w, box_h};
  Brush bg_b{bg_};
  rt.round_rect(body, radius_, {}, &bg_b);

  if (with_arrow) {
    const float ax =
        std::clamp(arrow_x, radius_ + kArrowW * 0.5f,
                   std::max(radius_ + kArrowW * 0.5f, box_w - radius_ - kArrowW * 0.5f));
    std::vector<std::vector<PointF>> tri(1);
    if (arrow_at_top) {
      tri[0] = {{ax - kArrowW * 0.5f, body_y + 0.5f},
                {ax, 0.0f},
                {ax + kArrowW * 0.5f, body_y + 0.5f}};
    } else {
      const float by = body.bottom();
      tri[0] = {{ax - kArrowW * 0.5f, by - 0.5f},
                {ax + kArrowW * 0.5f, by - 0.5f},
                {ax, by + kArrowH}};
    }
    rt.fill_path(tri, bg_b);
  }

  TextStyle ts;
  ts.size = font_size;
  ts.color = text_color_;
  ts.wrap = multiline;
  ts.align_h = multiline ? AlignH::Left : AlignH::Center;
  ts.align_v = AlignV::Center;
  rt.draw_text(text_,
               {body.x + pad_h, body.y + pad_v,
                std::max(0.0f, body.w - pad_h * 2.0f),
                std::max(0.0f, body.h - pad_v * 2.0f)},
               ts);
}

void TooltipHost::hover(Widget* target, PointF global) {
  // 命中控件无提示时沿父链上溯（Label 等不吞鼠标，提示挂在父容器上）
  Widget* owner = nullptr;
  std::wstring t;
  for (Widget* w = target; w; w = w->parent()) {
    t = w->tooltip_text();
    if (!t.empty()) {
      owner = w;
      break;
    }
  }
  if (t.empty() || !owner) {
    leave();
    return;
  }

  const RectF new_rect =
      owner->tooltip_anchor_rect().value_or(owner->global_bounds());
  const bool new_arrow = owner->tooltip_prefer_top_arrow();
  const bool target_changed =
      (text_ != t) || (target_widget_ != owner) ||
      (prefer_top_arrow_ != new_arrow) ||
      (std::fabs(target_rect_.x - new_rect.x) > 0.5f) ||
      (std::fabs(target_rect_.y - new_rect.y) > 0.5f);

  target_widget_ = owner;
  target_rect_ = new_rect;
  prefer_top_arrow_ = new_arrow;
  anchor_ = global;

  if (target_changed) {
    text_ = std::move(t);
    hover_start_ = now_;  // 新目标/新子段重新计时
    if (visible()) {
      hide_popup();
      set_visible(false);
      invalidate();
    }
  } else if (hover_start_ < 0) {
    hover_start_ = now_;
  }
  place(anchor_);
}

void TooltipHost::leave() {
  hover_start_ = -1;
  target_widget_ = nullptr;
  target_rect_ = {};
  prefer_top_arrow_ = false;
  text_.clear();
  hide_popup();
  if (visible()) {
    set_visible(false);
    invalidate();
  }
}

void TooltipHost::tick_now(int64_t now_ms) {
  now_ = now_ms;
  if (hover_start_ >= 0 && can_use_popup() == false && owner_hwnd_ != nullptr) {
    // 宿主窗口最小化/隐藏时立即收起顶层提示窗
    leave();
    return;
  }
  if (hover_start_ >= 0) place(anchor_);
  if (hover_start_ >= 0 && now_ - hover_start_ >= kShowDelayMs) {
    if (!visible()) {
      place(anchor_);
      set_visible(true);
      if (can_use_popup()) {
        present_popup();
      } else {
        invalidate();
      }
    } else if (can_use_popup() && !popup_active_) {
      present_popup();
    }
  }
}

void TooltipHost::on_arrange(const RectF& area) {
  host_area_ = area;
  if (!text_.empty()) {
    place(anchor_);
  } else {
    set_bounds({0.0f, 0.0f, 0.0f, 0.0f});
  }
}

void TooltipHost::place(PointF anchor) {
  // 画布内兜底布局（离屏测试及 tip_bounds 查询用）
  const float avail_w =
      (host_area_.w > 0.0f) ? host_area_.w
                            : (parent() ? parent()->bounds().w : 0.0f);
  const float avail_h =
      (host_area_.h > 0.0f) ? host_area_.h
                            : (parent() ? parent()->bounds().h : 0.0f);
  const float max_box_w = (!can_use_popup() && avail_w > 24.0f)
                              ? std::min(kMaxWidth, avail_w - 8.0f)
                              : kMaxWidth;
  const MeasuredTip m = measure_tip(text_engine(), max_box_w);
  multiline_ = m.multiline;

  float x = anchor.x + 12.0f;
  float y = anchor.y + 16.0f;
  const bool compact_target =
      (target_rect_.w > 0.0f && target_rect_.h > 0.0f &&
       (prefer_top_arrow_ || (target_rect_.w <= 120.0f && target_rect_.h <= 48.0f)));
  if (compact_target) {
    x = target_rect_.x + (target_rect_.w - m.box_w) * 0.5f;
    y = prefer_top_arrow_ ? (target_rect_.y - kGapDip - m.box_h)
                          : (target_rect_.bottom() + kGapDip);
  }
  if (avail_w > 0.0f && avail_h > 0.0f) {
    if (y + m.box_h > avail_h - 4.0f) {
      y = compact_target ? (target_rect_.y - kGapDip - m.box_h)
                         : (anchor.y - 8.0f - m.box_h);
    }
    if (y < 4.0f && compact_target) {
      y = target_rect_.bottom() + kGapDip;
    }
    x = std::clamp(x, 4.0f, std::max(4.0f, avail_w - m.box_w - 4.0f));
    y = std::clamp(y, 4.0f, std::max(4.0f, avail_h - m.box_h - 4.0f));
  }
  set_bounds({x, y, m.box_w, m.box_h});
}

void TooltipHost::present_popup() {
  if (!can_use_popup() || text_.empty()) {
    hide_popup();
    return;
  }
  if (!ensure_popup_hwnd()) return;
  if (!popup_ctx_) {
    popup_ctx_ = std::make_unique<D2dContext>();
    if (!popup_ctx_->init()) {
      popup_ctx_.reset();
      return;
    }
  }
  if (!popup_rt_) {
    popup_rt_ = std::make_unique<D2dRenderTarget>(*popup_ctx_);
  }

  const UINT raw_dpi = GetDpiForWindow(owner_hwnd_);
  const float dpi = raw_dpi > 0 ? static_cast<float>(raw_dpi) : 96.0f;
  const float scale = dpi / 96.0f;

  // 先确保 popup_rt_ 按当前窗口 DPI 就绪以便精确测量换行宽度
  if (popup_rt_->dpi() != dpi || popup_rt_->pixel_width() <= 0) {
    popup_rt_->create_offscreen(16, 16, dpi);
  }
  const MeasuredTip m = measure_tip(popup_rt_.get(), kMaxWidth);
  multiline_ = m.multiline;
  const bool with_arrow = prefer_top_arrow_;
  const float total_w_dip = m.box_w;
  const float total_h_dip = m.box_h + (with_arrow ? kArrowH : 0.0f);
  const int pw = std::max(1, static_cast<int>(std::ceil(total_w_dip * scale)));
  const int ph = std::max(1, static_cast<int>(std::ceil(total_h_dip * scale)));

  POINT client_origin{0, 0};
  ClientToScreen(owner_hwnd_, &client_origin);

  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  RECT work{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  HMONITOR mon = MonitorFromWindow(owner_hwnd_, MONITOR_DEFAULTTONEAREST);
  if (mon && GetMonitorInfoW(mon, &mi)) {
    work = mi.rcWork;
  }

  const bool compact_target =
      (target_rect_.w > 0.0f && target_rect_.h > 0.0f &&
       (prefer_top_arrow_ || (target_rect_.w <= 120.0f && target_rect_.h <= 48.0f)));
  const int gap_px = static_cast<int>(std::round(kGapDip * scale));

  int sx = 0;
  int sy = 0;
  int anchor_center_screen_x =
      client_origin.x + static_cast<int>(std::round(anchor_.x * scale));
  bool arrow_at_top = false;

  if (compact_target) {
    const int target_left =
        client_origin.x + static_cast<int>(std::round(target_rect_.x * scale));
    const int target_top =
        client_origin.y + static_cast<int>(std::round(target_rect_.y * scale));
    const int target_right =
        client_origin.x +
        static_cast<int>(std::round(target_rect_.right() * scale));
    const int target_bottom =
        client_origin.y +
        static_cast<int>(std::round(target_rect_.bottom() * scale));
    anchor_center_screen_x = (target_left + target_right) / 2;
    sx = anchor_center_screen_x - pw / 2;
    if (prefer_top_arrow_) {
      sy = target_top - gap_px - ph;
      arrow_at_top = false;
      if (sy < work.top + 4) {
        sy = target_bottom + gap_px;
        arrow_at_top = true;
      }
    } else {
      sy = target_bottom + gap_px;
      if (sy + ph > work.bottom - 4) {
        sy = target_top - gap_px - ph;
      }
    }
  } else {
    const int cur_x =
        client_origin.x + static_cast<int>(std::round(anchor_.x * scale));
    const int cur_y =
        client_origin.y + static_cast<int>(std::round(anchor_.y * scale));
    sx = cur_x + static_cast<int>(std::round(10.0f * scale));
    sy = cur_y + static_cast<int>(std::round(16.0f * scale));
    if (sy + ph > work.bottom - 4) {
      sy = cur_y - static_cast<int>(std::round(8.0f * scale)) - ph;
    }
  }

  sx = std::clamp(sx, static_cast<int>(work.left) + 4,
                  std::max(static_cast<int>(work.left) + 4,
                           static_cast<int>(work.right) - pw - 4));
  sy = std::clamp(sy, static_cast<int>(work.top) + 4,
                  std::max(static_cast<int>(work.top) + 4,
                           static_cast<int>(work.bottom) - ph - 4));

  const float arrow_x_dip =
      static_cast<float>(anchor_center_screen_x - sx) / scale;

  if (!popup_rt_->create_offscreen(pw, ph, dpi)) return;
  popup_rt_->begin_frame();
  popup_rt_->clear({0.0f, 0.0f, 0.0f, 0.0f});
  paint_tip_body(*popup_rt_, m.box_w, m.box_h, m.multiline, with_arrow,
                 arrow_at_top, arrow_x_dip);
  if (!popup_rt_->end_frame()) return;

  std::vector<uint8_t> bgra;
  int rw = 0, rh = 0;
  if (!popup_rt_->read_pixels(bgra, rw, rh) || rw <= 0 || rh <= 0) return;

  HDC screen_dc = GetDC(nullptr);
  if (!screen_dc) return;
  HDC mem_dc = CreateCompatibleDC(screen_dc);
  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = rw;
  bmi.bmiHeader.biHeight = -rh;  // top-down PBGRA
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (mem_dc && dib && bits) {
    std::memcpy(bits, bgra.data(), static_cast<size_t>(rw) * static_cast<size_t>(rh) * 4u);
    HGDIOBJ old_bmp = SelectObject(mem_dc, dib);
    POINT pt_dst{sx, sy};
    SIZE sz_wnd{rw, rh};
    POINT pt_src{0, 0};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(popup_hwnd_, screen_dc, &pt_dst, &sz_wnd, mem_dc,
                        &pt_src, 0, &blend, ULW_ALPHA);
    SetWindowPos(popup_hwnd_, HWND_TOPMOST, sx, sy, rw, rh,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    popup_screen_rect_ = {sx, sy, sx + rw, sy + rh};
    popup_active_ = true;
    SelectObject(mem_dc, old_bmp);
  }
  if (dib) DeleteObject(dib);
  if (mem_dc) DeleteDC(mem_dc);
  ReleaseDC(nullptr, screen_dc);
}

void TooltipHost::on_paint(IRenderTarget& rt) {
  if (popup_active_) return;  // 顶层分层窗口激活时不再于窄主窗画布内重复绘制
  paint_tip_body(rt, bounds().w, bounds().h, multiline_, false, false, 0.0f);
}

}  // namespace clashwin::ui
