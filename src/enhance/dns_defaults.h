#pragma once
// ← ClashVerge src-tauri/src/enhance/mod.rs::apply_mandatory_dns_settings 全量移植
// 仅补缺不覆盖（保留订阅/用户已有设置）；default-nameserver /
// proxy-server-nameserver 追加保底明文上游 223.5.5.5（冷启动防鸡生蛋）。
#include <yaml-cpp/yaml.h>

namespace clashwin::enhance {

YAML::Node apply_mandatory_dns_settings(const YAML::Node& config);

}  // namespace clashwin::enhance
