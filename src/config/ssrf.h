#pragma once
// SSRF 防护（← config/prfitem.rs::validate_url_no_ssrf 全量移植）
//
// 两道防线：
//   1. 静态检查：IPv4 字面量（loopback/私网 10/172.16-31/192.168/unspecified/
//      链路本地 169.254）、IPv6（::1/::/fe80::/10/fc00::/7）、
//      域名分支（localhost/127.0.0.1/::1/0.0.0.0/169.254.* 前缀）；
//      WHATWG 数字主机（"127.1"、"2130706433"）已由 parse_http_url 归一为
//      IPv4 字面量后再判（与 url crate 口径一致）。
//   2. DNS 解析后二次校验（防重绑定）：getaddrinfo 结果逐 IP 复查；
//      解析失败不拒绝（← Rust `if let Ok(addrs)` 宽松语义）。
#include <string>

#include "system/net_http.h"
#include "util/result.h"

namespace clashwin::config::ssrf {

// ← prfitem.rs::validate_url_no_ssrf
Result<void> validate_url_no_ssrf(const system::UrlParts& url);

// ---- 供单测的纯函数 ----

// Rust Ipv4Addr::{is_loopback, is_private, is_unspecified, is_link_local}
bool ipv4_forbidden(uint32_t ip);
// Rust Ipv6Addr：is_loopback/is_unspecified + fe80::/10 + fc00::/7
bool ipv6_forbidden(const uint16_t segs[8]);
// 对已解析的 sockaddr（getaddrinfo 产物）复查
bool sockaddr_forbidden(const void* sa);

}  // namespace clashwin::config::ssrf
