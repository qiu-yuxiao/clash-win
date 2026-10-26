// CanvasGraph 实现（T10）：双折线绘制。
#include "canvas_graph.h"

#include <algorithm>
#include <cmath>

namespace clashwin::ui {

void CanvasGraph::set_series(std::vector<float> up, std::vector<float> down,
                             float max_value) {
  up_ = std::move(up);
  down_ = std::move(down);
  if (max_value > 0) max_value_ = max_value;
  invalidate();
}

SizeF CanvasGraph::on_measure(float max_w, float max_h) {
  return {max_w > 0 ? max_w : 200, max_h > 0 && max_h < 1e5 ? max_h : 80};
}

void CanvasGraph::draw_series(IRenderTarget& rt, const std::vector<float>& data,
                              const Color4f& c) {
  if (data.size() < 2 || bounds().w <= 0 || bounds().h <= 0) return;
  const size_t n = data.size();
  const float w = bounds().w, h = bounds().h;
  const float step_x = w / static_cast<float>(kMaxPoint - 1);
  const float x0 = w - static_cast<float>(n - 1) * step_x;  // 靠右对齐
  PointF prev{};
  for (size_t i = 0; i < n; ++i) {
    float v = data[i];
    if (v < 0) v = 0;
    float t = v / max_value_;
    if (t > 1) t = 1;
    PointF p{x0 + static_cast<float>(i) * step_x,
             h - 1 - t * (h - 2)};
    if (i > 0) rt.draw_line(prev, p, {c, line_width});
    prev = p;
  }
}

void CanvasGraph::on_paint(IRenderTarget& rt) {
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{bg_});
  // 中线参考
  rt.draw_line({0, bounds().h / 2}, {bounds().w, bounds().h / 2},
               {line_, 1});
  draw_series(rt, down_, down_color_);
  draw_series(rt, up_, up_color_);
}

}  // namespace clashwin::ui
