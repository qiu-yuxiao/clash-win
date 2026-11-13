#pragma once
// 4 态路径控制状态与合成逻辑（← src/utils/path-control.ts:1-644）
// - 状态：Direct / Proxy / Reject / Disconnected
// - 目标提取：Domain (host / destinationIP / remoteDestination) / Process (process) / managedRule 兜底
// - 持久化：%APPDATA%/.../path_control_state.json（或 CLASHWIN_HOME_OVERRIDE）
//   MAX_DISCONNECTED_RECORDS = 100 / MAX_DISCONNECTED_IDS = 500 / SKEW = 500ms

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "connection_data.h"
#include "core/quick_routing.h"

namespace clashwin::ui::scene {

// src: path-control.ts:49-51
inline constexpr size_t kMaxDisconnectedRecords = 100;
inline constexpr size_t kMaxDisconnectedIds = 500;
inline constexpr int64_t kReconnectClockSkewToleranceMs = 500;

struct ConnectionTarget {
  core::quick_routing::RuleType type = core::quick_routing::RuleType::Domain;
  std::string value;
  std::string key;
};

struct DisconnectedTargetRecord {
  std::string key;
  core::quick_routing::RuleType target_type = core::quick_routing::RuleType::Domain;
  std::string target_value;
  std::string host;
  std::string chains_cat;
  std::string network;
  ConnMetadata metadata;
  std::string chains_summary;
  std::string rule;
  std::string rule_payload;
  int64_t disconnected_at_ms = 0;
};

struct PathControlState {
  std::vector<DisconnectedTargetRecord> records;
  std::vector<std::string> disconnected_ids;
};

// 解析 RFC3339 时间戳为 UTC 毫秒（失败返回 0）
int64_t parse_iso8601_utc_ms(const std::string& iso);

// 提取用于快捷分流和断开追踪的稳定目标（支持 fallback_rule 兜底）
std::optional<ConnectionTarget> extract_connection_target(
    const ConnItem& item,
    const std::optional<core::quick_routing::ManagedMergeRule>& fallback_rule = std::nullopt);
bool matches_connection_target(const ConnItem& item, const ConnectionTarget& target);
std::string extract_target_key_for_managed_rule(
    const core::quick_routing::ManagedMergeRule& rule);

// 从 Managed Merge Rules 中查找首条命中规则
std::optional<core::quick_routing::ManagedMergeRule> find_matching_managed_rule(
    const ConnItem& item,
    const std::vector<core::quick_routing::ManagedMergeRule>& rules);

// 4 态优先级：Disconnected > synthetic 自带 > chains REJECT/DIRECT > rule REJECT > Proxy
PathControlStatus resolve_connection_control_status(
    const ConnItem& item,
    const std::optional<core::quick_routing::ManagedMergeRule>& managed_rule,
    bool is_disconnected);

std::string path_control_status_key(PathControlStatus status);

// 基于活跃连接中未在 disconnected_ids 里的重连样本（start_ms >= disconnected_at - 500ms）清理过期记录
bool reconcile_disconnected_records(PathControlState& state,
                                    const std::vector<ConnItem>& active);

// 合成活跃连接列表（活跃行 + 已断开目标 synthetic 行 + 托管 Merge 规则 synthetic 行）
std::vector<ConnItem> compose_active_path_control_connections(
    const std::vector<ConnItem>& raw_active,
    const std::vector<core::quick_routing::ManagedMergeRule>& managed_rules,
    const PathControlState& state);

// 为历史连接补充 4 态控制信息（不注入 synthetic 行）
std::vector<ConnItem> enrich_closed_path_control_connections(
    const std::vector<ConnItem>& raw_closed,
    const std::vector<core::quick_routing::ManagedMergeRule>& managed_rules,
    const PathControlState& state);

// Host 显示：优先 *.domain / host，其次 IP / process，全空兜底 "--"
std::string format_path_control_host_label(const ConnItem& item);

// 按 path-control.ts:613-644 四分桶重排：disconnected -> reject -> live -> idle
void prioritize_path_control_connections(std::vector<ConnItem>& items);

// 磁盘状态存储
class PathControlStore {
 public:
  PathControlStore() = default;

  void load_default();
  bool load_from_file(const std::wstring& path);
  bool save() const;

  void mark_disconnected(const ConnItem& item, int64_t now_ms);
  bool clear_disconnected_target(const std::string& target_key,
                                 const std::string& conn_id = {});
  void clear_all();
  bool reconcile(const std::vector<ConnItem>& active);

  const PathControlState& state() const { return state_; }
  PathControlState& mutable_state() { return state_; }
  const std::wstring& path() const { return path_; }

 private:
  std::wstring path_;
  PathControlState state_;
};

}  // namespace clashwin::ui::scene
