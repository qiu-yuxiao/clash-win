#pragma once
// CanvasGraph（T10 / BUG-410）：宽窄窗统一随窗宽自适应的实时滚动流量曲线。
// src: src/components/layout/traffic-graph.tsx:66-471
//   - 缓冲 63 点（src: src/utils/parse-traffic.ts:3-6），跨 285px 断点零销毁保留历史波形
//   - 窄窗(isMinimalWidth=true)：30s 极简双参考线（dy / 4dy，alpha 0.15）
//   - 宽窗(isMinimalWidth=false)：自适应 30s→60s 时间跨度 + 1M/10K/100B 对数横网格
//     + 10s 纵向网格 + 垂向渐变面积填充 + 左刻度和右上 "{visiblePoints}s" 角标
//   - 左键点击画布切换 Smooth(二次贝塞尔) / Linear(折线)（src: :91-94, :462）
#include <cstddef>
#include <cstdint>
#include <vector>

#include "util/parse_traffic.h"
#include "widget.h"

namespace clashwin::ui {

class CanvasGraph : public Widget {
 public:
  explicit CanvasGraph(Widget* parent = nullptr) : Widget(parent) {
    up_.assign(static_cast<size_t>(kBufferLen), 0.0f);
    down_.assign(static_cast<size_t>(kBufferLen), 0.0f);
  }

  static constexpr int kMinPoint = util::kTrafficGraphMinPoints;    // ← src/utils/parse-traffic.ts:3
  static constexpr int kMaxPoint = util::kTrafficGraphMaxPoints;    // ← src/utils/parse-traffic.ts:4
  static constexpr int kBufferLen = util::kTrafficGraphBufferLen;   // ← src/utils/parse-traffic.ts:6

  // 数据入（不足 63 点前端补零对齐基线，超出截尾保留最新 63 点；max_value 仅保留兼容签名）
  void set_series(std::vector<float> up, std::vector<float> down,
                  float max_value = 1.0f);
  void set_max_value(float v) {
    max_value_ = v > 0.0f ? v : 1.0f;
    invalidate();
  }

  // 宽/窄模式切换（非中断式即时重绘，不重置动画与历史缓冲；src: src/components/layout/traffic-graph.tsx:107-110）
  void set_minimal_width(bool minimal);
  bool is_minimal_width() const { return minimal_width_; }
  int visible_points() const;
  size_t buffer_size() const { return down_.size(); }

  bool is_bezier() const { return bezier_style_; }
  void toggle_style();

  SizeF on_measure(float max_w, float max_h) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  HCURSOR cursor(const PointF& local) const override;

  // 绘制参数（配色由宿主 TrafficPane 按主题 token 注入；此处为缺省回退）
  Color4f up_color_ = Color4f::hex(0xFC9B76);                 // src: src/components/layout/traffic-graph.tsx:172 secondary.main(light)
  Color4f down_color_ = Color4f::hex(0x007AFF);               // src: src/components/layout/traffic-graph.tsx:173 primary.main(light)
  Color4f divider_color_ = Color4f(0.0f, 0.0f, 0.0f, 0.12f);  // src: src/components/layout/traffic-graph.tsx:171 palette.divider
  Color4f scale_text_color_ = Color4f(0.0f, 0.0f, 0.0f, 0.60f);  // src: src/components/layout/traffic-graph.tsx:174 text.secondary
  Color4f bg_ = Color4f(0.0f, 0.0f, 0.0f, 0.0f);              // src: src/components/layout/traffic-graph.tsx:216 透明底清画布
  float line_width = 2.5f;                                    // src: src/components/layout/traffic-graph.tsx:28,30 up/downLineWidth=2.5

 private:
  void trace_series(const std::vector<float>& data, float height, float dx,
                    float progress, std::vector<PointF>& out) const;

  std::vector<float> up_, down_;
  float max_value_ = 1.0f;
  float progress_ = 0.0f;
  int64_t anim_start_ms_ = 0;
  bool minimal_width_ = true;  // src: src/components/layout/traffic-graph.tsx:73 默认 isMinimalWidth = true
  bool bezier_style_ = true;   // src: src/components/layout/traffic-graph.tsx:79 styleRef = true
  bool pointer_down_ = false;
};

}  // namespace clashwin::ui
