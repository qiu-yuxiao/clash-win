#include "enhance/nameserver_policy.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "util/log.h"
#include "util/str_utils.h"

namespace clashwin::enhance {

namespace {

struct ParsedDnsUpstream {
  std::string host;
  bool is_plain = false;
  bool force_direct = false;
  bool explicit_proxy = false;
  bool is_special = false;
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

bool contains_str(const std::vector<std::string>& vec, const std::string& val) {
  return std::find(vec.begin(), vec.end(), val) != vec.end();
}

ParsedDnsUpstream parse_dns_upstream(std::string_view raw) {
  ParsedDnsUpstream out;
  std::string trimmed_all = util::trim(raw);
  std::string without_frag = trimmed_all;
  std::string frag;
  if (size_t hash_pos = trimmed_all.find('#'); hash_pos != std::string::npos) {
    without_frag = util::trim(std::string_view(trimmed_all).substr(0, hash_pos));
    frag = util::trim(std::string_view(trimmed_all).substr(hash_pos + 1));
  }
  std::string_view head = frag;
  if (size_t amp_pos = head.find('&'); amp_pos != std::string_view::npos) {
    head = head.substr(0, amp_pos);
  }
  std::string head_trimmed = util::trim(head);
  out.force_direct = iequals(head_trimmed, "direct");
  out.explicit_proxy = iequals(head_trimmed, "proxy");

  std::string scheme;
  std::string_view rest = without_frag;
  if (size_t sep = without_frag.find("://"); sep != std::string::npos) {
    scheme = util::to_lower(std::string_view(without_frag).substr(0, sep));
    rest = std::string_view(without_frag).substr(sep + 3);
  }
  out.is_special = (scheme == "dhcp" || scheme == "rcode" || iequals(rest, "system"));
  out.is_plain = (scheme.empty() || scheme == "udp" || scheme == "tcp");

  size_t path_pos = rest.find_first_of("/?");
  std::string authority = util::trim(path_pos == std::string_view::npos ? rest : rest.substr(0, path_pos));

  std::string host;
  if (!authority.empty() && authority[0] == '[') {
    size_t close = authority.find(']');
    if (close != std::string::npos) {
      host = util::trim(std::string_view(authority).substr(1, close - 1));
    }
  } else {
    size_t colon_count = static_cast<size_t>(std::count(authority.begin(), authority.end(), ':'));
    if (colon_count == 1) {
      size_t colon = authority.find(':');
      host = util::trim(std::string_view(authority).substr(0, colon));
    } else {
      host = authority;
    }
  }
  out.host = util::to_lower(host);
  return out;
}

bool is_blocked_foreign_public_dns(const std::string& host) {
  static const char* const kForeignDnsIps[] = {
      "8.8.8.8",
      "8.8.4.4",
      "1.1.1.1",
      "1.0.0.1",
      "9.9.9.9",
      "149.112.112.112",
      "208.67.222.222",
      "208.67.220.220",
      "2001:4860:4860::8888",
      "2001:4860:4860::8844",
      "2606:4700:4700::1111",
      "2606:4700:4700::1001",
      "2620:fe::fe",
      "2620:fe::9",
      "2620:119:35::35",
      "2620:119:53::53",
  };
  static const char* const kForeignDnsDomains[] = {
      "dns.cloudflare.com",
      "cloudflare-dns.com",
      "dns.google",
      "dns.google.com",
      "8888.google",
      "quad9.net",
      "opendns.com",
      "adguard-dns.com",
      "dns.adguard.com",
      "nextdns.io",
      "cleanbrowsing.org",
      "controld.com",
  };
  for (const char* ip : kForeignDnsIps) {
    if (host == ip) return true;
  }
  for (const char* dom : kForeignDnsDomains) {
    size_t dlen = std::strlen(dom);
    if (host == dom) return true;
    if (host.size() > dlen && util::ends_with(host, dom) &&
        host[host.size() - dlen - 1] == '.') {
      return true;
    }
  }
  return false;
}

const char* foreign_plain_doh_url(const std::string& host) {
  if (host == "8.8.8.8") return "https://8.8.8.8/dns-query";
  if (host == "8.8.4.4") return "https://8.8.4.4/dns-query";
  if (host == "1.1.1.1") return "https://1.1.1.1/dns-query";
  if (host == "1.0.0.1") return "https://1.0.0.1/dns-query";
  return nullptr;
}

std::string dns_host_proxy_rule(const std::string& host) {
  if (host.empty()) return {};
  in_addr v4{};
  if (inet_pton(AF_INET, host.c_str(), &v4) == 1) {
    return "IP-CIDR," + host + "/32,PROXY,no-resolve";
  }
  in6_addr v6{};
  if (inet_pton(AF_INET6, host.c_str(), &v6) == 1) {
    return "IP-CIDR6," + host + "/128,PROXY,no-resolve";
  }
  return "DOMAIN," + host + ",PROXY";
}

bool yaml_bool_or_false(const YAML::Node& map, const std::string& key) {
  const YAML::Node v = static_cast<const YAML::Node&>(map)[key];
  if (!v || !v.IsScalar()) return false;
  return v.Scalar() == "true" || v.Scalar() == "True" || v.Scalar() == "TRUE";
}

}  // namespace

YAML::Node sanitize_nameserver_policy(const YAML::Node& config) {
  if (!config || !config.IsMap()) return YAML::Clone(config);
  YAML::Node cfg = YAML::Clone(config);

  YAML::Node dns = cfg["dns"];
  if (!dns || !dns.IsMap()) return cfg;

  const bool respect_rules = yaml_bool_or_false(dns, "respect-rules");
  const YAML::Node policy = static_cast<const YAML::Node&>(dns)["nameserver-policy"];
  if (!policy || !policy.IsMap()) return cfg;

  std::vector<std::string> proxy_hosts;
  size_t dropped_entries = 0;
  size_t rewritten_plain = 0;
  size_t dropped_keys = 0;

  YAML::Node next_policy(YAML::NodeType::Map);
  for (const auto& kv : policy) {
    std::string key_str = kv.first.Scalar();
    const YAML::Node& val = kv.second;

    bool was_scalar = false;
    std::vector<std::string> raw_items;
    if (val && val.IsScalar()) {
      was_scalar = true;
      raw_items.push_back(val.Scalar());
    } else if (val && val.IsSequence()) {
      for (const auto& item : val) {
        if (item && item.IsScalar()) raw_items.push_back(item.Scalar());
      }
    } else {
      ++dropped_keys;
      continue;
    }

    std::vector<std::string> cleaned;
    for (const auto& raw : raw_items) {
      std::string trimmed = util::trim(raw);
      if (trimmed.empty()) {
        ++dropped_entries;
        continue;
      }
      ParsedDnsUpstream parsed = parse_dns_upstream(trimmed);
      if (parsed.is_special) {
        if (!contains_str(cleaned, trimmed)) cleaned.push_back(trimmed);
        continue;
      }

      std::string resolved;
      if (is_blocked_foreign_public_dns(parsed.host)) {
        if (parsed.force_direct || (!respect_rules && !parsed.explicit_proxy)) {
          ++dropped_entries;
          continue;
        }
        if (!parsed.explicit_proxy && respect_rules && !contains_str(proxy_hosts, parsed.host)) {
          proxy_hosts.push_back(parsed.host);
        }
        if (parsed.is_plain) {
          if (const char* doh = foreign_plain_doh_url(parsed.host)) {
            ++rewritten_plain;
            resolved = doh;
          } else {
            resolved = trimmed;
          }
        } else {
          resolved = trimmed;
        }
      } else {
        resolved = trimmed;
      }

      if (!contains_str(cleaned, resolved)) cleaned.push_back(resolved);
    }

    if (cleaned.empty()) {
      ++dropped_keys;
    } else if (was_scalar && cleaned.size() == 1) {
      YAML::Node k_node = YAML::Clone(kv.first);
      YAML::Node s_node(cleaned[0]);
      s_node.SetTag("!");
      next_policy[k_node] = s_node;
    } else {
      YAML::Node k_node = YAML::Clone(kv.first);
      YAML::Node seq(YAML::NodeType::Sequence);
      for (const auto& entry : cleaned) {
        YAML::Node n(entry);
        n.SetTag("!");
        seq.push_back(n);
      }
      next_policy[k_node] = seq;
    }
  }

  if (next_policy.size() == 0) {
    dns.remove("nameserver-policy");
  } else {
    dns["nameserver-policy"] = next_policy;
  }

  if (!proxy_hosts.empty()) {
    const YAML::Node existing_rules = static_cast<const YAML::Node&>(cfg)["rules"];
    std::vector<std::string> to_insert;
    for (const auto& host : proxy_hosts) {
      std::string rule = dns_host_proxy_rule(host);
      if (rule.empty()) continue;
      bool already_present = false;
      if (existing_rules && existing_rules.IsSequence()) {
        for (const auto& r : existing_rules) {
          if (r.IsScalar() && r.Scalar() == rule) {
            already_present = true;
            break;
          }
        }
      }
      if (!already_present && !contains_str(to_insert, rule)) {
        to_insert.push_back(rule);
      }
    }

    if (!to_insert.empty() && existing_rules && existing_rules.IsSequence()) {
      size_t insert_pos = existing_rules.size();
      for (size_t i = 0; i < existing_rules.size(); ++i) {
        if (!existing_rules[i].IsScalar()) continue;
        const std::string& s = existing_rules[i].Scalar();
        if (util::starts_with(s, "GEOIP,") ||
            util::starts_with(s, "IP-CIDR6,::/0,") ||
            util::starts_with(s, "MATCH,")) {
          insert_pos = i;
          break;
        }
      }

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
  }

  if (dropped_entries > 0 || rewritten_plain > 0 || dropped_keys > 0) {
    CWLOG_INFO("DNS nameserver-policy 卫生清理: 移除不可达项 " +
               std::to_string(dropped_entries) + " 条（空键 " +
               std::to_string(dropped_keys) + " 个），明文转 DoH " +
               std::to_string(rewritten_plain) + " 条");
  }
  return cfg;
}

}  // namespace clashwin::enhance
