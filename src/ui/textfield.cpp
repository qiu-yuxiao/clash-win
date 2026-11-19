// TextField 实现（T10）：光标/选区/键盘/粘贴/绘制。
#include "textfield.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>
#include <vector>

#include "soft_shadow.h"
#include "theme/theme.h"

namespace clashwin::ui {

namespace {
// ★ MUI 输入框行盒 = 23px 绝对值：InputBase 根 `lineHeight:'1.4375em'` 在 16px
//   根字号下结算为 23px。多行输入框（48 / 125）的行距、光标步进、选区高都按这个行盒走。
// src: node_modules/@mui/material/InputBase/InputBase.js:76-78
constexpr float kMuiRowH = 23.0f;

struct VisualLine {
  int start = 0;
  int end = 0;
};

std::vector<VisualLine> compute_visual_lines(ITextEngine* eng,
                                             const std::wstring& text,
                                             float avail_w,
                                             const TextStyle& ts) {
  std::vector<VisualLine> out;
  const int len = static_cast<int>(text.size());
  int pos = 0;
  while (pos <= len) {
    int nl = len;
    for (int i = pos; i < len; ++i) {
      if (text[static_cast<size_t>(i)] == L'\n') {
        nl = i;
        break;
      }
    }
    if (pos == nl) {
      out.push_back({pos, pos});
    } else if (!eng || avail_w <= 16.0f) {
      out.push_back({pos, nl});
    } else {
      int cur = pos;
      for (int i = pos + 1; i <= nl; ++i) {
        const float w = eng->measure_text(
            text.substr(static_cast<size_t>(cur), static_cast<size_t>(i - cur)),
            ts, 0).w;
        if (w > avail_w && i - 1 > cur) {
          out.push_back({cur, i - 1});
          cur = i - 1;
        }
      }
      out.push_back({cur, nl});
    }
    if (nl == len) break;
    pos = nl + 1;
  }
  if (out.empty()) out.push_back({0, 0});
  return out;
}

int find_visual_line_index(const std::vector<VisualLine>& lines, int cursor_idx) {
  for (int r = 0; r < static_cast<int>(lines.size()); ++r) {
    const VisualLine& vl = lines[static_cast<size_t>(r)];
    if (cursor_idx < vl.start) return std::max(0, r - 1);
    if (cursor_idx >= vl.start && cursor_idx <= vl.end) {
      if (cursor_idx == vl.end && r + 1 < static_cast<int>(lines.size()) &&
          lines[static_cast<size_t>(r + 1)].start == cursor_idx) {
        return r + 1;
      }
      return r;
    }
  }
  return std::max(0, static_cast<int>(lines.size()) - 1);
}

std::wstring normalize_clipboard_text(std::wstring s, bool multiline) {
  std::wstring out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == L'\r') {
      if (i + 1 < s.size() && s[i + 1] == L'\n') ++i;
      if (multiline) out.push_back(L'\n');
    } else if (s[i] == L'\n') {
      if (multiline) out.push_back(L'\n');
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

std::function<std::wstring()>& clip_get() {
  static std::function<std::wstring()> f;
  return f;
}
std::function<void(const std::wstring&)>& clip_set() {
  static std::function<void(const std::wstring&)> f;
  return f;
}
std::wstring clip_get_str() {
  if (clip_get()) return clip_get()();
  if (!OpenClipboard(nullptr)) return L"";
  std::wstring out;
  if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
    if (const auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
      out = p;
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return out;
}
void clip_set_str(const std::wstring& s) {
  if (clip_set()) {
    clip_set()(s);
    return;
  }
  if (!OpenClipboard(nullptr)) return;
  EmptyClipboard();
  const size_t bytes = (s.size() + 1) * sizeof(wchar_t);
  if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
    if (auto* p = static_cast<wchar_t*>(GlobalLock(h))) {
      memcpy(p, s.c_str(), bytes);
      GlobalUnlock(h);
      SetClipboardData(CF_UNICODETEXT, h);
    } else {
      GlobalFree(h);
    }
  }
  CloseClipboard();
}
}  // namespace

void TextField::set_clipboard_hooks(std::function<std::wstring()> get_fn,
                                    std::function<void(const std::wstring&)> set_fn) {
  clip_get() = std::move(get_fn);
  clip_set() = std::move(set_fn);
}

void TextField::set_text(const std::wstring& t) {
  const bool changed = (text_ != t);
  text_ = t;
  cursor_ = anchor_ = static_cast<int>(t.size());
  scroll_x_ = 0;
  invalidate();
  if (changed && on_text_changed) on_text_changed(text_);
}

void TextField::set_text_silent(const std::wstring& t) {
  // 状态迁移与 set_text 完全一致，但刻意不触发 on_text_changed。
  // ← Mini base-search-box.tsx:95-97（受控 value 的 useEffect(emitSearch)→onSearch
  //   通道，不经 proxy-head.tsx:213 onChangeText）；Win 侧 on_text_changed 承载
  //   700ms 去抖自动测速择优，程序化回填须绕开，否则重启自启会误触发测速。
  text_ = t;
  cursor_ = anchor_ = static_cast<int>(t.size());
  scroll_x_ = 0;
  invalidate();
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
  constexpr float kBox = 24.0f;  // src: src/components/base/base-search-box.tsx:126
  return {bounds().w - 4.0f - kBox, (bounds().h - kBox) * 0.5f, kBox, kBox};  // src: src/components/base/base-search-box.tsx:126
}

void TextField::set_end_spinner(bool visible, std::wstring tooltip) {
  if (end_spinner_visible_ == visible && end_spinner_tooltip_ == tooltip) return;
  end_spinner_visible_ = visible;
  end_spinner_tooltip_ = std::move(tooltip);
  if (!visible) spinner_hover_ = false;
  clamp_scroll();
  invalidate();
}

RectF TextField::end_spinner_rect() const {
  constexpr float kSpin = 11.0f;  // src: src/components/proxy/proxy-head.tsx:260
  const float right_edge = clear_button_ ? clear_button_rect().x : (bounds().w - 4.0f);  // src: src/components/proxy/proxy-head.tsx:257
  return {right_edge - 2.0f - kSpin, (bounds().h - kSpin) * 0.5f, kSpin, kSpin};  // src: src/components/proxy/proxy-head.tsx:257
}

float TextField::text_right_pad() const {
  float base = clear_button_ ? 32.0f : pad_x_;  // src: src/components/base/base-search-box.tsx:123
  if (end_spinner_visible_) base += 15.0f;  // src: src/components/proxy/proxy-head.tsx:257
  return std::max(base, right_pad_);
}

std::wstring TextField::tooltip_text() const {
  if (end_spinner_visible_ && spinner_hover_ && !end_spinner_tooltip_.empty()) {
    return end_spinner_tooltip_;
  }
  if (clear_button_ && clear_hover_) return clear_tooltip_;
  return Widget::tooltip_text();
}

std::optional<RectF> TextField::tooltip_anchor_rect() const {
  if (end_spinner_visible_ && spinner_hover_) {
    const RectF g = global_bounds();
    const RectF s = end_spinner_rect();
    return RectF{g.x + s.x, g.y + s.y, s.w, s.h};
  }
  if (clear_button_ && clear_hover_) {
    const RectF g = global_bounds();
    const RectF c = clear_button_rect();
    return RectF{g.x + c.x, g.y + c.y, c.w, c.h};
  }
  return std::nullopt;
}

// ---- 编辑命令（EM_* 等价） ----
bool TextField::can_cut() const { return has_selection(); }
bool TextField::can_copy() const { return has_selection(); }
bool TextField::can_paste() const { return true; }  // src: _layout.tsx:1908 粘贴项无 disabled
bool TextField::can_select_all() const { return !text_.empty(); }
bool TextField::can_clear() const { return !text_.empty(); }

void TextField::cut() {
  if (!has_selection()) return;
  clip_set_str(selection());
  erase_selection();
}

void TextField::copy() {
  if (!has_selection()) return;
  clip_set_str(selection());
}

void TextField::paste() {
  const std::wstring s = normalize_clipboard_text(clip_get_str(), multiline_);
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

void TextField::set_multiline(bool multi, int min_rows, int max_rows) {
  multiline_ = multi;
  min_rows_ = std::max(1, min_rows);
  max_rows_ = std::max(min_rows_, max_rows);
  scroll_y_ = 0.0f;
  invalidate();
}

float TextField::preferred_height(float width) const {
  if (!multiline_) return height_;
  // src: src/pages/_layout/components/profile-import-card.tsx:140,164 minRows/maxRows
  const float avail_w = std::max(0.0f, width - pad_x_ - text_right_pad());
  TextStyle ts = style_;
  ts.size = (style_.size > 0) ? style_.size : 13;
  const auto lines = compute_visual_lines(text_engine(), text_, avail_w, ts);
  const int rows = std::clamp(static_cast<int>(lines.size()), min_rows_, max_rows_);
  return multiline_pad_y_ * 2.0f + static_cast<float>(rows) * kMuiRowH;
}

SizeF TextField::on_measure(float max_w, float max_h) {
  (void)max_h;
  const float w = max_w > 0 ? max_w : 120;
  return {w, multiline_ ? preferred_height(w) : height_};
}

// ---- 键盘 ----
bool TextField::on_key(const KeyEvent& e) {
  if (on_key_intercept && on_key_intercept(e)) return true;
  if (e.type == KeyEventType::Char) {
    if (e.ctrl) return false;  // Ctrl 组合键非字符输入
    if (e.ch >= 0x20) {
      insert_text(std::wstring(1, e.ch));
      return true;
    }
    if (e.ch == L'\r') {
      if (multiline_) {
        insert_text(L"\n");
      } else if (on_submit) {
        on_submit();
      }
      return true;
    }
    return false;
  }
  if (e.type != KeyEventType::Down) return false;

  int len = static_cast<int>(text_.size());
  int lo = std::min(cursor_, anchor_), hi = std::max(cursor_, anchor_);
  bool has_sel = lo != hi;

  switch (e.key) {
    case VK_RETURN:
      if (multiline_ && e.ctrl) {
        if (on_submit) on_submit();
        return true;
      }
      return false;
    case VK_UP:
    case VK_DOWN: {
      if (!multiline_) return false;
      const float avail_w = std::max(0.0f, bounds().w - pad_x_ - text_right_pad());
      TextStyle ts = style_;
      ts.size = (style_.size > 0) ? style_.size : 13;
      ITextEngine* eng = text_engine();
      const auto lines = compute_visual_lines(eng, text_, avail_w, ts);
      const int row = find_visual_line_index(lines, cursor_);
      const int next_row = std::clamp(row + (e.key == VK_UP ? -1 : 1), 0,
                                      static_cast<int>(lines.size()) - 1);
      if (next_row == row) return true;
      const VisualLine& cur_vl = lines[static_cast<size_t>(row)];
      const float target_x =
          eng ? eng->measure_text(
                       text_.substr(static_cast<size_t>(cur_vl.start),
                                    static_cast<size_t>(std::max(0, cursor_ - cur_vl.start))),
                       ts, 0).w
              : 0.0f;
      const VisualLine& dst_vl = lines[static_cast<size_t>(next_row)];
      int target_idx = dst_vl.start;
      if (eng && dst_vl.end > dst_vl.start) {
        float prev_w = 0.0f;
        target_idx = dst_vl.end;
        for (int i = dst_vl.start + 1; i <= dst_vl.end; ++i) {
          const float w = eng->measure_text(
              text_.substr(static_cast<size_t>(dst_vl.start),
                           static_cast<size_t>(i - dst_vl.start)),
              ts, 0).w;
          if (w >= target_x) {
            target_idx = (target_x - prev_w < w - target_x) ? (i - 1) : i;
            break;
          }
          prev_w = w;
        }
      }
      cursor_ = target_idx;
      if (!e.shift) anchor_ = cursor_;
      clamp_scroll();
      invalidate();
      return true;
    }
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
      if (e.ctrl && has_sel) {
        clip_set_str(selection());
        return true;
      }
      return false;
    case 'V':
      if (e.ctrl) {
        const std::wstring s = normalize_clipboard_text(clip_get_str(), multiline_);
        if (!s.empty()) insert_text(s);
        return true;
      }
      return false;
    case 'X':
      if (e.ctrl && has_sel) {
        clip_set_str(selection());
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
  if (e.type == MouseEventType::Wheel) {
    if (!multiline_) return false;
    const float avail_w = std::max(0.0f, bounds().w - pad_x_ - text_right_pad());
    TextStyle ts = style_;
    ts.size = (style_.size > 0) ? style_.size : 13;
    const auto lines = compute_visual_lines(text_engine(), text_, avail_w, ts);
    const float total_h = static_cast<float>(lines.size()) * kMuiRowH;
    const float view_h = std::max(0.0f, bounds().h - multiline_pad_y_ * 2.0f);
    const float max_scroll = std::max(0.0f, total_h - view_h);
    const float step = (static_cast<float>(e.wheel_delta) / 120.0f) * kMuiRowH;
    const float next_sy = std::clamp(scroll_y_ - step, 0.0f, max_scroll);
    if (next_sy != scroll_y_) {
      scroll_y_ = next_sy;
      invalidate();
    }
    return true;
  }
  if (e.type == MouseEventType::Enter) {
    if (!hover_) {
      hover_ = true;
      invalidate();
    }
    return false;
  }
  if (e.type == MouseEventType::Down) {
    if (e.buttons & kMouseRight) {
      // 右键：焦点 + 选区外点击移动光标（Win32 编辑控件行为），再交给宿主弹菜单
      request_focus();
      if (right_pad_ > 0 && (e.buttons & kMouseRight) == 0 && e.pos.x >= bounds().w - right_pad_) {
        request_focus();
        return true;
      }
      int idx = multiline_ ? hit_index_xy(e.pos.x, e.pos.y) : hit_index(e.pos.x);
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
    if (right_pad_ > 0 && e.pos.x >= bounds().w - right_pad_) {
      request_focus();
      return true;
    }
    int idx = multiline_ ? hit_index_xy(e.pos.x, e.pos.y) : hit_index(e.pos.x);
    cursor_ = anchor_ = idx;
    clamp_scroll();
    invalidate();
    return true;
  }
  if (e.type == MouseEventType::Move) {
    if (!hover_) {
      hover_ = true;
      invalidate();
    }
    if (right_pad_ > 0) {
      bool eh = e.pos.x >= bounds().w - right_pad_;
      if (eh != enter_hover_) {
        enter_hover_ = eh;
        invalidate();
      }
    }
    if ((e.buttons & kMouseLeft) && focused()) {
      cursor_ = multiline_ ? hit_index_xy(e.pos.x, e.pos.y) : hit_index(e.pos.x);
      clamp_scroll();
      invalidate();
      return true;
    }
    if (end_spinner_visible_) {
      bool sh = end_spinner_rect().contains(e.pos.x, e.pos.y);
      if (sh != spinner_hover_) {
        spinner_hover_ = sh;
        invalidate();
      }
    }
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
    bool changed = false;
    if (hover_) {
      hover_ = false;
      changed = true;
    }
    if (enter_hover_) {
      enter_hover_ = false;
      changed = true;
    }
    if (spinner_hover_) {
      spinner_hover_ = false;
      changed = true;
    }
    if (clear_hover_) {
      clear_hover_ = false;
      changed = true;
    }
    if (changed) invalidate();
    return false;
  }
  if (e.type == MouseEventType::Up) {
    if (right_pad_ > 0 && e.pos.x >= bounds().w - right_pad_ && on_submit) on_submit();
    return true;
  }
  return false;
}

int TextField::hit_index(float x) {
  ITextEngine* eng = text_engine();
  if (!eng || text_.empty()) return 0;
  float content_x = x - pad_x_ + scroll_x_;
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

int TextField::hit_index_xy(float x, float y) {
  ITextEngine* eng = text_engine();
  if (text_.empty()) return 0;
  const float avail_w = std::max(0.0f, bounds().w - pad_x_ - text_right_pad());
  TextStyle ts = style_;
  ts.size = (style_.size > 0) ? style_.size : 13;
  const auto lines = compute_visual_lines(eng, text_, avail_w, ts);
  const float rel_y = y - multiline_pad_y_ + scroll_y_;
  const int row = std::clamp(static_cast<int>(std::floor(rel_y / kMuiRowH)), 0,
                             static_cast<int>(lines.size()) - 1);
  const VisualLine& vl = lines[static_cast<size_t>(row)];
  const float content_x = x - pad_x_;
  if (!eng || content_x <= 0.0f || vl.start == vl.end) return vl.start;
  float prev_w = 0.0f;
  for (int i = vl.start + 1; i <= vl.end; ++i) {
    const float w = eng->measure_text(
        text_.substr(static_cast<size_t>(vl.start), static_cast<size_t>(i - vl.start)),
        ts, 0).w;
    if (w >= content_x) {
      return (content_x - prev_w < w - content_x) ? (i - 1) : i;
    }
    prev_w = w;
  }
  return vl.end;
}

float TextField::cursor_x(int index) {
  // 文本内容坐标（不含 scroll 偏移）
  ITextEngine* eng = text_engine();
  if (!eng || index <= 0) return pad_x_;
  SizeF w = eng->measure_text(text_.substr(0, static_cast<size_t>(index)),
                              style_, 0);
  return pad_x_ + w.w;
}

PointF TextField::caret_point() const {
  const RectF r = bounds();
  const float right_pad = text_right_pad();
  const RectF text_r{pad_x_, 0, std::max(0.0f, r.w - pad_x_ - right_pad), r.h};
  TextStyle ts = style_;
  ts.size = (style_.size > 0) ? style_.size : 13;

  float cx = const_cast<TextField*>(this)->cursor_x(cursor_) - scroll_x_;
  if (!composition_.empty()) {
    if (ITextEngine* eng = text_engine()) {
      cx = pad_x_ + eng->measure_text(
                            composition_.substr(
                                0, static_cast<size_t>(composition_cursor_)),
                            ts, 0)
                          .w -
                scroll_x_;
    }
  }
  const float line_h = multiline_
                           ? kMuiRowH
                           : std::min(r.h - 6.0f, std::round(ts.size * 1.4375f));
  // 多行：光标高沿用 lineHeight 折算值（13→19），在 23px 行盒内垂直居中
  const float caret_h =
      multiline_
          ? std::min(r.h - multiline_pad_y_ * 2.0f, std::round(ts.size * 1.4375f))
          : line_h;
  float cy = multiline_ ? multiline_pad_y_ + (kMuiRowH - caret_h) * 0.5f
                        : std::max(3.0f, (r.h - line_h) * 0.5f);
  if (multiline_ && composition_.empty()) {
    ITextEngine* eng = text_engine();
    const auto lines = compute_visual_lines(eng, text_, text_r.w, ts);
    const int row = find_visual_line_index(lines, cursor_);
    const VisualLine& vl = lines[static_cast<size_t>(row)];
    cy = multiline_pad_y_ - scroll_y_ + static_cast<float>(row) * line_h +
         (kMuiRowH - caret_h) * 0.5f;
    cx = pad_x_ + (eng ? eng->measure_text(
                                 text_.substr(static_cast<size_t>(vl.start),
                                              static_cast<size_t>(std::max(0, cursor_ - vl.start))),
                                 ts, 0).w
                       : 0.0f);
  }

  RectF gb = global_bounds();
  return {gb.x + cx, gb.y + cy};
}

float TextField::caret_height() const {
  TextStyle ts = style_;
  ts.size = (style_.size > 0) ? style_.size : 13;
  if (multiline_)  // 23px 行盒内的光标高（沿用 lineHeight 折算值，V6）
    return std::min(bounds().h - multiline_pad_y_ * 2.0f, std::round(ts.size * 1.4375f));
  return std::min(bounds().h - 6.0f, std::round(ts.size * 1.4375f));
}

void TextField::clamp_scroll() {
  if (bounds().w <= 0) return;
  const float avail_w = std::max(0.0f, bounds().w - pad_x_ - text_right_pad());
  if (multiline_) {
    scroll_x_ = 0.0f;
    TextStyle ts = style_;
    ts.size = (style_.size > 0) ? style_.size : 13;
    const auto lines = compute_visual_lines(text_engine(), text_, avail_w, ts);
    const int row = find_visual_line_index(lines, cursor_);
    const float view_h = std::max(kMuiRowH, bounds().h - multiline_pad_y_ * 2.0f);
    const float total_h = static_cast<float>(lines.size()) * kMuiRowH;
    const float top = static_cast<float>(row) * kMuiRowH;
    const float bot = top + kMuiRowH;
    if (bot - scroll_y_ > view_h) scroll_y_ = bot - view_h;
    if (top < scroll_y_) scroll_y_ = top;
    const float max_scroll = std::max(0.0f, total_h - view_h);
    scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll);
    return;
  }
  float cx = pad_x_;
  if (ITextEngine* eng = text_engine()) {
    SizeF w = eng->measure_text(text_.substr(0, static_cast<size_t>(cursor_)),
                                style_, 0);
    cx = pad_x_ + w.w;
  }
  if (cx - scroll_x_ > avail_w) scroll_x_ = cx - avail_w;  // 光标在右缘外
  if (cx - scroll_x_ < 0) scroll_x_ = cx;                  // 光标在左缘外
  if (scroll_x_ < 0) scroll_x_ = 0;
}

// ---- 绘制 ----
void TextField::apply_theme() {
  const auto p = theme::ThemeService::instance().input_paint();
  bg_ = p.bg;
  border_ = p.border;
  border_hover_ = p.border_hover;
  border_focused_ = p.border_focused;
  border_w_ = p.border_w;
  border_w_focused_ = p.border_w_focused;
  radius_ = p.radius;
  placeholder_color_ = p.placeholder;
  style_.color = p.text;
  // src: index.scss:28-31 ::selection 背景 = var(--primary-main)（不透明），
  //      选中文字 = var(--selection-color)（use-custom-theme.ts:365 selectColor 亮 #f5f5f5
  //      / 暗 #3E3E3E，:371 setProperty('--selection-color')）
  selection_bg_ = theme::ThemeService::instance().primary();
  selection_text_ = theme::ThemeService::instance().vars().selection_color;
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
  // src: src/utils/button-styles.ts:757-876 hover notchedOutline 描边色
  const Color4f bcol = focused()
                           ? border_focused_
                           : ((hover_ && border_hover_.a > 0.001f) ? border_hover_ : border_);
  rt.round_rect(r, radius_, {bcol, bw}, &bg_b);
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
  RectF text_r{pad_x_, 0, r.w - pad_x_ - text_right_pad(), r.h};
  ts.align_v = AlignV::Center;

  if (text_.empty() && composition_.empty()) {
    if (!placeholder_.empty()) {
      ts.color = placeholder_color_;
      if (multiline_) {
        ts.wrap = true;
        ts.align_v = AlignV::Top;
        ts.line_h = kMuiRowH;  // 占位符与正文同行盒（V6）
        // src: profile-import-card.tsx:164 '& .MuiInputBase-input': { py: '4px' }
        rt.draw_text(placeholder_,
                     {text_r.x, multiline_pad_y_, text_r.w, r.h - multiline_pad_y_ * 2.0f},
                     ts);
      } else {
        rt.draw_text(placeholder_, text_r, ts);
      }
    }
  } else if (multiline_ && composition_.empty()) {
    // src: src/pages/_layout/components/profile-import-card.tsx:140-165 & add-url-dialog.tsx:222
    const RectF clip_r{text_r.x, multiline_pad_y_, std::max(0.0f, text_r.w),
                       std::max(0.0f, r.h - multiline_pad_y_ * 2.0f)};
    rt.push_clip(clip_r);
    TextStyle lts = ts;
    lts.size = (style_.size > 0) ? style_.size : 13;
    lts.wrap = false;
    lts.align_v = AlignV::Center;
    ITextEngine* eng = text_engine();
    const auto lines = compute_visual_lines(eng, text_, text_r.w, lts);
    const int lo = std::min(cursor_, anchor_), hi = std::max(cursor_, anchor_);
    const bool has_sel = (lo != hi && focused());
    for (int row = 0; row < static_cast<int>(lines.size()); ++row) {
      const float ly = multiline_pad_y_ - scroll_y_ + static_cast<float>(row) * kMuiRowH;
      if (ly + kMuiRowH < clip_r.y || ly > clip_r.bottom()) continue;
      const VisualLine& vl = lines[static_cast<size_t>(row)];
      const std::wstring line_str =
          text_.substr(static_cast<size_t>(vl.start), static_cast<size_t>(vl.end - vl.start));
      float sx0 = 0.0f, sx1 = 0.0f;
      if (has_sel && lo <= vl.end && hi >= vl.start) {
        const int s0 = std::clamp(lo, vl.start, vl.end);
        const int s1 = std::clamp(hi, vl.start, vl.end);
        const float x0 =
            (eng && s0 > vl.start)
                ? eng->measure_text(line_str.substr(0, static_cast<size_t>(s0 - vl.start)), lts, 0).w
                : 0.0f;
        const float x1 =
            (eng && s1 > vl.start)
                ? eng->measure_text(line_str.substr(0, static_cast<size_t>(s1 - vl.start)), lts, 0).w
                : 0.0f;
        if (s1 > s0) {
          sx0 = text_r.x + x0;
          sx1 = text_r.x + x1;
          rt.fill_rect({sx0, ly, sx1 - sx0, kMuiRowH}, Brush{selection_bg_});
        }
      }
      rt.draw_text(line_str, {text_r.x, ly, text_r.w, kMuiRowH}, lts);
      if (has_sel && sx1 > sx0) {
        rt.push_clip({sx0, ly, sx1 - sx0, kMuiRowH});
        TextStyle sel_ts = lts;
        sel_ts.color = selection_text_;
        rt.draw_text(line_str, {text_r.x, ly, text_r.w, kMuiRowH}, sel_ts);
        rt.pop_clip();
      }
    }
    rt.pop_clip();
  } else {
    rt.push_clip(text_r);
    // 选区高亮：src: index.scss:28-31 ::selection 背景 = var(--primary-main)（不透明）
    int lo = std::min(cursor_, anchor_), hi = std::max(cursor_, anchor_);
    const bool has_sel = (lo != hi && focused());
    float sx0 = 0.0f, sx1 = 0.0f;
    if (has_sel) {
      float x0 = pad_x_, x1 = pad_x_;
      if (ITextEngine* eng = text_engine()) {
        x0 = pad_x_ + eng->measure_text(text_.substr(0, static_cast<size_t>(lo)), ts, 0).w;
        x1 = pad_x_ + eng->measure_text(text_.substr(0, static_cast<size_t>(hi)), ts, 0).w;
      }
      sx0 = x0 - scroll_x_;
      sx1 = x1 - scroll_x_;
      Brush sel_b{selection_bg_};
      const float sel_h = std::min(r.h - 6.0f, std::round(ts.size * 1.4375f));
      const float sel_y = (r.h - sel_h) * 0.5f;
      rt.fill_rect({sx0, sel_y, sx1 - sx0, sel_h}, sel_b);
    }
    // 文本 / composition 预编辑串
    const RectF text_rect{text_r.x - scroll_x_, 0, text_r.w, r.h};
    if (!composition_.empty()) {
      rt.draw_text(composition_, text_rect, ts);
    } else {
      rt.draw_text(text_, text_rect, ts);
      // src: index.scss:29 ::selection { color: var(--selection-color) }：
      //   选区内的文字需以 --selection-color 重绘（剪裁到高亮矩形）
      if (has_sel && sx1 > sx0) {
        rt.push_clip({sx0, 0, sx1 - sx0, r.h});
        TextStyle sel_ts = ts;
        sel_ts.color = selection_text_;
        rt.draw_text(text_, text_rect, sel_ts);
        rt.pop_clip();
      }
    }
    rt.pop_clip();
  }

  // 光标（focused 时）
  if (focused()) {
    float cx = cursor_x(cursor_) - scroll_x_;
    if (!composition_.empty()) {
      if (ITextEngine* eng = text_engine()) {
        cx = pad_x_ + eng->measure_text(
                              composition_.substr(
                                  0, static_cast<size_t>(composition_cursor_)),
                              ts, 0)
                            .w -
                  scroll_x_;
      }
    }
    // src: profile-import-card.tsx:145-165 单行行高 13px * 1.4375，multiline 时光标按折行落位且保持单行高度
    const float line_h = multiline_
                             ? kMuiRowH
                             : std::min(r.h - 6.0f, std::round(ts.size * 1.4375f));
    const float caret_h =
        multiline_ ? std::min(r.h - multiline_pad_y_ * 2.0f, std::round(ts.size * 1.4375f))
                   : line_h;
    float cy = multiline_ ? multiline_pad_y_ + (kMuiRowH - caret_h) * 0.5f
                          : std::max(3.0f, (r.h - line_h) * 0.5f);
    if (multiline_ && composition_.empty()) {
      TextStyle lts = ts;
      lts.size = (style_.size > 0) ? style_.size : 13;
      ITextEngine* eng = text_engine();
      const auto lines = compute_visual_lines(eng, text_, text_r.w, lts);
      const int row = find_visual_line_index(lines, cursor_);
      const VisualLine& vl = lines[static_cast<size_t>(row)];
      cy = multiline_pad_y_ - scroll_y_ + static_cast<float>(row) * line_h +
           (kMuiRowH - caret_h) * 0.5f;
      cx = pad_x_ + (eng ? eng->measure_text(
                                   text_.substr(static_cast<size_t>(vl.start),
                                                static_cast<size_t>(std::max(0, cursor_ - vl.start))),
                                   lts, 0).w
                         : 0.0f);
    }
    if (!multiline_ || (cy + caret_h >= multiline_pad_y_ && cy <= r.h - multiline_pad_y_)) {
      rt.fill_rect({cx, cy, 1.5f, caret_h}, Brush{caret_color_});
    }
  }

  // BUG-360: 尾部加载旋转环（src: src/components/proxy/proxy-head.tsx:250-263）
  if (end_spinner_visible_) {
    const RectF sr = end_spinner_rect();
    const float cx = sr.x + sr.w * 0.5f;
    const float cy = sr.y + sr.h * 0.5f;
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
    const double phase = std::fmod(ms / 1400.0, 1.0);  // src: src/components/proxy/proxy-head.tsx:260
    const Color4f warn = theme::ThemeService::instance().palette().warning_color;  // src: src/components/proxy/proxy-head.tsx:256
    for (int i = 0; i < 8; ++i) {  // src: src/components/proxy/proxy-head.tsx:260
      const double ang = (phase + static_cast<double>(i) / 8.0) * 6.283185307;  // src: src/components/proxy/proxy-head.tsx:260
      constexpr float kDot = 1.8f;  // src: src/components/proxy/proxy-head.tsx:260
      constexpr float kRad = 4.2f;  // src: src/components/proxy/proxy-head.tsx:260
      Color4f ink = warn;
      ink.a = 0.9f * (1.0f - static_cast<float>(i) / 8.0f);  // src: src/components/proxy/proxy-head.tsx:260
      Brush db{ink};
      rt.round_rect({cx + static_cast<float>(std::cos(ang)) * kRad - kDot * 0.5f,
                     cy + static_cast<float>(std::sin(ang)) * kRad - kDot * 0.5f,
                     kDot, kDot},
                    kDot * 0.5f, Pen{ink, 0.0f}, &db);  // src: src/components/proxy/proxy-head.tsx:260
    }
    invalidate();
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
  if (right_pad_ > 0) {
    // src: first-run-guide-dialog.tsx:713-728 endAdornment KeyboardReturn 16px
    TextStyle ets = ts;
    ets.size = 16.0f;  // src: first-run-guide-dialog.tsx:725 fontSize 16
    ets.align_h = AlignH::Center;
    ets.align_v = AlignV::Center;
    ets.color = enter_hover_ ? theme::ThemeService::instance().primary() : placeholder_color_;
    rt.draw_text(L"⏎", {r.w - right_pad_, 0, right_pad_, r.h}, ets);
  }
}

}  // namespace clashwin::ui
