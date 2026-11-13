#pragma once
// ← ClashVerge src-tauri/src/enhance/mod.rs::sanitize_dns_upstreams 全量移植（BUG-308 / BUG-364 / A9 / C10-C12）：
// 统一治理 dns.nameserver、dns.fallback 与 dns.nameserver-policy（绝不触碰 default-nameserver）：
// - respect-rules: false 或 #DIRECT 时，不可达海外上游就地改写为由 nameserver 推导的可达境内 DoH 并去重；
// - respect-rules: true 时，将境外明文公共 DNS 升级为 DoH，并在 RULE-SET,gfwlist,PROXY 之后注入对偶 PROXY 规则。
#include <yaml-cpp/yaml.h>

namespace clashwin::enhance {

YAML::Node sanitize_dns_upstreams(const YAML::Node& config);
YAML::Node sanitize_nameserver_policy(const YAML::Node& config);

}  // namespace clashwin::enhance
