#include "enhance/dns_defaults.h"

#include "enhance/field.h"

namespace clashwin::enhance {

namespace {

bool seq_contains_text(const YAML::Node& seq, const std::string& text) {
  for (const auto& item : seq) {
    if (item.IsScalar() && !item.IsNull() && item.Scalar() == text) return true;
  }
  return false;
}

YAML::Node str_list_node(std::initializer_list<const char*> items) {
  YAML::Node seq(YAML::NodeType::Sequence);
  for (const char* s : items) {
    YAML::Node n(s);
    n.SetTag("!");
    seq.push_back(n);
  }
  return seq;
}

}  // namespace

YAML::Node apply_mandatory_dns_settings(const YAML::Node& config) {
  YAML::Node cfg = config;

  // 仅补全缺失字段，绝不整体替换 dns 节点（保留订阅的 default-nameserver、
  // fallback、fallback-filter、nameserver-policy 等）。
  // ← mod.rs:439 config.remove("dns")：先取出再插回 → dns 恒移至映射尾部
  // （serde IndexMap insert 语义），与实机 golden 的键序一致。
  const YAML::Node dns_src = static_cast<const YAML::Node&>(cfg)["dns"];
  YAML::Node dns_val =
      (dns_src && dns_src.IsMap()) ? YAML::Clone(dns_src) : YAML::Node(YAML::NodeType::Map);
  if (dns_src.IsDefined()) cfg.remove("dns");

  auto has_key = [&dns_val](const std::string& k) {
    const YAML::Node v = static_cast<const YAML::Node&>(dns_val)[k];
    return v.IsDefined();
  };

  if (!has_key("enable")) {
    YAML::Node n("true");
    n.SetTag("?");
    dns_val["enable"] = n;
  }
  // 注：此处不补 dns.ipv6 —— 由 use_tun 作为唯一真源同步
  // 注：不设置 enhanced-mode / fake-ip-range —— 由 use_tun 在 TUN 场景写入，
  //     非 TUN 交由内核默认值，避免覆盖用户显式配置
  if (!has_key("nameserver")) {
    dns_val["nameserver"] = str_list_node(
        {"https://doh.pub/dns-query", "https://dns.alidns.com/dns-query",
         "114.114.114.114"});
  }
  if (!has_key("default-nameserver")) {
    dns_val["default-nameserver"] =
        str_list_node({"223.6.6.6", "223.5.5.5", "114.114.114.114"});
  }
  if (!has_key("proxy-server-nameserver")) {
    dns_val["proxy-server-nameserver"] = str_list_node(
        {"https://doh.pub/dns-query", "https://dns.alidns.com/dns-query",
         "tls://223.5.5.5"});
  }

  // 冷启动保底：default-nameserver / proxy-server-nameserver 确保含明文 UDP
  // 上游 223.5.5.5（仅追加，不覆盖已有条目）
  for (const char* key : {"default-nameserver", "proxy-server-nameserver"}) {
    YAML::Node val = dns_val[key];
    if (!val.IsDefined()) continue;
    if (val.IsSequence()) {
      if (!seq_contains_text(val, "223.5.5.5")) {
        YAML::Node n("223.5.5.5");
        n.SetTag("!");
        val.push_back(n);
      }
    } else if (val.IsScalar() && !val.IsNull() && val.Scalar() != "223.5.5.5" &&
               field::is_string_key(val)) {
      YAML::Node seq(YAML::NodeType::Sequence);
      YAML::Node orig(val.Scalar());
      orig.SetTag("!");
      seq.push_back(orig);
      YAML::Node n("223.5.5.5");
      n.SetTag("!");
      seq.push_back(n);
      dns_val[key] = seq;
    }
  }

  cfg["dns"] = dns_val;
  return cfg;
}

}  // namespace clashwin::enhance
