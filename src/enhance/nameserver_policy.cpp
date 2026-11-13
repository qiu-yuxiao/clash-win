#include "enhance/nameserver_policy.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "enhance/field.h"
#include "enhance/mini_agreements.h"
#include "util/log.h"
#include "util/str_utils.h"

namespace clashwin::enhance {

namespace {

struct ParsedDnsUpstream {
  std::string host;
  bool is_plain = false;
  bool force_direct = false;
  bool is_special = false;
  std::string clean_upstream;
};

struct SanitizeDnsCounters {
  size_t rewritten_foreign = 0;
  size_t rewritten_plain = 0;
};

bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char ca = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] + 32) : a[i];
    char cb = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] + 32) : b[i];
    if (ca != cb) return false;
  }
  return true;
}

void push_unique_string(std::vector<std::string>& vec, const std::string& val) {
  if (std::find(vec.begin(), vec.end(), val) == vec.end()) {
    vec.push_back(val);
  }
}

ParsedDnsUpstream parse_dns_upstream(std::string_view raw) {
  ParsedDnsUpstream out;
  std::string trimmed = util::trim(raw);
  std::string without_frag = trimmed;
  std::string frag;
  if (size_t hash_pos = trimmed.find('#'); hash_pos != std::string::npos) {
    without_frag = util::trim(std::string_view(trimmed).substr(0, hash_pos));
    frag = util::trim(std::string_view(trimmed).substr(hash_pos + 1));
  }

  std::vector<std::string> kept_frags;
  if (!frag.empty()) {
    size_t start = 0;
    while (start <= frag.size()) {
      size_t amp = frag.find('&', start);
      std::string_view part_sv =
          (amp == std::string::npos)
              ? std::string_view(frag).substr(start)
              : std::string_view(frag).substr(start, amp - start);
      std::string part = util::trim(part_sv);
      if (!part.empty()) {
        // C12：mihomo 原生并不识别 #DIRECT / #PROXY 政策后缀；
        // #DIRECT 供清理器降级使用，且与 #PROXY 一并剥离，保留 #h3=true 等合法参数。
        if (iequals(part, "direct")) {
          out.force_direct = true;
        } else if (!iequals(part, "proxy")) {
          kept_frags.push_back(std::move(part));
        }
      }
      if (amp == std::string::npos) break;
      start = amp + 1;
    }
  }

  if (kept_frags.empty()) {
    out.clean_upstream = without_frag;
  } else {
    out.clean_upstream = without_frag + "#";
    for (size_t i = 0; i < kept_frags.size(); ++i) {
      if (i > 0) out.clean_upstream += '&';
      out.clean_upstream += kept_frags[i];
    }
  }

  std::string scheme;
  std::string_view rest = without_frag;
  if (size_t sep = without_frag.find("://"); sep != std::string::npos) {
    scheme = util::to_lower(std::string_view(without_frag).substr(0, sep));
    rest = std::string_view(without_frag).substr(sep + 3);
  }
  out.is_special = (scheme == "dhcp" || scheme == "rcode" || iequals(rest, "system"));
  out.is_plain = (scheme.empty() || scheme == "udp" || scheme == "tcp");
  out.host = util::to_lower(extract_upstream_host(without_frag));
  return out;
}

bool is_ipv4_private_or_special(const in_addr& addr) {
  const uint32_t ip = ntohl(addr.s_addr);
  const uint8_t o0 = static_cast<uint8_t>((ip >> 24) & 0xff);
  const uint8_t o1 = static_cast<uint8_t>((ip >> 16) & 0xff);
  if (o0 == 10) return true;                                  // 10.0.0.0/8
  if (o0 == 172 && o1 >= 16 && o1 <= 31) return true;         // 172.16.0.0/12
  if (o0 == 192 && o1 == 168) return true;                    // 192.168.0.0/16
  if (o0 == 127) return true;                                 // 127.0.0.0/8
  if (o0 == 169 && o1 == 254) return true;                    // 169.254.0.0/16
  if (o0 == 100 && (o1 & 0xc0) == 64) return true;            // 100.64.0.0/10
  return false;
}

bool is_ipv6_local_or_special(const in6_addr& addr) {
  bool all_zero = true;
  bool is_loopback = true;
  for (int i = 0; i < 16; ++i) {
    if (addr.s6_addr[i] != 0) all_zero = false;
    if (i < 15 && addr.s6_addr[i] != 0) is_loopback = false;
    if (i == 15 && addr.s6_addr[i] != 1) is_loopback = false;
  }
  if (all_zero || is_loopback) return true;
  const uint16_t seg0 =
      static_cast<uint16_t>((static_cast<uint16_t>(addr.s6_addr[0]) << 8) |
                            static_cast<uint16_t>(addr.s6_addr[1]));
  return (seg0 & 0xfe00) == 0xfc00 || (seg0 & 0xffc0) == 0xfe80;
}

bool is_local_or_domestic_dns_host(const std::string& host) {
  static const char* const kCnDnsIpv4[] = {
      "223.5.5.5",       "223.6.6.6",       "119.29.29.29",   "119.28.28.28",
      "114.114.114.114", "114.114.115.115", "180.76.76.76",   "180.184.1.1",
      "180.184.2.2",     "1.2.4.8",         "210.2.4.8",
  };
  static const char* const kCnDnsIpv6[] = {
      "2400:3200::1",
      "2400:3200:baba::1",
      "2402:4e00::",
      "2402:4e00:1::",
  };
  static const char* const kCnOrLocalDomains[] = {
      "doh.pub",   "dot.pub",      "dns.pub",       "alidns.com",  "dns.alidns.com",
      "doh.360.cn","dot.360.cn",   "360.cn",        "dnspod.cn",   "dns.dnspod.cn",
      "bytedns.com","cn",          "lan",           "local",       "internal",
      "home",      "home.arpa",    "localhost",     "corp",        "intranet",
  };

  if (host.empty()) return true;

  in_addr v4{};
  if (inet_pton(AF_INET, host.c_str(), &v4) == 1) {
    if (is_ipv4_private_or_special(v4)) return true;
    for (const char* ip : kCnDnsIpv4) {
      if (host == ip) return true;
    }
    return false;
  }

  in6_addr v6{};
  if (inet_pton(AF_INET6, host.c_str(), &v6) == 1) {
    if (is_ipv6_local_or_special(v6)) return true;
    for (const char* ip : kCnDnsIpv6) {
      if (host == ip) return true;
    }
    return false;
  }

  if (host.find('.') == std::string::npos) return true;

  for (const char* suffix : kCnOrLocalDomains) {
    const size_t slen = std::strlen(suffix);
    if (host == suffix) return true;
    if (host.size() > slen && util::ends_with(host, suffix) &&
        host[host.size() - slen - 1] == '.') {
      return true;
    }
  }
  return false;
}

std::vector<std::string> collect_dns_field_strings(const YAML::Node& dns,
                                                   const char* field_name) {
  std::vector<std::string> out;
  const YAML::Node val = static_cast<const YAML::Node&>(dns)[field_name];
  if (!val.IsDefined()) return out;
  if (val.IsScalar() && !val.IsNull() && field::is_string_key(val)) {
    out.push_back(val.Scalar());
  } else if (val.IsSequence()) {
    for (const auto& item : val) {
      if (item.IsScalar() && !item.IsNull() && field::is_string_key(item)) {
        out.push_back(item.Scalar());
      }
    }
  }
  return out;
}

std::string derive_domestic_doh_upstream(const YAML::Node& dns) {
  for (const std::string& raw : collect_dns_field_strings(dns, "nameserver")) {
    ParsedDnsUpstream parsed = parse_dns_upstream(raw);
    if (!parsed.is_special && !parsed.is_plain && !parsed.clean_upstream.empty() &&
        is_local_or_domestic_dns_host(parsed.host)) {
      // 降级保通模板去掉 fragment（如 #h3=true），避免跨上游携带不兼容参数（D-1）
      size_t hash = parsed.clean_upstream.find('#');
      return (hash == std::string::npos)
                 ? parsed.clean_upstream
                 : parsed.clean_upstream.substr(0, hash);
    }
  }
  return "https://doh.pub/dns-query";
}

const char* foreign_plain_doh_url(const std::string& host) {
  if (host == "8.8.8.8") return "https://8.8.8.8/dns-query";
  if (host == "8.8.4.4") return "https://8.8.4.4/dns-query";
  if (host == "1.1.1.1") return "https://1.1.1.1/dns-query";
  if (host == "1.0.0.1") return "https://1.0.0.1/dns-query";
  if (host == "9.9.9.9") return "https://9.9.9.9/dns-query";
  if (host == "149.112.112.112") return "https://149.112.112.112/dns-query";
  if (host == "208.67.222.222" || host == "208.67.220.220") {
    return "https://doh.opendns.com/dns-query";
  }
  return nullptr;
}

const char* dual_secondary_v4(const std::string& ip) {
  if (ip == "8.8.8.8") return "8.8.4.4";
  if (ip == "8.8.4.4") return "8.8.8.8";
  if (ip == "1.1.1.1") return "1.0.0.1";
  if (ip == "1.0.0.1") return "1.1.1.1";
  if (ip == "9.9.9.9") return "149.112.112.112";
  if (ip == "149.112.112.112") return "9.9.9.9";
  if (ip == "208.67.222.222") return "208.67.220.220";
  if (ip == "208.67.220.220") return "208.67.222.222";
  return nullptr;
}

const char* dual_secondary_v6(const std::string& ip) {
  if (ip == "2001:4860:4860::8888") return "2001:4860:4860::8844";
  if (ip == "2001:4860:4860::8844") return "2001:4860:4860::8888";
  if (ip == "2606:4700:4700::1111") return "2606:4700:4700::1001";
  if (ip == "2606:4700:4700::1001") return "2606:4700:4700::1111";
  if (ip == "2620:fe::fe") return "2620:fe::9";
  if (ip == "2620:fe::9") return "2620:fe::fe";
  if (ip == "2620:119:35::35") return "2620:119:53::53";
  if (ip == "2620:119:53::53") return "2620:119:35::35";
  return nullptr;
}

std::vector<const char*> known_domain_landing_v4_ips(const std::string& domain) {
  if (domain == "dns.google" || domain == "dns.google.com" || domain == "8888.google") {
    return {"8.8.8.8", "8.8.4.4"};
  }
  if (domain == "cloudflare-dns.com" || domain == "dns.cloudflare.com") {
    return {"1.1.1.1", "1.0.0.1"};
  }
  if (domain == "dns.quad9.net") {
    return {"9.9.9.9", "149.112.112.112"};
  }
  if (domain == "doh.opendns.com") {
    return {"208.67.222.222", "208.67.220.220"};
  }
  return {};
}

std::vector<std::string> dns_upstream_proxy_rules_from_hosts(
    const std::vector<std::string>& hosts) {
  std::vector<std::string> rules;
  std::unordered_set<std::string> seen;

  for (const std::string& raw_host : hosts) {
    std::string host = util::to_lower(util::trim(raw_host));
    if (host.empty() || is_local_or_domestic_dns_host(host)) continue;

    std::vector<std::string> candidates;
    in_addr v4{};
    in6_addr v6{};
    if (inet_pton(AF_INET, host.c_str(), &v4) == 1) {
      if (is_ipv4_private_or_special(v4)) continue;
      candidates.push_back("IP-CIDR," + host + "/32,PROXY,no-resolve");
      if (const char* peer = dual_secondary_v4(host)) {
        candidates.push_back(std::string("IP-CIDR,") + peer + "/32,PROXY,no-resolve");
      }
    } else if (inet_pton(AF_INET6, host.c_str(), &v6) == 1) {
      if (is_ipv6_local_or_special(v6)) continue;
      candidates.push_back("IP-CIDR6," + host + "/128,PROXY,no-resolve");
      if (const char* peer = dual_secondary_v6(host)) {
        candidates.push_back(std::string("IP-CIDR6,") + peer + "/128,PROXY,no-resolve");
      }
    } else {
      candidates.push_back("DOMAIN," + host + ",PROXY");
      for (const char* ip : known_domain_landing_v4_ips(host)) {
        candidates.push_back(std::string("IP-CIDR,") + ip + "/32,PROXY,no-resolve");
      }
    }

    for (const auto& rule : candidates) {
      if (seen.insert(rule).second) rules.push_back(rule);
    }
  }
  return rules;
}

size_t find_dns_proxy_rule_insert_pos(const YAML::Node& rules) {
  if (!rules || !rules.IsSequence()) return 0;
  for (size_t i = 0; i < rules.size(); ++i) {
    if (rules[i].IsScalar() && rules[i].Scalar() == "RULE-SET,gfwlist,PROXY") {
      size_t pos = i + 1;
      while (pos < rules.size() && rules[pos].IsScalar()) {
        const std::string& next_str = rules[pos].Scalar();
        if (util::ends_with(next_str, ",PROXY") ||
            util::ends_with(next_str, ",PROXY,no-resolve")) {
          ++pos;
        } else {
          break;
        }
      }
      return pos;
    }
  }

  for (size_t i = 0; i < rules.size(); ++i) {
    if (!rules[i].IsScalar()) continue;
    const std::string& s = rules[i].Scalar();
    if (util::starts_with(s, "IP-CIDR,127.0.0.0/8,") ||
        util::starts_with(s, "GEOIP,") ||
        util::starts_with(s, "IP-CIDR6,::/0,") ||
        util::starts_with(s, "MATCH,")) {
      return i;
    }
  }
  return rules.size();
}

std::vector<std::string> sanitize_upstream_items(
    const std::vector<std::string>& raw_items,
    bool respect_rules,
    const std::string& domestic_fallback,
    std::vector<std::string>& proxy_hosts,
    SanitizeDnsCounters& counters) {
  std::vector<std::string> cleaned;
  for (const std::string& raw : raw_items) {
    ParsedDnsUpstream parsed = parse_dns_upstream(raw);
    if (parsed.clean_upstream.empty()) continue;

    if (parsed.is_special || is_local_or_domestic_dns_host(parsed.host)) {
      push_unique_string(cleaned, parsed.clean_upstream);
      continue;
    }

    if (parsed.force_direct || !respect_rules) {
      ++counters.rewritten_foreign;
      push_unique_string(cleaned, domestic_fallback);
      continue;
    }

    std::string target_upstream;
    if (parsed.is_plain) {
      if (const char* doh = foreign_plain_doh_url(parsed.host)) {
        ++counters.rewritten_plain;
        target_upstream = doh;
      } else {
        target_upstream = parsed.clean_upstream;
      }
    } else {
      target_upstream = parsed.clean_upstream;
    }

    std::string target_host = util::to_lower(extract_upstream_host(target_upstream));
    if (target_host.empty()) target_host = parsed.host;
    if (!target_host.empty()) {
      push_unique_string(proxy_hosts, target_host);
    }
    push_unique_string(cleaned, target_upstream);
  }
  return cleaned;
}

void sanitize_dns_list_field(YAML::Node& dns,
                             const char* field_name,
                             bool respect_rules,
                             const std::string& domestic_fallback,
                             std::vector<std::string>& proxy_hosts,
                             SanitizeDnsCounters& counters) {
  const YAML::Node val = static_cast<const YAML::Node&>(dns)[field_name];
  if (!val.IsDefined()) return;

  bool was_scalar = false;
  std::vector<std::string> raw_items;
  if (val.IsScalar() && !val.IsNull() && field::is_string_key(val)) {
    was_scalar = true;
    raw_items.push_back(val.Scalar());
  } else if (val.IsSequence()) {
    for (const auto& item : val) {
      if (item.IsScalar() && !item.IsNull() && field::is_string_key(item)) {
        raw_items.push_back(item.Scalar());
      }
    }
  } else {
    return;
  }
  if (raw_items.empty()) return;

  std::vector<std::string> cleaned = sanitize_upstream_items(
      raw_items, respect_rules, domestic_fallback, proxy_hosts, counters);

  if (cleaned.empty()) {
    if (was_scalar) {
      YAML::Node s(domestic_fallback);
      s.SetTag("!");
      dns[field_name] = s;
    } else {
      YAML::Node seq(YAML::NodeType::Sequence);
      YAML::Node n(domestic_fallback);
      n.SetTag("!");
      seq.push_back(n);
      dns[field_name] = seq;
    }
  } else if (was_scalar && cleaned.size() == 1) {
    YAML::Node s(cleaned[0]);
    s.SetTag("!");
    dns[field_name] = s;
  } else {
    YAML::Node seq(YAML::NodeType::Sequence);
    for (const auto& entry : cleaned) {
      YAML::Node n(entry);
      n.SetTag("!");
      seq.push_back(n);
    }
    dns[field_name] = seq;
  }
}

void sanitize_nameserver_policy_map(YAML::Node& dns,
                                    bool respect_rules,
                                    const std::string& domestic_fallback,
                                    std::vector<std::string>& proxy_hosts,
                                    SanitizeDnsCounters& counters) {
  const YAML::Node policy = static_cast<const YAML::Node&>(dns)["nameserver-policy"];
  if (!policy || !policy.IsMap()) return;

  YAML::Node next_policy(YAML::NodeType::Map);
  for (const auto& kv : policy) {
    const YAML::Node& val = kv.second;
    bool was_scalar = false;
    std::vector<std::string> raw_items;
    if (val && val.IsScalar() && !val.IsNull() && field::is_string_key(val)) {
      was_scalar = true;
      raw_items.push_back(val.Scalar());
    } else if (val && val.IsSequence()) {
      for (const auto& item : val) {
        if (item.IsScalar() && !item.IsNull() && field::is_string_key(item)) {
          raw_items.push_back(item.Scalar());
        }
      }
    } else {
      next_policy[YAML::Clone(kv.first)] = YAML::Clone(kv.second);
      continue;
    }

    std::vector<std::string> cleaned = sanitize_upstream_items(
        raw_items, respect_rules, domestic_fallback, proxy_hosts, counters);

    YAML::Node k_node = YAML::Clone(kv.first);
    if (cleaned.empty()) {
      YAML::Node s(domestic_fallback);
      s.SetTag("!");
      next_policy[k_node] = s;
    } else if (was_scalar && cleaned.size() == 1) {
      YAML::Node s(cleaned[0]);
      s.SetTag("!");
      next_policy[k_node] = s;
    } else {
      YAML::Node seq(YAML::NodeType::Sequence);
      for (const auto& entry : cleaned) {
        YAML::Node n(entry);
        n.SetTag("!");
        seq.push_back(n);
      }
      next_policy[k_node] = seq;
    }
  }
  dns["nameserver-policy"] = next_policy;
}

void inject_dns_proxy_rules(YAML::Node& cfg,
                            const std::vector<std::string>& proxy_hosts) {
  if (proxy_hosts.empty()) return;
  const YAML::Node existing_rules = static_cast<const YAML::Node&>(cfg)["rules"];
  if (!existing_rules || !existing_rules.IsSequence()) return;

  std::vector<std::string> generated = dns_upstream_proxy_rules_from_hosts(proxy_hosts);
  std::vector<std::string> to_insert;
  for (const auto& rule : generated) {
    bool already_present = false;
    for (const auto& r : existing_rules) {
      if (r.IsScalar() && r.Scalar() == rule) {
        already_present = true;
        break;
      }
    }
    if (!already_present) to_insert.push_back(rule);
  }
  if (to_insert.empty()) return;

  const size_t insert_pos = find_dns_proxy_rule_insert_pos(existing_rules);
  YAML::Node rebuilt(YAML::NodeType::Sequence);
  for (size_t i = 0; i < insert_pos; ++i) {
    rebuilt.push_back(YAML::Clone(existing_rules[i]));
  }
  for (const auto& rule : to_insert) {
    YAML::Node n(rule);
    n.SetTag("!");
    rebuilt.push_back(n);
  }
  for (size_t i = insert_pos; i < existing_rules.size(); ++i) {
    rebuilt.push_back(YAML::Clone(existing_rules[i]));
  }
  cfg["rules"] = rebuilt;
}

bool yaml_bool_or_false(const YAML::Node& map, const std::string& key) {
  const YAML::Node v = static_cast<const YAML::Node&>(map)[key];
  if (!v || !v.IsScalar()) return false;
  return v.Scalar() == "true" || v.Scalar() == "True" || v.Scalar() == "TRUE";
}

}  // namespace

YAML::Node sanitize_dns_upstreams(const YAML::Node& config) {
  if (!config || !config.IsMap()) return YAML::Clone(config);
  YAML::Node cfg = YAML::Clone(config);

  std::vector<std::string> proxy_hosts;
  SanitizeDnsCounters counters;

  YAML::Node dns = cfg["dns"];
  if (dns && dns.IsMap()) {
    const bool respect_rules = yaml_bool_or_false(dns, "respect-rules");
    const std::string domestic_fallback = derive_domestic_doh_upstream(dns);

    sanitize_dns_list_field(dns, "nameserver", respect_rules, domestic_fallback,
                            proxy_hosts, counters);
    sanitize_dns_list_field(dns, "fallback", respect_rules, domestic_fallback,
                            proxy_hosts, counters);
    sanitize_nameserver_policy_map(dns, respect_rules, domestic_fallback,
                                   proxy_hosts, counters);
    cfg["dns"] = dns;
  }

  inject_dns_proxy_rules(cfg, proxy_hosts);

  if (counters.rewritten_foreign > 0 || counters.rewritten_plain > 0) {
    CWLOG_INFO("DNS 上游完成统一卫生处理：海外不可达改写境内 DoH " +
               std::to_string(counters.rewritten_foreign) + " 条，明文 UDP 升级 DoH " +
               std::to_string(counters.rewritten_plain) + " 条");
  }
  return cfg;
}

YAML::Node sanitize_nameserver_policy(const YAML::Node& config) {
  return sanitize_dns_upstreams(config);
}

}  // namespace clashwin::enhance
