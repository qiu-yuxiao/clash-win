#pragma once
// 下栏流量区（T14）：CanvasGraph 接 /traffic WS + 速率文本 + Mini 态布局。
// ← mini-traffic-panel.tsx（图 flex1 + 指标区 22px/窄窗两行）
//   + traffic-graph.tsx（maxPoint 30、1s 采样、10fps、折线色
//     down=primary.main 蓝 / up=secondary.main——**源码口径**，census 口述
//     "↑紫"为 secondary 暗色 #FF9F0A 橙的目测偏差）
//   + parse_traffic（T02）格式化 KB/s MB/s
//   + CSS --download/--upload 色系（index.scss:304-312）
// Mini 态（高≤126）：只留流量条且占满上栏空间（← census 极小窗布局）。
#include <cstdint>
#include <deque>
#include <functional>

#include "../canvas_graph.h"
#include "../container.h"
#include "../label.h"
#include "../widget.h"
#include "connection_data.h"  // now_ms 注入约定

namespace clashwin::ui::scene {

inline constexpr size_t kTrafficMaxPoint = 30;       // ← traffic-graph.tsx:6
inline constexpr int64_t kTrafficDedupMs = 50;       // ← T14 卡 50ms 去重
inline constexpr float kTrafficMetricsH = 22.0f;     // ← 指标行 22px

// 采样序列（滚动窗 maxPoint+2、峰值跟踪；CanvasGraph 峰值自动缩放=线性 max）
class TrafficSeries {
 public:
  void append(double up, double down);
  // 喂 CanvasGraph（倒序：最新在尾）
  std::vector<float> up() const;
  std::vector<float> down() const;
  double peak() const { return peak_; }
  size_t size() const { return points_.size(); }

 private:
  std::deque<std::pair<double, double>> points_;  // (up, down) 新的在尾
  double peak_ = 0;
};

class TrafficPane : public Widget {
 public:
  TrafficPane(Widget* parent = nullptr);
  ~TrafficPane() override;

  // ← /traffic WS payload {"up":..,"down":..}（10fps；50ms 内去重丢弃）
  // 返回 true = 已入窗
  bool on_traffic_payload(const std::string& payload_text);
  bool on_traffic_json(const nlohmann::json& j);

  // 失焦停采开关（T14 卡⑤；宿主 WM_ACTIVATE 接）
  void set_enabled(bool on) { enabled_ = on; }

  void set_mini(bool mini);  // Mini：只留流量条占满
  bool is_mini() const { return mini_; }

  // 变更通知（UI invalidate）
  uint64_t subscribe(std::function<void()> cb);
  void unsubscribe(uint64_t id);
  void set_now_ms(std::function<int64_t()> f) { now_ms_ = std::move(f); }

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;

  // 测试观察
  TrafficSeries& series() { return series_; }
  CanvasGraph* graph() const { return graph_; }
  const std::wstring& down_text() const { return down_text_; }
  const std::wstring& up_text() const { return up_text_; }

 private:
  void relayout();
  void refresh_labels();

  TrafficSeries series_;
  CanvasGraph* graph_ = nullptr;
  Label* down_label_ = nullptr;
  Label* up_label_ = nullptr;
  Label* down_total_label_ = nullptr;
  Label* up_total_label_ = nullptr;
  bool mini_ = false;
  bool enabled_ = true;
  int64_t last_payload_ms_ = -1;
  double down_total_ = 0, up_total_ = 0;
  std::wstring down_text_, up_text_;
  std::vector<std::pair<uint64_t, std::function<void()>>> subs_;
  std::function<int64_t()> now_ms_;  // 缺省 GetTickCount64（见 .cpp）
  uint64_t next_id_ = 1;
};

}  // namespace clashwin::ui::scene
