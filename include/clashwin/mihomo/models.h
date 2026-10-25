#pragma once
// T05 mihomo 运行时数据模型（← crates/tauri-plugin-mihomo/src/models.rs 逐字段对照；
// snapshot/delta 协议 ← docs/phase1/04_round2_clarifications.md §2 原样）。
//
// 【解析契约】mihomo REST 响应字段为 kebab-case（GET /configs）；WS 载荷为
// camelCase。所有 from_json 容忍缺键（对齐原版 #[serde(default)]），未知键忽略
// （对齐 #[serde(flatten)] extra——语义上允许透传，此处不保留以省内存）。
// epochId 为 16 位十六进制字符串（← mihomo.rs ws_connections: format!("{:016x}")）。
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

#include "util/result.h"

namespace clashwin::mihomo {

// URL percent-encoding（← urlencoding::encode：unreserved 之外全部转义）
std::string url_encode(const std::string& s);

// ---------- Version（← models.rs MihomoVersion） ----------
struct MihomoVersion {
  bool meta = false;
  std::string version;

  static MihomoVersion from_json(const nlohmann::json& j);
};

// ---------- 日志级别（← models.rs LogLevel） ----------
enum class LogLevel { Debug, Info, Warning, Error, Silent };
const char* log_level_to_str(LogLevel l);
// 非法值回落 Info（原版 string_enum 语义为保留原串，此处解析入口统一兜底）
LogLevel log_level_from_str(const std::string& s);

// ---------- 通用配置项（GET/PATCH /configs；kebab-case 解析） ----------
// 原版 BaseConfig 字段众多且含大量 optional；此处落位常用字段 + 原始 JSON
// 透传（patch 时原样回传，保证 T09/T17 的 patch_base_config 零丢失）。
struct BaseConfig {
  uint16_t port = 0;
  uint16_t socks_port = 0;
  uint16_t redir_port = 0;
  uint16_t tproxy_port = 0;
  uint16_t mixed_port = 0;
  bool allow_lan = false;
  std::string bind_address;
  std::string mode;               // rule / global / direct
  bool unified_delay = false;
  std::string log_level;          // debug/info/warning/error/silent
  bool ipv6 = false;
  std::string interface_name;
  bool tcp_concurrent = false;
  std::string find_process_mode;  // strict/always/off
  bool sniffing = false;
  bool etag_support = false;
  nlohmann::json tun;             // TunConfig 原样（T18 注入用）
  nlohmann::json raw;             // 全量原始响应（未知字段零丢失）

  static BaseConfig from_json(const nlohmann::json& j);
  nlohmann::json to_patch_json(const std::vector<std::string>& keys) const;
  // 便捷：构造 {key:value} patch 体（T09/T17 使用）
  static nlohmann::json patch_of(std::initializer_list<std::pair<const char*, nlohmann::json>> kv);
};

// ---------- Proxies（← models.rs Proxy/DelayHistory/Extra/Proxies） ----------
struct DelayHistory {
  std::string time;
  uint16_t delay = 0;
  static DelayHistory from_json(const nlohmann::json& j);
};

struct ProxyExtra {
  bool alive = false;
  std::vector<DelayHistory> history;
  static ProxyExtra from_json(const nlohmann::json& j);
};

struct ProxyItem {
  // group 字段
  std::optional<std::vector<std::string>> all;
  std::optional<std::string> expected_status;
  std::optional<std::string> fixed;
  std::optional<bool> hidden;
  std::optional<std::string> icon;
  std::optional<std::string> now;
  std::optional<std::string> test_url;
  // 单节点字段
  std::optional<std::string> id;
  // 基础字段
  bool alive = false;
  std::vector<DelayHistory> history;
  std::map<std::string, ProxyExtra> extra;  // 键=测试 URL
  std::string name;
  bool udp = false;
  bool uot = false;
  std::string type;  // ProxyType（原版 string_enum，保留原串）
  bool xudp = false;
  bool tfo = false;
  bool mptcp = false;
  bool smux = false;
  std::string interface;
  std::string dialer_proxy;   // ← dialer-proxy
  int32_t routing_mark = 0;   // ← routing-mark
  std::string provider_name;  // ← provider-name

  bool is_group() const { return all.has_value(); }
  static ProxyItem from_json(const nlohmann::json& j);
};

struct Proxies {
  std::map<std::string, ProxyItem> proxies;  // 键=节点名
  static Proxies from_json(const nlohmann::json& j);
};

struct Groups {
  std::vector<ProxyItem> proxies;
  static Groups from_json(const nlohmann::json& j);
};

struct ProxyDelay {
  uint32_t delay = 0;
  static ProxyDelay from_json(const nlohmann::json& j);
};

// ---------- Providers（← models.rs ProxyProvider/RuleProvider/SubscriptionInfo） ----------
struct SubscriptionInfo {
  int64_t upload = 0;
  int64_t download = 0;
  int64_t total = 0;
  int64_t expire = 0;
  static SubscriptionInfo from_json(const nlohmann::json& j);  // PascalCase 键
};

struct ProxyProvider {
  std::string name;
  std::string type;           // "Proxy"
  std::string vehicle_type;   // File/HTTP/Compatible/Inline
  std::vector<ProxyItem> proxies;
  std::string test_url;
  std::string expected_status;
  std::optional<std::string> updated_at;
  std::optional<SubscriptionInfo> subscription_info;
  static ProxyProvider from_json(const std::string& name, const nlohmann::json& j);
};

struct ProxyProviders {
  std::map<std::string, ProxyProvider> providers;
  static ProxyProviders from_json(const nlohmann::json& j);
};

struct Rule {
  std::string type;  // RuleType（原版 string_enum，保留原串）
  int32_t index = 0;
  std::string payload;
  std::string proxy;
  int32_t size = 0;
  static Rule from_json(const nlohmann::json& j);
};

struct Rules {
  std::vector<Rule> rules;
  static Rules from_json(const nlohmann::json& j);
};

struct RuleProvider {
  std::string behavior;      // Domain/IPCIDR/Classical
  std::string format;        // YamlRule/TextRule/MrsRule
  std::string name;
  uint32_t rule_count = 0;
  std::string type;          // "Rule"
  std::string updated_at;
  std::string vehicle_type;
  static RuleProvider from_json(const std::string& name, const nlohmann::json& j);
};

struct RuleProviders {
  std::map<std::string, RuleProvider> providers;
  static RuleProviders from_json(const nlohmann::json& j);
};

// ---------- Connections（← models.rs Connection/ConnectionMetaData/Connections） ----------
struct ConnectionMetaData {
  std::string network;          // tcp/udp/all
  std::string type;             // HTTP/Socks5/Vmess/...（原版 string_enum）
  std::string source_ip;        // ← sourceIP
  std::string destination_ip;   // ← destinationIP
  std::optional<std::vector<std::string>> source_geo_ip;
  std::optional<std::vector<std::string>> destination_geo_ip;
  std::string source_ip_asn;
  std::string destination_ip_asn;
  std::string source_port;
  std::string destination_port;
  std::string inbound_ip;
  std::string inbound_port;
  std::string inbound_name;
  std::string inbound_user;
  std::string host;
  std::string dns_mode;         // normal/fake-ip/redir-host/hosts
  uint32_t uid = 0;
  std::string process;
  std::string process_path;
  std::string special_proxy;
  std::string special_rules;
  std::string remote_destination;
  uint8_t dscp = 0;
  std::string sniff_host;

  static ConnectionMetaData from_json(const nlohmann::json& j);
};

struct ConnectionItem {
  std::string id;
  ConnectionMetaData metadata;
  uint64_t upload = 0;
  uint64_t download = 0;
  std::string start;                    // RFC3339 字符串（原版不转毫秒，前端解析）
  std::vector<std::string> chains;
  std::optional<std::vector<std::string>> provider_chains;
  std::string rule;
  std::string rule_payload;

  static ConnectionItem from_json(const nlohmann::json& j);
};

// GET /connections 响应（REST 全量口径，含 memory）
struct Connections {
  uint64_t download_total = 0;
  uint64_t upload_total = 0;
  std::vector<ConnectionItem> connections;  // 可为 null → 空
  uint64_t memory = 0;
  static Connections from_json(const nlohmann::json& j);
};

// ---------- WS connections 通道 snapshot/delta 协议（04§2 原样） ----------
// snapshot = {type:"snapshot", data:{epochId, sequenceId, uploadTotal,
//            downloadTotal, connections[], memory}}
// delta    = {type:"delta", data:{epochId, sequenceId, uploadTotal,
//            downloadTotal, memory, added[], removed[], updated:[id,up,down]×n}}
// 校验规则：epochId≠当前 或 sequenceId≠last+1 → 全量 resync；closed 保留上限 500
struct ConnectionsSnapshot {
  std::string epoch_id;
  uint64_t sequence_id = 0;
  uint64_t upload_total = 0;
  uint64_t download_total = 0;
  std::vector<ConnectionItem> connections;
  uint64_t memory = 0;

  static ConnectionsSnapshot from_json(const nlohmann::json& j);
};

struct ConnectionsDelta {
  std::string epoch_id;
  uint64_t sequence_id = 0;
  uint64_t upload_total = 0;
  uint64_t download_total = 0;
  uint64_t memory = 0;
  std::vector<ConnectionItem> added;
  std::vector<std::string> removed;
  // 扁平数组步长3：[id1, up1, down1, id2, up2, down2, ...]
  std::vector<std::tuple<std::string, int64_t, int64_t>> updated;

  static ConnectionsDelta from_json(const nlohmann::json& j);
};

// {type:"snapshot"|"delta", data:{...}} 信封解析；type 不识别 → .type 置空
struct ConnectionMessage {
  std::string type;  // "snapshot" / "delta" / 其他
  std::optional<ConnectionsSnapshot> snapshot;
  std::optional<ConnectionsDelta> delta;

  static ConnectionMessage from_json(const nlohmann::json& j);
};

// ---------- WS traffic / memory / logs 载荷 ----------
struct TrafficMsg {
  uint64_t up = 0;
  uint64_t down = 0;
  std::optional<uint64_t> up_total;    // ← upTotal（新内核附带）
  std::optional<uint64_t> down_total;  // ← downTotal
  static TrafficMsg from_json(const nlohmann::json& j);
};

struct MemoryMsg {
  uint64_t inuse = 0;
  uint64_t oslimit = 0;
  static MemoryMsg from_json(const nlohmann::json& j);
};

struct LogItem {
  std::string type;  // info/warning/error/debug/critical
  std::string payload;
  static LogItem from_json(const nlohmann::json& j);
};

// ---------- JSON → 模型公共入口（失败返回 Err，供测试与 T09 使用） ----------
// 模板化解析：Result<T> parse_model<T>(const std::string& text)
template <typename T>
Result<T> parse_model(const std::string& text) {
  nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
  if (j.is_discarded()) {
    return Result<T>::err({4001, "invalid JSON: parse failed"});
  }
  return Result<T>::ok(T::from_json(j));
}

}  // namespace clashwin::mihomo
