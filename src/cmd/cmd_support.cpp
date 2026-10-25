// T09 命令层共享辅助实现
#include "cmd/cmd_support.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <iphlpapi.h>

#include "config/config.h"
#include "core/validate.h"
#include "enhance/enhance.h"
#include "system/net_http.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#pragma comment(lib, "iphlpapi.lib")

namespace fs = std::filesystem;

namespace clashwin::cmd {

// ---------- 参数取值 ----------
const Json* find_arg(const Json& args, std::initializer_list<const char*> keys) {
  if (!args.is_object()) return nullptr;
  for (const char* k : keys) {
    auto it = args.find(k);
    if (it != args.end() && !it->is_null()) return &*it;
  }
  return nullptr;
}

// ---------- JSON ↔ YAML ----------
Json yaml_to_json(const YAML::Node& node) {
  if (!node.IsDefined() || node.IsNull()) return nullptr;
  if (node.IsScalar()) {
    // 与 serde_json 口径对齐：bool/int/float/null 原样；其余落字符串
    const std::string& s = node.Scalar();
    if (s == "true") return true;
    if (s == "false") return false;
    if (s == "null" || s == "~" || s.empty()) return nullptr;
    try {
      size_t pos = 0;
      long long i = std::stoll(s, &pos);
      if (pos == s.size()) return static_cast<int64_t>(i);
    } catch (...) {
    }
    try {
      size_t pos = 0;
      double d = std::stod(s, &pos);
      if (pos == s.size()) return d;
    } catch (...) {
    }
    return s;
  }
  if (node.IsMap()) {
    Json o = Json::object();
    for (const auto& kv : node) {
      o[kv.first.Scalar()] = yaml_to_json(kv.second);
    }
    return o;
  }
  Json a = Json::array();
  for (const auto& item : node) a.push_back(yaml_to_json(item));
  return a;
}

YAML::Node json_to_yaml(const Json& j) {
  YAML::Node n;
  switch (j.type()) {
    case Json::value_t::null:
    default:
      n = YAML::Node(YAML::NodeType::Null);
      break;
    case Json::value_t::boolean:
      n = static_cast<bool>(j.get<bool>());
      break;
    case Json::value_t::number_integer:
    case Json::value_t::number_unsigned:
      n = static_cast<long long>(j.get<int64_t>());
      break;
    case Json::value_t::number_float:
      n = j.get<double>();
      break;
    case Json::value_t::string:
      n = j.get<std::string>();
      break;
    case Json::value_t::object:
      for (auto it = j.begin(); it != j.end(); ++it) n[it.key()] = json_to_yaml(it.value());
      break;
    case Json::value_t::array:
      for (const auto& item : j) n.push_back(json_to_yaml(item));
      break;
  }
  return n;
}

// ---------- IVerge 补丁解码（逐字段 is_some 才覆盖，键名与 serde 布尔一致） ----------
namespace {
template <typename T>
void take(const Json& j, const char* key, std::optional<T>& dst) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null()) return;
  if constexpr (std::is_same_v<T, std::string>) {
    if (it->is_string()) dst = it->get<std::string>();
  } else if constexpr (std::is_same_v<T, bool>) {
    if (it->is_boolean()) dst = it->get<bool>();
  } else if constexpr (std::is_integral_v<T>) {
    if (it->is_number()) dst = static_cast<T>(it->get<double>());
  }
}
}  // namespace

IVerge iverge_from_json(const Json& j) {
  IVerge v;  // 全 optional 空 → patch_config 只覆盖出现的键
  take(j, "config_version", v.config_version);
  take(j, "app_log_level", v.app_log_level);
  take(j, "app_log_max_size", v.app_log_max_size);
  take(j, "app_log_max_count", v.app_log_max_count);
  take(j, "language", v.language);
  take(j, "theme_mode", v.theme_mode);
  take(j, "env_type", v.env_type);
  take(j, "start_page", v.start_page);
  take(j, "startup_script", v.startup_script);
  take(j, "traffic_graph", v.traffic_graph);
  take(j, "enable_memory_usage", v.enable_memory_usage);
  take(j, "enable_group_icon", v.enable_group_icon);
  take(j, "pause_render_traffic_stats_on_blur", v.pause_render_traffic_stats_on_blur);
  take(j, "menu_icon", v.menu_icon);
  take(j, "collapse_navbar", v.collapse_navbar);
  take(j, "enable_tun_mode", v.enable_tun_mode);
  take(j, "enable_auto_launch", v.enable_auto_launch);
  take(j, "enable_silent_start", v.enable_silent_start);
  take(j, "enable_system_proxy", v.enable_system_proxy);
  take(j, "enable_proxy_guard", v.enable_proxy_guard);
  take(j, "enable_bypass_check", v.enable_bypass_check);
  take(j, "use_default_bypass", v.use_default_bypass);
  take(j, "system_proxy_bypass", v.system_proxy_bypass);
  take(j, "proxy_guard_duration", v.proxy_guard_duration);
  take(j, "proxy_auto_config", v.proxy_auto_config);
  take(j, "pac_file_content", v.pac_file_content);
  take(j, "proxy_host", v.proxy_host);
  take(j, "web_ui_list", v.web_ui_list);
  take(j, "clash_core", v.clash_core);
  take(j, "enable_always_on_top", v.enable_always_on_top);
  take(j, "auto_close_connection", v.auto_close_connection);
  take(j, "auto_check_update", v.auto_check_update);
  take(j, "default_latency_test", v.default_latency_test);
  take(j, "enable_auto_delay_detection", v.enable_auto_delay_detection);
  take(j, "enable_builtin_enhanced", v.enable_builtin_enhanced);
  take(j, "proxy_layout_column", v.proxy_layout_column);
  take(j, "auto_log_clean", v.auto_log_clean);
  take(j, "verge_mixed_port", v.verge_mixed_port);
  take(j, "verge_socks_port", v.verge_socks_port);
  take(j, "verge_socks_enabled", v.verge_socks_enabled);
  take(j, "verge_port", v.verge_port);
  take(j, "verge_http_enabled", v.verge_http_enabled);
  take(j, "enable_auto_light_weight_mode", v.enable_auto_light_weight_mode);
  take(j, "auto_light_weight_minutes", v.auto_light_weight_minutes);
  take(j, "enable_external_controller", v.enable_external_controller);
  take(j, "rule_fallback", v.rule_fallback);
  if (auto it = j.find("theme_setting"); it != j.end() && it->is_object()) {
    IVergeTheme t;
    take(*it, "primary_color", t.primary_color);
    take(*it, "secondary_color", t.secondary_color);
    take(*it, "primary_text", t.primary_text);
    take(*it, "secondary_text", t.secondary_text);
    take(*it, "info_color", t.info_color);
    take(*it, "error_color", t.error_color);
    take(*it, "warning_color", t.warning_color);
    take(*it, "success_color", t.success_color);
    take(*it, "font_family", t.font_family);
    v.theme_setting = t;
  }
  return v;
}

IProfiles iprofiles_patch_from_json(const Json& j) {
  IProfiles p;
  if (auto it = j.find("current"); it != j.end() && it->is_string()) {
    p.current = it->get<std::string>();
  }
  return p;
}

PrfOption prf_option_from_json(const Json& j) {
  PrfOption o;
  take(j, "user_agent", o.user_agent);
  take(j, "with_proxy", o.with_proxy);
  take(j, "self_proxy", o.self_proxy);
  take(j, "update_interval", o.update_interval);
  take(j, "timeout_seconds", o.timeout_seconds);
  take(j, "danger_accept_invalid_certs", o.danger_accept_invalid_certs);
  take(j, "allow_auto_update", o.allow_auto_update);
  take(j, "merge", o.merge);
  take(j, "script", o.script);
  take(j, "rules", o.rules);
  take(j, "proxies", o.proxies);
  take(j, "groups", o.groups);
  return o;
}

PrfItem prfitem_from_json(const Json& j) {
  PrfItem it;
  take(j, "uid", it.uid);
  take(j, "type", it.itype);
  take(j, "name", it.name);
  take(j, "file", it.file);
  take(j, "desc", it.desc);
  take(j, "url", it.url);
  take(j, "home", it.home);
  take(j, "updated", it.updated);
  if (auto sel = j.find("selected"); sel != j.end() && sel->is_array()) {
    std::vector<PrfSelected> vec;
    for (const auto& s : *sel) {
      PrfSelected one;
      take(s, "name", one.name);
      take(s, "now", one.now);
      vec.push_back(std::move(one));
    }
    it.selected = std::move(vec);
  }
  if (auto ex = j.find("extra"); ex != j.end() && ex->is_object()) {
    PrfExtra e;
    auto num = [&](const char* k) -> uint64_t {
      auto f = ex->find(k);
      return f != ex->end() && f->is_number() ? static_cast<uint64_t>(f->get<double>()) : 0;
    };
    e.upload = num("upload");
    e.download = num("download");
    e.total = num("total");
    e.expire = num("expire");
    it.extra = e;
  }
  if (auto op = j.find("option"); op != j.end() && op->is_object()) {
    it.option = prf_option_from_json(*op);
  }
  return it;
}

// ---------- ValidationOutcome ↔ JSON ----------
Json validation_outcome_to_json(const core::validate::ValidationOutcome& o) {
  using VS = core::validate::ValidationStatus;
  switch (o.status) {
    case VS::Valid:
      return Json{{"status", "valid"}};
    case VS::Busy:
      return Json{{"status", "busy"}};
    case VS::Skipped:
      return Json{{"status", "skipped"},
                  {"reason", o.reason == core::validate::ValidationSkipReason::Exiting
                                 ? "exiting"
                                 : "debounced"}};
    case VS::Invalid:
      return Json{{"status", "invalid"},
                  {"kind", core::validate::to_string(o.kind)},
                  {"message", o.message}};
  }
  return Json{{"status", "invalid"}, {"message", "unknown"}};
}

// ---------- mihomo 客户端 ----------
namespace {
std::unique_ptr<mihomo::MihomoClient>& mihomo_singleton() {
  static std::unique_ptr<mihomo::MihomoClient> client = [] {
    std::string secret;
    try {
      auto info = config::Config::instance().clash().latest_arc()->mapping();
      if (info["secret"] && info["secret"].IsScalar()) secret = info["secret"].Scalar();
    } catch (...) {
    }
    return std::make_unique<mihomo::MihomoClient>(util::dirs::ipc_path(), secret);
  }();
  return client;
}
}  // namespace

mihomo::MihomoClient& mihomo_unchecked() { return *mihomo_singleton(); }

Result<mihomo::MihomoClient*> mihomo() {
  if (!is_core_running()) {
    return Result<mihomo::MihomoClient*>::err({errs::kCoreDown, "core is not running"});
  }
  return Result<mihomo::MihomoClient*>::ok(mihomo_singleton().get());
}

bool is_core_running() { return core::CoreManager::instance().running_mode() != core::RunningMode::NotRunning; }

core::RunningMode running_mode() { return core::CoreManager::instance().running_mode(); }

bool is_admin() {
  // ← utils/sysinfo.rs app_is_admin（管理员组成员判定，IsUserAnAdmin 等价）
  BOOL is_member = FALSE;
  PSID admin_group = nullptr;
  SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
  if (::AllocateAndInitializeSid(&nt_authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &admin_group)) {
    ::CheckTokenMembership(nullptr, admin_group, &is_member);
    ::FreeSid(admin_group);
  }
  return is_member != FALSE;
}

// ---------- enhance 全链 ----------
core::validate::ValidationOutcome run_enhance_pipeline() {
  using core::validate::ValidationOutcome;
  // ① 生成运行时产物（enhance 流水线）
  auto runtime = enhance::run_from_config();
  if (runtime.is_err()) {
    return ValidationOutcome::invalid_from_message(runtime.error().message);
  }
  // ② 落盘 clash-mini.yaml（← Config::generate_file(Run)）
  std::ostringstream oss;
  oss << runtime.value();
  std::string yaml_text = oss.str();
  auto run_path = util::dirs::clash_mini_path();
  {
    std::error_code ec;
    fs::create_directories(run_path.substr(0, run_path.find_last_of(L'\\')), ec);
    std::ofstream f(run_path, std::ios::binary);
    if (!f.is_open()) {
      return ValidationOutcome::invalid(core::validate::ValidationErrorKind::FileRead,
                                        "Failed to write clash-mini.yaml");
    }
    f << yaml_text;
  }
  // ③ check 副本 + `-t` 校验（← CoreConfigValidator::validate_config_outcome）
  auto check = core::validate::make_check_copy(run_path);
  if (check.is_err()) {
    return ValidationOutcome::invalid(core::validate::ValidationErrorKind::FileRead,
                                      check.error().message);
  }
  core::validate::ValidateOptions opt;
  opt.core_path = core::locate_core_path();
  opt.app_dir = util::dirs::app_home_dir();
  if (opt.core_path.empty()) {
    // 无内核可校验 → 视为通过（纯配置面操作；实机恒有内核）
    return ValidationOutcome{};
  }
  auto outcome = core::validate::validate_config_file(opt, utf::utf16_to_utf8(check.value()));
  if (outcome.is_err()) {
    return ValidationOutcome::invalid_from_message(outcome.error().message);
  }
  return outcome.value();
}

// ---------- 测速 ----------
Result<uint32_t> test_delay_impl(const std::string& url) {
  // ← feat/clash.rs::test_delay：解析 URL → TCP（可选经 mixed-port CONNECT）→
  //   https 完成握手 / http 发 HEAD 收响应；整体 2000ms 超时；失败/超时由上层折算
  constexpr uint32_t kNodeDelayMaxMs = 2000;  // ← constants.rs NODE_DELAY_MAX_MS
  auto parts = system::parse_http_url(url);
  if (parts.is_err()) return Result<uint32_t>::err({errs::kBadArgs, "invalid URL"});
  const auto& u = parts.value();
  bool is_https = u.scheme == "https";
  uint16_t port = u.port_str.empty() ? (is_https ? 443 : 80)
                                     : static_cast<uint16_t>(std::stoi(u.port_str));

  bool proxy_enabled = false;
  uint16_t proxy_port = 0;
  try {
    const auto& verge = *config::Config::instance().verge().latest_arc();
    proxy_enabled = verge.enable_system_proxy.value_or(false) || verge.enable_tun_mode.value_or(false);
    if (proxy_enabled) {
      proxy_port = verge.verge_mixed_port ? *verge.verge_mixed_port
                                          : config::Config::instance().clash().latest_arc()->get_mixed_port();
    }
  } catch (...) {
  }

  auto tick = [] {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  };
  int64_t start = tick();
  auto elapsed = [&] { return static_cast<uint32_t>(tick() - start); };

  static bool wsa_ok = [] {
    WSADATA w{};
    return WSAStartup(MAKEWORD(2, 2), &w) == 0;
  }();
  (void)wsa_ok;

  auto connect_host = [&](const std::string& host, uint16_t p) -> Result<SOCKET> {
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return Result<SOCKET>::err({errs::kIo, "socket failed"});
    // 非阻塞 + select 实现连接超时（2000ms 预算内）
    u_long nonblock = 1;
    ::ioctlsocket(s, FIONBIO, &nonblock);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(p);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
      // 域名：同步解析（原版为 rustls/系统解析，此处简化留 T16 校准）
      addrinfo hint{};
      hint.ai_family = AF_INET;
      hint.ai_socktype = SOCK_STREAM;
      addrinfo* res = nullptr;
      if (::getaddrinfo(host.c_str(), nullptr, &hint, &res) != 0 || !res) {
        ::closesocket(s);
        return Result<SOCKET>::err({errs::kIo, "resolve failed: " + host});
      }
      addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
      ::freeaddrinfo(res);
    }
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
      int wsa_err = WSAGetLastError();
      if (wsa_err != WSAEWOULDBLOCK) {
        // 拒绝/不可达等立即失败（原版连接错误 → 10000）
        ::closesocket(s);
        return Result<SOCKET>::err({errs::kIo, "connect refused (" +
                                                    std::to_string(wsa_err) + ")"});
      }
      fd_set wset, eset;
      FD_ZERO(&wset);
      FD_SET(s, &wset);
      FD_ZERO(&eset);
      FD_SET(s, &eset);
      timeval tv{0, (long)(kNodeDelayMaxMs * 1000)};
      int ready = ::select(0, nullptr, &wset, &eset, &tv);
      if (ready <= 0) {
        ::closesocket(s);
        return Result<SOCKET>::err({errs::kIo, "connect timeout"});
      }
      if (FD_ISSET(s, &eset)) {
        ::closesocket(s);
        return Result<SOCKET>::err({errs::kIo, "connect refused (except)"});
      }
      int so_err = 0;
      int len = sizeof(so_err);
      if (::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_err), &len) != 0 ||
          so_err != 0) {
        ::closesocket(s);
        return Result<SOCKET>::err({errs::kIo, "connect refused (so_err " +
                                                    std::to_string(so_err) + ")"});
      }
    }
    return Result<SOCKET>::ok(s);
  };

  auto send_all = [&](SOCKET s, const std::string& data) -> bool {
    return ::send(s, data.c_str(), static_cast<int>(data.size()), 0) != SOCKET_ERROR;
  };
  auto read_some = [&](SOCKET s, char* buf, int len) -> int {
    return ::recv(s, buf, len, 0);
  };

  SOCKET sock = INVALID_SOCKET;
  if (proxy_enabled && proxy_port != 0) {
    auto c = connect_host("127.0.0.1", proxy_port);
    if (c.is_err()) return Result<uint32_t>::err(c.error());
    sock = c.value();
    if (is_https) {
      std::string req = "CONNECT " + u.host + ":" + std::to_string(port) +
                        " HTTP/1.1\r\nHost: " + u.host + ":" + std::to_string(port) + "\r\n\r\n";
      if (!send_all(sock, req)) {
        ::closesocket(sock);
        return Result<uint32_t>::err({errs::kIo, "CONNECT send failed"});
      }
      char buf[1024];
      int n = read_some(sock, buf, sizeof(buf) - 1);
      if (n <= 0 ||
          std::string(buf, buf + n).find("200") == std::string::npos) {
        ::closesocket(sock);
        return Result<uint32_t>::err({errs::kIo, "Proxy CONNECT failed"});
      }
    }
  } else {
    auto c = connect_host(u.host, port);
    if (c.is_err()) return Result<uint32_t>::err(c.error());
    sock = c.value();
  }

  if (is_https) {
    // TLS 握手段未计（对照差异见覆盖表）；TCP+CONNECT 已是时延主体
    ::closesocket(sock);
    return Result<uint32_t>::ok(std::max(elapsed(), 1u));
  }
  std::string req = "HEAD " + (proxy_enabled ? u.request_target() : "/") +
                    " HTTP/1.1\r\nHost: " + u.host +
                    "\r\nConnection: close\r\n\r\n";
  if (!send_all(sock, req)) {
    ::closesocket(sock);
    return Result<uint32_t>::err({errs::kIo, "send failed"});
  }
  char buf[1024];
  read_some(sock, buf, sizeof(buf));
  ::closesocket(sock);
  if (elapsed() > kNodeDelayMaxMs) return Result<uint32_t>::err({errs::kIo, "timeout"});
  return Result<uint32_t>::ok(std::max(elapsed(), 1u));
}

// ---------- 剪贴板 ----------
bool copy_text_to_clipboard(const std::string& text) {
  auto w = utf::utf8_to_utf16(text);
  if (!OpenClipboard(nullptr)) return false;
  EmptyClipboard();
  size_t bytes = (w.size() + 1) * sizeof(wchar_t);
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!h) {
    CloseClipboard();
    return false;
  }
  memcpy(GlobalLock(h), w.c_str(), bytes);
  GlobalUnlock(h);
  bool ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
  CloseClipboard();
  return ok;
}

// ---------- 系统打开 ----------
Result<void> shell_open(const std::wstring& target) {
  HINSTANCE r = ::ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<intptr_t>(r) <= 32) {
    return Result<void>::err({errs::kIo, "ShellExecuteW failed"});
  }
  return Result<void>::ok();
}

Result<void> show_in_folder(const std::wstring& path) {
  std::wstring cmd = L"/select,\"" + path + L"\"";
  HINSTANCE r = ::ShellExecuteW(nullptr, L"open", L"explorer.exe", cmd.c_str(), nullptr,
                                SW_SHOWNORMAL);
  if (reinterpret_cast<intptr_t>(r) <= 32) {
    return Result<void>::err({errs::kIo, "explorer /select failed"});
  }
  return Result<void>::ok();
}

Result<void> open_url_checked(const std::string& url) {
  auto parts = system::parse_http_url(url);
  if (parts.is_err() || (parts.value().scheme != "http" && parts.value().scheme != "https")) {
    return Result<void>::err({errs::kBadArgs, "only http/https URLs are allowed"});
  }
  return shell_open(utf::utf8_to_utf16(url));
}

// ---------- 注册表自启状态 ----------
Result<bool> get_auto_launch_status_impl() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ,
                    &key) != ERROR_SUCCESS) {
    return Result<bool>::ok(false);
  }
  DWORD type = 0, size = 0;
  LONG rc = RegQueryValueExW(key, L"Clash Mini", nullptr, &type, nullptr, &size);
  RegCloseKey(key);
  return Result<bool>::ok(rc == ERROR_SUCCESS);
}

}  // namespace clashwin::cmd
