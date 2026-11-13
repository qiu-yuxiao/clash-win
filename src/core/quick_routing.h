#pragma once
// 快捷分流规则管理（← Clash Mini src/utils/quick-routing.ts 全量移植）：
// - normalize_quick_target_host：去除 IPv6 方括号/单冒号端口/%zone/前导*.或./尾点并转小写（不剥子域或 www.）
// - build_exact_quick_routing_rule：精确 PROCESS-NAME / IP-CIDR(/32) / IP-CIDR6(/128) / DOMAIN
// - build_proxy_rule：显式 *.domain / .domain → DOMAIN-SUFFIX，否则精确 DOMAIN / IP-CIDR / IP-CIDR6
// - Merge prepend-rules 持久化读取 / 幂等身份键替换（upsert） / 精确撤销（remove） / enhance_profiles_live 热重载
#include <optional>
#include <string>
#include <vector>

namespace clashwin::core::quick_routing {

enum class RuleType {
  Domain,
  Process,
};

enum class RuleTarget {
  Direct,
  Proxy,
  Reject,
};

struct ManagedMergeRule {
  std::string raw;
  std::string rule_type;  // "DOMAIN" | "DOMAIN-SUFFIX" | "IP-CIDR" | "IP-CIDR6" | "PROCESS-NAME"
  std::string value;      // lowercase domain/cidr, or trimmed process name
  std::string match_key;  // strips trailing /32 or /128, lowercased for PROCESS-NAME
  RuleTarget target = RuleTarget::Direct;
  std::string extra;
};

struct QuickRouteResult {
  bool ok = false;
  std::string rule;
  std::string error;
};

std::string rule_target_to_string(RuleTarget target);
RuleTarget rule_target_from_string(const std::string& target_str);

bool is_ip_address(const std::string& str);
bool is_ipv6_address(const std::string& str);

std::string normalize_quick_target_host(const std::string& raw);
std::string build_exact_quick_routing_rule(RuleType type,
                                           const std::string& value,
                                           RuleTarget target);
std::string build_rule_string(RuleType type, const std::string& value, RuleTarget target);
std::string build_proxy_rule(const std::string& input);

// 兼容保留（供遗留调用方使用）
std::string extract_domain_suffix(const std::string& domain_or_ip);

std::optional<ManagedMergeRule> parse_managed_merge_rule_line(const std::string& line);
std::string get_rule_identity_key(const std::string& rule_str);
std::vector<ManagedMergeRule> parse_managed_merge_rules_from_yaml(
    const std::string& merge_yaml);
std::vector<std::string> upsert_prepend_rule(
    const std::vector<std::string>& existing,
    const std::string& new_rule,
    const std::vector<std::string>& replace_raw_rules = {});
std::pair<std::vector<std::string>, std::vector<std::string>> remove_prepend_rules(
    const std::vector<std::string>& existing,
    const std::string& raw_rule,
    const std::vector<std::string>& identity_keys);
std::vector<std::string> build_target_identity_keys(RuleType type,
                                                    const std::string& value);

std::vector<ManagedMergeRule> read_managed_merge_rules();

// 注入单个精确快捷分流规则到 Merge.yaml 并触发 live enhance + 通知
QuickRouteResult add_quick_routing_rule(
    RuleType type, const std::string& value, RuleTarget target,
    const std::vector<std::string>& replace_raw_rules = {});

// 精确撤销 Merge.yaml 中的规则（支持 raw_rule 或 (target_type, target_value)）并触发 live enhance + 通知
QuickRouteResult remove_quick_routing_rule(
    const std::string& raw_rule,
    std::optional<RuleType> target_type = std::nullopt,
    const std::string& target_value = {});

// 批量添加网址规则（以换行/逗号/分号分割），全量以 PROXY 规则置顶写入 Merge.yaml 并热重载
int add_quick_routing_rules(const std::string& raw_text);

}  // namespace clashwin::core::quick_routing
