// CanvasGraph 实现（T10 / BUG-410）：宽窄窗统一自适应实时滚动流量曲线。
#include "canvas_graph.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace clashwin::ui {

namespace {

// src: src/components/layout/traffic-graph.tsx:22-34 绘制节奏与透明度常量
constexpr float kRefLineAlpha = 0.15f;
constexpr float kWideGridAlpha = 0.18f;
constexpr float kWideVerticalGridAlpha = 0.10f;
constexpr float kRefLineWidth = 1.0f;
constexpr float kUpLineAlpha = 0.80f;
constexpr float kDownLineAlpha = 0.90f;
constexpr int64_t kAnimDurationMs = 1000;
constexpr int kBezierSubdiv = 6;  // src: src/components/layout/traffic-graph.tsx:264 quadraticCurveTo 6 段扁平化

// src: src/components/layout/traffic-graph.tsx:314-375 宽窗面积填充、刻度字号与可见高度阈值
constexpr float kWideDownAreaAlpha = 0.20f;
constexpr float kWideUpAreaAlpha = 0.16f;
constexpr float kScaleTextAlpha = 0.55f;
constexpr float kBadgeTextAlpha = 0.60f;
constexpr float kScaleFontSize = 9.0f;
constexpr float kWideLabelsMinHeight = 60.0f;
constexpr int kTimeGridStepSec = 10;

struct ScaleLevel {
  const wchar_t* label;
  double bps;
};
// src: src/components/layout/traffic-graph.tsx:36-40 WIDE_SCALE_LEVELS
constexpr std::array<ScaleLevel, 3> kWideScaleLevels = {{
    {L"1M", 1048576.0},
    {L"10K", 10240.0},
    {L"100B", 100.0},
}};

inline Color4f with_alpha_mul(Color4f c, float mul) {
  c.a *= mul;
  return c;
}

inline Color4f with_alpha_exact(Color4f c, float alpha) {
  c.a = alpha;
  return c;
}

}  // namespace

void CanvasGraph::set_series(std::vector<float> up, std::vector<float> down,
                             float max_value) {
  // src: src/components/layout/traffic-graph.tsx:43-44, 125-130
  auto normalize_buf = [](std::vector<float>& src) {
    const size_t target = static_cast<size_t>(kBufferLen);
    if (src.size() < target) {
      std::vector<float> pad(target - src.size(), 0.0f);
      pad.insert(pad.end(), src.begin(), src.end());
      return pad;
    }
    if (src.size() > target) {
      return std::vector<float>(
          src.end() - static_cast<std::ptrdiff_t>(target), src.end());
    }
    return src;
  };
  up_ = normalize_buf(up);
  down_ = normalize_buf(down);
  if (max_value > 0.0f) max_value_ = max_value;
  progress_ = 0.0f;
  anim_start_ms_ = static_cast<int64_t>(::GetTickCount64());
  invalidate();
}

void CanvasGraph::set_minimal_width(bool minimal) {
  // src: src/components/layout/traffic-graph.tsx:107-110, 427-431 非中断式即时重绘
  if (minimal_width_ == minimal) return;
  minimal_width_ = minimal;
  invalidate();
}

int CanvasGraph::visible_points() const {
  // src: src/components/layout/traffic-graph.tsx:226-228
  if (minimal_width_) return kMinPoint;
  return util::resolve_traffic_visible_points(bounds().w);
}

void CanvasGraph::toggle_style() {
  // src: src/components/layout/traffic-graph.tsx:91-94
  bezier_style_ = !bezier_style_;
  invalidate();
}

SizeF CanvasGraph::on_measure(float max_w, float max_h) {
  // src: src/components/layout/traffic-graph.tsx:464-465 width:100% height:100%
  return {max_w > 0.0f ? max_w : 200.0f,
          (max_h > 0.0f && max_h < 1e5f) ? max_h : 80.0f};
}

void CanvasGraph::trace_series(const std::vector<float>& data, float height,
                               float dx, float progress,
                               std::vector<PointF>& out) const {
  // src: src/components/layout/traffic-graph.tsx:232-274 traceCurvePath
  out.clear();
  if (data.empty()) return;
  out.reserve(bezier_style_ ? data.size() * kBezierSubdiv + 2 : data.size());

  auto point_x = [&](size_t idx) {
    return static_cast<float>(
        util::compute_traffic_point_x(static_cast<int>(idx), dx, progress));
  };
  auto point_y = [&](size_t idx) {
    return static_cast<float>(
        util::compute_traffic_graph_y(data[idx], height));
  };

  out.push_back({point_x(0), point_y(0)});
  if (bezier_style_) {
    for (size_t i = 1; i < data.size(); ++i) {
      const PointF start = out.back();
      const float p1x = point_x(i);
      const float p1y = point_y(i);
      const bool has_next = (i + 1 < data.size());
      const float p2x = has_next ? point_x(i + 1) : p1x;
      const float p2y = has_next ? point_y(i + 1) : p1y;
      const float ex = (p1x + p2x) * 0.5f;
      const float ey = (p1y + p2y) * 0.5f;

      for (int k = 1; k <= kBezierSubdiv; ++k) {
        const float t =
            static_cast<float>(k) / static_cast<float>(kBezierSubdiv);
        const float u = 1.0f - t;
        out.push_back({u * u * start.x + 2.0f * u * t * p1x + t * t * ex,
                       u * u * start.y + 2.0f * u * t * p1y + t * t * ey});
      }
    }
  } else {
    for (size_t i = 1; i < data.size(); ++i) {
      out.push_back({point_x(i), point_y(i)});
    }
  }
}

void CanvasGraph::on_paint(IRenderTarget& rt) {
  const float w = bounds().w;
  const float h = bounds().h;
  if (w <= 0.0f || h <= 0.0f) return;

  if (bg_.a > 0.0f) {
    rt.fill_rect({0.0f, 0.0f, w, h}, Brush{bg_});
  }

  // 裁剪在画布内（对齐 HTML5 <canvas> 边界裁剪，避免首尾锚点外溢进父容器）
  rt.push_clip({0.0f, 0.0f, w, h});

  // src: src/components/layout/traffic-graph.tsx:394-407 1000ms 滑动动画进度
  float progress = progress_;
  if (anim_start_ms_ > 0) {
    const int64_t elapsed =
        static_cast<int64_t>(::GetTickCount64()) - anim_start_ms_;
    if (elapsed < kAnimDurationMs) {
      progress = (std::max)(
          0.0f, static_cast<float>(elapsed) / static_cast<float>(kAnimDurationMs));
      invalidate();
    } else {
      progress = 1.0f;
    }
    progress_ = progress;
  }

  const bool is_wide = !minimal_width_;
  const int vis_pts = visible_points();
  const auto up_slice = util::slice_traffic_visible_buffer(up_, vis_pts);
  const auto down_slice = util::slice_traffic_visible_buffer(down_, vis_pts);
  const float dx = w / static_cast<float>(vis_pts);

  // 1) 背景参考线与刻度网格（src: src/components/layout/traffic-graph.tsx:276-310）
  if (is_wide) {
    const Pen h_pen{with_alpha_mul(divider_color_, kWideGridAlpha), kRefLineWidth};
    for (const auto& level : kWideScaleLevels) {
      const float y =
          static_cast<float>(util::compute_traffic_graph_y(level.bps, h));
      rt.draw_line({0.0f, y}, {w, y}, h_pen);
    }
    const Pen v_pen{with_alpha_mul(divider_color_, kWideVerticalGridAlpha),
                    kRefLineWidth};
    for (int s = kTimeGridStepSec; s < vis_pts; s += kTimeGridStepSec) {
      const float x = w - static_cast<float>(s) * dx;
      rt.draw_line({x, 0.0f}, {x, h}, v_pen);
    }
  } else {
    // src: src/components/layout/traffic-graph.tsx:300-309 窄窗双水平参考线
    const Pen ref_pen{with_alpha_mul(divider_color_, kRefLineAlpha), kRefLineWidth};
    const float dy = h / 7.0f;
    rt.draw_line({0.0f, dy}, {w, dy}, ref_pen);
    rt.draw_line({0.0f, dy * 4.0f}, {w, dy * 4.0f}, ref_pen);
  }

  std::vector<PointF> up_pts;
  std::vector<PointF> down_pts;
  trace_series(up_slice, h, dx, progress, up_pts);
  trace_series(down_slice, h, dx, progress, down_pts);

  // 2) 宽窗下先填充面积垂向渐变，再统一描边曲线（src: src/components/layout/traffic-graph.tsx:312-338）
  if (is_wide && !down_slice.empty() && !up_slice.empty()) {
    const float baseline_y = h - 2.0f;
    auto fill_area = [&](std::vector<PointF> curve, size_t n_pts,
                         const Color4f& col, float top_alpha) {
      if (curve.empty() || n_pts == 0) return;
      const float first_x = static_cast<float>(
          util::compute_traffic_point_x(0, dx, progress));
      const float last_x = static_cast<float>(util::compute_traffic_point_x(
          static_cast<int>(n_pts) - 1, dx, progress));
      curve.push_back({last_x, baseline_y});
      curve.push_back({first_x, baseline_y});
      rt.fill_polygon_vertical_gradient(
          curve.data(), static_cast<int>(curve.size()), 0.0f, h,
          with_alpha_exact(col, top_alpha), with_alpha_exact(col, 0.0f));
    };

    fill_area(down_pts, down_slice.size(), down_color_, kWideDownAreaAlpha);
    fill_area(up_pts, up_slice.size(), up_color_, kWideUpAreaAlpha);
  }

  // 3) 前景描边：先 up(0.8)，后 down(0.9)（src: src/components/layout/traffic-graph.tsx:340-355）
  rt.draw_polyline(up_pts.data(), static_cast<int>(up_pts.size()),
                   {with_alpha_exact(up_color_, kUpLineAlpha), line_width});
  rt.draw_polyline(down_pts.data(), static_cast<int>(down_pts.size()),
                   {with_alpha_exact(down_color_, kDownLineAlpha), line_width});

  // 4) 宽窗下补充对数纵轴刻度标识与实时时间跨度角标（src: src/components/layout/traffic-graph.tsx:356-375）
  if (is_wide && h >= kWideLabelsMinHeight) {
    TextStyle label_ts;
    label_ts.font_family = L"Segoe UI";  // src: src/components/layout/traffic-graph.tsx:360
    label_ts.size = kScaleFontSize;
    label_ts.weight = static_cast<int>(Weight::Normal);
    label_ts.color = with_alpha_mul(scale_text_color_, kScaleTextAlpha);
    label_ts.align_h = AlignH::Left;
    label_ts.align_v = AlignV::Bottom;
    label_ts.wrap = false;

    for (const auto& level : kWideScaleLevels) {
      const float y =
          static_cast<float>(util::compute_traffic_graph_y(level.bps, h));
      const float bottom_y = (std::max)(11.0f, y - 2.0f);  // src: src/components/layout/traffic-graph.tsx:366
      rt.draw_text(level.label, {4.0f, bottom_y - 12.0f, 48.0f, 12.0f},
                   label_ts);
    }

    TextStyle badge_ts;
    badge_ts.font_family = L"Segoe UI";  // src: src/components/layout/traffic-graph.tsx:370
    badge_ts.size = kScaleFontSize;
    badge_ts.weight = static_cast<int>(Weight::Bold);
    badge_ts.color = with_alpha_mul(scale_text_color_, kBadgeTextAlpha);
    badge_ts.align_h = AlignH::Right;
    badge_ts.align_v = AlignV::Top;
    badge_ts.wrap = false;

    const float badge_w = (std::min)(48.0f, (std::max)(0.0f, w - 4.0f));  // src: src/components/layout/traffic-graph.tsx:374
    rt.draw_text(std::to_wstring(vis_pts) + L"s",
                 {(std::max)(0.0f, w - 4.0f - badge_w), 3.0f, badge_w, 12.0f},
                 badge_ts);
  }

  rt.pop_clip();
}

bool CanvasGraph::on_mouse(const MouseEvent& e) {
  // src: src/components/layout/traffic-graph.tsx:91-94, 462 onClick={handleToggleStyle}
  switch (e.type) {
    case MouseEventType::Down:
      if (e.buttons & kMouseLeft) {
        pointer_down_ = true;
        return true;
      }
      return false;
    case MouseEventType::Up:
      if (!pointer_down_) return false;
      pointer_down_ = false;
      if (hit_self(e.pos)) toggle_style();
      return true;
    case MouseEventType::Leave:
      pointer_down_ = false;
      return false;
    default:
      return false;
  }
}

HCURSOR CanvasGraph::cursor(const PointF&) const {
  // src: src/components/layout/traffic-graph.tsx:467 cursor: 'pointer'
  return ::LoadCursorW(nullptr, IDC_HAND);
}

}  // namespace clashwin::ui
