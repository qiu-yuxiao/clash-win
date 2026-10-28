// TextViewer 实现（T10 / T21）：分行缓存 + 可视行绘制 + 行号栏 + 换行切换。
#include "text_viewer.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <string>

#include "util/utf.h"

namespace clashwin::ui {

namespace utf = clashwin::utf;

TextViewer::TextViewer(Widget* parent) : Widget(parent) {
  style_.font_family = L"Consolas";
  style_.size = 12.0f;
  line_height_ = style_.size * line_spacing;
}

void TextViewer::set_show_line_numbers(bool show) {
  if (show_line_numbers_ == show) return;
  show_line_numbers_ = show;
  invalidate();
}

void TextViewer::set_word_wrap(bool wrap) {
  if (word_wrap_ == wrap) return;
  word_wrap_ = wrap;
  invalidate();
}

float TextViewer::gutter_width() const {
  if (!show_line_numbers_) return 0.0f;
  size_t n = std::max<size_t>(1, lines_.size());
  size_t digits = std::to_string(n).length();
  return std::max(36.0f, static_cast<float>(digits) * 8.0f + 16.0f);
}

void TextViewer::open_in_system_editor(const std::wstring& file_path) {
  if (on_open_in_editor) {
    on_open_in_editor();
  }
  std::wstring target = file_path.empty() ? file_path_ : file_path;
  if (!target.empty()) {
    ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  }
}

void TextViewer::set_text(const std::string& utf8) {
  lines_.clear();
  scroll_y_ = 0;
  // CRLF/CR/LF 归一拆行
  std::string s;
  s.reserve(utf8.size());
  for (char c : utf8) {
    if (c == '\r') continue;
    s.push_back(c);
  }
  size_t start = 0;
  while (true) {
    size_t pos = s.find('\n', start);
    std::string line =
        pos == std::string::npos ? s.substr(start) : s.substr(start, pos - start);
    lines_.push_back(utf::utf8_to_utf16(line));
    if (pos == std::string::npos) break;
    start = pos + 1;
  }
  if (ITextEngine* e = text_engine()) {
    SizeF m = e->measure_text(L"1234567890", style_, 0);
    if (m.h > 0) line_height_ = style_.size * line_spacing;
  }
  invalidate();
}

float TextViewer::max_scroll() const {
  float content = static_cast<float>(lines_.size()) * line_height_;
  return content > bounds().h ? content - bounds().h : 0;
}

void TextViewer::set_scroll_offset(float v) {
  float m = max_scroll();
  v = v < 0 ? 0 : (v > m ? m : v);
  if (scroll_y_ == v) return;
  scroll_y_ = v;
  invalidate();
}

SizeF TextViewer::on_measure(float max_w, float max_h) {
  return {max_w > 0 ? max_w : 200,
          max_h > 0 && max_h < 1e5 ? max_h : 200};
}

bool TextViewer::on_mouse(const MouseEvent& e) {
  if (e.type == MouseEventType::Wheel) {
    float lines = static_cast<float>(e.wheel_delta) / 120.0f * 3.0f;
    set_scroll_offset(scroll_y_ + lines * line_height_);  // 正 delta = 向下滚
    return true;
  }
  return false;
}

void TextViewer::on_paint(IRenderTarget& rt) {
  // 1. 全局背景（局部坐标）
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{bg_});

  float gw = gutter_width();

  // 2. 行号栏背景与分隔线
  if (show_line_numbers_ && gw > 0) {
    rt.fill_rect({0, 0, gw, bounds().h}, Brush{gutter_bg_});
    rt.fill_rect({gw - 1.0f, 0, 1.0f, bounds().h}, Brush{divider_color_});
  }

  // 3. 绘制可视行（行号 + 文本）
  int first = static_cast<int>(scroll_y_ / line_height_);
  int visible = static_cast<int>(bounds().h / line_height_) + 2;

  TextStyle num_ts = style_;
  num_ts.color = gutter_text_;
  num_ts.align_h = AlignH::Right;
  num_ts.align_v = AlignV::Center;

  TextStyle text_ts = style_;
  text_ts.color = text_;
  text_ts.wrap = word_wrap_;
  text_ts.ellipsis = !word_wrap_;
  text_ts.align_v = AlignV::Center;

  // 绘制文本带 clip
  rt.push_clip({gw, 0, std::max(0.0f, bounds().w - gw), bounds().h});
  for (int i = first; i < first + visible && i < static_cast<int>(lines_.size()); ++i) {
    if (i < 0) continue;
    float y = static_cast<float>(i) * line_height_ - scroll_y_;

    // 绘制文本内容
    rt.draw_text(lines_[static_cast<size_t>(i)],
                 {gw + pad_, y, std::max(0.0f, bounds().w - gw - pad_ * 2), line_height_},
                 text_ts);
  }
  rt.pop_clip();

  // 绘制行号（在 clip 之外，单独绘制）
  if (show_line_numbers_ && gw > 0) {
    rt.push_clip({0, 0, gw, bounds().h});
    for (int i = first; i < first + visible && i < static_cast<int>(lines_.size()); ++i) {
      if (i < 0) continue;
      float y = static_cast<float>(i) * line_height_ - scroll_y_;
      std::wstring num_str = std::to_wstring(i + 1);
      rt.draw_text(num_str, {0, y, gw - 8.0f, line_height_}, num_ts);
    }
    rt.pop_clip();
  }
}

}  // namespace clashwin::ui
