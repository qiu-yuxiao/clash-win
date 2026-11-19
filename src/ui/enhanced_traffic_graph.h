#pragma once
// EnhancedTrafficGraph（1:1 移植 ClashVerge enhanced-canvas-traffic-graph.tsx）。
// 宽窗（width > 285px，!narrow_）模式下替换 CanvasGraph 使用：
//   - 3 刻度 Y 轴 + 4x6 背景网格 + MM:SS / HH:MM 时间轴
//   - 先下载 (primary.main) 后上传 (secondary.main) 叠加曲线 + Bezier 垂直渐变填充
//   - 左上 10/1/5 分钟切换按钮（左击切换时间窗；空白点击切换 Smooth/Linear）
//   - 右上上传/下载图例、右下 Smooth/Linear、左下 Points | Compressed | FPS 统计
//   - 悬停 [4,4] 十字虚线 + 自动翻转气泡提示
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "traffic_sampler.h"
#include "widget.h"

namespace clashwin::ui {

class EnhancedTrafficGraph : public Widget {
 public:
  explicit EnhancedTrafficGraph(Widget* parent = nullptr);

  void set_data(std::vector<TrafficDataPoint> points,
                const TrafficSamplerStats& stats);
  void set_time_range_minutes(int minutes);
  void cycle_time_range();
  int time_range_minutes() const { return time_range_minutes_; }

  bool is_bezier() const { return bezier_style_; }
  void toggle_style();

  void set_focused(bool focused);
  void set_pause_on_blur(bool pause);

  // 点击时间范围按钮切换（1 -> 5 -> 10 -> 1）时触发宿主重新拉取样本窗。
  std::function<void(int)> on_range_change;

  SizeF on_measure(float max_w, float max_h) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;

  // 测试/诊断只读观察
  const std::vector<TrafficDataPoint>& data_points() const { return data_points_; }
  const TrafficSamplerStats& stats() const { return stats_; }
  const RectF& pill_rect() const { return pill_rect_; }
  bool tooltip_visible() const { return tooltip_.visible; }
  int hovered_index() const { return tooltip_.data_index; }

 private:
  struct YScale {
    double top_value = 1024.0;
    double bottom_value = 0.0;
  };
  struct TooltipState {
    bool visible = false;
    float x = 0.0f;
    float y = 0.0f;
    int data_index = -1;
    float highlight_y = 0.0f;
    std::wstring up_speed;
    std::wstring down_speed;
    std::wstring timestamp;
  };

  static YScale compute_y_scale(const std::vector<TrafficDataPoint>& data);
  static float calculate_y(double value, float height, double top_value,
                           double bottom_value);
  static std::string format_y_tick(double bytes);
  static std::string format_time_hhmm(int64_t timestamp_ms);
  static std::string format_time_mmss(int64_t timestamp_ms);
  static std::string format_time_hhmmss(int64_t timestamp_ms);

  void draw_y_axis(IRenderTarget& rt, float w, float h, const YScale& sc,
                   const Color4f& grid_col, const Color4f& text_col,
                   const Color4f& bg_col);
  void draw_grid(IRenderTarget& rt, float w, float h, const Color4f& grid_col);
  void draw_time_axis(IRenderTarget& rt, float w, float h,
                      const Color4f& text_col);
  void draw_series(IRenderTarget& rt, bool is_up, float w, float h,
                   const Color4f& col, const YScale& sc);
  void draw_overlays(IRenderTarget& rt, float w, float h,
                     const Color4f& up_col, const Color4f& down_col,
                     const Color4f& text_col, const Color4f& divider_col,
                     const Color4f& paper_col);
  void update_tooltip_at(float mx, float my);
  void compute_pill_rect();

  std::vector<TrafficDataPoint> data_points_;
  TrafficSamplerStats stats_{};
  int time_range_minutes_ = 10;
  bool bezier_style_ = true;
  bool focused_ = true;
  bool pause_on_blur_ = true;

  bool pill_hover_ = false;
  bool pointer_down_ = false;
  bool down_in_pill_ = false;
  RectF pill_rect_{};
  TooltipState tooltip_{};
};

}  // namespace clashwin::ui
