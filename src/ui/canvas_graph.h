#pragma once
// CanvasGraph（T10）：图表画布底座——数据入 → 贝塞尔曲线绘制（T14 流量图用）。
// src: components/layout/traffic-graph.tsx —— 无轴/浅底/双曲线（默认 bezier，:41）：
//   down=primary.main(:123)、up=secondary.main(:122)、maxPoint 30(:6)、
//   Y 轴 8 段分段映射(:172-184)、quadraticCurveTo(:202)、up→down 绘制顺序(:231-253)。
#include <cstdint>
#include <vector>

#include "widget.h"

namespace clashwin::ui {

class CanvasGraph : public Widget {
 public:
  explicit CanvasGraph(Widget* parent = nullptr) : Widget(parent) {
    up_.assign(kMaxPoint + 2, 0.0f);
    down_.assign(kMaxPoint + 2, 0.0f);
  }

  // 数据入（调用方维护序列长度 ≤ max_point，超出自行裁剪或 T14 采样链管）
  // max_value 仅作兼容保留：Y 轴按 Mini 的 8 段固定映射，不再按峰值归一。
  void set_series(std::vector<float> up, std::vector<float> down,
                  float max_value);
  void set_max_value(float v) {
    max_value_ = v > 0 ? v : 1;
    invalidate();
  }

  static constexpr int kMaxPoint = 30;

  SizeF on_measure(float max_w, float max_h) override;
  void on_paint(IRenderTarget& rt) override;

  // 绘制参数（配色由宿主 TrafficPane 按主题 token 注入；此处为缺省回退）
  Color4f up_color_ = Color4f::hex(0xFC9B76);      // src: secondary.main(light)
  Color4f down_color_ = Color4f::hex(0x007AFF);    // src: primary.main(light)
  Color4f bg_ = Color4f(0, 0, 0, 0);               // 卡底由宿主绘制，画布透明
  Color4f line_ = Color4f(0, 0, 0, 0.018f);  // src: traffic-graph.tsx:121 divider(rgba(0,0,0,.12))
                                             //   × :8 refLineAlpha .15 = .018（深色由宿主刷新）
  float line_width = 2.5f;                          // src: traffic-graph.tsx:12,15 up/downLineWidth=2.5

 private:
  void draw_series(IRenderTarget& rt, const std::vector<float>& data,
                   const Color4f& c, float offset);

  std::vector<float> up_, down_;
  float max_value_ = 1;
  int64_t anim_start_ms_ = 0;
};

}  // namespace clashwin::ui
