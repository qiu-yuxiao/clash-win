// 节点数据装配实现（T13）。
#include <windows.h>

#include "node_data.h"

#include <algorithm>
#include <cctype>

#include "cmd/cmd_support.h"
#include "util/log.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

namespace utf = clashwin::utf;

std::wstring format_delay(int delay) {
  // ← services/delay.ts formatDelay
  if (delay == -1) return L"-";
  if (delay == -2) return L"testing";
  if (delay == 0 || delay >= kNodeDelayMaxMs) return L"Timeout";
  if (delay > 100000) return L"Error";
  return std::to_wstring(delay);
}

bool NodeData::load() {
  std::vector<std::string> all;
  std::string now;
  if (!hooks_.fetch_group || !hooks_.fetch_group(&all, &now)) {
    CWLOG_WARN("NodeData: fetch_group 失败（内核未就绪？）");
    return false;
  }
  std::map<std::string, mihomo::ProxyItem> items;
  if (hooks_.fetch_proxies && hooks_.fetch_proxies(&items)) {
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

int NodeData::row_delay(const std::string& name) const {
  auto it = delay_cache_.find(name);
  if (it == delay_cache_.end()) return -1;
  // TTL 30min：读时惰性判过期（← delay.ts:68/260 now-updatedAt>TTL）
  int64_t now = hooks_.now_ms ? hooks_.now_ms() : 0;
  if (now - it->second.second > kDelayCacheTtlMs) return -1;
  return it->second.first;
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
    return static_cast<bool>(
        r.value()->delay_group(g, "", kNodeDelayMaxMs).is_ok());
  };
  h.now_ms = [] { return static_cast<int64_t>(GetTickCount64()); };
  return h;
}

}  // namespace clashwin::ui::scene
