// 节点数据装配实现（T13）。
#include <windows.h>

#include "node_data.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
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
  // ← services/delay.ts formatDelay（0/≥timeout 且 ≤1e5 = Timeout；>1e5 = Error）
  if (delay == -1) return L"";  // src: proxy-item.tsx:215「{delayValue >= 0 && …}」未测空白
  if (delay == -2) return L"testing";
  if (delay == 0 || (delay >= kNodeDelayMaxMs && delay <= 100000)) return L"Timeout";
  if (delay > 100000) return L"Error";
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

bool NodeData::apply_delay_results(const nlohmann::json& payload) {
  if (!payload.is_object() || !payload.contains("results") ||
      !payload["results"].is_array())
    return false;
  int64_t now = hooks_.now_ms ? hooks_.now_ms() : 0;
  int applied = 0;
  for (const auto& item : payload["results"]) {
    // results: [[name, delay], ...]（← notification.rs Vec<(String, u32)>）
    if (!item.is_array() || item.size() < 2) continue;
    std::string name = item[0].get<std::string>();
    int delay = item[1].get<int>();
    delay_cache_[name] = {delay, now};
    ++applied;
  }
  if (applied > 0) notify();  // 局部刷新：不重载列表
  return applied > 0;
}

void NodeData::set_delays_batch(const std::vector<std::string>& names, int delay) {
  int64_t now = hooks_.now_ms ? hooks_.now_ms() : 0;
  for (const auto& name : names) delay_cache_[name] = {delay, now};
  if (!names.empty()) notify();
}

void NodeData::set_delay(const std::string& name, int delay) {
  int64_t now = hooks_.now_ms ? hooks_.now_ms() : 0;
  delay_cache_[name] = {delay, now};
  notify();
}

bool NodeData::select(const std::string& node) {
  if (!hooks_.select_node || !hooks_.select_node(node)) {
    CWLOG_WARN("NodeData: select_node 失败 → UI 回弹");
    return false;
  }
  now_ = node;
  notify();
  return true;
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
  // 视觉占位：立即将待测节点标记为「测速中」（← Mini setDelay(name, group, -2)）
  bool need_batch = false;
  for (const auto& n : nodes) {
    auto it = delay_cache_.find(n);
    if (it == delay_cache_.end() || it->second.first != -2) {
      need_batch = true;
      break;
    }
  }
  if (need_batch) set_delays_batch(nodes, -2);
  if (!hooks_.test_and_select) return false;
  bool ok = hooks_.test_and_select(nodes);
  if (ok && hooks_.fetch_group) {
    std::vector<std::string> all;
    std::string now;
    if (hooks_.fetch_group(&all, &now)) {
      UiPostbox::instance().post([this, now] {
        now_ = now;
        notify();
      });
    }
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
  if (it != delay_cache_.end() && now - it->second.second <= kDelayCacheTtlMs)
    return it->second.first;
  // src: delay.ts:284-295 缓存过期 → 内核 history 最后一条；过旧(>10min)按 0 判死
  auto h = proxy_history_.find(name);
  if (h != proxy_history_.end() && h->second.delay >= 0) {
    int64_t rec = 0;
    if (parse_iso_time_ms(h->second.time, &rec)) {
      if (now - rec > kHistoryFallbackMaxAgeMs) return 0;
    }
    return h->second.delay;
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
  h.select_node = [group](const std::string& node) {
    auto r = cmd::mihomo();
    if (!r.is_ok()) return false;
    return static_cast<bool>(
        r.value()->select_node_for_group(group, node).is_ok());
  };
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
  h.test_and_select = [](const std::vector<std::string>& nodes) {
    std::string uid;
    try {
      auto latest = config::Config::instance().profiles().latest_arc();
      if (latest && latest->current) uid = *latest->current;
    } catch (...) {}
    if (uid.empty()) return false;
    // sort_type=0（head state）；select=true（择优切换）
    // nodes 为空 → 全量（与自动优选同语义）
    auto outcome = core::AutoSelect::instance().trigger(
        uid, nodes.empty() ? std::nullopt
                            : std::optional<std::vector<std::string>>(nodes),
        /*sort_type=*/0, /*select=*/true);
    if (outcome.busy) {
      CWLOG_WARN("test_and_select: AUTO_SELECT_BUSY（互斥占用，本次不执行）");
      return false;
    }
    // 结果经 backend-delay-results 事件回灌缓存（AutoSelect 内部已发）
    return true;
  };
  return h;
}

}  // namespace clashwin::ui::scene
