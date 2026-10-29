#include "enhance/mini_agreements.h"

#include <cctype>
#include <cstdio>
#include <set>
#include <string>
#include <unordered_set>

#include "enhance/field.h"
#include "util/node.h"

namespace clashwin::enhance {

namespace {

bool has_key(const YAML::Node& map, const std::string& key) {
  const YAML::Node v = static_cast<const YAML::Node&>(map)[key];
  return v.IsDefined();
}

// IPv4 字面量解析（严格 a.b.c.d，每段 0-255）
bool parse_ipv4(const std::string& s, unsigned (&out)[4]) {
  if (s.empty()) return false;
  int idx = 0;
  unsigned cur = 0;
  int digits = 0;
  for (char c : s) {
    if (c >= '0' && c <= '9') {
      cur = cur * 10 + static_cast<unsigned>(c - '0');
      if (++digits > 3 || cur > 255) return false;
    } else if (c == '.') {
      if (digits == 0 || idx == 3) return false;
      out[idx++] = cur;
      cur = 0;
      digits = 0;
    } else {
      return false;
    }
  }
  if (digits == 0 || idx != 3) return false;
  out[idx] = cur;
  return true;
}

bool ipv4_is_private(const unsigned o[4]) {
  if (o[0] == 10) return true;                               // 10/8
  if (o[0] == 172 && o[1] >= 16 && o[1] <= 31) return true;  // 172.16/12
  if (o[0] == 192 && o[1] == 168) return true;               // 192.168/16
  return false;
}
bool ipv4_is_loopback(const unsigned o[4]) { return o[0] == 127; }
bool ipv4_is_link_local(const unsigned o[4]) {
  return o[0] == 169 && o[1] == 254;
}

// IPv6 字面量解析（含 :: 压缩、可含尾部 IPv4 段；与 std::net::Ipv6Addr::from_str
// 接受域近似对齐）
bool parse_ipv6(const std::string& in, unsigned (&out)[8]) {
  std::string s = in;
  if (s.empty() || s.find(':') == std::string::npos) return false;

  // 尾部内嵌 IPv4 → 十六段化
  size_t dot = s.rfind('.');
  if (dot != std::string::npos && s.back() != ':') {
    size_t last_colon = s.rfind(':');
    if (last_colon == std::string::npos) return false;
    unsigned v4[4];
    if (!parse_ipv4(s.substr(last_colon + 1), v4)) return false;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%x:%x", v4[0] * 256 + v4[1],
                  v4[2] * 256 + v4[3]);
    s = s.substr(0, last_colon + 1) + buf;
  }

  unsigned segs[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  int seg = 0;
  int gap = -1;
  const size_t n = s.size();
  size_t p = 0;
  while (p < n) {
    if (s[p] == ':') {
      if (p + 1 < n && s[p + 1] == ':') {
        if (gap >= 0) return false;
        gap = seg;
        p += 2;
        if (p == n) break;
        if (s[p] == ':') return false;
        continue;
      }
      ++p;
      continue;
    }
    unsigned v = 0;
    int digits = 0;
    size_t q = p;
    while (q < n && std::isxdigit(static_cast<unsigned char>(s[q])) &&
           digits < 4) {
      char c = s[q];
      int d = (c <= '9') ? c - '0' : std::tolower(c) - 'a' + 10;
      v = v * 16 + static_cast<unsigned>(d);
      ++digits;
      ++q;
    }
    if (digits == 0) return false;
    if (q < n && std::isxdigit(static_cast<unsigned char>(s[q]))) return false;
    if (seg >= 8) return false;
    segs[seg++] = v;
    p = q;
  }

  if (gap >= 0) {
    if (seg == 8) return false;
    int left_n = gap;
    int right_n = seg - gap;
    for (int k = 0; k < left_n; ++k) out[k] = segs[k];
    for (int k = 0; k < right_n; ++k) out[8 - right_n + k] = segs[gap + k];
    for (int k = left_n; k < 8 - right_n; ++k) out[k] = 0;
  } else {
    if (seg != 8) return false;
    for (int k = 0; k < 8; ++k) out[k] = segs[k];
  }
  return true;
}

bool ipv6_is_loopback(const unsigned s[8]) {
  for (int i = 0; i < 6; ++i) {
    if (s[i] != 0) return false;
  }
  return s[6] == 0 && s[7] == 1;
}
bool ipv6_is_unspecified(const unsigned s[8]) {
  for (int i = 0; i < 8; ++i) {
    if (s[i] != 0) return false;
  }
  return true;
}

}  // namespace

std::string extract_upstream_host(const std::string& raw) {
  std::string s = raw;
  size_t b = s.find_first_not_of(" \t\r\n");
  size_t e = s.find_last_not_of(" \t\r\n");
  s = (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
  size_t hash = s.find('#');
  if (hash != std::string::npos) s = s.substr(0, hash);
  size_t scheme = s.find("://");
  if (scheme != std::string::npos) s = s.substr(scheme + 3);
  size_t slash = s.find('/');
  if (slash != std::string::npos) s = s.substr(0, slash);

  std::string host;
  if (!s.empty() && s[0] == '[') {
    size_t close = s.find(']');
    host = (close == std::string::npos) ? "" : s.substr(1, close - 1);
  } else {
    size_t colons = 0;
    for (char c : s) {
      if (c == ':') ++colons;
    }
    if (colons == 1) {
      host = s.substr(0, s.find(':'));
    } else {
      host = s;  // 裸 IPv6 或纯主机名
    }
  }
  b = host.find_first_not_of(" \t\r\n");
  e = host.find_last_not_of(" \t\r\n");
  host = (b == std::string::npos) ? "" : host.substr(b, e - b + 1);
  return host;
}

std::vector<std::string> dns_fallback_proxy_rules(const YAML::Node& dns) {
  std::vector<std::string> rules;
  std::unordered_set<std::string> seen;
  if (!dns || !dns.IsMap()) return rules;

  const YAML::Node fallback = static_cast<const YAML::Node&>(dns)["fallback"];
  if (!fallback.IsDefined()) return rules;

  auto process_server = [&](const YAML::Node& server) {
    if (!field::is_string_key(server)) return;  // serde Value::as_str 语义
    std::string host = extract_upstream_host(server.Scalar());
    if (host.empty()) return;

    std::vector<std::string> candidates;
    unsigned v4[4];
    unsigned v6[8];
    if (parse_ipv4(host, v4)) {
      if (ipv4_is_private(v4) || ipv4_is_loopback(v4) || ipv4_is_link_local(v4))
        return;
      candidates.push_back("IP-CIDR," + host + "/32,PROXY,no-resolve");
      // 公共 DNS 对偶补齐（消除辅 IP 漏入 MATCH,DIRECT 造成的超时）
      if (host == "8.8.8.8") {
        candidates.push_back("IP-CIDR,8.8.4.4/32,PROXY,no-resolve");
      } else if (host == "8.8.4.4") {
        candidates.push_back("IP-CIDR,8.8.8.8/32,PROXY,no-resolve");
      } else if (host == "1.1.1.1") {
        candidates.push_back("IP-CIDR,1.0.0.1/32,PROXY,no-resolve");
      } else if (host == "1.0.0.1") {
        candidates.push_back("IP-CIDR,1.1.1.1/32,PROXY,no-resolve");
      }
    } else if (parse_ipv6(host, v6)) {
      if (ipv6_is_loopback(v6) || ipv6_is_unspecified(v6)) return;
      if ((v6[0] & 0xfe00) == 0xfc00) return;  // ULA
      if ((v6[0] & 0xffc0) == 0xfe80) return;  // 链路本地
      candidates.push_back("IP-CIDR6," + host + "/128,PROXY,no-resolve");
    } else {
      candidates.push_back("DOMAIN," + host + ",PROXY");
      if (host == "dns.google") {
        candidates.push_back("IP-CIDR,8.8.8.8/32,PROXY,no-resolve");
        candidates.push_back("IP-CIDR,8.8.4.4/32,PROXY,no-resolve");
      }
    }
    for (const auto& r : candidates) {
      if (seen.insert(r).second) rules.push_back(r);
    }
  };

  if (fallback.IsSequence()) {
    for (const auto& server : fallback) process_server(server);
  } else {
    process_server(fallback);
  }
  return rules;
}

YAML::Node enforce_mini_agreements(const YAML::Node& config,
                                   const MiniAgreementOptions& opt) {
  YAML::Node cfg = config;

  // 1. 收集 proxies 名单（dummy 与 server 无效节点不入组；节点定义保留在 proxies）
  std::vector<std::string> proxy_names;
  {
    const YAML::Node proxies = static_cast<const YAML::Node&>(cfg)["proxies"];
    if (proxies && proxies.IsSequence()) {
      for (const auto& p : proxies) {
        if (!p.IsMap()) {
          // 纯字符串形式（"- 节点名"）没有 server 字段，沿用名字过滤
          if (field::is_string_key(p) && !util::is_dummy_node(p.Scalar())) {
            proxy_names.push_back(p.Scalar());
          }
          continue;
        }
        const YAML::Node name = p["name"];
        if (!field::is_string_key(name)) continue;
        const std::string& name_s = name.Scalar();
        if (util::is_dummy_node(name_s)) continue;
        // BUG-301：占位假节点的 server 藏不住 —— 按有效性拦截入组
        const YAML::Node server = p["server"];
        if (field::is_string_key(server) &&
            !util::is_valid_node_server(server.Scalar())) {
          continue;
        }
        proxy_names.push_back(name_s);
      }
    }
  }

  // 2. 收集 proxy-providers 名单
  std::vector<std::string> provider_names;
  {
    const YAML::Node providers =
        static_cast<const YAML::Node&>(cfg)["proxy-providers"];
    if (providers && providers.IsMap()) {
      for (const auto& kv : providers) {
        if (field::is_string_key(kv.first)) {
          provider_names.push_back(kv.first.Scalar());
        }
      }
    }
  }

  auto build_group = [&](const char* name, const char* type) {
    YAML::Node g(YAML::NodeType::Map);
    {
      YAML::Node n(name);
      n.SetTag("!");
      g["name"] = n;
    }
    {
      YAML::Node n(type);
      n.SetTag("!");
      g["type"] = n;
    }
    if (!proxy_names.empty()) {
      YAML::Node seq(YAML::NodeType::Sequence);
      for (const auto& p : proxy_names) {
        YAML::Node n(p);
        n.SetTag("!");
        seq.push_back(n);
      }
      g["proxies"] = seq;
    }
    if (!provider_names.empty()) {
      YAML::Node seq(YAML::NodeType::Sequence);
      for (const auto& p : provider_names) {
        YAML::Node n(p);
        n.SetTag("!");
        seq.push_back(n);
      }
      g["use"] = seq;
    }
    if (!g["proxies"].IsDefined() && !g["use"].IsDefined()) {
      YAML::Node seq(YAML::NodeType::Sequence);
      YAML::Node n("DIRECT");
      n.SetTag("!");
      seq.push_back(n);
      g["proxies"] = seq;
    }
    return g;
  };

  // 3. 控制组 PROXY(select) —— 用户/app 控制，选择持久不被内核翻回
  YAML::Node single_group = build_group("PROXY", "select");

  // 3b. 测量组 PROXY__METRICS(url-test, interval 300) —— 节点延迟唯一权威来源
  YAML::Node metrics_group = build_group("PROXY__METRICS", "url-test");
  {
    YAML::Node n(opt.test_url);
    n.SetTag("!");
    metrics_group["url"] = n;
  }
  metrics_group["interval"] = YAML::Node("300");

  YAML::Node groups(YAML::NodeType::Sequence);
  groups.push_back(single_group);
  groups.push_back(metrics_group);
  cfg["proxy-groups"] = groups;

  // 4. 注入 gfwlist rule-provider
  auto text_node = [](const std::string& s) {
    YAML::Node n(s);
    n.SetTag("!");
    return n;
  };

  YAML::Node gfwlist(YAML::NodeType::Map);
  gfwlist["type"] = text_node("http");
  gfwlist["behavior"] = text_node("domain");
  gfwlist["url"] =
      text_node("https://cdn.jsdelivr.net/gh/Loyalsoldier/clash-rules@release/gfw.txt");
  gfwlist["path"] = text_node("./ruleset/gfwlist.yaml");
  gfwlist["interval"] = YAML::Node("86400");
  gfwlist["proxy"] = text_node("PROXY");

  YAML::Node rule_providers(YAML::NodeType::Map);
  {
    const YAML::Node rp = static_cast<const YAML::Node&>(cfg)["rule-providers"];
    if (rp && rp.IsMap()) rule_providers = YAML::Clone(rp);
  }
  rule_providers["gfwlist"] = gfwlist;
  cfg["rule-providers"] = rule_providers;

  // 5. 规则链
  std::vector<YAML::Node> final_rules;
  auto push_text = [&](const std::string& s) { final_rules.push_back(text_node(s)); };

  // prepend-rules（取出并删除键）
  {
    const YAML::Node prepend =
        static_cast<const YAML::Node&>(cfg)["prepend-rules"];
    if (prepend && prepend.IsSequence()) {
      for (const auto& r : prepend) final_rules.push_back(r);
    }
    if (prepend.IsDefined()) cfg.remove("prepend-rules");
  }

  push_text("RULE-SET,gfwlist,PROXY");

  // DNS fallback 上游保通（境外 DoH 走代理，救活防投毒链路；规则由真实
  // dns.fallback 推导，订阅换上游时自动跟随）
  {
    const YAML::Node dns = static_cast<const YAML::Node&>(cfg)["dns"];
    for (const auto& r : dns_fallback_proxy_rules(dns)) push_text(r);
  }

  // IPv4 私网/环回/链路本地 DIRECT（防「代理所有流量」下本地流量误转远端）
  push_text("IP-CIDR,127.0.0.0/8,DIRECT,no-resolve");
  push_text("IP-CIDR,172.16.0.0/12,DIRECT,no-resolve");
  push_text("IP-CIDR,192.168.0.0/16,DIRECT,no-resolve");
  push_text("IP-CIDR,10.0.0.0/8,DIRECT,no-resolve");
  push_text("IP-CIDR,100.64.0.0/10,DIRECT,no-resolve");
  push_text("IP-CIDR,169.254.0.0/16,DIRECT,no-resolve");
  // IPv6 环回/未指定/ULA/链路本地/组播（ULA 覆盖 TUN 自身地址段）
  push_text("IP-CIDR6,::1/128,DIRECT,no-resolve");
  push_text("IP-CIDR6,::/128,DIRECT,no-resolve");
  push_text("IP-CIDR6,fc00::/7,DIRECT,no-resolve");
  push_text("IP-CIDR6,fe80::/10,DIRECT,no-resolve");
  push_text("IP-CIDR6,ff00::/8,DIRECT,no-resolve");
  // IPv4-mapped IPv6 私网等价段（不可整体 ::ffff:0:0/96 DIRECT——会把国外也直连）
  push_text("IP-CIDR6,::ffff:127.0.0.0/104,DIRECT,no-resolve");
  push_text("IP-CIDR6,::ffff:10.0.0.0/104,DIRECT,no-resolve");
  push_text("IP-CIDR6,::ffff:172.16.0.0/108,DIRECT,no-resolve");
  push_text("IP-CIDR6,::ffff:192.168.0.0/112,DIRECT,no-resolve");
  push_text("IP-CIDR6,::ffff:100.64.0.0/106,DIRECT,no-resolve");
  push_text("IP-CIDR6,::ffff:169.254.0.0/112,DIRECT,no-resolve");

  // append-rules（取出并删除键）
  {
    const YAML::Node append =
        static_cast<const YAML::Node&>(cfg)["append-rules"];
    if (append && append.IsSequence()) {
      for (const auto& r : append) final_rules.push_back(r);
    }
    if (append.IsDefined()) cfg.remove("append-rules");
  }

  // MATCH 兜底方向随 rule_fallback；IPv6 兜底（GEOIP,CN + ::/0）仅在 GFWList
  // 模式生效；GEOIP 而非 GEOIP6（mihomo 不支持后者）；刻意不带 no-resolve
  // （域名目标先解析再判定，否则漏到 MATCH,DIRECT）
  if (opt.rule_fallback == "proxy") {
    push_text("MATCH,PROXY");
  } else {
    push_text("GEOIP,CN,DIRECT");
    push_text("IP-CIDR6,::/0,PROXY");
    push_text("MATCH,DIRECT");
  }

  YAML::Node rules(YAML::NodeType::Sequence);
  for (const auto& r : final_rules) rules.push_back(r);
  cfg["rules"] = rules;

  // 4b. BUG-310（v3.2.0）：仅保留最终 rules 实际引用的 rule-providers。
  // 机场订阅常自带未被使用的 China-Site/China-IP 等规则集，会在首次启动触发
  // 联网拉取并常驻内存；按引用裁剪后默认仅剩随包预置的 gfwlist，实现零拉取。
  {
    std::set<std::string> referenced{"gfwlist"};
    for (const auto& rule : final_rules) {
      if (!rule.IsScalar() || rule.IsNull()) continue;
      const std::string s = rule.Scalar();
      size_t c1 = s.find(',');
      if (c1 == std::string::npos) continue;
      size_t c2 = s.find(',', c1 + 1);
      if (c2 == std::string::npos) continue;
      std::string kind = s.substr(0, c1);
      size_t b = kind.find_first_not_of(" \t");
      size_t e = kind.find_last_not_of(" \t");
      if (b == std::string::npos) continue;
      std::string kind_trim = kind.substr(b, e - b + 1);
      for (char& ch : kind_trim) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (kind_trim != "RULE-SET") continue;
      std::string name = s.substr(c1 + 1, c2 - c1 - 1);
      b = name.find_first_not_of(" \t");
      e = name.find_last_not_of(" \t");
      if (b == std::string::npos) continue;
      referenced.insert(name.substr(b, e - b + 1));
    }

    YAML::Node pruned(YAML::NodeType::Map);
    for (const auto& kv : rule_providers) {
      if (!field::is_string_key(kv.first)) continue;
      if (referenced.count(kv.first.Scalar())) pruned[kv.first] = kv.second;
    }
    // gfwlist 始终由本流程注入（即使规则链被用户脚本改写也不缺失源）
    pruned["gfwlist"] = gfwlist;
    cfg["rule-providers"] = pruned;
  }

  return cfg;
}

}  // namespace clashwin::enhance
