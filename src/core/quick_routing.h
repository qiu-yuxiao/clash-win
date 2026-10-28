#pragma once
// 快捷分流规则管理（T20）：置顶写入 profiles\Merge.yaml 的 prepend-rules。
// 逐行对照 src/utils/quick-routing.ts:21-219（只读参考）：
// - extract_domain_suffix：提取二级或三级域名后缀（支持 .com.cn / .org.uk 等双 TLD）
// - build_rule_string：构造 PROCESS-NAME / IP-CIDR / DOMAIN-SUFFIX 规则
// - add_quick_routing_rule：读 Merge.yaml → 去重旧匹配规则 → 置顶写入 prepend-rules → 存盘 → 触发 enhanceProfiles
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

std::string rule_target_to_string(RuleTarget target);
RuleTarget rule_target_from_string(const std::string& target_str);

std::string extract_domain_suffix(const std::string& domain_or_ip);
bool is_ip_address(const std::string& str);
bool is_ipv6_address(const std::string& str);

std::string build_rule_string(RuleType type, const std::string& value, RuleTarget target);

struct QuickRouteResult {
  bool ok = false;
  std::string rule;
  std::string error;
};

// 注入单个快捷分流规则到 Merge.yaml 并触发流水线
QuickRouteResult add_quick_routing_rule(RuleType type, const std::string& value, RuleTarget target);

// 批量添加网址规则（以换行/逗号/分号分割），全量以 PROXY 规则置顶写入
int add_quick_routing_rules(const std::string& raw_text);

}  // namespace clashwin::core::quick_routing
