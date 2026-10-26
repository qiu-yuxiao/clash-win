// TextViewer 实现（T10）：分行缓存 + 可视行绘制。
#include "text_viewer.h"

#include <algorithm>

#include "util/utf.h"

namespace clashwin::ui {

namespace utf = clashwin::utf;

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
    SizeF m = e->measure_text(L"中Wgj", style_, 0);
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
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{bg_});
  rt.push_clip({0, 0, bounds().w, bounds().h});
  int first = static_cast<int>(scroll_y_ / line_height_);
  int visible = static_cast<int>(bounds().h / line_height_) + 1;
  TextStyle ts = style_;
  ts.color = text_;
  for (int i = first; i < first + visible && i < static_cast<int>(lines_.size());
       ++i) {
    if (i < 0) continue;
    float y = static_cast<float>(i) * line_height_ - scroll_y_;
    ts.ellipsis = true;
    rt.draw_text(lines_[static_cast<size_t>(i)],
                 {pad_, y, bounds().w - pad_ * 2, line_height_}, ts);
  }
  rt.pop_clip();
}

}  // namespace clashwin::ui
