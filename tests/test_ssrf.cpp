// T06：SSRF 校验 + URL 解析 + is_dummy_node/is_valid_node_server 测试
// 静态判定全量对照 prfitem.rs::validate_url_no_ssrf / utils/node.rs 单测；
// DNS 二次校验经 sockaddr_forbidden 纯函数与 127.0.0.1 解析实测覆盖。
#include <windows.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>

#include "config/ssrf.h"
#include "system/net_http.h"
#include "test_util.h"
#include "util/node.h"
#include "util/str_utils.h"

using namespace clashwin;
using namespace clashwin::config::ssrf;
using namespace clashwin::system;
using clashwin::util::is_dummy_node;
using clashwin::util::is_valid_node_server;

namespace {

bool rejects(const char* url) {
  auto p = parse_http_url(url);
  if (!p.is_ok()) return true;  // 解析失败 = 拒绝（URL 层失败）
  auto r = validate_url_no_ssrf(p.value());
  return !r.is_ok();
}

bool allows(const char* url) {
  auto p = parse_http_url(url);
  if (!p.is_ok()) return false;
  auto r = validate_url_no_ssrf(p.value());
  return r.is_ok();
}

}  // namespace

// ---------- URL 解析（parse_http_url） ----------

UT_TEST(t06_parse_http_url_basic) {
  auto p = parse_http_url("https://EXAMPLE.com:443/a?b=c#d");
  UT_CHECK(p.is_ok());
  if (!p.is_ok()) return;
  const UrlParts& u = p.value();
  UT_CHECK_EQ(u.scheme, "https");
  UT_CHECK_EQ(u.host, "example.com");  // 归一小写
  UT_CHECK(u.port_str.empty());        // 默认端口剥离
  UT_CHECK_EQ(u.path, "/a");
  UT_CHECK_EQ(u.query, "b=c");
  UT_CHECK_EQ(u.fragment, "d");
  UT_CHECK_EQ(u.effective_port(), 443);
  UT_CHECK_EQ(u.request_target(), "/a?b=c");
  UT_CHECK_EQ(u.host_header(), "example.com");
  UT_CHECK_EQ(u.serialize(), "https://example.com/a?b=c#d");
}

UT_TEST(t06_parse_http_url_userinfo_and_ipv6) {
  auto p = parse_http_url("http://user%3Aname:pw%40x@host.com:8080/p");
  UT_CHECK(p.is_ok());
  if (!p.is_ok()) return;
  UT_CHECK_EQ(p.value().username, "user%3Aname");
  UT_CHECK_EQ(p.value().password, "pw%40x");
  UT_CHECK_EQ(p.value().host_header(), "host.com:8080");

  auto p6 = parse_http_url("http://[2001:db8::1]:8080/x");
  UT_CHECK(p6.is_ok());
  if (!p6.is_ok()) return;
  UT_CHECK(p6.value().is_ipv6);
  UT_CHECK_EQ(p6.value().host, "2001:db8::1");
  UT_CHECK_EQ(p6.value().host_header(), "[2001:db8::1]:8080");

  auto p6d = parse_http_url("http://[::1]/x");
  UT_CHECK(p6d.is_ok() && p6d.value().is_ipv6 && p6d.value().host == "::1");
}

UT_TEST(t06_parse_http_url_whatwg_ipv4_normalization) {
  // WHATWG 数字主机归一（SSRF 绕过面）
  auto a = parse_http_url("http://127.1/");
  UT_CHECK(a.is_ok() && a.value().is_ipv4 && a.value().host == "127.0.0.1");
  auto b = parse_http_url("http://2130706433/");
  UT_CHECK(b.is_ok() && b.value().is_ipv4 && b.value().host == "127.0.0.1");
  auto c = parse_http_url("http://0x7f.1/");
  UT_CHECK(c.is_ok() && c.value().is_ipv4 && c.value().host == "127.0.0.1");
  auto d = parse_http_url("http://1.2.3.4/");
  UT_CHECK(d.is_ok() && d.value().is_ipv4 && d.value().host == "1.2.3.4");
  // >4 段 → 域名
  auto e = parse_http_url("http://1.2.3.4.5/");
  UT_CHECK(e.is_ok() && !e.value().is_ipv4 && e.value().host == "1.2.3.4.5");
  // IPv4 形态但段非法 → URL 解析失败（url crate 同口径）
  UT_CHECK(!parse_http_url("http://1.2.3.999/").is_ok());
  // 端口非法
  UT_CHECK(!parse_http_url("http://host:99999/").is_ok());
  UT_CHECK(!parse_http_url("http://host:abc/").is_ok());
  // scheme 非 http(s)
  UT_CHECK(!parse_http_url("ftp://example.com/").is_ok());
  // 无 authority
  UT_CHECK(!parse_http_url("http://").is_ok());
}

// ---------- 静态 IP 判定纯函数 ----------

UT_TEST(t06_ipv4_forbidden_table) {
  auto f = [](uint32_t ip) { return ipv4_forbidden(ip); };
  UT_CHECK(f(0x00000000));        // 0.0.0.0 unspecified
  UT_CHECK(f(0x7F000001));        // 127.0.0.1 loopback
  UT_CHECK(f(0x7FFF0001));        // 127.255.0.1 loopback/8
  UT_CHECK(f(0x0A010203));        // 10.1.2.3 private
  UT_CHECK(f(0xAC100001));        // 172.16.0.1 private
  UT_CHECK(f(0xAC1FFFFF));        // 172.31.255.255 private
  UT_CHECK(!f(0xAC200001));       // 172.32.0.1 非 private
  UT_CHECK(f(0xC0A80101));        // 192.168.1.1 private
  UT_CHECK(f(0xA9FE0101));        // 169.254.1.1 link-local
  UT_CHECK(!f(0x08080808));       // 8.8.8.8
  UT_CHECK(!f(0x01010101));       // 1.1.1.1
  UT_CHECK(!f(0x64400001));       // 100.64.0.1（CGNAT 不在 Rust is_private 内）
}

UT_TEST(t06_ipv6_forbidden_table) {
  auto mk = [](uint16_t s0, uint16_t s7 = 0) {
    uint16_t segs[8] = {s0, 0, 0, 0, 0, 0, 0, s7};
    return ipv6_forbidden(segs);
  };
  uint16_t lo[8] = {0, 0, 0, 0, 0, 0, 0, 1};
  UT_CHECK(ipv6_forbidden(lo));   // ::1
  UT_CHECK(mk(0));                // ::
  UT_CHECK(mk(0xfe80, 1));        // fe80::1 链路本地
  UT_CHECK(mk(0xfebf, 1));        // febf::1 仍在 fe80::/10
  UT_CHECK(!mk(0xfec0, 1));       // fec0::1 site-local 不拦（Rust 同口径）
  UT_CHECK(mk(0xfc00, 1));        // fc00::/7 ULA
  UT_CHECK(mk(0xfd12, 1));        // fd12::/8 ULA
  UT_CHECK(!mk(0x2001, 1));       // 2001:: 全球单播
}

// ---------- validate_url_no_ssrf 全分支 ----------

UT_TEST(t06_ssrf_rejects_private_and_loopback) {
  UT_CHECK(rejects("http://localhost/sub"));
  UT_CHECK(rejects("http://LOCALHOST/sub"));
  UT_CHECK(rejects("http://127.0.0.1/sub"));
  UT_CHECK(rejects("https://127.10.10.1/sub"));  // loopback /8
  UT_CHECK(rejects("http://10.0.0.1/sub"));
  UT_CHECK(rejects("http://172.16.0.1/sub"));
  UT_CHECK(rejects("http://172.31.255.254/sub"));
  UT_CHECK(rejects("http://192.168.1.1/sub"));
  UT_CHECK(rejects("http://169.254.169.254/latest/meta-data"));  // 云元数据
  UT_CHECK(rejects("http://0.0.0.0/sub"));
  UT_CHECK(rejects("http://[::1]/sub"));
  UT_CHECK(rejects("http://[fc00::1]/sub"));
  UT_CHECK(rejects("http://[fd12::1]/sub"));
  UT_CHECK(rejects("http://[fe80::1]/sub"));
  // WHATWG 数字主机归一后拦截
  UT_CHECK(rejects("http://127.1/sub"));
  UT_CHECK(rejects("http://2130706433/sub"));
  UT_CHECK(rejects("http://0x7f000001/sub"));
}

UT_TEST(t06_ssrf_allows_public) {
  // 公网 IP 字面量（无 DNS 依赖）
  UT_CHECK(allows("http://8.8.8.8/sub"));
  UT_CHECK(allows("http://1.1.1.1/sub"));
  UT_CHECK(allows("https://93.184.216.34/sub"));
  // 域名：Rust 语义——解析失败不拒绝；若沙箱假 DNS 把公网域名映射到私网 IP，
  // 拒绝同样是正确行为 → 对该断言做环境探测（域名 + >4 段域名两条）
  {
    bool sandbox_maps_public_to_private = false;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo("example.com", "80", &hints, &res) == 0 && res) {
      for (addrinfo* it = res; it; it = it->ai_next) {
        if (sockaddr_forbidden(it->ai_addr)) sandbox_maps_public_to_private = true;
      }
      freeaddrinfo(res);
    }
    UT_CHECK(sandbox_maps_public_to_private ||
             allows("http://example.com/sub"));
  }
  UT_CHECK(allows("http://1.2.3.4.5/sub"));  // 域名（>4 段，解析失败不拒）
}

UT_TEST(t06_ssrf_dns_resolution_second_check) {
  // DNS 二次校验（防重绑定）：静态分支放过、解析结果落回环 → 拒。
  // localhost 经系统 hosts 必指回环——第二道防线必拦（静态分支也已拦）。
  UT_CHECK(rejects("http://localhost/x"));

  // sockaddr_forbidden 纯函数（getaddrinfo 产物口径）
  sockaddr_in sa4{};
  sa4.sin_family = AF_INET;
  sa4.sin_addr.S_un.S_addr = htonl(0x7F000001);
  UT_CHECK(sockaddr_forbidden(&sa4));
  sa4.sin_addr.S_un.S_addr = htonl(0x08080808);
  UT_CHECK(!sockaddr_forbidden(&sa4));

  sockaddr_in6 sa6{};
  sa6.sin6_family = AF_INET6;
  inet_pton(AF_INET6, "::1", &sa6.sin6_addr);
  UT_CHECK(sockaddr_forbidden(&sa6));
  inet_pton(AF_INET6, "2001:db8::1", &sa6.sin6_addr);
  UT_CHECK(!sockaddr_forbidden(&sa6));
}

// ---------- from_url SSRF 集成已由 test_uri_list 覆盖 ----------
// ---------- node.rs 全量对照 ----------

UT_TEST(t06_valid_node_server) {
  // ← node.rs test_valid_node_server
  UT_CHECK(is_valid_node_server("1.2.3.4"));
  UT_CHECK(is_valid_node_server("::1"));
  UT_CHECK(is_valid_node_server("a03.jiffdl.net"));
  UT_CHECK(is_valid_node_server(" jp.example.com "));
  // 无效：占位假节点（BUG-301 现场 server='1'）与空值
  UT_CHECK(!is_valid_node_server("1"));
  UT_CHECK(!is_valid_node_server(""));
  UT_CHECK(!is_valid_node_server("   "));
  // 严格 IP 语义：Rust IpAddr::from_str 不接受 "127.1" 简写；
  // "127.1" 含 '.' → 按原版语义视为合法域名
  UT_CHECK(is_valid_node_server("127.1"));
}

UT_TEST(t06_dummy_node_detection) {
  // ← node.rs test_dummy_node_detection
  UT_CHECK(is_dummy_node("剩余流量：100GB"));
  UT_CHECK(is_dummy_node("过期时间：2024-12-31"));
  UT_CHECK(is_dummy_node("官网 https://example.com"));
  UT_CHECK(is_dummy_node("套餐到期"));
  UT_CHECK(is_dummy_node("续费链接"));
  UT_CHECK(is_dummy_node("公告栏"));
  UT_CHECK(is_dummy_node("购买地址"));
  UT_CHECK(is_dummy_node("群：123456"));
  UT_CHECK(is_dummy_node("expire soon"));
  UT_CHECK(is_dummy_node("traffic left"));
  UT_CHECK(is_dummy_node("website"));
  UT_CHECK(is_dummy_node("http://example.com"));
  UT_CHECK(is_dummy_node("https://example.com"));
  UT_CHECK(is_dummy_node("subscribe now"));
}

UT_TEST(t06_dummy_node_false_positives) {
  // ← node.rs test_dummy_node_false_positives
  UT_CHECK(!is_dummy_node("HK-流量优化-01"));
  UT_CHECK(!is_dummy_node("日本群组-A"));
  UT_CHECK(!is_dummy_node("CN2-购买入口"));
  UT_CHECK(!is_dummy_node("BGP-公告频道"));
  UT_CHECK(!is_dummy_node("新加坡-官网加速"));
  UT_CHECK(!is_dummy_node("美国-网址专线"));
  UT_CHECK(!is_dummy_node("韩国-剩余节点-01"));
  UT_CHECK(!is_dummy_node("台湾-套餐到期提醒"));
  UT_CHECK(!is_dummy_node("香港-续费专线"));
  UT_CHECK(!is_dummy_node("日本-过期时间测试"));
  UT_CHECK(!is_dummy_node("上海-群节点-01"));
}

UT_TEST(t06_dummy_node_leak_side) {
  // ← node.rs test_dummy_node_leak_side（包裹/前缀漏判修复）
  UT_CHECK(is_dummy_node("【剩余流量】"));
  UT_CHECK(is_dummy_node("【剩余流量：100GB】"));
  UT_CHECK(is_dummy_node("(购买入口)"));
  UT_CHECK(is_dummy_node("（购买入口）"));
  UT_CHECK(is_dummy_node("[官网]"));
  UT_CHECK(is_dummy_node("「公告频道」"));
  UT_CHECK(is_dummy_node("节点-购买入口"));
  UT_CHECK(is_dummy_node("节点：剩余流量"));
  UT_CHECK(is_dummy_node("【(剩余流量)】"));
}
