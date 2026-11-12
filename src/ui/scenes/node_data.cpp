// 节点数据装配实现（T13）。
#include <windows.h>

#include "node_data.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <memory>
#include <sstream>

#include "cmd/cmd_support.h"
#include "config/config.h"
#include "core/auto_select.h"
#include "events/event_bus.h"
#include "ui/ui_postbox.h"
#include "util/log.h"
#include "util/node.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

namespace utf = clashwin::utf;

std::wstring format_delay(int delay) {
  // ← src/services/delay.ts:454-460 formatDelay（-1='-'；-2='testing'；0/≥timeout 且 ≤1e5 = Timeout；>1e5 = Error）
  if (delay == -1) return L"-";  // src: src/services/delay.ts:455
  if (delay == -2) return L"testing";  // src: src/services/delay.ts:456
  if (delay == 0 || (delay >= kNodeDelayMaxMs && delay <= 100000)) return L"Timeout";  // src: src/services/delay.ts:457
  if (delay > 100000) return L"Error";  // src: src/services/delay.ts:458
  return std::to_wstring(delay);
}

bool NodeData::load() {
  std::vector<std::string> raw;
  std::string now;
  if (!hooks_.fetch_group || !hooks_.fetch_group(&raw, &now)) {
    CWLOG_WARN("NodeData: fetch_group 失败（内核未就绪？）");
    return false;
  }
  // ← MF:services/cmds.ts:344-349 calcuProxies：
  //   all.filter((item) => item?.name && !isDummyNode(item.name))
  //   无订阅（裸配置 PROXY 组 all=[DIRECT]）时列表应为空态，不得展示 DIRECT。
  std::vector<std::string> all;
  all.reserve(raw.size());
  for (const auto& name : raw) {
    if (!name.empty() && !clashwin::util::is_dummy_node(name))
      all.push_back(name);
  }
  std::map<std::string, mihomo::ProxyItem> items;
  if (hooks_.fetch_proxies && hooks_.fetch_proxies(&items)) {
    // src: delay.ts:283-295 缓存过期后回落到 kernel history 最后一条记录
    proxy_history_.clear();
    for (const auto& [name, item] : items) {
      ProxyHistory h;
      h.alive = item.alive;  // ★ 保留内核 alive 真源（← delay.ts:279-281 alive===false → 0）
      if (!item.history.empty()) {
        h.delay = static_cast<int>(item.history.back().delay);
        h.time = item.history.back().time;
      }
      proxy_history_[name] = std::move(h);
    }
    // 细节合并：成员顺序按 group.all，type 从 get_proxies 细节取
    rows_.clear();
    rows_.reserve(all.size());
    for (const auto& name : all) {
      NodeRow row;
      row.name = name;
      auto it = items.find(name);
      if (it != items.end()) row.type = it->second.type;
      rows_.push_back(std::move(row));
    }
  } else {
    // 细节缺失仍展示成员（type 空串）
    rows_.clear();
    for (const auto& name : all) rows_.push_back({name, ""});
  }
  now_ = now;
  notify();
  return true;
}

bool NodeData::apply_delay_results(
    const std::vector<std::pair<std::string, int>>& items, bool from_probe) {
  if (items.empty()) return false;
  const int64_t now = hooks_.now_ms ? hooks_.now_ms() : 0;
  bool any_visible_changed = false;
  for (const auto& [name, delay] : items) {
    if (name.empty()) continue;
    const int before_vis = row_delay(name);
    int64_t ttl = kDelayCacheTtlMs;
    if (!from_probe) {
      // src: src/services/delay.ts:408-413 复用缓存指标保鲜上限钳制在 30s，不给即将过期的旧记录续命
      ttl = kReusedMetricsMaxTtlMs;
      auto it = delay_cache_.find(name);
      if (it != delay_cache_.end() && it->second.delay != -2) {
        const int64_t rem = it->second.ttl_ms - (now - it->second.updated_at_ms);
        if (rem > 0 && rem < ttl) ttl = rem;
      }
    }
    delay_cache_[name] = DelayCacheEntry{delay, now, ttl};
    if (row_delay(name) != before_vis) any_visible_changed = true;
  }
  // BUG-339: 仅当至少一个节点的可见延迟发生变化时才广播 UI 重绘通知
  if (any_visible_changed) notify();
  return true;
}

bool NodeData::apply_delay_results(const nlohmann::json& payload, bool from_probe) {
  if (!payload.is_object() || !payload.contains("results") ||
      !payload["results"].is_array())
    return false;
  if (payload.contains("fromProbe") && payload["fromProbe"].is_boolean()) {
    from_probe = payload["fromProbe"].get<bool>();
  }
  std::vector<std::pair<std::string, int>> items;
  items.reserve(payload["results"].size());
  for (const auto& item : payload["results"]) {
    // results: [[name, delay], ...]（← notification.rs Vec<(String, u32)>）
    if (!item.is_array() || item.size() < 2) continue;
    items.emplace_back(item[0].get<std::string>(), item[1].get<int>());
  }
  return apply_delay_results(items, from_probe);
}

void NodeData::set_delays_batch(const std::vector<std::string>& names, int delay) {
  if (names.empty()) return;
  const int64_t now = hooks_.now_ms ? hooks_.now_ms() : 0;
  bool any_visible_changed = false;
  for (const auto& name : names) {
    if (name.empty()) continue;
    const int before_vis = row_delay(name);
    delay_cache_[name] = DelayCacheEntry{delay, now, kDelayCacheTtlMs};
    if (row_delay(name) != before_vis) any_visible_changed = true;
  }
  if (any_visible_changed) notify();
}

void NodeData::set_delay(const std::string& name, int delay) {
  if (name.empty()) return;
  const int64_t now = hooks_.now_ms ? hooks_.now_ms() : 0;
  const int before_vis = row_delay(name);
  delay_cache_[name] = DelayCacheEntry{delay, now, kDelayCacheTtlMs};
  if (row_delay(name) != before_vis) notify();
}

NodeData::DelaySnapshotMap NodeData::snapshot_delays(
    const std::vector<std::string>& names) const {
  DelaySnapshotMap snaps;
  for (const auto& name : names) {
    if (name.empty()) continue;
    auto it = delay_cache_.find(name);
    if (it != delay_cache_.end()) {
      snaps[name] = it->second;
    } else {
      snaps[name] = std::nullopt;
    }
  }
  return snaps;
}

bool NodeData::restore_delays_for_still_pending(const DelaySnapshotMap& snaps) {
  bool any_changed = false;
  for (const auto& [name, snap] : snaps) {
    auto it = delay_cache_.find(name);
    if (it == delay_cache_.end() || it->second.delay != -2) continue;  // src: src/services/delay.ts:456
    if (snap.has_value()) {
      it->second = *snap;
    } else {
      delay_cache_.erase(it);
    }
    any_changed = true;
  }
  if (any_changed) notify();
  return any_changed;
}

bool NodeData::select(const std::string& node) {
  if (!hooks_.select_node || !hooks_.select_node(node)) {
    CWLOG_WARN("NodeData: select_node 失败 → UI 回弹");
    return false;
  }
  if (now_ != node) {
    now_ = node;
    notify();
  }
  return true;
}

std::string NodeData::set_now_optimistic(const std::string& node) {
  std::string prev = now_;
  if (now_ != node) {
    now_ = node;
    notify();
  }
  return prev;
}

bool NodeData::test_group() {
  if (!hooks_.test_group || !hooks_.test_group(group_)) return false;
  // 组测速结果经 backend-delay-results 事件回灌（此处标记 testing 由 UI 决定）
  return true;
}

// ★ §5.2/§5.6「所见即所选」：批量测速可见子集 + 择优切换
//   ← Mini proxy-groups.tsx:230-275 handleCheckAll
//   视觉占位：先置 -2（testing）触发流光动画，再委托后端统一引擎
bool NodeData::test_and_select(const std::vector<std::string>& nodes) {
  std::vector<std::string> target_names = nodes;
  if (target_names.empty()) {
    target_names.reserve(rows_.size());
    for (const auto& r : rows_) {
      if (!r.name.empty() && !clashwin::util::is_dummy_node(r.name)) {
        target_names.push_back(r.name);
      }
    }
  }
  // BUG-321: 仅为当前尚未处于 -2 的节点建立快照（若 UI 线程已提前拍摄快照并置 -2，外层也会兜底恢复）
  std::vector<std::string> non_pending;
  non_pending.reserve(target_names.size());
  for (const auto& n : target_names) {
    auto it = delay_cache_.find(n);
    if (it == delay_cache_.end() || it->second.delay != -2) {  // src: src/services/delay.ts:456
      non_pending.push_back(n);
    }
  }
  const DelaySnapshotMap internal_snaps = snapshot_delays(non_pending);
  if (!non_pending.empty()) set_delays_batch(non_pending, -2);  // src: src/services/delay.ts:456
  last_batch_summary_ = BatchTestSummary{};
  if (!hooks_.test_and_select) {
    restore_delays_for_still_pending(internal_snaps);
    return false;
  }
  std::string fastest = hooks_.test_and_select(nodes);
  bool ok = !fastest.empty();
  if (hooks_.last_batch_summary) {
    last_batch_summary_ = hooks_.last_batch_summary();
  } else {
    last_batch_summary_.executed = true;
    last_batch_summary_.selected = ok;
    last_batch_summary_.selected_node = fastest;
    last_batch_summary_.selected_delay_ms = ok ? row_delay(fastest) : -1;
  }
  auto anchor = self_;
  if (ok) {
    CWLOG_INFO("NodeData::test_and_select: 群测最优节点 = " + fastest +
               "，执行切换");
    UiPostbox::instance().post([anchor, fastest, internal_snaps] {
      if (!anchor) return;
      NodeData* self = nullptr;
      {
        std::lock_guard<std::mutex> lk(anchor->mu);
        self = anchor->ptr;
      }
      if (self) {
        self->restore_delays_for_still_pending(internal_snaps);
        self->select(fastest);
      }
    });
  } else {
    std::vector<std::string> all;
    std::string now;
    const bool fetched = hooks_.fetch_group && hooks_.fetch_group(&all, &now);
    UiPostbox::instance().post([anchor, fetched, now, internal_snaps] {
      if (!anchor) return;
      NodeData* self = nullptr;
      {
        std::lock_guard<std::mutex> lk(anchor->mu);
        self = anchor->ptr;
      }
      if (self) {
        self->restore_delays_for_still_pending(internal_snaps);
        if (fetched) {
          self->now_ = now;
          self->notify();
        }
      }
    });
  }
  return ok;
}

namespace {
// RFC3339 → epoch ms（无时区按 UTC，等价 JS Date.parse 的 Z 分支）
bool parse_iso_time_ms(const std::string& s, int64_t* out) {
  if (s.size() < 19) return false;
  std::tm tm{};
  std::istringstream iss(s.substr(0, 19));
  iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
  if (iss.fail()) return false;
  *out = static_cast<int64_t>(_mkgmtime(&tm)) * 1000;
  return true;
}
}  // namespace

int NodeData::row_delay(const std::string& name) const {
  int64_t now = hooks_.now_ms ? hooks_.now_ms() : 0;
  auto it = delay_cache_.find(name);
  if (it != delay_cache_.end()) {
    // -2（测速中）永不按 TTL 惰性过期；完成或失败时由 apply_delay_results / restore_delays_for_still_pending 收敛
    if (it->second.delay == -2) return -2;  // src: src/services/delay.ts:456
    if (now - it->second.updated_at_ms <= it->second.ttl_ms)
      return it->second.delay;
  }
  // src: delay.ts:279-295 alive===false → 一律 0（Timeout），禁止陈旧 history 冒充当前延迟；
  //   否则回落内核 history 最后一条，过旧(>10min)按 0 判死
  auto h = proxy_history_.find(name);
  if (h != proxy_history_.end()) {
    if (!h->second.alive) return 0;  // ★ 内核已判死 → 一律按 Timeout(0) 呈现（❌2）
    if (h->second.delay >= 0) {
      int64_t rec = 0;
      if (parse_iso_time_ms(h->second.time, &rec)) {
        if (now - rec > kHistoryFallbackMaxAgeMs) return 0;
      }
      return h->second.delay;
    }
  }
  return -1;
}

uint64_t NodeData::subscribe(std::function<void()> cb) {
  uint64_t id = next_id_++;
  subs_.emplace_back(id, std::move(cb));
  return id;
}

void NodeData::unsubscribe(uint64_t id) {
  subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
                             [id](const auto& p) { return p.first == id; }),
              subs_.end());
}

DataHooks NodeData::production_hooks(const std::string& group) {
  DataHooks h;
  h.fetch_group = [group](std::vector<std::string>* all, std::string* now) {
    auto r = cmd::mihomo();  // T09 访问器（Result<MihomoClient*>）
    if (!r.is_ok()) return false;
    mihomo::MihomoClient* client = r.value();
    auto g = client->get_group_by_name(group);
    if (!g.is_ok()) return false;
    *all = g.value().all.value_or(std::vector<std::string>{});
    *now = g.value().now.value_or("");
    return true;
  };
  h.fetch_proxies = [](std::map<std::string, mihomo::ProxyItem>* items) {
    auto r = cmd::mihomo();
    if (!r.is_ok()) return false;
    mihomo::MihomoClient* client = r.value();
    auto px = client->get_proxies();
    if (!px.is_ok()) return false;
    *items = std::move(px.value().proxies);
    return true;
  };
  auto select_err = std::make_shared<std::string>();
  h.select_node = [group, select_err](const std::string& node) {
    auto r = cmd::mihomo();
    if (!r.is_ok()) {
      *select_err = r.error().message;
      return false;
    }
    auto sr = r.value()->select_node_for_group(group, node);
    if (!sr.is_ok()) {
      *select_err = sr.error().message;
      return false;
    }
    select_err->clear();
    return true;
  };
  h.last_select_error = [select_err] { return *select_err; };
  h.test_group = [group](const std::string& g) {
    auto r = cmd::mihomo();
    if (!r.is_ok()) return false;
    // 组测速结果由内核回灌 backend-delay-results 事件（T05 WS/事件链）
    // test_url 与 cmd_profile 探针同源：verge.default_latency_test 优先
    std::string url = "https://cp.cloudflare.com/generate_204";
    try {
      const auto& verge = *config::Config::instance().verge().latest_arc();
      url = verge.default_latency_test.value_or(url);
    } catch (...) {}
    auto dr = r.value()->delay_group(g, url, kNodeDelayMaxMs);
    if (!dr.is_ok()) {
      CWLOG_WARN("test_group delay_group 失败: " + dr.error().message);
      return false;
    }
    CWLOG_INFO("test_group 完成，返回节点数=" +
               std::to_string(dr.value().size()));
    // HTTP 响应即全量结果 → 直接发事件回灌缓存
    std::vector<std::pair<std::string, uint32_t>> vec;
    for (const auto& [name, ms] : dr.value()) vec.emplace_back(name, ms);
    events::EventBus::backend_delay_results(g, vec);
    return true;
  };
  h.now_ms = [] { return static_cast<int64_t>(GetTickCount64()); };
  // ★ §5.2/§5.6「所见即所选」：可见子集 → 测速 + 择优切换
  //   ← Mini proxy-groups.tsx:230-275 handleCheckAll
  //     triggerAutoSelect(currentUid, visibleNames, 0, true)
  //   → cmd trigger_auto_select（cmd_profile.cpp:585）→ AutoSelect::trigger
  auto batch_sum = std::make_shared<BatchTestSummary>();
  h.test_and_select = [batch_sum](const std::vector<std::string>& nodes) -> std::string {
    *batch_sum = BatchTestSummary{};
    std::string uid;
    try {
      auto latest = config::Config::instance().profiles().latest_arc();
      if (latest && latest->current) uid = *latest->current;
    } catch (...) {}
    if (uid.empty()) return "";
    // sort_type=0（head state）；select=true（择优切换）
    // nodes 为空 → 全量（与自动优选同语义）
    auto outcome = core::AutoSelect::instance().trigger(
        uid, nodes.empty() ? std::nullopt
                            : std::optional<std::vector<std::string>>(nodes),
        /*sort_type=*/0, /*select=*/true);
    batch_sum->busy = outcome.busy;
    batch_sum->executed = !outcome.busy && !outcome.display.empty();
    batch_sum->selected = outcome.selected && !outcome.selected_node.empty();
    if (batch_sum->selected) {
      batch_sum->selected_node = outcome.selected_node;
      for (const auto& [nm, ms] : outcome.display) {
        if (nm == outcome.selected_node) {
          batch_sum->selected_delay_ms = static_cast<int>(ms);
          break;
        }
      }
    }
    if (outcome.busy) {
      CWLOG_WARN("test_and_select: AUTO_SELECT_BUSY（互斥占用，本次不执行）");
      return "";
    }
    // 结果经 backend-delay-results 事件回灌缓存（AutoSelect 内部已发）
    return batch_sum->selected ? batch_sum->selected_node : "";
  };
  h.last_batch_summary = [batch_sum] { return *batch_sum; };
  return h;
}

}  // namespace clashwin::ui::scene
