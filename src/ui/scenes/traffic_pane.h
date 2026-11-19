#pragma once
// 下栏流量区（T14 / BUG-410）：单一随窗宽自适应 CanvasGraph 接 /traffic WS + 指标卡 + Mini 态布局。
// src: pages/_layout.tsx:1480-1501 容器（宽 135 / 窄 100，padding 8 12 2 12 / 3 6 2 6，background inherit）
//   + _layout/components/mini-traffic-panel.tsx:40-330（单图 flex1 + 指标卡行，
//     卡高 18、三段文字 9/13/8px、色取 CSS fallback——--download-text 等
//     仅 original 皮肤定义 index.scss:304-312，retro-3d 默认皮肤走 fallback）
//   + utils/button-styles.ts:520-533/549 get3DCardStyle('download'/'upload')
//   + components/layout/traffic-graph.tsx（63 点历史缓冲、1s 采样、窄 30s→宽 60s 自适应）
//   + parse_traffic（T02）格式化 KB/s MB/s
// 迷你窗（isMiniStatus）：面板总高 100（_layout.tsx:1484），四张指标卡照常渲染。
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "../canvas_graph.h"
#include "../label.h"
#include "../widget.h"
#include "util/parse_traffic.h"

namespace clashwin::ui::scene {

inline constexpr size_t kTrafficMaxPoint =
    static_cast<size_t>(clashwin::util::kTrafficGraphMaxPoints);   // src: utils/parse-traffic.ts:4
inline constexpr size_t kTrafficBufferLen =
    static_cast<size_t>(clashwin::util::kTrafficGraphBufferLen);   // src: utils/parse-traffic.ts:6
inline constexpr int64_t kTrafficSampleMs = 1000;                  // src: components/layout/traffic-graph.tsx:32 sampleIntervalMs

// 采样序列（63 点滚动缓冲、峰值跟踪；CanvasGraph Y 轴走分段对数映射）
class TrafficSeries {
 public:
  TrafficSeries();
  void append(double up, double down);
  // 喂 CanvasGraph（最新在尾）
  std::vector<float> up() const;
  std::vector<float> down() const;
  double peak() const { return peak_; }
  size_t size() const { return points_.size(); }
  bool has_traffic() const;  // 序列内是否出现过非零流量（零流量守卫）

 private:
  std::deque<std::pair<double, double>> points_;  // (up, down) 新的在尾
  double peak_ = 0;
};

class TrafficPane : public Widget {
 public:
  explicit TrafficPane(Widget* parent = nullptr);
  ~TrafficPane() override;

  // ← /traffic WS payload {"up":..,"down":..}（10fps 到达；按 1s 采样入窗，
  //   src: components/layout/traffic-graph.tsx:32, 116-130 + 零流量守卫）
  // 返回 true = 已接收（缓存；是否入窗取决于采样节拍）
  bool on_traffic_payload(const std::string& payload_text);
  bool on_traffic_json(const nlohmann::json& j);

  // 失焦停绘开关（T14 卡⑤；宿主 WM_SIZE MINIMIZED 接）
  void set_enabled(bool on);

  void set_mini(bool mini);  // 迷你窗：四卡仍常驻，仅面板总高 100（_layout.tsx:1484）
  bool is_mini() const { return mini_; }
  // 宽/窄窗切换（只更新 CanvasGraph::set_minimal_width 与内距，绝不销毁重建；src: _layout.tsx:1483-1493）
  void set_narrow(bool narrow);
  bool is_narrow() const { return narrow_; }

  // 变更通知（UI invalidate）
  uint64_t subscribe(std::function<void()> cb);
  void unsubscribe(uint64_t id);
  void set_now_ms(std::function<int64_t()> f) { now_ms_ = std::move(f); }

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;

  A11yRole a11y_default_role() const override { return A11yRole::Group; }
  std::wstring a11y_default_name() const override;

  // 测试观察
  TrafficSeries& series() { return series_; }
  CanvasGraph* graph() const { return graph_; }
  const std::wstring& down_text() const { return down_text_; }
  const std::wstring& up_text() const { return up_text_; }

 private:
  void refresh_labels();

  TrafficSeries series_;
  CanvasGraph* graph_ = nullptr;
  // 指标卡文字段（src: mini-traffic-panel.tsx:108-328 每卡 prefix 9px bold +
  //   value 13px bold + unit 8px normal；dl=下载速率 dt=下载总量 ul/ut=上传）
  Label* dl_prefix_ = nullptr; Label* dl_value_ = nullptr; Label* dl_unit_ = nullptr;
  Label* dt_prefix_ = nullptr; Label* dt_value_ = nullptr; Label* dt_unit_ = nullptr;
  Label* ul_prefix_ = nullptr; Label* ul_value_ = nullptr; Label* ul_unit_ = nullptr;
  Label* ut_prefix_ = nullptr; Label* ut_value_ = nullptr; Label* ut_unit_ = nullptr;
  // 指标卡几何（on_arrange 算，on_paint 画；src: mini-traffic-panel.tsx:57-330）
  RectF graph_rect_, dl_l_, dl_r_, ul_l_, ul_r_;
  float metrics_border_y_ = -1.0f;  // 宽窗指标行 borderTop（src: mini-traffic-panel.tsx:64-65）
  PointF dl_icon_{}, ul_icon_{};  // 仅速率卡有箭头图标（总量卡无图标 :150-204）
  bool mini_ = false;
  bool narrow_ = false;
  bool enabled_ = true;
  int64_t last_sample_ms_ = -1;  // 上一次 1s 采样入窗时刻
  double down_total_ = 0, up_total_ = 0;
  std::wstring down_text_, up_text_;
  std::string last_lang_;
  std::vector<std::pair<uint64_t, std::function<void()>>> subs_;
  std::function<int64_t()> now_ms_;
  uint64_t next_id_ = 1;
};

}  // namespace clashwin::ui::scene
