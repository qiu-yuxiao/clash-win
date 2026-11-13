#pragma once
// ← ClashVerge src-tauri/src/enhance/mod.rs::enforce_mini_agreements 全量移植
// ① dummy/无效 server 节点过滤（不入组，节点定义保留）→ ② 注入 PROXY(select)
// + PROXY__METRICS(url-test, interval 300) → ③ gfwlist rule-provider →
// ④ 规则链（prepend + RULE-SET,gfwlist + 私网 DIRECT 全套
// + append + GEOIP,CN / MATCH 方向随 rule_fallback；DNS PROXY 规则由 sanitize_dns_upstreams 统一注入）。
#include <string>

#include <yaml-cpp/yaml.h>

namespace clashwin::enhance {

struct MiniAgreementOptions {
  // verge.default_latency_test（None → 默认 cp.cloudflare.com/generate_204）
  std::string test_url = "https://cp.cloudflare.com/generate_204";
  // verge.rule_fallback（"proxy" → MATCH,PROXY；其余 → GEOIP,CN + MATCH,DIRECT）
  std::string rule_fallback = "direct";
};

YAML::Node enforce_mini_agreements(const YAML::Node& config,
                                   const MiniAgreementOptions& opt);

// ← mod.rs::extract_upstream_host：从 DNS 上游字符串取主机名
std::string extract_upstream_host(const std::string& raw);  // 无 → ""

}  // namespace clashwin::enhance
