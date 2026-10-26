// TextField 实现（T10）：光标/选区/键盘/粘贴/绘制。
#include "textfield.h"

#include <windows.h>

#include <algorithm>

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
    request_focus();
    int idx = hit_index(e.pos.x);
    cursor_ = anchor_ = idx;
    scroll_x_ = scroll_x_;  // 点击不滚
    invalidate();
    return true;
  }
  if (e.type == MouseEventType::Move) {
    // 左键拖动扩选（简化：无 capture 时也生效）
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
  float vis = bounds().w - 16;
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
void TextField::on_paint(IRenderTarget& rt) {
  RectF r{0, 0, bounds().w, bounds().h};
  Brush bg_b{bg_};
  rt.round_rect(r, radius_, {focused() ? border_focused_ : border_, 1}, &bg_b);

  TextStyle ts = style_;
  ts.wrap = false;
  ts.ellipsis = false;
  RectF text_r{8, 0, r.w - 16, r.h};
  ts.align_v = AlignV::Center;

  if (text_.empty() && composition_.empty()) {
    if (!placeholder_.empty()) {
      ts.color = placeholder_color_;
      rt.draw_text(placeholder_, text_r, ts);
    }
    return;
  }

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
}

}  // namespace clashwin::ui
