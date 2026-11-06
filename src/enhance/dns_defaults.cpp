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
  size_t path = s.find('/');
  if (path != std::string::npos) s = s.substr(0, path);

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
  // 【P0-6】DNS 监听端口避开 :53（同机 Clash Mini 内核默认占 0.0.0.0:53）。
  // 两个内核同时 bind :53 时后启动者报 “Start DNS server(UDP) error ... permitted”，
  // 表现为本内核解析失败、mixed-port 502。显式非 53 端口配置予以保留。
  {
    const YAML::Node ln = static_cast<const YAML::Node&>(dns_val)["listen"];
    bool need = !ln.IsDefined() || !ln.IsScalar() || ln.IsNull();
    if (!need) {
      const std::string s = trim_copy(ln.Scalar());
      need = s == ":53" || s == "0.0.0.0:53" || s == "127.0.0.1:53";
    }
    if (need) {
      YAML::Node n("127.0.0.1:1053");
      n.SetTag("!");
      dns_val["listen"] = n;
    }
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

YAML::Node sanitize_foreign_plain_dns_upstreams(const YAML::Node& config) {
  // ← mod.rs:543-586（v3.1.8 BUG-308）
  static constexpr const char* kForeignDns[][2] = {
      {"8.8.8.8", "https://8.8.8.8/dns-query"},
      {"8.8.4.4", "https://8.8.4.4/dns-query"},
      {"1.1.1.1", "https://1.1.1.1/dns-query"},
      {"1.0.0.1", "https://1.0.0.1/dns-query"},
  };

  YAML::Node cfg = config;
  const YAML::Node dns_src = static_cast<const YAML::Node&>(cfg)["dns"];
  if (!dns_src || !dns_src.IsMap()) return cfg;
  const YAML::Node ns_src = static_cast<const YAML::Node&>(dns_src)["nameserver"];
  if (!ns_src || !ns_src.IsSequence()) return cfg;

  YAML::Node dns_val = YAML::Clone(dns_src);
  YAML::Node out(YAML::NodeType::Sequence);
  bool rewritten = false;
  for (const auto& entry : ns_src) {
    if (!entry.IsScalar() || entry.IsNull() || !field::is_string_key(entry)) {
      out.push_back(YAML::Clone(entry));
      continue;
    }
    const std::string raw = trim_copy(entry.Scalar());
    // 已是加密上游（带 scheme）原样保留
    if (raw.find("://") != std::string::npos) {
      out.push_back(YAML::Clone(entry));
      continue;
    }
    const std::string host = extract_upstream_host(raw);
    const char* doh = nullptr;
    for (const auto& pair : kForeignDns) {
      if (host == pair[0]) {
        doh = pair[1];
        break;
      }
    }
    if (doh) {
      YAML::Node n(doh);
      n.SetTag("!");
      out.push_back(n);
      rewritten = true;
    } else {
      out.push_back(YAML::Clone(entry));
    }
  }
  if (rewritten) dns_val["nameserver"] = out;
  cfg["dns"] = dns_val;
  return cfg;
}

}  // namespace clashwin::enhance
