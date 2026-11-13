#include "enhance/tun.h"

#include "enhance/enhance_constants.h"

namespace clashwin::enhance {

namespace {

// revise!：原位写入（yaml-cpp 已有键保持原位，新键尾部追加）
void revise(YAML::Node& map, const std::string& key, const YAML::Node& val) {
  map[key] = val;
}
void revise_str(YAML::Node& map, const std::string& key, const std::string& v) {
  YAML::Node n(v);
  n.SetTag("!");  // 字符串语义（fake-ip-range 等含 '/' 仍 plain 安全）
  map[key] = n;
}
void revise_bool(YAML::Node& map, const std::string& key, bool v) {
  YAML::Node n(v ? "true" : "false");
  n.SetTag("?");
  map[key] = n;
}

// contains_key 检查（const 视图，绝不创建占位键——键序是 G3 命门）
bool has_key(const YAML::Node& map, const std::string& key) {
  const YAML::Node v = static_cast<const YAML::Node&>(map)[key];
  return v.IsDefined();
}

bool seq_contains_text(const YAML::Node& seq, const std::string& text) {
  for (const auto& item : seq) {
    if (item.IsScalar() && !item.IsNull() && item.Scalar() == text) return true;
  }
  return false;
}

}  // namespace

YAML::Node use_tun(const YAML::Node& config, bool enable) {
  return use_tun_with_sniffer(config, enable, std::nullopt);
}

YAML::Node use_tun_with_sniffer(const YAML::Node& config, bool enable,
                                std::optional<bool> enable_sniffer) {
  YAML::Node cfg = config;

  const YAML::Node tun_src = static_cast<const YAML::Node&>(cfg)["tun"];
  YAML::Node tun_val = (tun_src && tun_src.IsMap()) ? YAML::Clone(tun_src)
                                                    : YAML::Node(YAML::NodeType::Map);

  // 用户「Allow IPv6」开关值（顶层 ipv6）——必须在 TUN 分支强制 ipv6=true 之前读取
  bool user_ipv6 = false;
  {
    const YAML::Node v = static_cast<const YAML::Node&>(cfg)["ipv6"];
    if (v && v.IsScalar() && v.Scalar() == "true") user_ipv6 = true;
  }

  if (enable) {
    // TUN 开启时接管 DNS：仅当 enhanced-mode 为 fake-ip（或未设置）时注入
    // fake-ip 键组，绝不整体覆盖订阅已提供的自定义字段
    const YAML::Node dns_src = static_cast<const YAML::Node&>(cfg)["dns"];
    YAML::Node dns_val = (dns_src && dns_src.IsMap()) ? YAML::Clone(dns_src)
                                                      : YAML::Node(YAML::NodeType::Map);
    std::string current_mode = "fake-ip";
    {
      const YAML::Node m = static_cast<const YAML::Node&>(dns_val)["enhanced-mode"];
      if (m && m.IsScalar() && !m.IsNull()) current_mode = m.Scalar();
    }

    if (current_mode == "fake-ip" || !has_key(dns_val, "enhanced-mode")) {
      revise_bool(dns_val, "enable", true);
      if (!has_key(dns_val, "enhanced-mode")) {
        revise_str(dns_val, "enhanced-mode", "fake-ip");
      }
      if (!has_key(dns_val, "fake-ip-range")) {
        revise_str(dns_val, "fake-ip-range", "198.18.0.1/16");
      }
      // fake-ip-range6 必须落在 fc00::/7 之外（2001:2::/64 为 RFC 5180 基准测试段），
      // 否则撞上 enforce_mini_agreements 注入的 IP-CIDR6,fc00::/7,DIRECT 直连规则
      if (user_ipv6 && !has_key(dns_val, "fake-ip-range6")) {
        revise_str(dns_val, "fake-ip-range6", "2001:2::0/64");
      }
      // fake-ip-filter：按条目 merge；真实订阅几乎都自带该键
      if (!has_key(dns_val, "fake-ip-filter")) {
        YAML::Node filters(YAML::NodeType::Sequence);
        for (const char* item : consts::kDefaultFakeIpFilter) {
          YAML::Node n(item);
          n.SetTag("!");
          filters.push_back(n);
        }
        dns_val["fake-ip-filter"] = filters;
      } else {
        const YAML::Node existing =
            static_cast<const YAML::Node&>(dns_val)["fake-ip-filter"];
        if (existing.IsSequence()) {
          YAML::Node seq = dns_val["fake-ip-filter"];
          for (const char* item : consts::kDefaultFakeIpFilter) {
            if (!seq_contains_text(seq, item)) {
              YAML::Node n(item);
              n.SetTag("!");
              seq.push_back(n);
            }
          }
        }
      }
    }
    // dns.ipv6 跟随「Allow IPv6」开关：对所有 enhanced-mode 一律生效（TUN/系统
    // 代理/手动三模式语义一致）。本函数是 dns.ipv6 的唯一写入点。
    revise_bool(dns_val, "ipv6", user_ipv6);
    revise(cfg, "dns", dns_val);

    // IPv6 防泄漏：TUN 启用必须让内核具备 IPv6 收包能力（否则 IPv6 绕过代理直连
    // 泄漏）。顶层 ipv6 管「TUN 是否接管 IPv6 路由」，dns.ipv6 管「是否使用 IPv6」。
    revise_bool(cfg, "ipv6", true);
    YAML::Node inet6(YAML::NodeType::Sequence);
    {
      YAML::Node n(consts::kTunInet6Address);
      n.SetTag("!");
      inet6.push_back(n);
    }
    tun_val["inet6-address"] = inet6;

    // TUN 模式下默认按需开启并补全 sniffer 域名嗅探（← ClashVerge enhance/tun.rs A10/§3.7）：
    // 若用户或订阅显式关闭，则尊重关闭状态；否则补全缺失的 TLS/HTTP/QUIC 嗅探与排除段。
    detail::apply_tun_sniffer_policy(cfg, enable_sniffer);
  } else {
    // 非 TUN：不注入 TUN 专属 DNS 字段；dns.ipv6 一律跟随开关（无论订阅是否自带 dns 段）
    const YAML::Node dns_src = static_cast<const YAML::Node&>(cfg)["dns"];
    YAML::Node dns_val = (dns_src && dns_src.IsMap()) ? YAML::Clone(dns_src)
                                                      : YAML::Node(YAML::NodeType::Map);
    revise_bool(dns_val, "ipv6", user_ipv6);
    revise(cfg, "dns", dns_val);
  }

  revise_bool(tun_val, "enable", enable);
  revise(cfg, "tun", tun_val);

  return cfg;
}

namespace detail {

namespace {

bool has_str_key(const YAML::Node& map, const std::string& key) {
  return static_cast<const YAML::Node&>(map)[key].IsDefined();
}

void insert_bool_if_missing(YAML::Node& map, const std::string& key, bool v) {
  if (!has_str_key(map, key)) {
    revise_bool(map, key, v);
  }
}

void append_missing_str_items(YAML::Node& map, const std::string& key,
                              const std::vector<const char*>& items) {
  const YAML::Node existing = static_cast<const YAML::Node&>(map)[key];
  YAML::Node seq(YAML::NodeType::Sequence);
  if (existing && existing.IsSequence()) {
    for (const auto& item : existing) {
      seq.push_back(YAML::Clone(item));
    }
  }
  for (const char* item : items) {
    bool found = false;
    for (const auto& cur : seq) {
      if (cur.IsScalar() && cur.Scalar() == item) {
        found = true;
        break;
      }
    }
    if (!found) {
      YAML::Node n(item);
      n.SetTag("!");
      seq.push_back(n);
    }
  }
  revise(map, key, seq);
}

YAML::Node make_ports_proto(std::initializer_list<std::pair<const char*, bool>> ports,
                            std::optional<bool> override_destination) {
  YAML::Node proto(YAML::NodeType::Map);
  YAML::Node seq(YAML::NodeType::Sequence);
  for (const auto& [val, is_string] : ports) {
    YAML::Node p(val);
    if (is_string) p.SetTag("!");
    seq.push_back(p);
  }
  proto["ports"] = seq;
  if (override_destination.has_value()) {
    revise_bool(proto, "override-destination", *override_destination);
  }
  return proto;
}

bool read_bool_true_default(const YAML::Node& map, const std::string& key, bool def_val) {
  const YAML::Node v = static_cast<const YAML::Node&>(map)[key];
  if (v && v.IsScalar() && !v.IsNull()) {
    if (v.Scalar() == "false") return false;
    if (v.Scalar() == "true") return true;
  }
  return def_val;
}

}  // namespace

void merge_tun_sniffer_defaults(YAML::Node& cfg, bool force_enable) {
  const YAML::Node sniffer_src = static_cast<const YAML::Node&>(cfg)["sniffer"];
  YAML::Node sniffer = (sniffer_src && sniffer_src.IsMap())
                           ? YAML::Clone(sniffer_src)
                           : YAML::Node(YAML::NodeType::Map);

  if (force_enable) {
    revise_bool(sniffer, "enable", true);
  } else {
    insert_bool_if_missing(sniffer, "enable", true);
  }
  // C9: 强制修正与虚假 IP 还原/纯 IP 嗅探强相关字段
  revise_bool(sniffer, "force-dns-mapping", true);
  revise_bool(sniffer, "parse-pure-ip", true);
  insert_bool_if_missing(sniffer, "override-destination", true);
  const bool effective_override_dest =
      read_bool_true_default(sniffer, "override-destination", true);

  const YAML::Node sniff_src = static_cast<const YAML::Node&>(sniffer)["sniff"];
  YAML::Node sniff_map = (sniff_src && sniff_src.IsMap())
                             ? YAML::Clone(sniff_src)
                             : YAML::Node(YAML::NodeType::Map);
  if (!has_str_key(sniff_map, "HTTP")) {
    revise(sniff_map, "HTTP",
           make_ports_proto({{"80", false}, {"8080-8880", true}},
                            effective_override_dest));
  } else {
    const YAML::Node existing_http = static_cast<const YAML::Node&>(sniff_map)["HTTP"];
    if (existing_http && existing_http.IsMap()) {
      YAML::Node http_copy = YAML::Clone(existing_http);
      revise_bool(http_copy, "override-destination", effective_override_dest);
      revise(sniff_map, "HTTP", http_copy);
    }
  }
  if (!has_str_key(sniff_map, "TLS")) {
    revise(sniff_map, "TLS",
           make_ports_proto({{"443", false}, {"8443", false}}, std::nullopt));
  }
  if (!has_str_key(sniff_map, "QUIC")) {
    revise(sniff_map, "QUIC",
           make_ports_proto({{"443", false}, {"8443", false}}, std::nullopt));
  }
  revise(sniffer, "sniff", sniff_map);

  static const std::vector<const char*> kSkipDomains = {
      "Mijia Cloud",
      "+.push.apple.com",
  };
  static const std::vector<const char*> kSkipDstCidrs = {
      "0.0.0.0/8",
      "127.0.0.0/8",
      "10.0.0.0/8",
      "172.16.0.0/12",
      "192.168.0.0/16",
      "100.64.0.0/10",
      "169.254.0.0/16",
      "224.0.0.0/4",
      "255.255.255.255/32",
      "::/128",
      "::1/128",
      "fc00::/7",
      "fe80::/10",
      "ff00::/8",
      "::ffff:127.0.0.0/104",
      "::ffff:10.0.0.0/104",
      "::ffff:172.16.0.0/108",
      "::ffff:192.168.0.0/112",
      "::ffff:100.64.0.0/106",
      "::ffff:169.254.0.0/112",
      "223.5.5.5/32",
      "223.6.6.6/32",
      "114.114.114.114/32",
      "119.29.29.29/32",
  };
  append_missing_str_items(sniffer, "skip-domain", kSkipDomains);
  append_missing_str_items(sniffer, "skip-dst-address", kSkipDstCidrs);

  revise(cfg, "sniffer", sniffer);
}

void apply_tun_sniffer_policy(YAML::Node& cfg,
                              std::optional<bool> enable_sniffer) {
  const YAML::Node sniffer_src = static_cast<const YAML::Node&>(cfg)["sniffer"];
  bool sub_explicit_disable = false;
  if (sniffer_src && sniffer_src.IsMap()) {
    const YAML::Node en = static_cast<const YAML::Node&>(sniffer_src)["enable"];
    if (en && en.IsScalar() && !en.IsNull() && en.Scalar() == "false") {
      sub_explicit_disable = true;
    }
  }

  if (enable_sniffer.has_value() && !*enable_sniffer) {
    if (sniffer_src && sniffer_src.IsMap()) {
      YAML::Node sniffer = YAML::Clone(sniffer_src);
      revise_bool(sniffer, "enable", false);
      revise(cfg, "sniffer", sniffer);
    }
    return;
  }
  if (!enable_sniffer.has_value() && sub_explicit_disable) {
    return;
  }
  if (enable_sniffer.has_value() && *enable_sniffer) {
    merge_tun_sniffer_defaults(cfg, true);
    return;
  }
  merge_tun_sniffer_defaults(cfg, false);
}

}  // namespace detail

}  // namespace clashwin::enhance
