// T08 单测：enforce_mini_agreements（规则链逐条对照 mod.rs 测试）+
// dns_fallback_proxy_rules + extract_upstream_host + get_merged_proxies
#include <fstream>
#include <string>
#include <vector>

#include "enhance/mini_agreements.h"
#include "enhance/enhance.h"
#include "enhance/nameserver_policy.h"
#include "test_enhance_util.h"
#include "test_util.h"

using namespace clashwin;
using namespace clashwin::enhance;

UT_TEST(t08_mini_agreements_rules_chain) {
  // ← mod.rs::test_enforce_mini_agreements_logic（23 条规则逐条对照）
  YAML::Node config = YAML::Load(
      "proxies:\n"
      "- name: node-A\n  type: ss\n  server: 1.2.3.4\n"
      "- name: node-B\n  type: vmess\n  server: 5.6.7.8\n"
      "proxy-providers:\n"
      "  providerA:\n    type: http\n    url: https://example.com\n"
      "proxy-groups:\n"
      "- name: Group-A\n  type: select\n  proxies:\n  - node-A\n"
      "- name: Group-B\n  type: select\n  proxies:\n  - node-B\n"
      "prepend-rules:\n- PROCESS-NAME,custom-process,DIRECT\n"
      "append-rules:\n- DOMAIN,custom-domain,REJECT\n");
  MiniAgreementOptions opt;  // test_url 默认 / rule_fallback direct
  YAML::Node out = enforce_mini_agreements(config, opt);

  // ① 双组：PROXY(select) + PROXY__METRICS(url-test)
  UT_CHECK_EQ(out["proxy-groups"].size(), size_t(2));
  const YAML::Node& g0 = out["proxy-groups"][0];
  UT_CHECK(g0["name"].Scalar() == "PROXY");
  UT_CHECK(g0["type"].Scalar() == "select");
  const YAML::Node& g1 = out["proxy-groups"][1];
  UT_CHECK(g1["name"].Scalar() == "PROXY__METRICS");
  UT_CHECK(g1["type"].Scalar() == "url-test");
  UT_CHECK(g1["url"].Scalar() == "https://cp.cloudflare.com/generate_204");
  UT_CHECK(g1["interval"].Scalar() == "300");
  UT_CHECK_EQ(g0["proxies"].size(), size_t(2));
  UT_CHECK(g0["proxies"][0].Scalar() == "node-A");
  UT_CHECK(g0["proxies"][1].Scalar() == "node-B");
  UT_CHECK_EQ(g0["use"].size(), size_t(1));
  UT_CHECK(g0["use"][0].Scalar() == "providerA");

  // ② gfwlist provider
  UT_CHECK(out["rule-providers"]["gfwlist"]["type"].Scalar() == "http");
  UT_CHECK(out["rule-providers"]["gfwlist"]["behavior"].Scalar() == "domain");
  UT_CHECK(out["rule-providers"]["gfwlist"]["url"].Scalar() ==
           "https://cdn.jsdelivr.net/gh/Loyalsoldier/clash-rules@release/gfw.txt");
  UT_CHECK(out["rule-providers"]["gfwlist"]["proxy"].Scalar() == "PROXY");
  UT_CHECK(out["rule-providers"]["gfwlist"]["interval"].Scalar() == "86400");

  // ③ 规则链 22 条（无 dns fallback 规则；v3.3.0 移除 IP-CIDR6,::/0,PROXY）
  const YAML::Node& rules = out["rules"];
  UT_CHECK_EQ(rules.size(), size_t(22));
  const char* expect[] = {
      "PROCESS-NAME,custom-process,DIRECT",
      "RULE-SET,gfwlist,PROXY",
      "IP-CIDR,127.0.0.0/8,DIRECT,no-resolve",
      "IP-CIDR,172.16.0.0/12,DIRECT,no-resolve",
      "IP-CIDR,192.168.0.0/16,DIRECT,no-resolve",
      "IP-CIDR,10.0.0.0/8,DIRECT,no-resolve",
      "IP-CIDR,100.64.0.0/10,DIRECT,no-resolve",
      "IP-CIDR,169.254.0.0/16,DIRECT,no-resolve",
      "IP-CIDR6,::1/128,DIRECT,no-resolve",
      "IP-CIDR6,::/128,DIRECT,no-resolve",
      "IP-CIDR6,fc00::/7,DIRECT,no-resolve",
      "IP-CIDR6,fe80::/10,DIRECT,no-resolve",
      "IP-CIDR6,ff00::/8,DIRECT,no-resolve",
      "IP-CIDR6,::ffff:127.0.0.0/104,DIRECT,no-resolve",
      "IP-CIDR6,::ffff:10.0.0.0/104,DIRECT,no-resolve",
      "IP-CIDR6,::ffff:172.16.0.0/108,DIRECT,no-resolve",
      "IP-CIDR6,::ffff:192.168.0.0/112,DIRECT,no-resolve",
      "IP-CIDR6,::ffff:100.64.0.0/106,DIRECT,no-resolve",
      "IP-CIDR6,::ffff:169.254.0.0/112,DIRECT,no-resolve",
      "DOMAIN,custom-domain,REJECT",
      "GEOIP,CN,DIRECT",
      "MATCH,DIRECT",
  };
  for (size_t i = 0; i < 22; ++i) {
    if (rules[i].Scalar() != expect[i]) {
      std::printf("    rule[%zu] = %s (expect %s)\n", i,
                  rules[i].Scalar().c_str(), expect[i]);
    }
    UT_CHECK(rules[i].Scalar() == expect[i]);
  }
  UT_CHECK(!out["prepend-rules"].IsDefined());
  UT_CHECK(!out["append-rules"].IsDefined());
}

UT_TEST(t08_mini_agreements_match_proxy_fallback) {
  YAML::Node config = YAML::Load("proxies:\n- name: a\n  type: ss\n  server: 1.1.1.1\n");
  MiniAgreementOptions opt;
  opt.rule_fallback = "proxy";
  YAML::Node out = enforce_mini_agreements(config, opt);
  const YAML::Node& rules = out["rules"];
  // rule_fallback=proxy → 仅 MATCH,PROXY（无 GEOIP/::/0 兜底）
  UT_CHECK(rules[rules.size() - 1].Scalar() == "MATCH,PROXY");
  // 顺序断言：prepend(RULE-SET 前) + GEOIP 不存在
  bool has_geoip = false;
  for (const auto& r : rules) {
    if (r.Scalar() == "GEOIP,CN,DIRECT") has_geoip = true;
  }
  UT_CHECK(!has_geoip);
}

UT_TEST(t08_mini_agreements_filter_dummy_and_invalid_server) {
  YAML::Node config = YAML::Load(
      "proxies:\n"
      "- name: 网址：https://x.com\n  type: ss\n  server: ok.com\n"
      "- name: good\n  type: ss\n  server: 1.2.3.4\n"
      "- name: v0-网址:a03.jiffdl.net\n  type: vmess\n  server: '1'\n"
      "- name: 5.6.7.8\n  type: ss\n  server: 5.6.7.8\n");
  MiniAgreementOptions opt;
  YAML::Node out = enforce_mini_agreements(config, opt);
  // dummy（广告名）与 server 无效（'1'）不入组；节点定义保留在 proxies
  UT_CHECK_EQ(out["proxies"].size(), size_t(4));
  const YAML::Node& g = out["proxy-groups"][0]["proxies"];
  UT_CHECK_EQ(g.size(), size_t(2));  // good + 5.6.7.8
  UT_CHECK(g[0].Scalar() == "good");
  UT_CHECK(g[1].Scalar() == "5.6.7.8");
}

UT_TEST(t08_dns_fallback_rules) {
  // v3.3.0: PROXY 规则统一收口至 sanitize_dns_upstreams → inject_dns_proxy_rules
  YAML::Node cfg = YAML::Load(
      "dns:\n"
      "  respect-rules: true\n"
      "  fallback:\n"
      "  - 'https://1.1.1.1/dns-query'\n"
      "  - 'https://8.8.8.8/dns-query#h3=true'\n"
      "  - 'tls://8.8.4.4:853'\n"
      "  - 'https://dns.google/dns-query'\n"
      "  - 'https://[2606:4700:4700::1111]/dns-query'\n"
      "  - '1.1.1.1'\n"
      "  - 'https://192.168.1.1/dns-query'\n"
      "  - '127.0.0.1'\n"
      "  - '10.0.0.1'\n"
      "  - 'https://[::1]/dns-query'\n"
      "  - 'https://[fe80::1]/dns-query'\n"
      "rules:\n"
      "- MATCH,DIRECT\n");
  YAML::Node out = sanitize_dns_upstreams(cfg);
  const YAML::Node rules = out["rules"];
  std::vector<std::string> expect{
      "IP-CIDR,1.1.1.1/32,PROXY,no-resolve",
      "IP-CIDR,1.0.0.1/32,PROXY,no-resolve",
      "IP-CIDR,8.8.8.8/32,PROXY,no-resolve",
      "IP-CIDR,8.8.4.4/32,PROXY,no-resolve",
      "DOMAIN,dns.google,PROXY",
      "IP-CIDR6,2606:4700:4700::1111/128,PROXY,no-resolve",
      "IP-CIDR6,2606:4700:4700::1001/128,PROXY,no-resolve",
      "MATCH,DIRECT",
  };
  UT_CHECK_EQ(rules.size(), expect.size());
  for (size_t i = 0; i < expect.size() && i < rules.size(); ++i) {
    UT_CHECK_EQ(rules[i].Scalar(), expect[i]);
  }

  // 单 8.8.8.8 → 对偶补齐
  YAML::Node single = YAML::Load(
      "dns:\n  respect-rules: true\n  fallback: ['https://8.8.8.8/dns-query']\n"
      "rules:\n  - MATCH,DIRECT\n");
  YAML::Node single_out = sanitize_dns_upstreams(single);
  UT_CHECK_EQ(single_out["rules"].size(), size_t(3));
  UT_CHECK_EQ(single_out["rules"][0].Scalar(),
              std::string("IP-CIDR,8.8.8.8/32,PROXY,no-resolve"));
  UT_CHECK_EQ(single_out["rules"][1].Scalar(),
              std::string("IP-CIDR,8.8.4.4/32,PROXY,no-resolve"));
  UT_CHECK_EQ(single_out["rules"][2].Scalar(), std::string("MATCH,DIRECT"));

  // 单 dns.google → DOMAIN + 落地 IP
  YAML::Node dg = YAML::Load(
      "dns:\n  respect-rules: true\n  fallback: ['https://dns.google/dns-query']\n"
      "rules:\n  - MATCH,DIRECT\n");
  YAML::Node dg_out = sanitize_dns_upstreams(dg);
  UT_CHECK_EQ(dg_out["rules"].size(), size_t(4));
  UT_CHECK_EQ(dg_out["rules"][0].Scalar(), std::string("DOMAIN,dns.google,PROXY"));
  UT_CHECK_EQ(dg_out["rules"][1].Scalar(),
              std::string("IP-CIDR,8.8.8.8/32,PROXY,no-resolve"));
  UT_CHECK_EQ(dg_out["rules"][2].Scalar(),
              std::string("IP-CIDR,8.8.4.4/32,PROXY,no-resolve"));
  UT_CHECK_EQ(dg_out["rules"][3].Scalar(), std::string("MATCH,DIRECT"));

  // 无 dns / 无 fallback → rules 原样保留
  YAML::Node empty = YAML::Load("rules: ['MATCH,DIRECT']\n");
  UT_CHECK_EQ(sanitize_dns_upstreams(empty)["rules"].size(), size_t(1));
  YAML::Node no_fb = YAML::Load("dns:\n  enable: true\nrules:\n  - MATCH,DIRECT\n");
  UT_CHECK_EQ(sanitize_dns_upstreams(no_fb)["rules"].size(), size_t(1));
}

UT_TEST(t08_extract_upstream_host) {
  UT_CHECK(extract_upstream_host("1.1.1.1") == "1.1.1.1");
  UT_CHECK(extract_upstream_host("8.8.8.8:53") == "8.8.8.8");
  UT_CHECK(extract_upstream_host("https://1.1.1.1/dns-query") == "1.1.1.1");
  UT_CHECK(extract_upstream_host("tls://8.8.4.4:853") == "8.8.4.4");
  UT_CHECK(extract_upstream_host("https://dns.google/dns-query#h3=true") ==
           "dns.google");
  UT_CHECK(extract_upstream_host("https://[2606:4700:4700::1111]/dns-query") ==
           "2606:4700:4700::1111");
  UT_CHECK(extract_upstream_host("2606:4700:4700::1001") ==
           "2606:4700:4700::1001");
  UT_CHECK(extract_upstream_host("   ") == "");
}

UT_TEST(t08_merged_proxies_suffix) {
  // 重名 (name, suffix) → ` #n`；不重名 → ` (suffix)`
  auto dir = enhance_test::temp_dir("clashwin_t08_mp");
  CreateDirectoryW((dir + L"\\profiles").c_str(), nullptr);
  const char* p1 =
      "proxies:\n"
      "- name: dup\n  type: ss\n  server: 1.1.1.1\n"
      "- name: only1\n  type: ss\n  server: 2.2.2.2\n";
  const char* p2 =
      "proxies:\n"
      "- name: dup\n  type: vmess\n  server: 3.3.3.3\n"
      "- name: only2\n  type: ss\n  server: 4.4.4.4\n";
  {
    std::ofstream f(dir + L"\\profiles\\Ra.yaml", std::ios::binary);
    f << p1;
  }
  {
    std::ofstream f(dir + L"\\profiles\\Rb.yaml", std::ios::binary);
    f << p2;
  }
  MergedProxiesSource src;
  src.profiles_dir = dir + L"\\profiles";
  src.items = {{"remote", "Ra.yaml", 1792947899}, {"remote", "Rb.yaml", 1792947899}};
  YAML::Node merged = get_merged_proxies(src);
  UT_CHECK_EQ(merged.size(), size_t(4));
  // 同 updated 同名 → 计数 2 → #1/#2
  std::string n0 = merged[0]["name"].Scalar();
  std::string n2 = merged[2]["name"].Scalar();
  std::string n1 = merged[1]["name"].Scalar();
  auto expect_suffix = format_updated_suffix(1792947899);
  std::printf("    n0=[%s] n1=[%s] n2=[%s] suffix=[%s]\n", n0.c_str(),
              n1.c_str(), n2.c_str(), expect_suffix.c_str());
  UT_CHECK(n0 == "dup #1 (" + expect_suffix + ")");
  UT_CHECK(n2 == "dup #2 (" + expect_suffix + ")");
  // 单名 → (suffix)
  UT_CHECK(n1 == "only1 (" + expect_suffix + ")");
  // HHMMSS 形态
  UT_CHECK_EQ(expect_suffix.size(), size_t(6));
}

UT_TEST(t08_sanitize_dns_upstreams_v330) {
  auto contains_rule = [](const YAML::Node& rules, const std::string& target) {
    for (const auto& r : rules) {
      if (r.IsScalar() && r.Scalar() == target) return true;
    }
    return false;
  };

  YAML::Node cfg_rules = YAML::Load(
      "dns:\n"
      "  enable: true\n"
      "  respect-rules: true\n"
      "  nameserver:\n"
      "    - 223.5.5.5\n"
      "    - 8.8.8.8\n"
      "rules:\n"
      "  - MATCH,DIRECT\n");
  YAML::Node out1 = sanitize_dns_upstreams(cfg_rules);
  const YAML::Node ns1 = out1["dns"]["nameserver"];
  UT_CHECK_EQ(ns1.size(), size_t(2));
  UT_CHECK_EQ(ns1[0].Scalar(), std::string("223.5.5.5"));
  UT_CHECK_EQ(ns1[1].Scalar(), std::string("https://8.8.8.8/dns-query"));
  const YAML::Node r1 = out1["rules"];
  UT_CHECK(contains_rule(r1, "IP-CIDR,8.8.8.8/32,PROXY,no-resolve"));
  UT_CHECK(contains_rule(r1, "IP-CIDR,8.8.4.4/32,PROXY,no-resolve"));
  UT_CHECK(!contains_rule(r1, "IP-CIDR,223.5.5.5/32,PROXY,no-resolve"));
  size_t match_idx = r1.size();
  size_t cidr_idx = r1.size();
  for (size_t i = 0; i < r1.size(); ++i) {
    if (r1[i].Scalar() == "IP-CIDR,8.8.8.8/32,PROXY,no-resolve") cidr_idx = i;
    if (r1[i].Scalar() == "MATCH,DIRECT") match_idx = i;
  }
  UT_CHECK(cidr_idx < match_idx);

  YAML::Node cfg_no_rules = YAML::Load(
      "dns:\n"
      "  enable: true\n"
      "  respect-rules: false\n"
      "  nameserver:\n"
      "    - https://dns.google/dns-query\n");
  YAML::Node out2 = sanitize_dns_upstreams(cfg_no_rules);
  const YAML::Node ns2 = out2["dns"]["nameserver"];
  UT_CHECK_EQ(ns2.size(), size_t(1));
  UT_CHECK_EQ(ns2[0].Scalar(), std::string("https://doh.pub/dns-query"));
}
