// 通用 URI 解析器（← utils/resolve/universal_parser.rs 全量移植）
// 逐函数对齐：Rust 每个分支/边界处理均有对应实现，禁止意译重写。
#include "enhance/uri_list.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "util/base64.h"
#include "util/str_utils.h"

namespace clashwin::enhance {

using util::ends_with;
using util::is_valid_utf8;
using util::percent_decode;
using util::starts_with;
using util::to_lower;
using util::trim;

namespace {

// ---------- 基础切分（Rust split_once / rsplit_once / splitn 语义） ----------

std::optional<std::pair<std::string_view, std::string_view>> split_once_char(
    std::string_view s, char sep) {
  size_t pos = s.find(sep);
  if (pos == std::string_view::npos) return std::nullopt;
  return std::make_pair(s.substr(0, pos), s.substr(pos + 1));
}

std::optional<std::pair<std::string_view, std::string_view>> rsplit_once_char(
    std::string_view s, char sep) {
  size_t pos = s.rfind(sep);
  if (pos == std::string_view::npos) return std::nullopt;
  return std::make_pair(s.substr(0, pos), s.substr(pos + 1));
}

// Rust splitn(2, pat)：最多切一刀；second 有值 = 分隔符命中（"a#" → Some("")）
struct Split2 {
  std::string_view first;
  std::optional<std::string_view> second;
  explicit operator bool() const { return second.has_value(); }
};

Split2 splitn2_char(std::string_view s, char sep) {
  size_t pos = s.find(sep);
  if (pos == std::string_view::npos) return {s, std::nullopt};
  return {s.substr(0, pos), s.substr(pos + 1)};
}

Split2 splitn2_str(std::string_view s, std::string_view sep) {
  size_t pos = s.find(sep);
  if (pos == std::string_view::npos) return {s, std::nullopt};
  return {s.substr(0, pos), s.substr(pos + sep.size())};
}

// Rust str::lines()：\n 分隔，行尾 \r 仅在 \r\n 终止符下剥除，末尾单个换行
// 不产生空行
std::vector<std::string_view> rust_lines(std::string_view s) {
  std::vector<std::string_view> out;
  size_t pos = 0;
  while (pos < s.size()) {
    size_t nl = s.find('\n', pos);
    std::string_view line =
        (nl == std::string_view::npos) ? s.substr(pos) : s.substr(pos, nl - pos);
    if (nl != std::string_view::npos && !line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    out.push_back(line);
    if (nl == std::string_view::npos) break;
    pos = nl + 1;
    if (pos == s.size()) break;  // 末尾换行不补空行
  }
  return out;
}

// ← u16::from_str：可选 '+'，全数字，无空白，>65535 → 失败
std::optional<uint16_t> rust_parse_u16(std::string_view s) {
  size_t i = 0;
  if (i < s.size() && s[i] == '+') ++i;
  if (i >= s.size()) return std::nullopt;
  unsigned long long v = 0;
  for (; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') return std::nullopt;
    v = v * 10 + static_cast<unsigned long long>(s[i] - '0');
    if (v > 65535) return std::nullopt;
  }
  return static_cast<uint16_t>(v);
}

// ← u32::from_str
std::optional<uint32_t> rust_parse_u32(std::string_view s) {
  size_t i = 0;
  if (i < s.size() && s[i] == '+') ++i;
  if (i >= s.size()) return std::nullopt;
  unsigned long long v = 0;
  for (; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') return std::nullopt;
    v = v * 10 + static_cast<unsigned long long>(s[i] - '0');
    if (v > 0xFFFFFFFFull) return std::nullopt;
  }
  return static_cast<uint32_t>(v);
}

// ← String::from_utf8：严格 UTF-8 校验（util::is_valid_utf8，str_utils 共享）

// ---------- YAML 构造小助手（保序：yaml-cpp Map 按插入序） ----------

void ystr(YAML::Node& m, const std::string& k, std::string_view v) {
  m[k] = std::string(v);
}

void yu16(YAML::Node& m, const std::string& k, uint16_t v) {
  m[k] = static_cast<int>(v);
}

// ← universal_parser.rs::parse_query_pairs：query 串 → (小写键, 原值)，
// 跳过键或值为空的项
std::vector<std::pair<std::string, std::string>> parse_query_pairs(
    std::string_view query) {
  std::vector<std::pair<std::string, std::string>> pairs;
  size_t start = 0;
  while (start <= query.size()) {
    size_t amp = query.find('&', start);
    std::string_view pair = (amp == std::string_view::npos)
                                ? query.substr(start)
                                : query.substr(start, amp - start);
    auto kv = splitn2_char(pair, '=');
    std::string key = to_lower(trim(kv.first));
    std::string value = kv.second ? std::string(*kv.second) : std::string();
    if (key.empty() || value.empty()) {
      if (amp == std::string_view::npos) break;
      start = amp + 1;
      continue;
    }
    pairs.emplace_back(std::move(key), std::move(value));
    if (amp == std::string_view::npos) break;
    start = amp + 1;
  }
  return pairs;
}

// ← normalize_bandwidth：纯数字补 " Mbps"，带单位原样保留
std::string normalize_bandwidth(std::string_view value) {
  if (!value.empty() &&
      value.find_first_not_of("0123456789") == std::string_view::npos) {
    return std::string(value) + " Mbps";
  }
  return std::string(value);
}

// 把 query 串切键值对（trojan/vless/hysteria2/tuic/wireguard 的裸循环口径：
// k 小写不 trim，v 原样，不跳空键）
std::vector<std::pair<std::string, std::string>> raw_query_pairs(
    std::string_view query) {
  std::vector<std::pair<std::string, std::string>> pairs;
  size_t start = 0;
  while (start <= query.size()) {
    size_t amp = query.find('&', start);
    std::string_view pair = (amp == std::string_view::npos)
                                ? query.substr(start)
                                : query.substr(start, amp - start);
    auto kv = splitn2_char(pair, '=');
    pairs.emplace_back(to_lower(std::string(kv.first)),
                       kv.second ? std::string(*kv.second) : std::string());
    if (amp == std::string_view::npos) break;
    start = amp + 1;
  }
  return pairs;
}

// alpn=v1,v2 → 逐段 percent-decode、滤空；全空 → 不写入
std::vector<std::string> decode_alpn_list(std::string_view v) {
  std::vector<std::string> list;
  size_t start = 0;
  while (start <= v.size()) {
    size_t comma = v.find(',', start);
    std::string_view seg = (comma == std::string_view::npos)
                               ? v.substr(start)
                               : v.substr(start, comma - start);
    std::string d = percent_decode(seg);
    if (!d.empty()) list.push_back(std::move(d));
    if (comma == std::string_view::npos) break;
    start = comma + 1;
  }
  return list;
}

}  // namespace

// ← extract_host_port
std::optional<std::pair<std::string, uint16_t>> extract_host_port(
    std::string_view host_port, uint16_t default_port) {
  host_port = trim(host_port);
  if (!host_port.empty() && host_port.front() == '[') {
    size_t close = host_port.find(']');
    if (close == std::string_view::npos) return std::nullopt;
    std::string host(host_port.substr(1, close - 1));
    std::string_view rest = host_port.substr(close + 1);
    uint16_t port = default_port;
    if (!rest.empty() && rest.front() == ':') {
      std::string port_str = trim(rest.substr(1));
      if (auto v = rust_parse_u16(port_str)) port = *v;
    }
    return std::make_pair(std::move(host), port);
  }
  auto parts = splitn2_char(host_port, ':');
  std::string host(trim(parts.first));
  std::string port_str = parts.second ? trim(*parts.second) : std::string();
  uint16_t port = default_port;
  if (auto v = rust_parse_u16(port_str)) port = *v;
  return std::make_pair(std::move(host), port);
}

// ← parse_vmess
std::optional<YAML::Node> parse_vmess(std::string_view link) {
  constexpr std::string_view kPrefix = "vmess://";
  if (!starts_with(link, kPrefix)) return std::nullopt;
  std::string payload = trim(link.substr(kPrefix.size()));
  auto decoded_bytes = util::decode_base64_robust(payload);
  if (!decoded_bytes) return std::nullopt;
  std::string decoded_str(reinterpret_cast<const char*>(decoded_bytes->data()),
                          decoded_bytes->size());
  if (!is_valid_utf8(decoded_str)) return std::nullopt;
  nlohmann::json j = nlohmann::json::parse(decoded_str, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return std::nullopt;

  // VMessJson 字段：Option<String> 非字符串（且非 null/缺失）→ 反序列化失败
  auto get_opt_str = [&](const char* k) -> std::optional<std::string> {
    if (!j.contains(k)) return std::nullopt;
    const auto& v = j[k];
    if (v.is_null()) return std::nullopt;
    if (!v.is_string()) return std::nullopt;  // 由下方 sentinel 区分
    return v.get<std::string>();
  };
  // 区分「字段缺失/null（合法）」与「类型错误（整体失败）」
  auto str_field_ok = [&](const char* k) {
    return !j.contains(k) || j[k].is_null() || j[k].is_string();
  };
  for (const char* k : {"ps", "add", "id", "net", "host", "path", "tls"}) {
    if (!str_field_ok(k)) return std::nullopt;
  }
  std::optional<std::string> ps = get_opt_str("ps");
  std::optional<std::string> add = get_opt_str("add");
  std::optional<std::string> id = get_opt_str("id");
  std::optional<std::string> net = get_opt_str("net");
  std::optional<std::string> host = get_opt_str("host");
  std::optional<std::string> path = get_opt_str("path");
  std::optional<std::string> tls = get_opt_str("tls");
  // port/aid：serde_json::Value（缺失视同 null）
  nlohmann::json port_v = j.contains("port") ? j["port"] : nlohmann::json();
  nlohmann::json aid_v = j.contains("aid") ? j["aid"] : nlohmann::json();

  // Number → as_u64 → as u16（截断）；String → parse；其它/失败 → 443
  auto num_as_u16 = [](const nlohmann::json& v) -> std::optional<uint16_t> {
    if (v.is_number_unsigned()) {
      return static_cast<uint16_t>(v.get<uint64_t>());
    }
    if (v.is_number_integer()) {
      int64_t n = v.get<int64_t>();
      if (n < 0) return std::nullopt;
      return static_cast<uint16_t>(static_cast<uint64_t>(n));
    }
    return std::nullopt;  // 浮点/其它类型 → None（serde as_u64 失败）
  };
  uint16_t port = 443;
  if (port_v.is_string()) {
    if (auto v = rust_parse_u16(port_v.get<std::string>())) port = *v;
  } else if (auto n = num_as_u16(port_v)) {
    port = *n;
  }

  uint32_t alter_id = 0;
  if (aid_v.is_string()) {
    if (auto v = rust_parse_u32(aid_v.get<std::string>())) alter_id = *v;
  } else if (aid_v.is_number_unsigned()) {
    alter_id = static_cast<uint32_t>(aid_v.get<uint64_t>());
  } else if (aid_v.is_number_integer()) {
    int64_t n = aid_v.get<int64_t>();
    if (n >= 0) alter_id = static_cast<uint32_t>(static_cast<uint64_t>(n));
  }

  YAML::Node map(YAML::NodeType::Map);
  ystr(map, "type", "vmess");
  ystr(map, "name", ps.value_or("VMess Node"));
  ystr(map, "server", add.value_or(""));
  yu16(map, "port", port);
  ystr(map, "uuid", id.value_or(""));
  map["alterId"] = static_cast<int>(alter_id);
  ystr(map, "cipher", "auto");
  map["udp"] = true;

  if (tls && *tls == "tls") {
    map["tls"] = true;
  }

  std::string network = net.value_or("");
  if (!network.empty() && network != "tcp") {
    ystr(map, "network", network);
    if (network == "ws") {
      YAML::Node ws_opts(YAML::NodeType::Map);
      if (path) ws_opts["path"] = *path;  // Some 即写入，空串同样写入
      if (host && !host->empty()) {
        YAML::Node headers(YAML::NodeType::Map);
        headers["Host"] = *host;
        ws_opts["headers"] = headers;
      }
      map["ws-opts"] = ws_opts;
    } else if (network == "grpc") {
      YAML::Node grpc_opts(YAML::NodeType::Map);
      if (path) grpc_opts["grpc-service-name"] = *path;
      map["grpc-opts"] = grpc_opts;
    }
  }
  return map;
}

// ← parse_ss
std::optional<YAML::Node> parse_ss(std::string_view link) {
  constexpr std::string_view kPrefix = "ss://";
  if (!starts_with(link, kPrefix)) return std::nullopt;
  std::string_view payload = link.substr(kPrefix.size());
  auto parts = splitn2_char(payload, '#');
  std::string_view base_part = parts.first;
  std::string remarks = parts.second ? percent_decode(*parts.second) : "SS Node";

  std::string method_pw;
  std::string host_port;
  if (base_part.find('@') != std::string_view::npos) {
    auto mp_hp = split_once_char(base_part, '@');
    if (!mp_hp) return std::nullopt;
    method_pw.assign(mp_hp->first);
    host_port.assign(mp_hp->second);
  } else {
    auto decoded = util::decode_base64_robust(base_part);
    if (!decoded) return std::nullopt;
    std::string decoded_str(reinterpret_cast<const char*>(decoded->data()),
                            decoded->size());
    if (!is_valid_utf8(decoded_str)) return std::nullopt;
    auto mp_hp = split_once_char(decoded_str, '@');
    if (!mp_hp) return std::nullopt;
    method_pw = std::string(mp_hp->first);
    host_port = std::string(mp_hp->second);
  }

  std::string decoded_method_pw;
  if (method_pw.find(':') == std::string::npos) {
    auto decoded = util::decode_base64_robust(method_pw);
    if (!decoded) return std::nullopt;
    decoded_method_pw.assign(reinterpret_cast<const char*>(decoded->data()),
                             decoded->size());
    if (!is_valid_utf8(decoded_method_pw)) return std::nullopt;
  } else {
    decoded_method_pw = method_pw;
  }

  auto mp_parts = splitn2_char(decoded_method_pw, ':');
  if (decoded_method_pw.find(':') == std::string_view::npos) {
    return std::nullopt;  // ← mp_parts.next()? 第二段缺失
  }
  std::string cipher(trim(mp_parts.first));
  std::string password =
      mp_parts.second ? trim(*mp_parts.second) : std::string();

  auto hp_parts = splitn2_char(host_port, '?');
  std::string_view server_port = hp_parts.first;

  auto sp = extract_host_port(server_port, 8388);
  if (!sp) return std::nullopt;

  YAML::Node map(YAML::NodeType::Map);
  ystr(map, "type", "ss");
  ystr(map, "name", remarks);
  ystr(map, "server", sp->first);
  yu16(map, "port", sp->second);
  ystr(map, "cipher", cipher);
  ystr(map, "password", password);
  map["udp"] = true;
  return map;
}

// ← parse_trojan
std::optional<YAML::Node> parse_trojan(std::string_view link) {
  constexpr std::string_view kPrefix = "trojan://";
  if (!starts_with(link, kPrefix)) return std::nullopt;
  std::string_view payload = link.substr(kPrefix.size());
  auto parts = splitn2_char(payload, '#');
  std::string_view base_part = parts.first;
  std::string remarks =
      parts.second ? percent_decode(*parts.second) : "Trojan Node";

  // 先分离 query，再按最后一个 @ 拆分 userinfo：密码含未编码 @、query 值含 @
  // 都不会切错
  auto qsplit = split_once_char(base_part, '?');
  std::string_view auth_host = qsplit ? qsplit->first : base_part;
  std::optional<std::string_view> query =
      qsplit ? std::optional<std::string_view>(qsplit->second) : std::nullopt;
  auto uh = rsplit_once_char(auth_host, '@');
  if (!uh) return std::nullopt;
  std::string password = percent_decode(uh->first);

  auto sp = extract_host_port(uh->second, 443);
  if (!sp) return std::nullopt;

  YAML::Node map(YAML::NodeType::Map);
  ystr(map, "type", "trojan");
  ystr(map, "name", remarks);
  ystr(map, "server", sp->first);
  yu16(map, "port", sp->second);
  ystr(map, "password", password);
  map["udp"] = true;

  if (query) {
    for (const auto& kv : raw_query_pairs(*query)) {
      const std::string& k = kv.first;
      const std::string& v = kv.second;
      if (k == "sni" && !v.empty()) {
        ystr(map, "sni", percent_decode(v));
      } else if (k == "allowinsecure" &&
                 (v == "1" || to_lower(v) == "true")) {
        map["skip-cert-verify"] = true;
      }
    }
  }
  return map;
}

// ← parse_vless
std::optional<YAML::Node> parse_vless(std::string_view link) {
  constexpr std::string_view kPrefix = "vless://";
  if (!starts_with(link, kPrefix)) return std::nullopt;
  std::string_view payload = link.substr(kPrefix.size());
  auto parts = splitn2_char(payload, '#');
  std::string_view base_part = parts.first;
  std::string remarks =
      parts.second ? percent_decode(*parts.second) : "VLESS Node";

  auto subparts = splitn2_char(base_part, '@');
  if (base_part.find('@') == std::string_view::npos) {
    return std::nullopt;  // 缺 '@'
  }
  std::string uuid(subparts.first);
  std::string_view host_port_query =
      subparts.second ? *subparts.second : std::string_view{};

  auto hpq = splitn2_char(host_port_query, '?');
  std::string_view host_port = hpq.first;
  std::optional<std::string_view> query = hpq.second;

  auto sp = extract_host_port(host_port, 443);
  if (!sp) return std::nullopt;

  YAML::Node map(YAML::NodeType::Map);
  ystr(map, "type", "vless");
  ystr(map, "name", remarks);
  ystr(map, "server", sp->first);
  yu16(map, "port", sp->second);
  ystr(map, "uuid", uuid);
  ystr(map, "cipher", "auto");
  map["udp"] = true;

  if (query) {
    YAML::Node reality_opts(YAML::NodeType::Map);
    bool has_reality = false;
    std::string network_type;
    std::string path_str;
    std::string host_str;

    for (const auto& kv : raw_query_pairs(*query)) {
      const std::string& k = kv.first;
      const std::string& v = kv.second;
      if (v.empty()) continue;
      if (k == "flow") {
        ystr(map, "flow", v);
      } else if (k == "security") {
        if (v == "tls" || v == "reality") map["tls"] = true;
      } else if (k == "sni") {
        ystr(map, "servername", percent_decode(v));
      } else if (k == "type") {
        network_type = v;
      } else if (k == "pbk") {
        reality_opts["public-key"] = v;
        has_reality = true;
      } else if (k == "sid") {
        reality_opts["short-id"] = v;
        has_reality = true;
      } else if (k == "fp") {
        ystr(map, "client-fingerprint", v);
      } else if (k == "alpn") {
        auto alpn_list = decode_alpn_list(v);
        if (!alpn_list.empty()) {
          YAML::Node seq(YAML::NodeType::Sequence);
          for (auto& a : alpn_list) seq.push_back(a);
          map["alpn"] = seq;
        }
      } else if (k == "path") {
        path_str = percent_decode(v);
      } else if (k == "host") {
        host_str = percent_decode(v);
      }
    }

    if (has_reality) map["reality-opts"] = reality_opts;

    if (!network_type.empty()) {
      std::string net_val = (network_type == "raw") ? "tcp" : network_type;
      ystr(map, "network", net_val);
      if (net_val == "ws") {
        YAML::Node ws_opts(YAML::NodeType::Map);
        if (!path_str.empty()) ws_opts["path"] = path_str;
        if (!host_str.empty()) {
          YAML::Node headers(YAML::NodeType::Map);
          headers["Host"] = host_str;
          ws_opts["headers"] = headers;
        }
        map["ws-opts"] = ws_opts;
      } else if (net_val == "grpc") {
        YAML::Node grpc_opts(YAML::NodeType::Map);
        if (!path_str.empty()) grpc_opts["grpc-service-name"] = path_str;
        map["grpc-opts"] = grpc_opts;
      }
    }
  }
  return map;
}

// ← parse_hysteria2
std::optional<YAML::Node> parse_hysteria2(std::string_view link) {
  constexpr std::string_view kPrefix = "hysteria2://";
  if (!starts_with(link, kPrefix)) return std::nullopt;
  std::string_view payload = link.substr(kPrefix.size());
  auto parts = splitn2_char(payload, '#');
  std::string_view base_part = parts.first;
  std::string remarks =
      parts.second ? percent_decode(*parts.second) : "Hysteria2 Node";

  auto qsplit = split_once_char(base_part, '?');
  std::string_view auth_host = qsplit ? qsplit->first : base_part;
  std::optional<std::string_view> query =
      qsplit ? std::optional<std::string_view>(qsplit->second) : std::nullopt;
  auto uh = rsplit_once_char(auth_host, '@');
  if (!uh) return std::nullopt;
  std::string password = percent_decode(uh->first);

  auto sp = extract_host_port(uh->second, 443);
  if (!sp) return std::nullopt;

  YAML::Node map(YAML::NodeType::Map);
  ystr(map, "type", "hysteria2");
  ystr(map, "name", remarks);
  ystr(map, "server", sp->first);
  yu16(map, "port", sp->second);
  ystr(map, "password", password);
  map["udp"] = true;

  if (query) {
    for (const auto& kv : raw_query_pairs(*query)) {
      const std::string& k = kv.first;
      const std::string& v = kv.second;
      if (v.empty()) continue;
      if (k == "insecure" && (v == "1" || to_lower(v) == "true")) {
        map["skip-cert-verify"] = true;
      } else if (k == "sni") {
        ystr(map, "sni", percent_decode(v));
      } else if (k == "obfs") {
        ystr(map, "obfs", percent_decode(v));
      } else if (k == "obfs-password") {
        ystr(map, "obfs-password", percent_decode(v));
      }
    }
  }
  return map;
}

// ← parse_hysteria
std::optional<YAML::Node> parse_hysteria(std::string_view link) {
  constexpr std::string_view kPrefix = "hysteria://";
  if (!starts_with(link, kPrefix)) return std::nullopt;
  std::string payload = trim(link.substr(kPrefix.size()));
  auto parts = splitn2_char(payload, '#');
  std::string_view base_part = parts.first;
  std::string remarks =
      parts.second ? percent_decode(*parts.second) : "Hysteria Node";

  auto qsplit = split_once_char(base_part, '?');
  std::string_view auth_host = qsplit ? qsplit->first : base_part;
  std::optional<std::string_view> query =
      qsplit ? std::optional<std::string_view>(qsplit->second) : std::nullopt;
  std::string_view host_port;
  std::optional<std::string_view> userinfo;
  if (auto uh = rsplit_once_char(auth_host, '@')) {
    host_port = uh->second;
    userinfo = uh->first;
  } else {
    host_port = auth_host;
  }

  auto sp = extract_host_port(host_port, 443);
  if (!sp) return std::nullopt;
  if (sp->first.empty()) return std::nullopt;

  YAML::Node map(YAML::NodeType::Map);
  ystr(map, "type", "hysteria");
  ystr(map, "name", remarks);
  ystr(map, "server", sp->first);
  yu16(map, "port", sp->second);

  std::optional<std::string> auth_str;
  if (userinfo) {
    std::string d = percent_decode(*userinfo);
    if (!d.empty()) auth_str = std::move(d);
  }
  std::optional<std::string> sni;
  std::optional<std::string> up;
  std::optional<std::string> down;
  std::optional<std::string> protocol;
  std::optional<std::string> alpn;
  std::optional<std::string> obfs_param;
  std::optional<std::string> obfs_plain;

  if (query) {
    for (const auto& kv : parse_query_pairs(*query)) {
      const std::string& k = kv.first;
      const std::string& v = kv.second;
      if (k == "auth" || k == "auth_str" || k == "auth-str") {
        auth_str = percent_decode(v);
      } else if (k == "peer" || k == "sni") {
        // SNI：v1 链接里通常叫 peer，也兼容直接给 sni
        sni = percent_decode(v);
      } else if (k == "up" || k == "upmbps") {
        up = normalize_bandwidth(v);
      } else if (k == "down" || k == "downmbps") {
        down = normalize_bandwidth(v);
      } else if (k == "protocol") {
        // 内核仅支持 udp / wechat-video / faketcp，其它取值忽略
        std::string proto = percent_decode(v);
        if (proto == "udp" || proto == "wechat-video" || proto == "faketcp") {
          protocol = std::move(proto);
        }
      } else if (k == "insecure" &&
                 (v == "1" || (v.size() == 4 && to_lower(v) == "true"))) {
        map["skip-cert-verify"] = true;
      } else if (k == "alpn") {
        alpn = v;
      } else if (k == "obfsparam" || k == "obfs-password" ||
                 k == "obfs_password") {
        // 混淆密码：不同生成器写作 obfsParam 或 obfs，都对应内核 obfs 字段
        obfs_param = percent_decode(v);
      } else if (k == "obfs") {
        obfs_plain = percent_decode(v);
      }
    }
  }

  if (auth_str) ystr(map, "auth-str", *auth_str);
  if (sni) ystr(map, "sni", *sni);
  if (up) ystr(map, "up", *up);
  if (down) ystr(map, "down", *down);
  if (protocol) ystr(map, "protocol", *protocol);
  if (obfs_param || obfs_plain) ystr(map, "obfs", obfs_param ? *obfs_param : *obfs_plain);
  if (alpn) {
    auto alpn_list = decode_alpn_list(*alpn);
    if (!alpn_list.empty()) {
      YAML::Node seq(YAML::NodeType::Sequence);
      for (auto& a : alpn_list) seq.push_back(a);
      map["alpn"] = seq;
    }
  }
  return map;
}

// ← parse_anytls
std::optional<YAML::Node> parse_anytls(std::string_view link) {
  constexpr std::string_view kPrefix = "anytls://";
  if (!starts_with(link, kPrefix)) return std::nullopt;
  std::string payload = trim(link.substr(kPrefix.size()));
  auto parts = splitn2_char(payload, '#');
  std::string_view base_part = parts.first;
  std::string remarks =
      parts.second ? percent_decode(*parts.second) : "AnyTLS Node";

  auto qsplit = split_once_char(base_part, '?');
  std::string_view auth_host = qsplit ? qsplit->first : base_part;
  std::optional<std::string_view> query =
      qsplit ? std::optional<std::string_view>(qsplit->second) : std::nullopt;
  auto uh = rsplit_once_char(auth_host, '@');
  if (!uh) return std::nullopt;
  std::string password = percent_decode(uh->first);
  if (password.empty()) return std::nullopt;

  auto sp = extract_host_port(uh->second, 443);
  if (!sp) return std::nullopt;
  if (sp->first.empty()) return std::nullopt;

  YAML::Node map(YAML::NodeType::Map);
  ystr(map, "type", "anytls");
  ystr(map, "name", remarks);
  ystr(map, "server", sp->first);
  yu16(map, "port", sp->second);
  ystr(map, "password", password);
  map["udp"] = true;

  if (query) {
    std::optional<std::string> sni;
    std::optional<std::string> alpn;
    for (const auto& kv : parse_query_pairs(*query)) {
      const std::string& k = kv.first;
      const std::string& v = kv.second;
      if (k == "sni" || k == "peer") {
        sni = percent_decode(v);
      } else if ((k == "insecure" || k == "allowinsecure" ||
                  k == "allow_insecure") &&
                 (v == "1" || (v.size() == 4 && to_lower(v) == "true"))) {
        map["skip-cert-verify"] = true;
      } else if (k == "alpn") {
        alpn = v;
      }
    }
    if (sni) ystr(map, "sni", *sni);
    if (alpn) {
      auto alpn_list = decode_alpn_list(*alpn);
      if (!alpn_list.empty()) {
        YAML::Node seq(YAML::NodeType::Sequence);
        for (auto& a : alpn_list) seq.push_back(a);
        map["alpn"] = seq;
      }
    }
  }
  return map;
}

// ← parse_tuic
std::optional<YAML::Node> parse_tuic(std::string_view link) {
  constexpr std::string_view kPrefix = "tuic://";
  if (!starts_with(link, kPrefix)) return std::nullopt;
  std::string_view payload = link.substr(kPrefix.size());
  auto parts = splitn2_char(payload, '#');
  std::string_view base_part = parts.first;
  std::string remarks =
      parts.second ? percent_decode(*parts.second) : "TUIC Node";

  auto qsplit = split_once_char(base_part, '?');
  std::string_view auth_host = qsplit ? qsplit->first : base_part;
  std::optional<std::string_view> query =
      qsplit ? std::optional<std::string_view>(qsplit->second) : std::nullopt;
  auto uh = rsplit_once_char(auth_host, '@');
  if (!uh) return std::nullopt;

  auto sp = extract_host_port(uh->second, 443);
  if (!sp) return std::nullopt;

  YAML::Node map(YAML::NodeType::Map);
  ystr(map, "type", "tuic");
  ystr(map, "name", remarks);
  ystr(map, "server", sp->first);
  yu16(map, "port", sp->second);
  map["udp"] = true;

  // Percent-decode user_info (uuid:password or token may be encoded)
  std::string user_info_decoded = percent_decode(uh->first);

  // v5: uuid:password；v4: token
  auto up = split_once_char(user_info_decoded, ':');
  if (up) {
    ystr(map, "uuid", up->first);
    ystr(map, "password", up->second);
  } else {
    ystr(map, "token", user_info_decoded);
  }

  if (query) {
    for (const auto& kv : raw_query_pairs(*query)) {
      const std::string& k = kv.first;
      const std::string& v = kv.second;
      if (v.empty()) continue;
      if (k == "congestion_control" || k == "congestion-controller") {
        ystr(map, "congestion-controller", v);
      } else if (k == "alpn") {
        auto alpn_list = decode_alpn_list(v);
        if (!alpn_list.empty()) {
          YAML::Node seq(YAML::NodeType::Sequence);
          for (auto& a : alpn_list) seq.push_back(a);
          map["alpn"] = seq;
        }
      } else if (k == "sni") {
        ystr(map, "sni", percent_decode(v));
      } else if (k == "allow_insecure" &&
                 (v == "1" || to_lower(v) == "true")) {
        map["skip-cert-verify"] = true;
      } else if (k == "disable_sni") {
        map["disable-sni"] = (v == "1" || to_lower(v) == "true");
      } else if (k == "udp_relay_mode") {
        ystr(map, "udp-relay-mode", v);
      } else if (k == "reduce_rtt") {
        map["reduce-rtt"] = (v == "1" || to_lower(v) == "true");
      } else if (k == "fast_open") {
        map["fast-open"] = (v == "1" || to_lower(v) == "true");
      }
    }
  }
  return map;
}

// ← parse_wireguard
std::optional<YAML::Node> parse_wireguard(std::string_view link) {
  constexpr std::string_view kPrefix = "wireguard://";
  if (!starts_with(link, kPrefix)) return std::nullopt;
  std::string_view payload = link.substr(kPrefix.size());
  auto parts = splitn2_char(payload, '#');
  std::string_view base_part = parts.first;
  std::string remarks =
      parts.second ? percent_decode(*parts.second) : "WireGuard Node";

  // wireguard://private-key@host:port?public-key=xxx&...
  auto kh = split_once_char(base_part, '@');
  if (!kh) return std::nullopt;
  std::string_view private_key_raw = kh->first;
  std::string_view host_port_query = kh->second;

  auto hq = split_once_char(host_port_query, '?');
  std::string_view host_port = hq ? hq->first : host_port_query;
  std::optional<std::string_view> query =
      hq ? std::optional<std::string_view>(hq->second) : std::nullopt;

  auto sp = extract_host_port(host_port, 51820);
  if (!sp) return std::nullopt;

  // Try base64 decode for private key, fallback to raw
  std::string private_key(private_key_raw);
  if (auto decoded = util::decode_base64_robust(private_key_raw)) {
    std::string decoded_str(reinterpret_cast<const char*>(decoded->data()),
                            decoded->size());
    if (is_valid_utf8(decoded_str)) private_key = std::move(decoded_str);
  }

  YAML::Node map(YAML::NodeType::Map);
  ystr(map, "type", "wireguard");
  ystr(map, "name", remarks);
  ystr(map, "server", sp->first);
  yu16(map, "port", sp->second);
  ystr(map, "private-key", private_key);
  map["udp"] = true;

  std::string public_key;
  std::string reserved_str;
  std::string address;
  std::string mtu_str;
  std::string psk;

  if (query) {
    for (const auto& kv : raw_query_pairs(*query)) {
      const std::string& k = kv.first;
      const std::string& v = kv.second;
      if (v.empty()) continue;
      if (k == "public-key" || k == "publickey" || k == "pk") {
        public_key = percent_decode(v);
      } else if (k == "reserved") {
        reserved_str = v;
      } else if (k == "address" || k == "ip") {
        address = percent_decode(v);
      } else if (k == "mtu") {
        mtu_str = v;
      } else if (k == "preshared-key" || k == "pre-shared-key" || k == "psk") {
        psk = percent_decode(v);
      }
    }
  }

  if (!public_key.empty()) ystr(map, "public-key", public_key);

  if (!reserved_str.empty()) {
    // reserved can be "209,98,59" or base64 like "U4An"
    if (reserved_str.find(',') != std::string::npos) {
      YAML::Node reserved(YAML::NodeType::Sequence);
      size_t start = 0;
      while (start <= reserved_str.size()) {
        size_t comma = reserved_str.find(',', start);
        std::string seg = (comma == std::string::npos)
                              ? reserved_str.substr(start)
                              : reserved_str.substr(start, comma - start);
        if (auto v = rust_parse_u16(trim(seg))) {
          reserved.push_back(static_cast<int>(*v));
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
      }
      if (reserved.size() > 0) map["reserved"] = reserved;
    } else if (auto decoded = util::decode_base64_robust(reserved_str)) {
      YAML::Node reserved(YAML::NodeType::Sequence);
      for (uint8_t b : *decoded) reserved.push_back(static_cast<int>(b));
      if (reserved.size() > 0) map["reserved"] = reserved;
    }
  }

  if (!address.empty()) {
    // address can be comma-separated ipv4,ipv6 with CIDR
    std::optional<std::string> ipv4_addr;
    std::vector<std::string> ipv6_addrs;
    size_t start = 0;
    while (start <= address.size()) {
      size_t comma = address.find(',', start);
      std::string part = trim((comma == std::string::npos)
                                  ? std::string_view(address).substr(start)
                                  : std::string_view(address).substr(start, comma - start));
      // Strip CIDR suffix if present
      std::string ip_only = part;
      if (auto slash = split_once_char(part, '/')) ip_only = std::string(slash->first);

      if (ip_only.find(':') != std::string::npos) {
        ipv6_addrs.push_back(ip_only);
      } else if (!ipv4_addr) {
        ipv4_addr = ip_only;  // 只取第一个 IPv4
      }
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
    if (ipv4_addr) ystr(map, "ip", *ipv4_addr);
    if (!ipv6_addrs.empty()) {
      // Clash schema: ipv6 can be a single string or an array
      if (ipv6_addrs.size() == 1) {
        ystr(map, "ipv6", ipv6_addrs[0]);
      } else {
        YAML::Node seq(YAML::NodeType::Sequence);
        for (auto& a : ipv6_addrs) seq.push_back(a);
        map["ipv6"] = seq;
      }
    }
  }

  if (!mtu_str.empty()) {
    if (auto v = rust_parse_u16(mtu_str)) yu16(map, "mtu", *v);
  }

  if (!psk.empty()) ystr(map, "pre-shared-key", psk);

  // Default allowed-ips
  YAML::Node allowed_ips(YAML::NodeType::Sequence);
  allowed_ips.push_back("0.0.0.0/0");
  map["allowed-ips"] = allowed_ips;

  return map;
}

// ← parse_socks5
std::optional<YAML::Node> parse_socks5(std::string_view link) {
  // Support both socks5:// and socks://
  std::string_view payload;
  if (starts_with(link, "socks5://")) {
    payload = link.substr(9);
  } else if (starts_with(link, "socks://")) {
    payload = link.substr(8);
  } else {
    return std::nullopt;
  }
  auto parts = splitn2_char(payload, '#');
  std::string_view base_part = parts.first;
  std::string remarks =
      parts.second ? percent_decode(*parts.second) : "SOCKS5 Node";

  // socks5://[username:password@]host:port（按最后一个 @ 拆）
  std::optional<std::string_view> user_info;
  std::string_view host_port = base_part;
  size_t at = base_part.rfind('@');
  if (at != std::string_view::npos) {
    user_info = base_part.substr(0, at);
    host_port = base_part.substr(at + 1);
  }

  auto sp = extract_host_port(host_port, 1080);
  if (!sp) return std::nullopt;

  YAML::Node map(YAML::NodeType::Map);
  ystr(map, "type", "socks5");
  ystr(map, "name", remarks);
  ystr(map, "server", sp->first);
  yu16(map, "port", sp->second);
  map["udp"] = true;

  if (user_info) {
    auto up = split_once_char(*user_info, ':');
    if (up) {
      ystr(map, "username", percent_decode(up->first));
      ystr(map, "password", percent_decode(up->second));
    }
  }
  return map;
}

// ← parse_http_fallback
std::optional<YAML::Node> parse_http_fallback(std::string_view line) {
  std::string t = trim(line);
  if (!t.empty() && t.front() == '#') {
    t = trim(t.substr(1));
  }
  constexpr std::string_view kMarker = "[HTTP]";
  if (!starts_with(t, kMarker)) return std::nullopt;
  std::string payload = trim(t.substr(kMarker.size()));

  auto parts = splitn2_str(payload, " (");
  std::string name(trim(parts.first));
  if (!parts.second) return std::nullopt;  // ← Rust parts.next()? 缺第二段
  std::string_view addr_port_part = *parts.second;
  if (addr_port_part.empty() || addr_port_part.back() != ')') return std::nullopt;
  const std::string addr_port =
      trim(addr_port_part.substr(0, addr_port_part.size() - 1));

  auto sp = split_once_char(addr_port, ':');
  if (!sp) return std::nullopt;
  uint16_t port = 80;
  if (auto v = rust_parse_u16(sp->second)) port = *v;

  YAML::Node map(YAML::NodeType::Map);
  ystr(map, "type", "http");
  ystr(map, "name", name);
  ystr(map, "server", trim(sp->first));
  yu16(map, "port", port);
  return map;
}

// ← parse_uri_list
std::optional<YAML::Node> parse_uri_list(std::string_view content) {
  std::vector<YAML::Node> proxies;

  // Check if whole content is base64 encoded（UTF-8 合法才采用）
  std::string decoded_content;
  if (auto decoded = util::decode_base64_robust(content)) {
    std::string s(reinterpret_cast<const char*>(decoded->data()), decoded->size());
    if (is_valid_utf8(s)) {
      decoded_content = std::move(s);
    }
  }
  std::string_view active =
      !decoded_content.empty() ? std::string_view(decoded_content) : content;

  for (auto line_sv : rust_lines(active)) {
    std::string line = trim(line_sv);
    if (line.empty()) continue;

    // Check for smart http fallback even if it starts with #
    if (auto map = parse_http_fallback(line)) {
      proxies.push_back(std::move(*map));
      continue;
    }

    if (starts_with(line, "#") || starts_with(line, "//")) continue;

    std::optional<YAML::Node> proxy_map;
    if (starts_with(line, "vmess://")) {
      proxy_map = parse_vmess(line);
    } else if (starts_with(line, "ss://")) {
      proxy_map = parse_ss(line);
    } else if (starts_with(line, "trojan://")) {
      proxy_map = parse_trojan(line);
    } else if (starts_with(line, "vless://")) {
      proxy_map = parse_vless(line);
    } else if (starts_with(line, "hysteria2://")) {
      proxy_map = parse_hysteria2(line);
    } else if (starts_with(line, "hysteria://")) {
      proxy_map = parse_hysteria(line);
    } else if (starts_with(line, "anytls://")) {
      proxy_map = parse_anytls(line);
    } else if (starts_with(line, "tuic://")) {
      proxy_map = parse_tuic(line);
    } else if (starts_with(line, "wireguard://")) {
      proxy_map = parse_wireguard(line);
    } else if (starts_with(line, "socks5://") || starts_with(line, "socks://")) {
      proxy_map = parse_socks5(line);
    }

    if (proxy_map) proxies.push_back(std::move(*proxy_map));
  }

  if (proxies.empty()) return std::nullopt;

  YAML::Node result(YAML::NodeType::Map);
  YAML::Node seq(YAML::NodeType::Sequence);
  for (auto& p : proxies) seq.push_back(p);
  result["proxies"] = seq;
  return result;
}

}  // namespace clashwin::enhance
