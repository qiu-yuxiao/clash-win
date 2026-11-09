#pragma once
// 节点数据装配（T13）：PROXY 组成员加载 + 延迟缓存（TTL/判死）+ 事件局部刷新。
// ← services/delay.ts（CACHE_TTL 30min / NODE_DELAY_MIN 30 / NODE_DELAY_MAX 5000
//   / formatDelay 五档色映射；BUG-304：2000→5000 与后端判死阈值统一）+ use-render-list（组行+成员行）+
//   notification.rs DelayResults payload {"group","results":[[name,delay]...]}。
// 数据源注入 DataHooks（生产绑 cmd::mihomo()，测试注入 mock）。
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mihomo/mihomo_client.h"

namespace clashwin::ui::scene {

// ← services/delay.ts:13-32（单一真源，严禁漂移）
inline constexpr int kNodeDelayMinMs = 30;       // 机场伪造低延迟过滤下限
inline constexpr int kNodeDelayMaxMs = 5000;     // 判死阈值（探针超时同值，BUG-304）
inline constexpr int64_t kDelayCacheTtlMs = 30 * 60 * 1000;  // CACHE_TTL
// src: services/delay.ts:22 HISTORY_FALLBACK_MAX_AGE_MS = 10 * 60 * 1000
inline constexpr int64_t kHistoryFallbackMaxAgeMs = 10 * 60 * 1000;

// ← formatDelayColor 五档（250/400/5000 分界，BUG-304）
enum class DelayTier { Unknown, Success, Mid, Slow, Timeout };
inline DelayTier delay_tier(int delay) {
  if (delay < 0) return DelayTier::Unknown;        // 未测 -1 / testing -2
  if (delay == 0 || delay >= kNodeDelayMaxMs) return DelayTier::Timeout;
  if (delay >= 400) return DelayTier::Slow;        // warning
  if (delay >= 250) return DelayTier::Mid;         // primary
  return DelayTier::Success;                       // success
}
// ← formatDelay：-1='-'、-2='testing'、0/≥5000(≤1e5)='Timeout'、>1e5='Error'
std::wstring format_delay(int delay);

struct NodeRow {
  std::string name;
  std::string type;
};

// 数据源钩子（测试注入 mock；生产在 main_window 绑 cmd::mihomo()）
struct DataHooks {
  // get_group_by_name("PROXY") → (成员列表, 当前选中)；false=失败
  std::function<bool(std::vector<std::string>* all, std::string* now)> fetch_group;
  // get_proxies() → 节点细节（type）；false=失败
  std::function<bool(std::map<std::string, mihomo::ProxyItem>* items)> fetch_proxies;
  // select_node_for_group("PROXY", node)；false=失败（UI 回弹）
  std::function<bool(const std::string& node)> select_node;
  // 触发组测速（delay_group；结果经 backend-delay-results 事件回灌）
  std::function<bool(const std::string& group)> test_group;
  // ★ §5.2/§5.6「所见即所选」：批量测速 + 择优切换（限可见子集）
  //   ← Mini proxy-groups.tsx:230-275 handleCheckAll：
  //     可见节点子集 → 置 -2 testing → triggerAutoSelect(uid, visibleNames, 0, true)
  //   生产 = AutoSelect::trigger(uid, 子集, sort_type=0, select=true)
  //   nodes 为空 → 全量（与 F1 自动优选同语义）
  //   返回择优选中的节点名（空表示未选中/繁忙）
  std::function<std::string(const std::vector<std::string>& nodes)> test_and_select;
  std::function<int64_t()> now_ms;  // 时钟注入（TTL/测试确定性）
};

class NodeData {
 public:
  explicit NodeData(DataHooks hooks) : hooks_(std::move(hooks)) {}

  // 全量加载（组 + 细节合并）；失败返回 false（内容保持上次状态）
  bool load();

  // ← backend-delay-results 事件：{"group","results":[[name,delay],...]}
  // 局部刷新：只更新延迟缓存与对应行，不触发全量重载（use-proxies 语义）
  bool apply_delay_results(const nlohmann::json& payload);

  // 单点写入（手动单节点测速完成时）
  void set_delay(const std::string& name, int delay);
  void set_delays_batch(const std::vector<std::string>& names, int delay);

  // 切换节点：select_node 成功后更新 now_（失败返回 false，UI 回弹）
  bool select(const std::string& node);

  // 触发组测速
  bool test_group();

  // ★ §5.2/§5.6「所见即所选」：批量测速可见子集 + 择优切换
  //   ← Mini proxy-groups.tsx:230-275 handleCheckAll
  //   nodes 非空 → 仅在子集内测速与择优；nodes 空 → 全量
  //   返回 false = 未执行（测速未执行 ≠ Timeout）
  bool test_and_select(const std::vector<std::string>& nodes);

  // ---- 读取 ----
  const std::vector<NodeRow>& rows() const { return rows_; }
  size_t row_count() const { return rows_.size(); }
  const std::string& now() const { return now_; }
  const std::string& group() const { return group_; }
  // 行延迟（TTL 30min 惰性判过期 → -1 Unknown；-2 testing 原样透传）
  int row_delay(const std::string& name) const;

  // 变更通知（UI invalidate / 局部刷新回调）
  uint64_t subscribe(std::function<void()> cb);
  void unsubscribe(uint64_t id);

  // 生产钩子（cmd::mihomo() 绑定；main_window 调）
  static DataHooks production_hooks(const std::string& group = "PROXY");

 private:
  void notify() {
    for (auto& [id, cb] : subs_) cb();
  }

  DataHooks hooks_;
  std::vector<NodeRow> rows_;
  std::string now_;
  std::string group_ = "PROXY";
  // 延迟缓存：name → (delay, updated_at_ms)
  std::map<std::string, std::pair<int, int64_t>> delay_cache_;
  // src: services/delay.ts:268-298 getDelayFix：缓存过期后回落内核 history
  struct ProxyHistory {
    bool alive = true;    // ★ 内核 alive 真源（← delay.ts:279-281）；false → 一律 Timeout(0)
    int delay = -1;       // 最后一条 history.delay；-1 = 无记录
    std::string time;     // history.time（RFC3339）
  };
  std::map<std::string, ProxyHistory> proxy_history_;
  std::vector<std::pair<uint64_t, std::function<void()>>> subs_;
  uint64_t next_id_ = 1;
};

}  // namespace clashwin::ui::scene
