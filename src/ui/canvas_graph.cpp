// CanvasGraph 实现（T10）：双折线绘制。
#include "canvas_graph.h"

#include <algorithm>
#include <cmath>

namespace clashwin::ui {

namespace {
// src: traffic-graph.tsx:172-184 countY：Y 轴 8 段分段映射（dy=h/7，越界落到 1）
float count_y(float v, float h) {
  const float dy = h / 7.0f;
  if (v <= 0) return h - 1;
  if (v <= 10) return h - (v / 10.0f) * dy;
  if (v <= 100) return h - (v / 100.0f + 1) * dy;
  if (v <= 1024) return h - (v / 1024.0f + 2) * dy;
  if (v <= 10240) return h - (v / 10240.0f + 3) * dy;
  if (v <= 102400) return h - (v / 102400.0f + 4) * dy;
  if (v <= 1048576) return h - (v / 1048576.0f + 5) * dy;
  if (v <= 10485760) return h - (v / 10485760.0f + 6) * dy;
  return 1;
}
}  // namespace

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
  const float w = bounds().w, h = bounds().h;
  // src: traffic-graph.tsx:167 dx = width / maxPoint（=30）
  const float dx = w / static_cast<float>(kMaxPoint);
  // src: traffic-graph.tsx:189-199 稳态 offset=dx 时 x(i)=dx*(i-2)+3（首点 3-2dx 由裁剪丢弃）
  auto x_at = [&](size_t i) { return dx * static_cast<float>(i) - 2.0f * dx + 3.0f; };
  auto y_at = [&](float v) { return count_y(v, h); };

  // src: traffic-graph.tsx:186-204 drawBezier：quadraticCurveTo(控制点=(x[i-1],y[i]),
  //   终点=((x[i-1]+x[i])/2, (y[i]+y[i+1])/2))，末点终点=控制点；此处按 6 段折线扁平化。
  std::vector<PointF> pts;
  pts.reserve(data.size() * 6 + 2);
  pts.push_back({x_at(0), y_at(data[0])});
  for (size_t i = 1; i < data.size(); ++i) {
    const PointF start = pts.back();
    const float cx = x_at(i - 1), cy = y_at(data[i]);
    float ex, ey;
    if (i + 1 < data.size()) {
      ex = (x_at(i - 1) + x_at(i)) / 2.0f;
      ey = (y_at(data[i]) + y_at(data[i + 1])) / 2.0f;
    } else {
      ex = cx;
      ey = cy;
    }
    for (int k = 1; k <= 6; ++k) {
      const float t = static_cast<float>(k) / 6.0f, u = 1.0f - t;
      pts.push_back({u * u * start.x + 2.0f * u * t * cx + t * t * ex,
                     u * u * start.y + 2.0f * u * t * cy + t * t * ey});
    }
  }
  rt.draw_polyline(pts.data(), static_cast<int>(pts.size()), {c, line_width});
}

void CanvasGraph::on_paint(IRenderTarget& rt) {
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{bg_});
  // 参考线：src: traffic-graph.tsx:167-170 dy=h/7，画 l1=dy、l2=4dy 两条；
  //   :221-227 alpha=refLineAlpha .15、width=refLineWidth 1
  const float dy = bounds().h / 7.0f;
  const float lw = 1.0f;  // src: traffic-graph.tsx:9 refLineWidth=1
  rt.draw_line({0, dy}, {bounds().w, dy}, {line_, lw});
  rt.draw_line({0, dy * 4}, {bounds().w, dy * 4}, {line_, lw});
  // src: traffic-graph.tsx:231-253 —— 先画 up（alpha .8），再画 down（alpha .9，覆盖在上）
  Color4f uc = up_color_; uc.a = 0.8f;     // src: :11 upLineAlpha
  Color4f dc = down_color_; dc.a = 0.9f;   // src: :14 downLineAlpha
  draw_series(rt, up_, uc);
  draw_series(rt, down_, dc);
}

}  // namespace clashwin::ui
