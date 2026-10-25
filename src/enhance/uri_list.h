#pragma once
// 通用 URI 解析器（← utils/resolve/universal_parser.rs 2008 行全量移植）
//
// parse_uri_list 支持协议：vmess://（Base64 JSON）、ss://（含 plugin 参数与
// 2022-blake3 系密码，query/plugin 按原版忽略）、trojan://、vless://（reality
// pbk/sid/sni/flow）、hysteria2://、hysteria://、anytls://、tuic://（v4/v5）、
// wireguard://、socks5://(socks://)、[HTTP] 兜底行 parse_http_fallback。
//
// 输出统一为 YAML::Node（对应 serde_yaml_ng::Mapping：{proxies: [...]}），
// 与 05§3.1 数据模型一致：下游 from_url_direct / parse_subscription_content /
// enhance 流水线全部消费 YAML mapping。
#include <optional>
#include <string>
#include <string_view>

#include <yaml-cpp/yaml.h>

namespace clashwin::enhance {

// ← universal_parser.rs::parse_uri_list：整体先尝试 Base64 解码（UTF-8 合法才
// 采用），再逐行分发各协议解析器；无有效节点 → nullopt
std::optional<YAML::Node> parse_uri_list(std::string_view content);

// 单协议解析器（测试直接驱动；与 Rust fn parse_* 一一对应）
std::optional<YAML::Node> parse_vmess(std::string_view link);
std::optional<YAML::Node> parse_ss(std::string_view link);
std::optional<YAML::Node> parse_trojan(std::string_view link);
std::optional<YAML::Node> parse_vless(std::string_view link);
std::optional<YAML::Node> parse_hysteria2(std::string_view link);
std::optional<YAML::Node> parse_hysteria(std::string_view link);
std::optional<YAML::Node> parse_anytls(std::string_view link);
std::optional<YAML::Node> parse_tuic(std::string_view link);
std::optional<YAML::Node> parse_wireguard(std::string_view link);
std::optional<YAML::Node> parse_socks5(std::string_view link);
std::optional<YAML::Node> parse_http_fallback(std::string_view line);

// ← extract_host_port：[v6]:p / host:p / host（缺省端口）；解析失败回退默认端口
std::optional<std::pair<std::string, uint16_t>> extract_host_port(
    std::string_view host_port, uint16_t default_port);

}  // namespace clashwin::enhance
