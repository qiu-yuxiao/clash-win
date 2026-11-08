// CanvasGraph 实现（T10）：双折线绘制。
#include "canvas_graph.h"

#include <windows.h>

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
  if (up.size() < static_cast<size_t>(kMaxPoint + 2)) {
    std::vector<float> pad(static_cast<size_t>(kMaxPoint + 2) - up.size(), 0.0f);
    pad.insert(pad.end(), up.begin(), up.end());
    up_ = std::move(pad);
  } else {
    up_ = std::move(up);
  }
  if (down.size() < static_cast<size_t>(kMaxPoint + 2)) {
    std::vector<float> pad(static_cast<size_t>(kMaxPoint + 2) - down.size(), 0.0f);
    pad.insert(pad.end(), down.begin(), down.end());
    down_ = std::move(pad);
  } else {
    down_ = std::move(down);
  }
  if (max_value > 0) max_value_ = max_value;
  anim_start_ms_ = static_cast<int64_t>(GetTickCount64());
  invalidate();
}

SizeF CanvasGraph::on_measure(float max_w, float max_h) {
  return {max_w > 0 ? max_w : 200, max_h > 0 && max_h < 1e5 ? max_h : 80};
}

void CanvasGraph::draw_series(IRenderTarget& rt, const std::vector<float>& data,
                              const Color4f& c, float offset) {
  if (data.size() < 2 || bounds().w <= 0 || bounds().h <= 0) return;
  const float w = bounds().w, h = bounds().h;
  // src: traffic-graph.tsx:167 dx = width / maxPoint（=30）
  const float dx = w / static_cast<float>(kMaxPoint);
  auto count_y_val = [&](float v) { return count_y(v, h); };

  // src: traffic-graph.tsx:186-204 drawBezier：quadraticCurveTo(控制点=(p1x,p1y),
  //   终点=((p1x+p2x)/2, (p1y+p2y)/2))，末点终点=控制点；按 6 段折线扁平化。
  std::vector<PointF> pts;
  pts.reserve(data.size() * 6 + 2);
  const float first_x = dx * -1.0f - offset + 3.0f;
  const float first_y = count_y_val(data[0]);
  pts.push_back({first_x, first_y});

  for (size_t i = 1; i < data.size(); ++i) {
    const PointF start = pts.back();
    const float p1x = dx * static_cast<float>(i - 1) - offset + 3.0f;
    const float p1y = count_y_val(data[i]);
    const bool has_next = (i + 1 < data.size());
    const float p2x = has_next ? (dx * static_cast<float>(i) - offset + 3.0f) : p1x;
    const float p2y = has_next ? count_y_val(data[i + 1]) : p1y;
    const float ex = (p1x + p2x) / 2.0f;
    const float ey = (p1y + p2y) / 2.0f;

    for (int k = 1; k <= 6; ++k) {
      const float t = static_cast<float>(k) / 6.0f, u = 1.0f - t;
      pts.push_back({u * u * start.x + 2.0f * u * t * p1x + t * t * ex,
                     u * u * start.y + 2.0f * u * t * p1y + t * t * ey});
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

  // src: traffic-graph.tsx:273-288 动画进度驱动 offset 从 0 平滑滑动到 dx
  const float dx = bounds().w / static_cast<float>(kMaxPoint);
  float offset = dx;
  if (anim_start_ms_ > 0) {
    int64_t elapsed = static_cast<int64_t>(GetTickCount64()) - anim_start_ms_;
    if (elapsed < 1000) {
      float progress = static_cast<float>(elapsed) / 1000.0f;
      offset = progress * dx;
      invalidate();  // 驱动下一帧平滑滚动动画
    }
  }

  draw_series(rt, up_, uc, offset);
  draw_series(rt, down_, dc, offset);
}

}  // namespace clashwin::ui
