#pragma once
// ← ClashVerge src-tauri/src/enhance/tun.rs::use_tun 全量移植
// TUN 开：dns 强制 enable + fake-ip 注入（fake-ip-range/range6/filter 按条目
// 补缺）、顶层 ipv6=true + tun.inet6-address（IPv6 防泄漏）；关：dns.ipv6
// 跟随用户开关（dns.ipv6 唯一写入点，两分支均写）。
#include <yaml-cpp/yaml.h>

namespace clashwin::enhance {

YAML::Node use_tun(const YAML::Node& config, bool enable);

}  // namespace clashwin::enhance
