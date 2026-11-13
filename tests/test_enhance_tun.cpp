// T08 单测：tun.rs 移植（fake-ip 注入 / filter 按条目 merge / ipv6 防泄漏 /
// TUN 关时 dns.ipv6 跟随）
#include <string>

#include "enhance/tun.h"
#include "test_util.h"

using namespace clashwin;
using namespace clashwin::enhance;

static bool has_key(const YAML::Node& map, const std::string& key) {
  const YAML::Node v = static_cast<const YAML::Node&>(map)[key];
  return v.IsDefined();
}

UT_TEST(t08_tun_enable_fake_ip_injection) {
  // 订阅自带 dns（enhanced-mode fake-ip + 自有 filter），无 fake-ip-range6
  YAML::Node config = YAML::Load(
      "ipv6: false\n"
      "dns:\n"
      "  enable: true\n"
      "  enhanced-mode: fake-ip\n"
      "  fake-ip-range: 198.18.0.1/16\n"
      "  ipv6: true\n"
      "  fake-ip-filter:\n"
      "  - '*.lan'\n"
      "  - time.*.com\n"
      "tun:\n  enable: false\n  stack: system\n");
  YAML::Node out = use_tun(config, true);

  // 顶层 ipv6 强制 true（IPv6 防泄漏）
  UT_CHECK(out["ipv6"].Scalar() == "true");
  // dns.ipv6 跟随用户开关（false）
  UT_CHECK(out["dns"]["ipv6"].Scalar() == "false");
  // fake-ip 键组已齐
  UT_CHECK(out["dns"]["enhanced-mode"].Scalar() == "fake-ip");
  UT_CHECK(out["dns"]["fake-ip-range"].Scalar() == "198.18.0.1/16");
  UT_CHECK(!has_key(out["dns"], "fake-ip-range6"));  // user_ipv6=false
  // fake-ip-filter 按条目补缺：订阅 2 条保留 + DEFAULT 补齐
  const YAML::Node& f = out["dns"]["fake-ip-filter"];
  UT_CHECK(f[0].Scalar() == "*.lan");
  UT_CHECK(f[1].Scalar() == "time.*.com");
  // DEFAULT 首三条（*.lan 已存在跳过）
  bool has_dns_msft = false, has_connect = false, has_nsi = false;
  for (const auto& item : f) {
    if (item.Scalar() == "dns.msftncsi.com") has_dns_msft = true;
    if (item.Scalar() == "+.msftconnecttest.com") has_connect = true;
    if (item.Scalar() == "+.msftncsi.com") has_nsi = true;
  }
  UT_CHECK(has_dns_msft && has_connect && has_nsi);
  // inet6-address 注入
  UT_CHECK_EQ(out["tun"]["inet6-address"].size(), size_t(1));
  UT_CHECK(out["tun"]["inet6-address"][0].Scalar() == "fdfe:dcba:9876::1/126");
  UT_CHECK(out["tun"]["enable"].Scalar() == "true");
  // 订阅自有 tun 字段保留
  UT_CHECK(out["tun"]["stack"].Scalar() == "system");
}

UT_TEST(t08_tun_enable_ipv6_on_fake_range6) {
  // 用户 ipv6=true → fake-ip-range6 注入（2001:2::0/64，fc00::/7 之外）+
  // dns.ipv6=true
  YAML::Node config = YAML::Load(
      "ipv6: true\ndns:\n  enable: true\n");
  YAML::Node out = use_tun(config, true);
  UT_CHECK(out["dns"]["fake-ip-range6"].Scalar() == "2001:2::0/64");
  UT_CHECK(out["dns"]["ipv6"].Scalar() == "true");
  // 无 filter 键 → DEFAULT 全量注入
  UT_CHECK_EQ(out["dns"]["fake-ip-filter"].size(), size_t(21));
  // enhanced-mode 缺失 → 注入 fake-ip
  UT_CHECK(out["dns"]["enhanced-mode"].Scalar() == "fake-ip");
}

UT_TEST(t08_tun_enable_redir_host_no_injection) {
  // enhanced-mode=redir-host → 不注入 fake-ip 键组，但 dns.ipv6 仍跟随
  YAML::Node config = YAML::Load(
      "ipv6: true\ndns:\n  enable: true\n  enhanced-mode: redir-host\n");
  YAML::Node out = use_tun(config, true);
  UT_CHECK(!has_key(out["dns"], "fake-ip-range"));
  UT_CHECK(!has_key(out["dns"], "fake-ip-filter"));
  UT_CHECK(out["dns"]["ipv6"].Scalar() == "true");
  UT_CHECK(out["ipv6"].Scalar() == "true");
  UT_CHECK(out["tun"]["enable"].Scalar() == "true");
}

UT_TEST(t08_tun_disable_follows_user_ipv6) {
  // TUN 关：不注入 TUN 专属字段；dns.ipv6 无条件跟随开关（订阅自带 true 也覆盖）
  YAML::Node config = YAML::Load(
      "ipv6: false\ndns:\n  enable: true\n  ipv6: true\n  enhanced-mode: fake-ip\n"
      "tun:\n  enable: true\n");
  YAML::Node out = use_tun(config, false);
  UT_CHECK(out["tun"]["enable"].Scalar() == "false");
  UT_CHECK(out["dns"]["ipv6"].Scalar() == "false");
  UT_CHECK(!has_key(out["dns"], "fake-ip-range"));
  UT_CHECK(!has_key(out["tun"], "inet6-address"));
}

UT_TEST(t08_tun_enable_sniffer_injection) {
  YAML::Node config = YAML::Load("ipv6: false\ndns:\n  enable: true\ntun:\n  enable: false\n");
  YAML::Node out = use_tun(config, true);

  const YAML::Node sniffer = out["sniffer"];
  UT_CHECK(sniffer && sniffer.IsMap());
  UT_CHECK(sniffer["enable"].Scalar() == "true");
  UT_CHECK(sniffer["force-dns-mapping"].Scalar() == "true");
  UT_CHECK(sniffer["parse-pure-ip"].Scalar() == "true");
  UT_CHECK(sniffer["override-destination"].Scalar() == "true");
  UT_CHECK(sniffer["sniff"]["HTTP"]["override-destination"].Scalar() == "true");
  UT_CHECK(sniffer["sniff"]["TLS"].IsDefined());
  UT_CHECK(sniffer["sniff"]["QUIC"].IsDefined());

  bool has_mijia = false, has_apple = false, has_lan = false, has_fe80 = false, has_fakeip = false;
  for (const auto& s : sniffer["skip-domain"]) {
    if (s.Scalar() == "Mijia Cloud") has_mijia = true;
    if (s.Scalar() == "+.push.apple.com") has_apple = true;
  }
  for (const auto& s : sniffer["skip-dst-address"]) {
    if (s.Scalar() == "192.168.0.0/16") has_lan = true;
    if (s.Scalar() == "fe80::/10") has_fe80 = true;
    if (s.Scalar() == "198.18.0.0/16") has_fakeip = true;
  }
  UT_CHECK(has_mijia && has_apple && has_lan && has_fe80 && !has_fakeip);
}

UT_TEST(t08_tun_enable_sniffer_merges_partial_without_duplicates) {
  YAML::Node config = YAML::Load(
      "ipv6: false\n"
      "sniffer:\n"
      "  enable: false\n"
      "  override-destination: false\n"
      "  skip-domain:\n"
      "  - Mijia Cloud\n"
      "  - '+.internal.example'\n"
      "dns:\n  enable: true\n");
  YAML::Node out = use_tun(config, true);
  const YAML::Node sniffer = out["sniffer"];
  UT_CHECK(sniffer["enable"].Scalar() == "true");
  UT_CHECK(sniffer["override-destination"].Scalar() == "false");
  UT_CHECK(sniffer["force-dns-mapping"].Scalar() == "true");
  UT_CHECK(sniffer["parse-pure-ip"].Scalar() == "true");
  int mijia_count = 0;
  bool has_internal = false, has_apple = false;
  for (const auto& s : sniffer["skip-domain"]) {
    if (s.Scalar() == "Mijia Cloud") ++mijia_count;
    if (s.Scalar() == "+.internal.example") has_internal = true;
    if (s.Scalar() == "+.push.apple.com") has_apple = true;
  }
  UT_CHECK_EQ(mijia_count, 1);
  UT_CHECK(has_internal && has_apple);
}

UT_TEST(t08_tun_disable_does_not_inject_sniffer) {
  YAML::Node config = YAML::Load("ipv6: false\ndns:\n  enable: true\n");
  YAML::Node out = use_tun(config, false);
  UT_CHECK(!has_key(out, "sniffer"));
}

UT_TEST(t08_tun_enable_user_ipv6_true_syncs_dns) {
  YAML::Node config = YAML::Load("ipv6: true\ndns:\n  enable: true\n");
  YAML::Node out = use_tun(config, true);
  UT_CHECK(out["dns"]["ipv6"].Scalar() == "true");
}
