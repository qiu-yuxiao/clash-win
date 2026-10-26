#pragma once
// CanvasGraph（T10）：图表画布底座——数据入 → 折线绘制接口（T14 流量图用）。
// 对照 traffic-graph.tsx：无轴/浅底/双折线（↓蓝 #0A84FF / ↑紫），maxPoint 30。
#include <vector>

#include "widget.h"

namespace clashwin::ui {

class CanvasGraph : public Widget {
 public:
  explicit CanvasGraph(Widget* parent = nullptr) : Widget(parent) {}

  // 数据入（调用方维护序列长度 ≤ max_point，超出自行裁剪或 T14 采样链管）
  void set_series(std::vector<float> up, std::vector<float> down,
                  float max_value);
  void set_max_value(float v) {
    max_value_ = v > 0 ? v : 1;
    invalidate();
  }

  static constexpr int kMaxPoint = 30;

  SizeF on_measure(float max_w, float max_h) override;
  void on_paint(IRenderTarget& rt) override;

  // 绘制参数（对照原版配色）
  Color4f up_color_ = Color4f::hex(0xAF52DE);      // ↑ 紫
  Color4f down_color_ = Color4f::hex(0x0A84FF);    // ↓ 蓝
  Color4f bg_ = Color4f::hex(0x000000, 0.03f);     // 浅灰底
  Color4f line_ = Color4f::hex(0x000000, 0.08f);
  float line_width = 1.5f;

 private:
  void draw_series(IRenderTarget& rt, const std::vector<float>& data,
                   const Color4f& c);

  std::vector<float> up_, down_;
  float max_value_ = 1;
};

}  // namespace clashwin::ui
