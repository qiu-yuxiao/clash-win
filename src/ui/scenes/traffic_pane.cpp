// 流量面板实现（T14）。
#include "traffic_pane.h"

#include <windows.h>

#include <cmath>
#include <memory>

#include "../theme/theme.h"
#include "util/log.h"
#include "util/parse_traffic.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

using theme::ThemeService;

namespace {
int64_t default_now_ms() { return static_cast<int64_t>(GetTickCount64()); }
}  // namespace

void TrafficSeries::append(double up, double down) {
  points_.emplace_back(up, down);
  if (points_.size() > kTrafficMaxPoint + 2) points_.pop_front();  // ← :75
  double v = std::max(up, down);
  if (v > peak_) peak_ = v;
}

std::vector<float> TrafficSeries::up() const {
  std::vector<float> v;
  v.reserve(points_.size());
  for (auto& p : points_) v.push_back(static_cast<float>(p.first));
  return v;
}

std::vector<float> TrafficSeries::down() const {
  std::vector<float> v;
  v.reserve(points_.size());
  for (auto& p : points_) v.push_back(static_cast<float>(p.second));
  return v;
}

TrafficPane::TrafficPane(Widget* parent) : Widget(parent) {
  now_ms_ = [] { return static_cast<int64_t>(GetTickCount64()); };
  auto graph = std::make_unique<CanvasGraph>();
  graph_ = graph.get();
  graph_->flex_weight = 1;
  // ← traffic-graph.tsx:122-123：down=primary.main / up=secondary.main
  const auto& pal = theme::ThemeService::instance().palette();
  graph_->down_color_ = pal.primary_color;
  graph_->up_color_ = pal.secondary_color;
  add_child(std::move(graph));

  // 指标区：↓ 下载行 + ↑ 上传行（窄窗两行 ← '@media max-width 560px' column）
  auto mkrow = [&]() {
    auto row = std::make_unique<Row>();
    row->set_padding(4, 1, 4, 1);
    row->spacing = 4;
    return row;
  };
  auto down_row = mkrow();
  auto dl = std::make_unique<Label>();
  down_label_ = dl.get();
  down_label_->style.size = 13;
  down_label_->style.weight = static_cast<int>(Weight::Bold);
  down_label_->flex_weight = 1;
  down_row->add_child(std::move(dl));
  auto dt = std::make_unique<Label>();
  down_total_label_ = dt.get();
  down_total_label_->style.size = 9;
  down_total_label_->style.align_h = AlignH::Right;
  down_total_label_->flex_weight = 1;
  down_row->add_child(std::move(dt));
  down_row->flex_weight = 0;
  add_child(std::move(down_row));

  auto up_row = mkrow();
  auto ul = std::make_unique<Label>();
  up_label_ = ul.get();
  up_label_->style.size = 13;
  up_label_->style.weight = static_cast<int>(Weight::Bold);
  up_label_->flex_weight = 1;
  up_row->add_child(std::move(ul));
  auto ut = std::make_unique<Label>();
  up_total_label_ = ut.get();
  up_total_label_->style.size = 9;
  up_total_label_->style.align_h = AlignH::Right;
  up_total_label_->flex_weight = 1;
  up_row->add_child(std::move(ut));
  up_row->flex_weight = 0;
  add_child(std::move(up_row));
}

TrafficPane::~TrafficPane() = default;

bool TrafficPane::on_traffic_payload(const std::string& payload_text) {
  try {
    return on_traffic_json(nlohmann::json::parse(payload_text));
  } catch (const std::exception&) {
    return false;
  }
}

bool TrafficPane::on_traffic_json(const nlohmann::json& j) {
  if (!j.is_object() || !j.contains("up") || !j.contains("down")) return false;
  // 50ms 去重（← T14 卡①；WS 1s 一条通常不触发）
  int64_t now = now_ms_ ? now_ms_() : 0;
  if (last_payload_ms_ >= 0 && now - last_payload_ms_ < kTrafficDedupMs)
    return false;
  last_payload_ms_ = now;
  if (!enabled_) return false;  // 失焦停采（开关）

  double up = j.value("up", 0.0);
  double down = j.value("down", 0.0);
  down_total_ = static_cast<double>(j.value("downTotal", 0.0));
  up_total_ = static_cast<double>(j.value("upTotal", 0.0));
  series_.append(up, down);
  graph_->set_series(series_.up(), series_.down(),
                     static_cast<float>(std::max(series_.peak(), 1.0)));
  refresh_labels();
  for (auto& [id, cb] : subs_) cb();
  return true;
}

void TrafficPane::refresh_labels() {
  // ← parse_traffic（T02）：值+单位两段（窄字符 → 宽转换）
  auto wide = [](const std::string& s) {
    return clashwin::utf::utf8_to_utf16(s);
  };
  auto [dv, du] = clashwin::util::parse_traffic(
      series_.size() ? series_.down().back() : 0.0f);
  auto [uv, uu] = clashwin::util::parse_traffic(
      series_.size() ? series_.up().back() : 0.0f);
  down_text_ = wide(dv) + L" " + wide(du) + L"/s";
  up_text_ = wide(uv) + L" " + wide(uu) + L"/s";
  down_label_->set_text(L"↓ 下载: " + down_text_);
  up_label_->set_text(L"↑ 上传: " + up_text_);
  auto [dtv, dtu] = clashwin::util::parse_traffic(down_total_);
  auto [utv, utu] = clashwin::util::parse_traffic(up_total_);
  down_total_label_->set_text(L"总计: " + wide(dtv) + L" " + wide(dtu));
  up_total_label_->set_text(L"总计: " + wide(utv) + L" " + wide(utu));
}

void TrafficPane::set_mini(bool mini) {
  if (mini_ == mini) return;
  mini_ = mini;
  // Mini：只留流量条（指标行卸载近似 = 不可见），CanvasGraph 占满
  for (size_t i = 1; i < child_count(); ++i)
    child(i)->set_visible(!mini);
  invalidate();
}

void TrafficPane::on_arrange(const RectF& area) {
  set_bounds(area);
  // 【坐标约定】子布局局部坐标
  float metrics_h = mini_ ? 0 : kTrafficMetricsH * 2;
  float graph_h = std::max(area.h - metrics_h, 0.0f);
  child(0)->layout_tree({0, 0, area.w, graph_h});
  float y = graph_h;
  for (size_t i = 1; i < child_count(); ++i) {
    child(i)->layout_tree({0, y, area.w, kTrafficMetricsH});
    y += kTrafficMetricsH;
  }
}

void TrafficPane::on_paint(IRenderTarget& rt) {
  // 指标区文字色（← --download-text #006064 / --upload-text hsl(38,100%,30%)）
  down_label_->style.color = Color4f::hex(0x006064);
  up_label_->style.color = Color4f::hex(0x996E00);  // hsl(38,100%,30%) 近似
  down_total_label_->style.color = Color4f::hex(0x006064);
  up_total_label_->style.color = Color4f::hex(0x996E00);
}

uint64_t TrafficPane::subscribe(std::function<void()> cb) {
  uint64_t id = next_id_++;
  subs_.emplace_back(id, std::move(cb));
  return id;
}

void TrafficPane::unsubscribe(uint64_t id) {
  subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
                             [id](const auto& p) { return p.first == id; }),
              subs_.end());
}

}  // namespace clashwin::ui::scene
