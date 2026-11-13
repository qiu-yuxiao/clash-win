#pragma once
// ← ClashVerge src-tauri/src/enhance/mod.rs::sanitize_nameserver_policy 全量移植（BUG-364）：
// 当 dns.respect-rules 未启用（或条目带 #DIRECT）时剔除不可达海外公共 DNS 策略条目；
// 当 respect-rules: true 时改写 4 家境外公共 DNS 明文 UDP 为 DoH 并补齐前置 PROXY 规则。
#include <yaml-cpp/yaml.h>

namespace clashwin::enhance {

YAML::Node sanitize_nameserver_policy(const YAML::Node& config);

}  // namespace clashwin::enhance
