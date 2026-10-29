#pragma once
// ← ClashVerge src-tauri/src/enhance/mod.rs::apply_mandatory_dns_settings 全量移植
// 仅补缺不覆盖（保留订阅/用户已有设置）；default-nameserver /
// proxy-server-nameserver 追加保底明文上游 223.5.5.5（冷启动防鸡生蛋）。
#include <yaml-cpp/yaml.h>

namespace clashwin::enhance {

YAML::Node apply_mandatory_dns_settings(const YAML::Node& config);

// ← mod.rs:526-586 sanitize_foreign_plain_dns_upstreams（v3.1.8 BUG-308）：
// dns.nameserver 中境外公共 DNS 的明文 UDP 上游（裸 IP / IP:53）改写为 DoH，
// 消除 respect-rules 下 UDP-over-TCP 竞速取消；default-nameserver 与境内上游不动。
YAML::Node sanitize_foreign_plain_dns_upstreams(const YAML::Node& config);

}  // namespace clashwin::enhance
