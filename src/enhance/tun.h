#pragma once
// ← ClashVerge src-tauri/src/enhance/tun.rs::use_tun 全量移植
// TUN 开：dns 强制 enable + fake-ip 注入（fake-ip-range/range6/filter 按条目
// 补缺）、顶层 ipv6=true + tun.inet6-address（IPv6 防泄漏）+ 3 态 sniffer；关：dns.ipv6
// 跟随用户开关（dns.ipv6 唯一写入点，两分支均写）。
#include <optional>
#include <yaml-cpp/yaml.h>

namespace clashwin::enhance {

YAML::Node use_tun(const YAML::Node& config, bool enable);
YAML::Node use_tun_with_sniffer(const YAML::Node& config, bool enable,
                                std::optional<bool> enable_sniffer = std::nullopt);

namespace detail {
void merge_tun_sniffer_defaults(YAML::Node& cfg, bool force_enable = true);
void apply_tun_sniffer_policy(YAML::Node& cfg,
                              std::optional<bool> enable_sniffer);
}  // namespace detail

}  // namespace clashwin::enhance
