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

std::string trim_copy(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// ← mod.rs:588-618 extract_upstream_host：支持 `1.1.1.1` / `8.8.8.8:53` /
// `https://1.1.1.1/dns-query` / `tls://8.8.8.8` / `[IPv6]:port` 写法
std::string extract_upstream_host(const std::string& raw) {
  std::string s = trim_copy(raw);
  size_t hash = s.find('#');
  if (hash != std::string::npos) s = s.substr(0, hash);
  size_t scheme = s.find("://");
  if (scheme != std::string::npos) s = s.substr(scheme + 3);
  size_t cut = s.find_first_of("/?");
  if (cut != std::string::npos) s = s.substr(0, cut);

  std::string host;
  if (!s.empty() && s.front() == '[') {
    size_t close = s.find(']');
    if (close == std::string::npos) return {};
    host = s.substr(1, close - 1);
  } else {
    size_t first_colon = s.find(':');
    size_t last_colon = s.rfind(':');
    if (first_colon != std::string::npos && first_colon == last_colon) {
      host = s.substr(0, first_colon);
    } else {
      host = s;  // 裸 IPv6 或纯主机名
    }
  }
  return trim_copy(host);
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
  // 对齐 Mini（M:enhance/mod.rs:429-524）：全链路不改写 dns.listen，保留订阅/默认配置
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
