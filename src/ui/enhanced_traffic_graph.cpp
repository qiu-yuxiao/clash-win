// EnhancedTrafficGraph 实现（1:1 对齐 enhanced-canvas-traffic-graph.tsx）。
#include "enhanced_traffic_graph.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "i18n_text.h"
#include "theme/theme.h"
#include "util/parse_traffic.h"
#include "util/utf.h"

namespace clashwin::ui {

using theme::ThemeService;

namespace {
// src: components/home/enhanced-canvas-traffic-graph.tsx:65-80
constexpr size_t kMaxPoints = 300;
constexpr int kTargetFps = 15;
constexpr float kLineWidthUp = 2.5f;
constexpr float kLineWidthGrid = 0.5f;
constexpr float kAlphaGradient = 0.15f;
constexpr float kAlphaLine = 0.9f;
constexpr float kPadTop = 16.0f;
constexpr float kPadRight = 16.0f;
constexpr float kPadBottom = 32.0f;
constexpr float kPadLeft = 35.0f;

// src: components/home/enhanced-canvas-traffic-graph.tsx:1151-1152 bgcolor hover + borderRadius 1
constexpr float kOuterRadius = 8.0f;
constexpr float kActionHoverAlphaLight = 0.04f;
constexpr float kActionHoverAlphaDark = 0.08f;

// src: components/home/enhanced-canvas-traffic-graph.tsx:1200-1215 时间范围切换按钮
constexpr float kPillLeft = 40.0f;
constexpr float kPillTop = 6.0f;
constexpr float kPillPadX = 8.0f;
constexpr float kPillPadY = 4.0f;
constexpr float kPillRadius = 4.0f;
constexpr float kPillFontSize = 11.0f;

inline std::tm to_tm_safe(int64_t ts_ms) {
  std::tm out{};
  if (ts_ms <= 0) return out;
  // epoch ms vs synthetic test relative ms (< 1 day)
  if (ts_ms < 86400000LL) {
    const int64_t total_s = ts_ms / 1000LL;
    out.tm_hour = static_cast<int>((total_s / 3600) % 24);
    out.tm_min = static_cast<int>((total_s / 60) % 60);
    out.tm_sec = static_cast<int>(total_s % 60);
    return out;
  }
  const time_t secs = static_cast<time_t>(ts_ms / 1000LL);
  localtime_s(&out, &secs);
  return out;
}

inline Color4f with_alpha(Color4f c, float a) {
  c.a = a;
  return c;
}
}  // namespace

EnhancedTrafficGraph::EnhancedTrafficGraph(Widget* parent) : Widget(parent) {
  compute_pill_rect();
}

void EnhancedTrafficGraph::set_data(std::vector<TrafficDataPoint> points,
                                    const TrafficSamplerStats& stats) {
  // src: components/home/enhanced-canvas-traffic-graph.tsx:66 maxPoints=300
  if (points.size() > kMaxPoints) {
    points.erase(points.begin(),
                 points.begin() +
                     static_cast<ptrdiff_t>(points.size() - kMaxPoints));
  }
  data_points_ = std::move(points);
  stats_ = stats;
  if (tooltip_.visible) {
    update_tooltip_at(tooltip_.x, tooltip_.y);
  }
  if (focused_ || !pause_on_blur_) invalidate();
}

void EnhancedTrafficGraph::set_time_range_minutes(int minutes) {
  if (minutes != 1 && minutes != 5 && minutes != 10) return;
  if (time_range_minutes_ == minutes) return;
  time_range_minutes_ = minutes;
  compute_pill_rect();
  if (on_range_change) on_range_change(time_range_minutes_);
  invalidate();
}

void EnhancedTrafficGraph::cycle_time_range() {
  // src: components/home/enhanced-canvas-traffic-graph.tsx:1087 10 -> 1 -> 5 -> 10
  int next = 10;
  if (time_range_minutes_ == 10) {
    next = 1;
  } else if (time_range_minutes_ == 1) {
    next = 5;
  } else {
    next = 10;
  }
  set_time_range_minutes(next);
}

void EnhancedTrafficGraph::toggle_style() {
  bezier_style_ = !bezier_style_;
  invalidate();
}

void EnhancedTrafficGraph::set_focused(bool focused) {
  if (focused_ == focused) return;
  focused_ = focused;
  if (focused_) invalidate();
}

void EnhancedTrafficGraph::set_pause_on_blur(bool pause) {
  pause_on_blur_ = pause;
}

SizeF EnhancedTrafficGraph::on_measure(float max_w, float max_h) {
  // src: components/home/enhanced-canvas-traffic-graph.tsx:1150 width/height 100%
  const float default_w = 240.0f;
  const float default_h = 80.0f;
  const float huge_limit = 100000.0f;
  return {max_w > 0.0f ? max_w : default_w,
          (max_h > 0.0f && max_h < huge_limit) ? max_h : default_h};
}

EnhancedTrafficGraph::YScale EnhancedTrafficGraph::compute_y_scale(
    const std::vector<TrafficDataPoint>& data) {
  // src: components/home/enhanced-canvas-traffic-graph.tsx:354-383 computeYScale
  constexpr double kEmptyTop = 1024.0;
  if (data.empty()) return {kEmptyTop, 0.0};

  double max_val = 0.0;
  double min_val = data.front().down;
  for (const auto& pt : data) {
    if (pt.up > max_val) max_val = pt.up;
    if (pt.down > max_val) max_val = pt.down;
    if (pt.up < min_val) min_val = pt.up;
    if (pt.down < min_val) min_val = pt.down;
  }
  if (!std::isfinite(min_val)) min_val = 0.0;
  if (max_val == 0.0) return {kEmptyTop, 0.0};

  const double range = max_val - min_val;
  if (range == 0.0) return {max_val * 1.2, 0.0};

  const double pct = 0.1;
  return {max_val + range * pct, (std::max)(0.0, min_val - range * pct)};
}

float EnhancedTrafficGraph::calculate_y(double value, float height,
                                        double top_value,
                                        double bottom_value) {
  // src: components/home/enhanced-canvas-traffic-graph.tsx:345-349 calculateY
  const float top_y = kPadTop;
  const float bottom_y = height - kPadBottom;
  if (top_value == bottom_value) return bottom_y;
  const double ratio = (value - bottom_value) / (top_value - bottom_value);
  return bottom_y - static_cast<float>(ratio) * (bottom_y - top_y);
}

std::string EnhancedTrafficGraph::format_y_tick(double bytes) {
  // src: components/home/enhanced-canvas-traffic-graph.tsx:502-507 formatTrafficValue
  char buf[32]{};
  if (bytes <= 0.0) return "0";
  if (bytes < 1024.0) {
    std::snprintf(buf, sizeof(buf), "%.0fB", std::round(bytes));
    return buf;
  }
  if (bytes < 1024.0 * 1024.0) {
    std::snprintf(buf, sizeof(buf), "%.0fKB", std::round(bytes / 1024.0));
    return buf;
  }
  std::snprintf(buf, sizeof(buf), "%.1fMB", bytes / (1024.0 * 1024.0));
  return buf;
}

std::string EnhancedTrafficGraph::format_time_hhmm(int64_t timestamp_ms) {
  std::tm t = to_tm_safe(timestamp_ms);
  char buf[16]{};
  std::snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  return buf;
}

std::string EnhancedTrafficGraph::format_time_mmss(int64_t timestamp_ms) {
  std::tm t = to_tm_safe(timestamp_ms);
  char buf[16]{};
  std::snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_min, t.tm_sec);
  return buf;
}

std::string EnhancedTrafficGraph::format_time_hhmmss(int64_t timestamp_ms) {
  std::tm t = to_tm_safe(timestamp_ms);
  char buf[16]{};
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min,
                t.tm_sec);
  return buf;
}

void EnhancedTrafficGraph::compute_pill_rect() {
  // src: components/home/enhanced-canvas-traffic-graph.tsx:1200-1215
  const std::wstring text =
      tr("home.components.traffic.patterns.minutes",
         {{"time", std::to_string(time_range_minutes_)}});
  TextStyle ts;
  ts.size = kPillFontSize;
  ts.weight = static_cast<int>(Weight::Bold);
  SizeF sz = text_engine() ? text_engine()->measure_text(text, ts, 0.0f)
                           : SizeF{34.0f, 13.0f};
  if (sz.w <= 0.0f) sz.w = 34.0f;
  if (sz.h <= 0.0f) sz.h = 13.0f;
  pill_rect_ = {kPillLeft, kPillTop, sz.w + kPillPadX * 2.0f,
                sz.h + kPillPadY * 2.0f};
}

void EnhancedTrafficGraph::draw_y_axis(IRenderTarget& rt, float w, float h,
                                       const YScale& sc,
                                       const Color4f& grid_col,
                                       const Color4f& text_col,
                                       const Color4f& bg_col) {
  // src: components/home/enhanced-canvas-traffic-graph.tsx:511-598 drawYAxis
  const float top_y = kPadTop + 10.0f;
  const float bottom_y = h - kPadBottom - 5.0f;
  if (bottom_y <= top_y) return;
  const float mid_y = (top_y + bottom_y) * 0.5f;
  const double mid_val = (sc.bottom_value + sc.top_value) * 0.5;

  struct Tick {
    double val;
    float y;
    bool bottom;
    bool top;
  };
  const Tick ticks[3] = {
      {sc.bottom_value, bottom_y, true, false},
      {mid_val, mid_y, false, false},
      {sc.top_value, top_y, false, true},
  };

  TextStyle ts;
  ts.size = 8.0f;
  ts.color = with_alpha(text_col, text_col.a * 0.9f);
  ts.align_h = AlignH::Right;
  ts.align_v = AlignV::Center;
  ts.wrap = false;

  for (const auto& tk : ticks) {
    if (tk.bottom || tk.top) {
      const float lw = tk.bottom ? 0.8f : 0.4f;
      const float ga = tk.bottom ? 0.25f : 0.15f;
      rt.draw_line({kPadLeft, tk.y}, {w - kPadRight, tk.y},
                   {with_alpha(grid_col, grid_col.a * ga), lw});
    }
    const std::string lbl = format_y_tick(tk.val);
    const std::wstring wlbl = utf::utf8_to_utf16(lbl);
    const float est_w =
        text_engine()
            ? (std::max)(8.0f, text_engine()->measure_text(wlbl, ts, 0.0f).w)
            : static_cast<float>(lbl.size()) * 5.0f;
    if (lbl != "0") {
      rt.fill_rect({kPadLeft - est_w - 8.0f, tk.y - 5.0f, est_w + 4.0f, 10.0f},
                   Brush{with_alpha(bg_col, 0.15f)});
    }
    rt.draw_text(wlbl, {0.0f, tk.y - 6.0f, kPadLeft - 4.0f, 12.0f}, ts);
  }
}

void EnhancedTrafficGraph::draw_grid(IRenderTarget& rt, float w, float h,
                                     const Color4f& grid_col) {
  // src: components/home/enhanced-canvas-traffic-graph.tsx:744-766 drawGrid
  const float eff_w = w - kPadLeft - kPadRight;
  const float eff_h = h - kPadTop - kPadBottom;
  if (eff_w <= 0.0f || eff_h <= 0.0f) return;

  const Pen pen{with_alpha(grid_col, grid_col.a * 0.7f), kLineWidthGrid};
  constexpr int kHorizontalLines = 4;
  for (int i = 1; i <= kHorizontalLines; ++i) {
    const float y =
        kPadTop + (eff_h / static_cast<float>(kHorizontalLines + 1)) * i;
    rt.draw_line({kPadLeft, y}, {w - kPadRight, y}, pen);
  }
  constexpr int kVerticalLines = 6;
  for (int i = 1; i <= kVerticalLines; ++i) {
    const float x =
        kPadLeft + (eff_w / static_cast<float>(kVerticalLines + 1)) * i;
    rt.draw_line({x, kPadTop}, {x, h - kPadBottom}, pen);
  }
}

void EnhancedTrafficGraph::draw_time_axis(IRenderTarget& rt, float w, float h,
                                          const Color4f& text_col) {
  // src: components/home/enhanced-canvas-traffic-graph.tsx:606-730 drawTimeAxis
  if (data_points_.empty()) return;
  const float eff_w = w - kPadLeft - kPadRight;
  if (eff_w <= 0.0f) return;

  int max_labels = 8;
  float min_px_dist = 40.0f;
  bool use_mmss = false;
  if (time_range_minutes_ == 1) {
    max_labels = 6;
    min_px_dist = 35.0f;
    use_mmss = true;
  } else if (time_range_minutes_ == 5) {
    max_labels = 6;
    min_px_dist = 38.0f;
  }

  const int n = static_cast<int>(data_points_.size());
  const int target = (std::min)(max_labels, n);
  const int step =
      target > 1 ? (std::max)(1, n / (target - 1)) : 1;
  const int pixel_step = static_cast<int>(
      std::ceil((static_cast<float>(n) * min_px_dist) / eff_w));
  const int actual_step = (std::max)(step, pixel_step);

  struct TimePt {
    float x;
    std::wstring label;
  };
  std::vector<TimePt> pts;
  auto fmt = [&](int64_t ts) {
    return utf::utf8_to_utf16(use_mmss ? format_time_mmss(ts)
                                       : format_time_hhmm(ts));
  };

  if (data_points_[0].timestamp > 0) {
    pts.push_back({kPadLeft, fmt(data_points_[0].timestamp)});
  }
  for (int i = actual_step; i < n - actual_step; i += actual_step) {
    if (data_points_[static_cast<size_t>(i)].timestamp <= 0) continue;
    const float x =
        kPadLeft + (static_cast<float>(i) / static_cast<float>(n - 1)) * eff_w;
    pts.push_back({x, fmt(data_points_[static_cast<size_t>(i)].timestamp)});
  }
  if (n > 1 && data_points_.back().timestamp > 0) {
    const float last_x = w - kPadRight;
    if (pts.empty() || last_x - pts.back().x >= min_px_dist) {
      pts.push_back({last_x, fmt(data_points_.back().timestamp)});
    }
  }

  // src: components/home/enhanced-canvas-traffic-graph.tsx:648-656
  const float axis_y = h - kPadBottom + 14.0f;
  TextStyle ts;
  ts.size = 10.0f;
  ts.color = with_alpha(text_col, text_col.a * 0.7f);
  ts.align_v = AlignV::Center;
  ts.wrap = false;

  const float box_w = 56.0f;
  const float box_h = 14.0f;
  for (size_t i = 0; i < pts.size(); ++i) {
    if (i == 0) {
      ts.align_h = AlignH::Left;
      rt.draw_text(pts[i].label, {pts[i].x, axis_y - 7.0f, box_w, box_h}, ts);
    } else if (i + 1 == pts.size()) {
      ts.align_h = AlignH::Right;
      rt.draw_text(pts[i].label, {pts[i].x - box_w, axis_y - 7.0f, box_w, box_h},
                   ts);
    } else {
      ts.align_h = AlignH::Center;
      rt.draw_text(pts[i].label,
                   {pts[i].x - box_w * 0.5f, axis_y - 7.0f, box_w, box_h}, ts);
    }
  }
}

void EnhancedTrafficGraph::draw_series(IRenderTarget& rt, bool is_up, float w,
                                       float h, const Color4f& col,
                                       const YScale& sc) {
  // src: components/home/enhanced-canvas-traffic-graph.tsx:785-862 drawTrafficLine
  if (data_points_.size() < 2) return;
  const float eff_w = w - kPadLeft - kPadRight;
  if (eff_w <= 0.0f) return;

  const size_t n = data_points_.size();
  const size_t last_idx = n - 1;
  auto get_x = [&](size_t idx) {
    return kPadLeft +
           (static_cast<float>(idx) / static_cast<float>(last_idx)) * eff_w;
  };
  auto get_y = [&](size_t idx) {
    const double v = is_up ? data_points_[idx].up : data_points_[idx].down;
    return calculate_y(v, h, sc.top_value, sc.bottom_value);
  };

  std::vector<PointF> curve;
  curve.reserve(bezier_style_ ? n * 6 + 2 : n);
  PointF start{get_x(0), get_y(0)};
  curve.push_back(start);

  if (bezier_style_) {
    constexpr int kSubdiv = 6;
    for (size_t i = 1; i < n; ++i) {
      const float cx = get_x(i);
      const float cy = get_y(i);
      const size_t next_i = (std::min)(i + 1, last_idx);
      const float ex = (cx + get_x(next_i)) * 0.5f;
      const float ey = (cy + get_y(next_i)) * 0.5f;
      const PointF p0 = curve.back();
      for (int k = 1; k <= kSubdiv; ++k) {
        const float t = static_cast<float>(k) / static_cast<float>(kSubdiv);
        const float u = 1.0f - t;
        curve.push_back({u * u * p0.x + 2.0f * u * t * cx + t * t * ex,
                         u * u * p0.y + 2.0f * u * t * cy + t * t * ey});
      }
    }

    // 渐变填充仅在 Bezier 模式下绘制（src: :798 withGradient && chartStyle === 'bezier'）
    std::vector<PointF> poly = curve;
    const float base_y = h - kPadBottom;
    poly.push_back({get_x(last_idx), base_y});
    poly.push_back({get_x(0), base_y});
    rt.fill_polygon_vertical_gradient(poly.data(), static_cast<int>(poly.size()),
                                      kPadTop, base_y,
                                      with_alpha(col, kAlphaGradient),
                                      with_alpha(col, 0.0f));
  } else {
    for (size_t i = 1; i < n; ++i) {
      curve.push_back({get_x(i), get_y(i)});
    }
  }

  rt.draw_polyline(curve.data(), static_cast<int>(curve.size()),
                   {with_alpha(col, kAlphaLine), kLineWidthUp});
}

void EnhancedTrafficGraph::draw_overlays(IRenderTarget& rt, float w, float h,
                                         const Color4f& up_col,
                                         const Color4f& down_col,
                                         const Color4f& text_col,
                                         const Color4f& divider_col,
                                         const Color4f& paper_col) {
  // ① 左上时间范围按钮（src: :1196-1218）
  compute_pill_rect();
  const Color4f pill_bg(0.0f, 0.0f, 0.0f, pill_hover_ ? 0.10f : 0.05f);
  const Brush pill_br{pill_bg};
  rt.round_rect(pill_rect_, kPillRadius, Pen{Color4f{0, 0, 0, 0}, 0.0f},
                &pill_br);
  {
    TextStyle pts;
    pts.size = kPillFontSize;
    pts.weight = static_cast<int>(Weight::Bold);
    pts.color = text_col;
    pts.align_h = AlignH::Center;
    pts.align_v = AlignV::Center;
    pts.wrap = false;
    rt.draw_text(tr("home.components.traffic.patterns.minutes",
                    {{"time", std::to_string(time_range_minutes_)}}),
                 pill_rect_, pts);
  }

  // ② 右上图例（src: :1221-1251 top: 6, right: 8, gap: 4, 11px bold）
  {
    constexpr float kLegendRight = 8.0f;
    constexpr float kLegendTop = 6.0f;
    constexpr float kLegendRowH = 13.0f;
    constexpr float kLegendGap = 4.0f;
    constexpr float kLegendW = 72.0f;
    TextStyle lts;
    lts.size = 11.0f;
    lts.weight = static_cast<int>(Weight::Bold);
    lts.align_h = AlignH::Right;
    lts.align_v = AlignV::Center;
    lts.wrap = false;
    lts.color = up_col;
    rt.draw_text(tr("home.components.traffic.legends.upload"),
                 {w - kLegendRight - kLegendW, kLegendTop, kLegendW, kLegendRowH},
                 lts);
    lts.color = down_col;
    rt.draw_text(tr("home.components.traffic.legends.download"),
                 {w - kLegendRight - kLegendW,
                  kLegendTop + kLegendRowH + kLegendGap, kLegendW, kLegendRowH},
                 lts);
  }

  // ③ 右下样式指示器（src: :1254-1265 bottom: 6, right: 8, 10px, opacity 0.7）
  {
    constexpr float kStatusInset = 8.0f;
    constexpr float kStatusBottom = 6.0f;
    constexpr float kStatusH = 12.0f;
    constexpr float kStatusW = 56.0f;
    TextStyle sts;
    sts.size = 10.0f;
    sts.color = with_alpha(text_col, text_col.a * 0.45f);
    sts.align_h = AlignH::Right;
    sts.align_v = AlignV::Center;
    sts.wrap = false;
    rt.draw_text(bezier_style_ ? L"Smooth" : L"Linear",
                 {w - kStatusInset - kStatusW, h - kStatusBottom - kStatusH,
                  kStatusW, kStatusH},
                 sts);
  }

  // ④ 左下统计信息（src: :1268-1281 bottom: 6, left: 8, 9px, opacity 0.6）
  {
    constexpr float kStatsLeft = 8.0f;
    constexpr float kStatsBottom = 6.0f;
    constexpr float kStatsH = 11.0f;
    char sbuf[96]{};
    std::snprintf(sbuf, sizeof(sbuf), "Points: %zu | Compressed: %zu | FPS: %d",
                  data_points_.size(), stats_.compressed_buffer_size,
                  kTargetFps);
    TextStyle bts;
    bts.size = 9.0f;
    bts.color = with_alpha(text_col, text_col.a * 0.40f);
    bts.align_h = AlignH::Left;
    bts.align_v = AlignV::Center;
    bts.wrap = false;
    rt.draw_text(utf::utf8_to_utf16(sbuf),
                 {kStatsLeft, h - kStatsBottom - kStatsH,
                  (std::max)(80.0f, w - 72.0f), kStatsH},
                 bts);
  }

  // ⑤ 悬浮十字线 + Tooltip 气泡（src: :984-1011, :1284-1318）
  if (tooltip_.visible && data_points_.size() >= 2 &&
      tooltip_.data_index >= 0) {
    const float eff_w = w - kPadLeft - kPadRight;
    if (eff_w > 0.0f) {
      const float data_x =
          kPadLeft + (static_cast<float>(tooltip_.data_index) /
                      static_cast<float>(data_points_.size() - 1)) *
                         eff_w;
      const Pen dash_pen{with_alpha(text_col, text_col.a * 0.6f), 1.0f};
      rt.draw_dashed_line({data_x, kPadTop}, {data_x, h - kPadBottom}, dash_pen,
                          4.0f, 4.0f);
      rt.draw_dashed_line({kPadLeft, tooltip_.highlight_y},
                          {w - kPadRight, tooltip_.highlight_y}, dash_pen, 4.0f,
                          4.0f);
    }

    constexpr float kTipFontSize = 10.0f;
    constexpr float kTipPadX = 8.0f;
    constexpr float kTipPadY = 4.0f;
    constexpr float kTipLineH = 12.0f;
    constexpr float kTipRadius = 4.0f;
    constexpr float kTipFlipX = 200.0f;
    TextStyle tts;
    tts.size = kTipFontSize;
    tts.wrap = false;
    tts.align_h = AlignH::Left;
    tts.align_v = AlignV::Center;
    const std::wstring up_line = L"\x2191 " + tooltip_.up_speed;
    const std::wstring down_line = L"\x2193 " + tooltip_.down_speed;
    auto m_w = [&](const std::wstring& s) {
      return text_engine() ? text_engine()->measure_text(s, tts, 0.0f).w
                           : static_cast<float>(s.size()) * 6.0f;
    };
    const float text_w =
        (std::max)({m_w(tooltip_.timestamp), m_w(up_line), m_w(down_line)});
    const float box_w = text_w + kTipPadX * 2.0f;
    const float box_h = kTipLineH * 3.0f + kTipPadY * 2.0f;

    float tx = tooltip_.x + 8.0f;
    if (tooltip_.x > kTipFlipX) tx -= box_w;
    tx = (std::max)(2.0f, (std::min)(tx, w - box_w - 2.0f));
    const float ty =
        (std::max)(2.0f, (std::min)(tooltip_.y - 8.0f, h - box_h - 2.0f));

    const Brush bg_br{paper_col};
    rt.round_rect({tx, ty, box_w, box_h}, kTipRadius, Pen{divider_col, 1.0f},
                  &bg_br);

    tts.color = text_col;
    rt.draw_text(tooltip_.timestamp,
                 {tx + kTipPadX, ty + kTipPadY, text_w + 4.0f, kTipLineH}, tts);
    tts.weight = static_cast<int>(Weight::Medium);
    tts.color = up_col;
    rt.draw_text(up_line,
                 {tx + kTipPadX, ty + kTipPadY + kTipLineH, text_w + 4.0f,
                  kTipLineH},
                 tts);
    tts.color = down_col;
    rt.draw_text(down_line,
                 {tx + kTipPadX, ty + kTipPadY + kTipLineH * 2.0f, text_w + 4.0f,
                  kTipLineH},
                 tts);
  }
}

void EnhancedTrafficGraph::on_paint(IRenderTarget& rt) {
  const float w = bounds().w;
  const float h = bounds().h;
  if (w <= 0.0f || h <= 0.0f) return;

  const auto& svc = ThemeService::instance();
  const auto& pal = svc.palette();
  const auto& vars = svc.vars();
  const bool light = svc.is_light();

  // src: components/home/enhanced-canvas-traffic-graph.tsx:1151-1152 bgcolor action hover + borderRadius 1(=8px)
  const Brush outer_bg{light ? Color4f(0.0f, 0.0f, 0.0f, kActionHoverAlphaLight)
                             : Color4f(1.0f, 1.0f, 1.0f, kActionHoverAlphaDark)};
  rt.round_rect({0.0f, 0.0f, w, h}, kOuterRadius,
                Pen{Color4f{0, 0, 0, 0}, 0.0f}, &outer_bg);

  rt.push_clip({0.0f, 0.0f, w, h});

  const Color4f up_col = pal.secondary_color;
  const Color4f down_col = pal.primary_color;
  const Color4f grid_col = vars.divider_color;
  const Color4f text_col = pal.secondary_text;
  const Color4f paper_col =
      with_alpha(vars.theme_panel_bg.a > 0.05f ? vars.theme_panel_bg
                                               : vars.theme_bg,
                 1.0f);

  const float eff_w = w - kPadLeft - kPadRight;
  const float eff_h = h - kPadTop - kPadBottom;
  if (eff_w > 4.0f && eff_h > 4.0f) {
    const YScale sc = compute_y_scale(data_points_);
    draw_y_axis(rt, w, h, sc, grid_col, text_col, paper_col);
    draw_grid(rt, w, h, grid_col);
    draw_time_axis(rt, w, h, text_col);
    // src: components/home/enhanced-canvas-traffic-graph.tsx:933-956 先下载后上传
    draw_series(rt, false, w, h, down_col, sc);
    draw_series(rt, true, w, h, up_col, sc);
  }

  draw_overlays(rt, w, h, up_col, down_col, text_col, grid_col, paper_col);
  rt.pop_clip();
}

void EnhancedTrafficGraph::update_tooltip_at(float mx, float my) {
  // src: components/home/enhanced-canvas-traffic-graph.tsx:419-461 handleMouseMove
  const float eff_w = bounds().w - kPadLeft - kPadRight;
  if (eff_w <= 0.0f || data_points_.empty() || bounds().h <= 0.0f) {
    tooltip_.visible = false;
    return;
  }
  const float rel_x = mx - kPadLeft;
  const float ratio = (std::max)(0.0f, (std::min)(1.0f, rel_x / eff_w));
  const int idx = static_cast<int>(
      std::lround(ratio * static_cast<float>(data_points_.size() - 1)));
  if (idx < 0 || static_cast<size_t>(idx) >= data_points_.size()) {
    tooltip_.visible = false;
    return;
  }
  const auto& pt = data_points_[static_cast<size_t>(idx)];
  auto [uv, uu] = util::parse_traffic(pt.up);
  auto [dv, du] = util::parse_traffic(pt.down);
  const YScale sc = compute_y_scale(data_points_);
  const float up_y =
      calculate_y(pt.up, bounds().h, sc.top_value, sc.bottom_value);
  const float down_y =
      calculate_y(pt.down, bounds().h, sc.top_value, sc.bottom_value);

  tooltip_.visible = true;
  tooltip_.x = mx;
  tooltip_.y = my;
  tooltip_.data_index = idx;
  tooltip_.highlight_y = pt.up >= pt.down ? up_y : down_y;
  tooltip_.up_speed = utf::utf8_to_utf16(uv + uu + "/s");
  tooltip_.down_speed = utf::utf8_to_utf16(dv + du + "/s");
  tooltip_.timestamp =
      pt.timestamp > 0 ? utf::utf8_to_utf16(format_time_hhmmss(pt.timestamp))
                       : std::wstring(L"-");
}

bool EnhancedTrafficGraph::on_mouse(const MouseEvent& e) {
  switch (e.type) {
    case MouseEventType::Move:
    case MouseEventType::Enter: {
      const bool next_pill_hover = pill_rect_.contains(e.pos.x, e.pos.y);
      const bool changed_pill = (next_pill_hover != pill_hover_);
      pill_hover_ = next_pill_hover;
      if (!data_points_.empty()) {
        update_tooltip_at(e.pos.x, e.pos.y);
        invalidate();
      } else if (changed_pill) {
        invalidate();
      }
      return true;
    }
    case MouseEventType::Leave: {
      const bool dirty = pill_hover_ || tooltip_.visible;
      pill_hover_ = false;
      tooltip_.visible = false;
      pointer_down_ = false;
      down_in_pill_ = false;
      if (dirty) invalidate();
      return true;
    }
    case MouseEventType::Down: {
      if (e.buttons & kMouseLeft) {
        pointer_down_ = true;
        down_in_pill_ = pill_rect_.contains(e.pos.x, e.pos.y);
        return true;
      }
      return false;
    }
    case MouseEventType::Up: {
      if (!pointer_down_) return false;
      const bool was_pill = down_in_pill_;
      pointer_down_ = false;
      down_in_pill_ = false;
      if (!hit_self(e.pos)) return true;
      if (was_pill && pill_rect_.contains(e.pos.x, e.pos.y)) {
        cycle_time_range();
      } else if (!was_pill) {
        toggle_style();
      }
      return true;
    }
    default:
      return false;
  }
}

}  // namespace clashwin::ui
