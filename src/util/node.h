#pragma once
// 伪节点/假 server 识别、关键词过滤、克隆拓扑连坐去重与 HTTPS 验真 24h 软隔离状态机
// （← ClashVerge utils/node.rs 全量 1:1 移植）
// 由 monitor、enhance、core_manager、cmd_proxy、ui 共享。
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "yaml-cpp/yaml.h"

namespace clashwin::util {

// ---- 节点名 / server / 过滤 ----
// ← node.rs::is_dummy_node：识别广告/假节点及结构形态横幅（含括号剥离 + 域名/配额结构检测）。
bool is_dummy_node(const std::string& name);

// ← node.rs::has_region_hint：判断节点名是否带地区或专线暗示。
bool has_region_hint(const std::string& name);

// ← node.rs::is_valid_node_server：IP 字面量或至少含一个点的域名（拦 server='1' 等占位假节点）。
bool is_valid_node_server(const std::string& server);

// ← node.rs::match_node_filter：多关键词过滤（AND/OR `|`/`｜`/排除 `-`/`!`/`！`，大小写不敏感）。
bool match_node_filter(const std::string& name, const std::string& filter_text);

// ---- 指纹 & 订阅拓扑连坐去重 ----
struct RawProxyEntry {
  std::string name;
  std::optional<std::string> fingerprint;
  bool is_dummy = false;
};

struct ProxyTopologyOutcome {
  std::vector<std::string> names;
  size_t clone_removed = 0;
  size_t tainted_banner_removed = 0;
  std::unordered_set<std::string> tainted_fps;
};

// ← node.rs::proxy_endpoint_fingerprint：去掉 name 后规范化 YAML 字段计算 SHA-256 hex。
std::optional<std::string> proxy_endpoint_fingerprint(const YAML::Node& mapping);

// ← node.rs::deduplicate_cloned_proxies：同指纹折叠，优先保留 (!colon && region) > region > !colon。
std::pair<std::vector<std::string>, size_t> deduplicate_cloned_proxies(
    const std::vector<std::pair<std::string, std::optional<std::string>>>& entries);

// ← node.rs::resolve_proxy_clone_topology：全量分组分析、同指纹纯横幅连坐剔除、寄生归真。
ProxyTopologyOutcome resolve_proxy_clone_topology(
    const std::vector<RawProxyEntry>& entries);

// ---- 真 HTTPS 验真状态机 & 24h 软隔离 ----
inline constexpr uint8_t kHttpsVerifyMigrateMinFails = 2;
inline constexpr uint8_t kHttpsVerifyMaxFails = 5;
inline constexpr int64_t kHttpsVerifyBanTtlSecs = 86400;

struct NodeVerifyRecord {
  bool verified_true = false;
  uint8_t fail_count = 0;
  int64_t last_verified_at = 0;

  bool is_confirmed_fake_at(int64_t now_ts) const;
  bool is_confirmed_fake() const;
};

struct NodeVerifyStoreData {
  std::map<std::string, NodeVerifyRecord> records;
};

struct VerifyBatchOutcome {
  size_t newly_verified = 0;
  size_t newly_confirmed_fake = 0;
  bool dirty = false;
};

// 运行态拓扑注册（enforce_mini_agreements 每次构建时刷新；tainted_fps 仅内存态不落盘）
void set_runtime_node_fingerprints(std::unordered_map<std::string, std::string> map);
void set_runtime_tainted_banner_fps(std::unordered_set<std::string> fps);
void ensure_runtime_node_fingerprints_loaded();

// 查询接口
bool is_https_quarantined_fp(const std::string& fp);
bool is_confirmed_fake_fp(const std::string& fp);
bool is_verified_true_fp(const std::string& fp);
bool is_confirmed_fake_node(const std::string& name);
bool is_verified_true_node(const std::string& name);
bool has_verify_failure_node(const std::string& name);
std::vector<std::string> confirmed_fake_node_names();
bool has_pending_unverified_nodes(const std::vector<std::string>& candidates);

// 纯判定 & 纯状态转移（便于单测锁定）
bool record_has_verify_failure(bool verified_true, uint8_t fail_count,
                               int64_t last_verified_at, int64_t now_ts);
VerifyBatchOutcome apply_verify_batch_to_records(
    std::map<std::string, NodeVerifyRecord>& records,
    const std::vector<std::string>& passed_fps,
    const std::vector<std::string>& failed_fps, int64_t now_ts);

// 持久化状态机写入口
VerifyBatchOutcome record_https_verify_results(
    const std::vector<std::string>& passed_names,
    const std::vector<std::string>& failed_names);
size_t clear_unverified_https_failures();

// JSON 序列化（node_https_verify.json 同结构 {"records":{...}}）
NodeVerifyStoreData load_verify_store_from_path(const std::wstring& path);
bool save_verify_store_to_path(const std::wstring& path,
                               const NodeVerifyStoreData& data);

// 测试辅助（单测隔离用）
void reset_verify_state_for_test(bool disable_disk_persistence = true);
void set_verify_store_path_for_test(const std::optional<std::wstring>& path);
void set_verify_record_for_test(const std::string& fp,
                                const std::optional<NodeVerifyRecord>& rec);

}  // namespace clashwin::util
