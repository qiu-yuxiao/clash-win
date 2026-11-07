// TextField 实现（T10）：光标/选区/键盘/粘贴/绘制。
#include "textfield.h"

#include <windows.h>

#include <algorithm>
#include <utility>

#include "soft_shadow.h"
#include "theme/theme.h"

namespace clashwin::ui {

namespace {
std::function<std::wstring()>& clip_get() {
  static std::function<std::wstring()> f;
  return f;
}
std::function<void(const std::wstring&)>& clip_set() {
  static std::function<void(const std::wstring&)> f;
  return f;
}
}  // namespace

void TextField::set_clipboard_hooks(std::function<std::wstring()> get_fn,
                                    std::function<void(const std::wstring&)> set_fn) {
  clip_get() = std::move(get_fn);
  clip_set() = std::move(set_fn);
}

void TextField::set_text(const std::wstring& t) {
  text_ = t;
  cursor_ = anchor_ = static_cast<int>(t.size());
  scroll_x_ = 0;
  invalidate();
  if (on_text_changed) on_text_changed(text_);
}

std::wstring TextField::selection() const {
  int lo = std::min(cursor_, anchor_), hi = std::max(cursor_, anchor_);
  return text_.substr(static_cast<size_t>(lo), static_cast<size_t>(hi - lo));
}

bool TextField::has_selection() const { return cursor_ != anchor_; }

// ---- 清空按钮（opt-in） ----
void TextField::set_clear_button(bool on, std::wstring tooltip) {
  clear_button_ = on;
  clear_tooltip_ = std::move(tooltip);
  if (!on) clear_hover_ = false;
  invalidate();
}

RectF TextField::clear_button_rect() const {
  constexpr float kBox = 24.0f;  // src: base-search-box.tsx:126-136 IconButton small 24
  return {bounds().w - 4.0f - kBox, (bounds().h - kBox) * 0.5f, kBox, kBox};
}

float TextField::text_right_pad() const {
  // 清空按钮 24 + 右内距 4 + 文本右内距 4 ≈ 32（src: base-search-box.tsx:123 pr 4）
  return clear_button_ ? 32.0f : 8.0f;
}

std::wstring TextField::tooltip_text() const {
  // 仅悬停在 ✕ 上时给 resetInput 提示（src: base-search-box.tsx:126）
  if (clear_button_ && clear_hover_) return clear_tooltip_;
  return Widget::tooltip_text();
}

// ---- 编辑命令（EM_* 等价） ----
bool TextField::can_cut() const { return has_selection(); }
bool TextField::can_copy() const { return has_selection(); }
bool TextField::can_paste() const { return true; }  // src: _layout.tsx:1908 粘贴项无 disabled
bool TextField::can_select_all() const { return !text_.empty(); }
bool TextField::can_clear() const { return !text_.empty(); }

void TextField::cut() {
  if (!has_selection()) return;
  if (clip_set()) clip_set()(selection());
  erase_selection();
}

void TextField::copy() {
  if (!has_selection()) return;
  if (clip_set()) clip_set()(selection());
}

void TextField::paste() {
  if (!clip_get()) return;
  std::wstring s = clip_get()();
  s.erase(std::remove(s.begin(), s.end(), L'\r'), s.end());
  s.erase(std::remove(s.begin(), s.end(), L'\n'), s.end());
  if (!s.empty()) insert_text(s);
}

void TextField::select_all() {
  request_focus();
  anchor_ = 0;
  cursor_ = static_cast<int>(text_.size());
  invalidate();
}

void TextField::clear_text() {
  if (text_.empty()) return;
  request_focus();
  set_text(L"");
}

SizeF TextField::on_measure(float max_w, float max_h) {
  (void)max_h;
  return {max_w > 0 ? max_w : 120, height_};
}

// ---- 键盘 ----
bool TextField::on_key(const KeyEvent& e) {
  if (e.type == KeyEventType::Char) {
    if (e.ctrl) return false;  // Ctrl 组合键非字符输入
    if (e.ch >= 0x20) {
      insert_text(std::wstring(1, e.ch));
      return true;
    }
    if (e.ch == L'\r') {
      if (on_submit) on_submit();
      return true;
    }
    return false;
  }
  if (e.type != KeyEventType::Down) return false;

  int len = static_cast<int>(text_.size());
  int lo = std::min(cursor_, anchor_), hi = std::max(cursor_, anchor_);
  bool has_sel = lo != hi;

  switch (e.key) {
    case VK_LEFT: {
      int target = has_sel && !e.shift ? lo : std::max(0, cursor_ - 1);
      cursor_ = target;
      if (!e.shift) anchor_ = cursor_;
      clamp_scroll();
      invalidate();
      return true;
    }
    case VK_RIGHT: {
      int target = has_sel && !e.shift ? hi : std::min(len, cursor_ + 1);
      cursor_ = target;
      if (!e.shift) anchor_ = cursor_;
      clamp_scroll();
      invalidate();
      return true;
    }
    case VK_HOME:
      cursor_ = 0;
      if (!e.shift) anchor_ = 0;
      clamp_scroll();
      invalidate();
      return true;
    case VK_END:
      cursor_ = len;
      if (!e.shift) anchor_ = len;
      clamp_scroll();
      invalidate();
      return true;
    case VK_BACK:
      if (has_sel) {
        erase_selection();
      } else if (cursor_ > 0) {
        text_.erase(static_cast<size_t>(cursor_ - 1), 1);
        cursor_ = anchor_ = cursor_ - 1;
        clamp_scroll();
        invalidate();
        if (on_text_changed) on_text_changed(text_);
      }
      return true;
    case VK_DELETE:
      if (has_sel) {
        erase_selection();
      } else if (cursor_ < len) {
        text_.erase(static_cast<size_t>(cursor_), 1);
        clamp_scroll();
        invalidate();
        if (on_text_changed) on_text_changed(text_);
      }
      return true;
    case 'A':
      if (e.ctrl) {
        anchor_ = 0;
        cursor_ = len;
        invalidate();
        return true;
      }
      return false;
    case 'C':
      if (e.ctrl && has_sel && clip_set()) {
        clip_set()(selection());
        return true;
      }
      return false;
    case 'V':
      if (e.ctrl && clip_get()) {
        std::wstring s = clip_get()();
        s.erase(std::remove(s.begin(), s.end(), L'\r'), s.end());
        s.erase(std::remove(s.begin(), s.end(), L'\n'), s.end());
        if (!s.empty()) insert_text(s);
        return true;
      }
      return false;
    case 'X':
      if (e.ctrl && has_sel && clip_set()) {
        clip_set()(selection());
        erase_selection();
        return true;
      }
      return false;
    default:
      return false;
  }
}

void TextField::erase_selection() {
  int lo = std::min(cursor_, anchor_), hi = std::max(cursor_, anchor_);
  text_.erase(static_cast<size_t>(lo), static_cast<size_t>(hi - lo));
  cursor_ = anchor_ = lo;
  clamp_scroll();
  invalidate();
  if (on_text_changed) on_text_changed(text_);
}

void TextField::insert_text(const std::wstring& s) {
  erase_selection();
  text_.insert(static_cast<size_t>(cursor_), s);
  cursor_ = anchor_ = cursor_ + static_cast<int>(s.size());
  clamp_scroll();
  invalidate();
  if (on_text_changed) on_text_changed(text_);
}

// ---- 鼠标 ----
bool TextField::on_mouse(const MouseEvent& e) {
  if (e.type == MouseEventType::Down) {
    if (e.buttons & kMouseRight) {
      // 右键：焦点 + 选区外点击移动光标（Win32 编辑控件行为），再交给宿主弹菜单
      request_focus();
      int idx = hit_index(e.pos.x);
      if (idx < std::min(cursor_, anchor_) || idx > std::max(cursor_, anchor_))
        cursor_ = anchor_ = idx;
      clamp_scroll();
      invalidate();
      if (on_context_menu) on_context_menu(to_global(e.pos));
      return true;
    }
    // ✕ 命中（非空才可点；空值时 disabled，点击落到输入区）：
    //   src: base-search-box.tsx:132-133 disabled={!text}
    if (clear_button_ && !text_.empty() &&
        clear_button_rect().contains(e.pos.x, e.pos.y)) {
      clear_text();
      return true;
    }
    request_focus();
    int idx = hit_index(e.pos.x);
    cursor_ = anchor_ = idx;
    invalidate();
    return true;
  }
  if (e.type == MouseEventType::Move) {
    if (clear_button_) {
      bool h = !text_.empty() && clear_button_rect().contains(e.pos.x, e.pos.y);
      if (h != clear_hover_) {
        clear_hover_ = h;
        invalidate();
      }
    }
    return false;
  }
  if (e.type == MouseEventType::Leave) {
    if (clear_hover_) {
      clear_hover_ = false;
      invalidate();
    }
    return false;
  }
  if (e.type == MouseEventType::Up) {
    return true;
  }
  return false;
}

int TextField::hit_index(float x) {
  ITextEngine* eng = text_engine();
  if (!eng || text_.empty()) return 0;
  float content_x = x - 8 + scroll_x_;  // 8 = 内边距
  if (content_x <= 0) return 0;
  for (int i = 1; i <= static_cast<int>(text_.size()); ++i) {
    SizeF w = eng->measure_text(text_.substr(0, static_cast<size_t>(i)),
                                style_, 0);
    if (w.w >= content_x) {
      // 取较近边界
      SizeF wp = eng->measure_text(text_.substr(0, static_cast<size_t>(i - 1)),
                                   style_, 0);
      return (content_x - wp.w < w.w - content_x) ? i - 1 : i;
    }
  }
  return static_cast<int>(text_.size());
}

float TextField::cursor_x(int index) {
  // 文本内容坐标（不含 scroll 偏移）
  ITextEngine* eng = text_engine();
  if (!eng || index <= 0) return 8;
  SizeF w = eng->measure_text(text_.substr(0, static_cast<size_t>(index)),
                              style_, 0);
  return 8 + w.w;
}

void TextField::clamp_scroll() {
  if (bounds().w <= 0) return;
  float vis = bounds().w - 8 - text_right_pad();
  float cx = 8;
  if (ITextEngine* eng = text_engine()) {
    SizeF w = eng->measure_text(text_.substr(0, static_cast<size_t>(cursor_)),
                                style_, 0);
    cx = 8 + w.w;
  }
  if (cx - scroll_x_ > vis) scroll_x_ = cx - vis;      // 光标在右缘外
  if (cx - scroll_x_ < 0) scroll_x_ = cx;              // 光标在左缘外
  if (scroll_x_ < 0) scroll_x_ = 0;
}

// ---- 绘制 ----
void TextField::apply_theme() {
  const auto p = theme::ThemeService::instance().input_paint();
  bg_ = p.bg;
  border_ = p.border;
  border_focused_ = p.border_focused;
  border_w_ = p.border_w;
  border_w_focused_ = p.border_w_focused;
  radius_ = p.radius;
  placeholder_color_ = p.placeholder;
  style_.color = p.text;
  selection_bg_ = Color4f{p.border_focused.r, p.border_focused.g,
                          p.border_focused.b, 0.25f};  // ← MUI selection 主色 25%
  caret_color_ = p.border_focused;
  has_inset_ = p.has_inset;
  inset_color_ = p.inset_color;
  inset_dy_ = p.inset_dy;
  inset_blur_ = p.inset_blur;
  inset_top_hi_ = p.inset_top_hi;
  focus_glow_ = p.focus_glow;
  focus_glow_color_ = p.focus_glow_color;
  focus_glow_blur_ = p.focus_glow_blur;
  focus_ring_ = p.focus_ring;
  invalidate();
}

void TextField::on_paint(IRenderTarget& rt) {
  RectF r{0, 0, bounds().w, bounds().h};
  // 聚焦外发光 / 硬环（← get3DInputStyle &.Mui-focused boxShadow）
  if (focused() && focus_glow_ && focus_glow_color_.a > 0.004f)
    draw_glow(rt, r, radius_, focus_glow_color_, focus_glow_blur_);
  if (focused() && focus_ring_ > 0.01f) {
    // CSS 0 0 0 Npx color = 紧贴外沿、宽 N 的实心环（无模糊）
    RectF rr{r.x - focus_ring_ * 0.5f, r.y - focus_ring_ * 0.5f,
             r.w + focus_ring_, r.h + focus_ring_};
    rt.round_rect(rr, radius_ + focus_ring_ * 0.5f,
                  Pen{focus_glow_color_, focus_ring_}, nullptr);
  }
  Brush bg_b{bg_};
  const float bw = focused() && border_w_focused_ > 0.01f ? border_w_focused_
                                                          : border_w_;
  rt.round_rect(r, radius_, {focused() ? border_focused_ : border_, bw}, &bg_b);
  // inset 内凹阴影（← get3DInputStyle inset 0 dy blur color）
  if (has_inset_ && inset_color_.a > 0.004f) {
    draw_inner_glow(rt, r, radius_, inset_color_, inset_blur_);
    // CSS inset 的纵向偏移：把内阴影「上轻下重」的观感靠顶高光补齐
    if (inset_top_hi_.a > 0.004f)
      rt.draw_line({r.x + radius_, r.y + 0.5f},
                   {r.right() - radius_, r.y + 0.5f},
                   Pen{inset_top_hi_, 1.0f});
  }

  TextStyle ts = style_;
  ts.wrap = false;
  ts.ellipsis = false;
  // 右侧给清空按钮让位（src: base-search-box.tsx:123 input pr 4）
  RectF text_r{8, 0, r.w - 8 - text_right_pad(), r.h};
  ts.align_v = AlignV::Center;

  if (text_.empty() && composition_.empty()) {
    if (!placeholder_.empty()) {
      ts.color = placeholder_color_;
      rt.draw_text(placeholder_, text_r, ts);
    }
  } else {
    rt.push_clip(text_r);
    // 选区高亮
    int lo = std::min(cursor_, anchor_), hi = std::max(cursor_, anchor_);
    if (lo != hi && focused()) {
      float x0 = 8, x1 = 8;
      if (ITextEngine* eng = text_engine()) {
        x0 = 8 + eng->measure_text(text_.substr(0, static_cast<size_t>(lo)), ts, 0).w;
        x1 = 8 + eng->measure_text(text_.substr(0, static_cast<size_t>(hi)), ts, 0).w;
      }
      Brush sel_b{selection_bg_};
      rt.fill_rect({x0 - scroll_x_, 3, x1 - x0, r.h - 6}, sel_b);
    }
    // 文本 / composition 预编辑串
    if (!composition_.empty()) {
      rt.draw_text(composition_, {text_r.x - scroll_x_, 0, text_r.w, r.h}, ts);
    } else {
      rt.draw_text(text_, {text_r.x - scroll_x_, 0, text_r.w, r.h}, ts);
    }
    rt.pop_clip();
  }

  // 光标（focused 时）
  if (focused()) {
    float cx = cursor_x(cursor_) - scroll_x_;
    if (!composition_.empty()) {
      if (ITextEngine* eng = text_engine()) {
        cx = 8 + eng->measure_text(
                        composition_.substr(
                            0, static_cast<size_t>(composition_cursor_)),
                        ts, 0)
                      .w -
                  scroll_x_;
      }
    }
    rt.fill_rect({cx, 5, 1.5f, r.h - 10}, Brush{caret_color_});
  }

  // ✕ 清空按钮：空值 opacity .5（disabled），非空 1.0，悬停提亮
  // src: base-search-box.tsx:126-136（ClearRounded 18 / disabled / opacity）
  if (clear_button_) {
    TextStyle cts = ts;
    cts.size = 15.0f;  // ← ClearRounded fontSize 18 的视觉近似（图标框 24）
    cts.align_h = AlignH::Center;
    cts.align_v = AlignV::Center;
    cts.color = style_.color;
    cts.color.a = text_.empty() ? 0.5f : (clear_hover_ ? 1.0f : 0.9f);
    rt.draw_text(L"✕", clear_button_rect(), cts);
  }
}

}  // namespace clashwin::ui
