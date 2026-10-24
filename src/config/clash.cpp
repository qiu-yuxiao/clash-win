#include "clash.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <mutex>
#include <string>

#include "clashwin/config/constants.h"
#include "migrate.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/str_utils.h"
#include "util/utf.h"
#include "yaml_serde.h"

// 布局说明：IClashTemp 成员定义必须在 clashwin::config 命名空间内；
// 自由函数（clash::load / clash::make_template / clash::save_clash / dump）
// 位于 clashwin::config::clash。helpers 在 clash 匿名命名空间，成员侧以
// clash:: 限定名访问。
namespace clashwin::config::clash {

namespace {

// ---------- 端口解析（← clash.rs::guard_*_port 的取值路径） ----------
// String → parse；Number → as_u64 后 u16 截断（Rust `as u16` wrapping 语义，
// 官方单测 65537 → 1）；缺失/类型不符 → nullopt；0 → def（调用方处理）
std::optional<uint16_t> parse_port(const YAML::Node& map, const char* key) {
  const YAML::Node n = map[key];
  if (!n || !n.IsScalar() || n.IsNull()) return std::nullopt;
  const std::string& s = util::trim(n.Scalar());
  if (s.empty()) return std::nullopt;
  try {
    size_t pos = 0;
    unsigned long long v = std::stoull(s, &pos);
    if (pos != s.size()) return std::nullopt;
    return static_cast<uint16_t>(v);
  } catch (...) {
    return std::nullopt;
  }
}

uint16_t guard_port_common(const YAML::Node& map, const char* key,
                           uint16_t def) {
  uint16_t port = parse_port(map, key).value_or(def);
  if (port == 0) port = def;
  return port;
}

// ---------- SocketAddr 轻量解析（← guard_server_ctrl） ----------
struct SockAddrParsed {
  bool ok = false;
  std::string ip;
  uint16_t port = 0;
  bool unspecified = false;
};

// 支持 "host:port"、":port"、"[v6]:port"；仅接受 IP 字面量（与 Rust
// SocketAddr::from_str 一致，域名解析失败 → 非法）
SockAddrParsed parse_sockaddr(const std::string& s) {
  SockAddrParsed r;
  std::string host, port_str;
  if (!s.empty() && s[0] == '[') {
    size_t close = s.find(']');
    if (close == std::string::npos) return r;
    host = s.substr(1, close - 1);
    if (close + 1 >= s.size() || s[close + 1] != ':') return r;
    port_str = s.substr(close + 2);
  } else {
    size_t colon = s.rfind(':');
    if (colon == std::string::npos) return r;
    host = s.substr(0, colon);
    port_str = s.substr(colon + 1);
    // v6 裸地址含多个 ':' → 非法
    if (host.find(':') != std::string::npos) return r;
  }
  if (port_str.empty()) return r;
  try {
    size_t pos = 0;
    unsigned long p = std::stoul(port_str, &pos);
    if (pos != port_str.size() || p > 65535) return r;
    r.port = static_cast<uint16_t>(p);
  } catch (...) {
    return r;
  }
  r.ip = host;
  r.ok = true;
  unsigned char buf[16];
  if (inet_pton(AF_INET, host.c_str(), buf) == 1) {
    r.unspecified = host == "0.0.0.0";
  } else if (inet_pton(AF_INET6, host.c_str(), buf) == 1) {
    r.unspecified = host == "::" || host == "0:0:0:0:0:0:0:0";
  } else {
    r.ok = false;
  }
  return r;
}

// ← clash.rs::is_port_conflict 的 HTTP 探测：100ms 内对
// http://127.0.0.1:port/configs 发 GET，2xx 响应判「是我们自己的内核」
bool http_probe_success(uint16_t port, const std::string& secret) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == INVALID_SOCKET) return false;
  DWORD timeout_ms = 100;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms,
             sizeof(timeout_ms));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_ms,
             sizeof(timeout_ms));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bool ok = false;
  if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
    std::string req = "GET /configs HTTP/1.1\r\nHost: 127.0.0.1\r\n";
    if (!secret.empty()) req += "Authorization: Bearer " + secret + "\r\n";
    req += "Connection: close\r\n\r\n";
    if (send(s, req.c_str(), (int)req.size(), 0) > 0) {
      char buf[256] = {};
      int n = recv(s, buf, sizeof(buf) - 1, 0);
      if (n > 0) {
        buf[n] = 0;
        if (strncmp(buf, "HTTP/1.", 7) == 0) {
          const char* sp = strchr(buf, ' ');
          if (sp && sp + 1 < buf + n && sp[1] == '2') ok = true;
        }
      }
    }
  }
  closesocket(s);
  return ok;
}

std::once_flag g_ws_init;

void ensure_winsock() {
  std::call_once(g_ws_init, [] {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
  });
}

}  // namespace

std::wstring clash_path() { return util::dirs::clash_path(); }

}  // namespace clashwin::config::clash

// ---------- IClashTemp 成员定义（clashwin::config） ----------
namespace clashwin::config {

// ---------- guard 系列 ----------
uint16_t IClashTemp::guard_mixed_port(const YAML::Node& map) {
  return clash::guard_port_common(map, "mixed-port", consts::kDefaultMixed);
}
uint16_t IClashTemp::guard_socks_port(const YAML::Node& map) {
  return clash::guard_port_common(map, "socks-port", consts::kDefaultSocks);
}
uint16_t IClashTemp::guard_port(const YAML::Node& map) {
  // ← clash.rs:283：http port 兜底是 7899（非 10803）
  return clash::guard_port_common(map, "port", consts::kHttpGuardFallback);
}

std::string IClashTemp::guard_server_ctrl(const YAML::Node& map) {
  const YAML::Node n = map["external-controller"];
  if (n && n.IsScalar() && !n.IsNull()) {
    std::string s = util::trim(n.Scalar());
    if (!s.empty()) {
      if (s[0] == ':') s = "127.0.0.1" + s;
      clash::SockAddrParsed p = clash::parse_sockaddr(s);
      if (p.ok) {
        // Rust SocketAddr Display：v6 输出 "[::]:port"
        if (p.ip.find(':') != std::string::npos)
          return "[" + p.ip + "]:" + std::to_string(p.port);
        return p.ip + ":" + std::to_string(p.port);
      }
      return consts::kDefaultExternalController;
    }
  }
  return consts::kDefaultExternalController;
}

std::string IClashTemp::guard_external_controller(const YAML::Node& map) {
  return guard_server_ctrl(map);
}

std::string IClashTemp::guard_client_ctrl(const YAML::Node& map) {
  std::string value = guard_server_ctrl(map);
  clash::SockAddrParsed p = clash::parse_sockaddr(value);
  if (!p.ok) return consts::kDefaultExternalController;
  // ← Rust set_ip(127.0.0.1)：v4/v6 unspecified 一律改写为 127.0.0.1
  if (p.unspecified) return "127.0.0.1:" + std::to_string(p.port);
  return value;
}

std::string IClashTemp::guard_external_controller_ipc() {
  // 总是使用当前 IPC 路径，保证配置文件与运行时路径一致（clash.rs:343-352）
  return utf::utf16_to_utf8(util::dirs::ipc_path());
}

void IClashTemp::apply_guard() {
  serde::set_u16(mapping_, "mixed-port", guard_mixed_port(mapping_));
  serde::set_u16(mapping_, "socks-port", guard_socks_port(mapping_));
  serde::set_u16(mapping_, "port", guard_port(mapping_));
  mapping_["external-controller"] = guard_external_controller(mapping_);
  mapping_["external-controller-pipe"] = guard_external_controller_ipc();
}

IClashTemp::ClashInfo IClashTemp::get_client_info() const {
  ClashInfo info;
  info.mixed_port = guard_mixed_port(mapping_);
  info.socks_port = guard_socks_port(mapping_);
  info.port = guard_port(mapping_);
  info.server = guard_client_ctrl(mapping_);
  // secret 允许 bool/number 形态（clash.rs:229-234 的 to_string 容错）
  const YAML::Node n = mapping_["secret"];
  if (n && n.IsScalar() && !n.IsNull()) {
    info.secret = n.Scalar();
  }
  info.ipv6 = serde::opt_bool(mapping_, "ipv6").value_or(false);
  info.allow_lan = serde::opt_bool(mapping_, "allow-lan").value_or(false);
  return info;
}

// ---------- patch / save ----------
void IClashTemp::patch_config(const YAML::Node& patch) {
  if (!patch.IsMap()) return;
  for (const auto& kv : patch) {
    if (!kv.first.IsScalar()) continue;
    // 坑（yaml_utils.h 注释）：跨文档合并必须用字符串键
    mapping_[kv.first.Scalar()] = kv.second;
  }
}

Result<void> IClashTemp::save_file() const { return clash::save_clash(*this); }

// ---------- 端口冲突探测与自动寻位 ----------
bool IClashTemp::is_port_conflict(uint16_t port, const std::string& secret) {
  clash::ensure_winsock();
  SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == INVALID_SOCKET) return true;  // 无法探测按冲突处理（保守）
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int ret = bind(s, (sockaddr*)&addr, sizeof(addr));
  closesocket(s);
  if (ret == 0) return false;  // 绑定成功 → 空闲 → 非冲突
  // 绑定失败：探测是否为我们自己的内核（有成功响应 → 非冲突）
  return !clash::http_probe_success(port, secret);
}

uint16_t IClashTemp::find_free_controller_port(uint16_t start_port) {
  clash::ensure_winsock();
  uint16_t port = start_port;
  while (port < 65535) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s != INVALID_SOCKET) {
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);
      addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      int ret = bind(s, (sockaddr*)&addr, sizeof(addr));
      closesocket(s);
      if (ret == 0) return port;
    }
    ++port;
  }
  return start_port;
}

void IClashTemp::resolve_controller_conflict() {
  // ← clash.rs:85-101：仅当 external-controller 为可解析字符串时检查
  const YAML::Node n = mapping_["external-controller"];
  if (!n || !n.IsScalar() || n.IsNull()) return;
  std::string s = util::trim(n.Scalar());
  if (s.empty()) return;
  if (s[0] == ':') s = "127.0.0.1" + s;
  clash::SockAddrParsed p = clash::parse_sockaddr(s);
  if (!p.ok) return;
  std::string secret = consts::kDefaultSecret;
  const YAML::Node sn = mapping_["secret"];
  if (sn && sn.IsScalar() && !sn.IsNull()) secret = sn.Scalar();
  if (is_port_conflict(p.port, secret)) {
    uint16_t free_port = find_free_controller_port(consts::kControllerPort);
    mapping_["external-controller"] = "127.0.0.1:" + std::to_string(free_port);
  }
}

// ---------- 模板（← clash.rs::template() 插入序逐键照抄） ----------
IClashTemp IClashTemp::make_template() {
  YAML::Node map(YAML::NodeType::Map);
  serde::set_u32(map, "config_version", consts::kCurrentClashVersion);

  YAML::Node tun(YAML::NodeType::Map);
  tun["enable"] = "false";
  tun["stack"] = consts::kDefaultStack;
  tun["auto-route"] = "true";
  tun["strict-route"] = "false";
  tun["auto-detect-interface"] = "true";
  YAML::Node hijack(YAML::NodeType::Sequence);
  hijack.push_back("any:53");
  hijack.push_back("tcp://any:53");
  tun["dns-hijack"] = hijack;

  serde::set_u16(map, "mixed-port", consts::kDefaultMixed);
  serde::set_u16(map, "socks-port", consts::kDefaultSocks);
  serde::set_u16(map, "port", consts::kDefaultHttp);
  map["log-level"] = "warning";
  // 对齐内核默认：allow-lan / ipv6 出厂关闭（clash.rs:133-136）
  map["allow-lan"] = "false";
  map["ipv6"] = "false";
  map["mode"] = "rule";
  map["tcp-concurrent"] = "false";

  YAML::Node dns(YAML::NodeType::Map);
  dns["enable"] = "true";
  serde::set_i64(dns, "cache-size", 512);
  map["dns"] = dns;

  YAML::Node profile(YAML::NodeType::Map);
  profile["store-selected"] = "true";
  profile["store-fake-ip"] = "false";
  map["profile"] = profile;

  map["external-controller"] = consts::kDefaultExternalController;
  map["external-controller-pipe"] = guard_external_controller_ipc();
  map["tun"] = tun;

  YAML::Node cors(YAML::NodeType::Map);
  cors["allow-private-network"] = "true";
  YAML::Node origins(YAML::NodeType::Sequence);
  origins.push_back("tauri://localhost");
  origins.push_back("http://tauri.localhost");
  origins.push_back("https://yacd.metacubex.one");
  origins.push_back("https://metacubex.github.io");
  origins.push_back("https://board.zash.run.place");
  cors["allow-origins"] = origins;
  map["secret"] = consts::kDefaultSecret;
  map["external-controller-cors"] = cors;
  map["unified-delay"] = "true";
  return IClashTemp(map);
}

// ---------- load（顺序关键：版本检测必须先于模板合并） ----------
IClashTemp IClashTemp::load() {
  auto node = serde::read_mapping_file(clash::clash_path());
  if (!node.is_ok()) {
    CWLOG_ERROR("failed to load config.yaml: " + node.error().message);
    return make_template();
  }
  YAML::Node map = node.value();
  const YAML::Node& cmap = map;

  // 【顺序关键·勿改】在模板合并之前检测版本：模板自带
  // config_version=当前版本，若先合并再检测，存量配置的起始版本会被污染，
  // v1→v2 迁移（tun.stack 归一化）将永远不会触发（clash.rs:29-33 回归注释）
  uint32_t from_version = migrate::detect_version(map);

  // 模板合并：仅补缺失键（clash.rs:36-40；const 视角查找避免插入空键）
  YAML::Node tmpl = make_template().mapping();
  for (const auto& kv : tmpl) {
    if (!kv.first.IsScalar()) continue;
    const std::string key = kv.first.Scalar();
    if (!cmap[key]) map[key] = kv.second;
  }

  // 备份成功才迁移；迁移后立即落盘（clash.rs:42-66）
  std::wstring path = clash::clash_path();
  if (from_version < migrate::current_clash_version()) {
    auto backup = migrate::backup_before_migration(path, from_version);
    if (!backup) {
      CWLOG_WARN("clash.yaml migration backup failed, skip migration this run");
    } else {
      uint32_t migrated = migrate::migrate_clash(map, from_version);
      serde::set_u32(map, "config_version", migrated);
      if (migrated != from_version) {
        auto sv = serde::save_yaml_text(path, serde::dump(map),
                                        "# Clash Verge Config");
        if (!sv.is_ok())
          CWLOG_WARN("clash.yaml migration save failed: " + sv.error().message);
      }
    }
  }

  // secret 修正：空 / "set-your-secret" → 默认 secret（clash.rs:68-75）
  {
    YAML::Node sn = cmap["secret"];
    if (sn && sn.IsScalar() && !sn.IsNull()) {
      std::string s = sn.Scalar();
      if (s.empty() || s == "set-your-secret")
        map["secret"] = consts::kDefaultSecret;
    }
  }

  IClashTemp out(map);
  // controller 冲突自动寻位（clash.rs:85-101），随后全量 guard
  out.resolve_controller_conflict();
  out.apply_guard();
  return out;
}

}  // namespace clashwin::config

// ---------- clash 命名空间自由函数 ----------
namespace clashwin::config::clash {

IClashTemp make_template() { return IClashTemp::make_template(); }

std::string dump_clash(const IClashTemp& c) { return serde::dump(c.mapping()); }

Result<void> save_clash(const IClashTemp& c) {
  return serde::save_yaml_text(clash_path(), dump_clash(c),
                               "# Generated by Clash Verge");
}

IClashTemp load() { return IClashTemp::load(); }

}  // namespace clashwin::config::clash
