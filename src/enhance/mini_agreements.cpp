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

  // DNS 上游 PROXY 规则已统一收口至 sanitize_dns_upstreams → inject_dns_proxy_rules
  // 单一真源（覆盖 nameserver + fallback + nameserver-policy），此处不再重复注入（B-1/F-2）。

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

  // MATCH 兜底方向随 rule_fallback；默认 GFWList 模式保持 GEOIP,CN,DIRECT +
  // MATCH,DIRECT 对称直连兜底（已移除 IP-CIDR6,::/0,PROXY 防境内 IPv6 偷跑流量）；
  // GEOIP 而非 GEOIP6（mihomo 不支持后者）；刻意不带 no-resolve
  if (opt.rule_fallback == "proxy") {
    push_text("MATCH,PROXY");
  } else {
    push_text("GEOIP,CN,DIRECT");
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
