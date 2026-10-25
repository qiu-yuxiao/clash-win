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

}  // namespace clashwin::enhance
