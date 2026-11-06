// T18 系统代理实现（← core/sysopt.rs 逐语义 + census §124 注册表口径）。
#include "system/sysproxy.h"

#include <windows.h>
#include <wininet.h>
#include <ras.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "system/guard_monitor.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "rasapi32.lib")

namespace utf = clashwin::utf;

namespace clashwin::system {

// ← sysopt.rs:54 逐字符（分号分隔，含 <local> 结尾）
const wchar_t kDefaultBypass[] =
    L"localhost;127.*;192.168.*;10.*;172.16.*;172.17.*;172.18.*;172.19.*;"
    L"172.20.*;172.21.*;172.22.*;172.23.*;172.24.*;172.25.*;172.26.*;172.27.*;"
    L"172.28.*;172.29.*;172.30.*;172.31.*;<local>";

std::wstring build_bypass(bool use_default,
                          const std::string& custom_bypass_utf8) {
  std::wstring custom = utf::utf8_to_utf16(custom_bypass_utf8);
  if (custom.empty()) return kDefaultBypass;
  if (!use_default) return custom;
  // ← format!("{DEFAULT_BYPASS},{custom}")：默认串在前，逗号拼接
  return std::wstring(kDefaultBypass) + L"," + custom;
}

std::vector<ApplyStep> proxy_apply_steps(bool sys_enabled,
                                         bool auto_enabled) {
  // ← sysopt.rs:26-34：禁 PAC 会清 WinINET 代理标志，纯全局模式必须先清 PAC
  if (sys_enabled && !auto_enabled) {
    return {ApplyStep::Autoproxy, ApplyStep::Sysproxy};
  }
  return {ApplyStep::Sysproxy, ApplyStep::Autoproxy};
}

// ---------- Sysopt ----------
Sysopt& Sysopt::instance() {
  static Sysopt s;
  return s;
}

void Sysopt::wait_idle() {
  // ← wait_idle：拿一次锁即等 update 完成
  std::lock_guard<std::mutex> lock(update_lock_);
}

// ---------- 快照默认实现（mock 走此路径，无需磁盘） ----------
Result<SysProxySnapshot> ISysProxyAccess::read_snapshot() {
  auto sp = read_sysproxy();
  if (sp.is_err()) return Result<SysProxySnapshot>::err(sp.error());
  auto ap = read_autoproxy();
  if (ap.is_err()) return Result<SysProxySnapshot>::err(ap.error());
  SysProxySnapshot s;
  s.valid = true;
  s.enable = sp.value().enable;
  s.server = sp.value().host +
             (sp.value().port ? L":" + std::to_wstring(sp.value().port) : L"");
  s.bypass = sp.value().bypass;
  s.auto_config_url = ap.value().url;
  s.auto_config_present = !ap.value().url.empty();
  return Result<SysProxySnapshot>::ok(s);
}

Result<void> ISysProxyAccess::write_snapshot(const SysProxySnapshot& s) {
  SysProxyState sp;
  sp.enable = s.enable;
  sp.bypass = s.bypass;
  size_t colon = s.server.rfind(L':');
  if (colon != std::wstring::npos) {
    sp.host = s.server.substr(0, colon);
    sp.port = static_cast<uint16_t>(_wtoi(s.server.substr(colon + 1).c_str()));
  } else {
    sp.host = s.server;
  }
  auto r = write_sysproxy(sp);
  if (r.is_err()) return r;
  AutoProxyState ap;
  ap.url = s.auto_config_url;
  ap.enable = !ap.url.empty();
  return write_autoproxy(ap);
}

namespace {
std::wstring backup_file_path() {
  return util::dirs::app_home_dir() + L"\\sysproxy_backup.json";
}
}  // namespace

bool Sysopt::load_backup_file() {
  if (!access_ || !access_->persistent()) return false;
  std::ifstream in(backup_file_path());
  if (!in) return false;
  try {
    nlohmann::json j;
    in >> j;
    SysProxySnapshot s;
    s.valid = j.value("valid", false);
    if (!s.valid) return false;
    s.enable = j.value("enable", false);
    s.server = utf::utf8_to_utf16(j.value("server", std::string()));
    s.bypass = utf::utf8_to_utf16(j.value("bypass", std::string()));
    s.auto_config_url = utf::utf8_to_utf16(j.value("auto_config_url", std::string()));
    s.auto_detect = j.value("auto_detect", false);
    s.proxy_server_present = j.value("proxy_server_present", true);
    s.proxy_override_present = j.value("proxy_override_present", true);
    s.auto_config_present = j.value("auto_config_present", false);
    s.auto_detect_present = j.value("auto_detect_present", false);
    backup_ = s;
    have_backup_ = true;
    CWLOG_INFO("sysproxy: loaded pre-existing backup: " +
               utf::utf16_to_utf8(backup_file_path()));
    return true;
  } catch (...) {
    return false;
  }
}

void Sysopt::save_backup_file() const {
  nlohmann::json j;
  j["valid"] = backup_.valid;
  j["enable"] = backup_.enable;
  j["server"] = utf::utf16_to_utf8(backup_.server);
  j["bypass"] = utf::utf16_to_utf8(backup_.bypass);
  j["auto_config_url"] = utf::utf16_to_utf8(backup_.auto_config_url);
  j["auto_detect"] = backup_.auto_detect;
  j["proxy_server_present"] = backup_.proxy_server_present;
  j["proxy_override_present"] = backup_.proxy_override_present;
  j["auto_config_present"] = backup_.auto_config_present;
  j["auto_detect_present"] = backup_.auto_detect_present;
  std::ofstream out(backup_file_path(), std::ios::trunc);
  if (!out) {
    CWLOG_WARN("sysproxy: failed to persist backup file");
    return;
  }
  out << j.dump(2);
  CWLOG_INFO("sysproxy: backup persisted: " + utf::utf16_to_utf8(backup_file_path()));
}

void Sysopt::clear_backup_file() const {
  std::error_code ec;
  std::filesystem::remove(backup_file_path(), ec);
}

void Sysopt::ensure_backup_locked() {
  if (have_backup_) return;
  if (load_backup_file()) return;
  auto r = access_->read_snapshot();
  if (r.is_err() || !r.value().valid) {
    CWLOG_WARN("sysproxy: snapshot current OS proxy failed (no restore point)");
    return;
  }
  backup_ = r.value();
  have_backup_ = true;
  if (access_->persistent()) save_backup_file();
  CWLOG_INFO("sysproxy: snapshot saved: server=[" + utf::utf16_to_utf8(backup_.server) +
             "] enable=" + (backup_.enable ? "1" : "0"));
}

Result<void> Sysopt::restore_backup_locked() {
  // ← 先停 guard（避免旧值被守护线程补写回注册表）
  SysproxyGuard::instance().set_guard_type(GuardType::None);
  auto r = access_->write_snapshot(backup_);
  if (r.is_err()) {
    CWLOG_WARN("sysproxy: restore original settings failed: " + r.error().message);
    return r;
  }
  CWLOG_INFO("sysproxy: restored original OS proxy settings (server=[" +
             utf::utf16_to_utf8(backup_.server) + "])");
  clear_backup_file();
  have_backup_ = false;
  return Result<void>::ok();
}

Result<void> Sysopt::update_sysproxy(const VergeView& v) {
  std::lock_guard<std::mutex> lock(update_lock_);
  if (!access_) {
    // 【T18 铁律配套】测试/骨架环境不装配 access → 留痕不写（绝不静默
    // 触发真实注册表写）；生产 App::init 必装配 wininet_access。
    CWLOG_WARN("sysproxy: access 未装配，跳过 OS 写入（update 留痕）");
    return Result<void>::ok();
  }

  // ← B-D40：对齐 Mini（M:core/sysopt.rs:199-235）语义——系统代理 master 开关
  // 只改 enable 双关，不做「进入前全量快照 + 退出时逐值还原 OS 原值」。
  // 本实现仅设置 Clash Win 自身的 ProxyServer（host/port/bypass），关闭时
  // enable=false 而保留自身服务串（与 Mini「保留 inner_proxy 的 host/port」一致）。
  // 旧的 sysproxy_backup.json 全量备份/还原逻辑已移除。

  // ← sysopt.rs:147-179 master switch 分派
  SysProxyState sys;
  sys.host = utf::utf8_to_utf16(v.proxy_host.empty() ? "127.0.0.1" : v.proxy_host);
  sys.port = v.mixed_port;
  sys.bypass = build_bypass(v.use_default_bypass, v.system_proxy_bypass);
  AutoProxyState auto_proxy;
  auto_proxy.url = utf::utf8_to_utf16("http://" +
                                      (v.proxy_host.empty() ? "127.0.0.1" : v.proxy_host) +
                                      ":" + std::to_string(v.pac_port) + "/commands/pac");

  bool sys_enable, auto_enable;
  GuardType guard_type = GuardType::None;
  if (!v.enable_system_proxy) {
    sys_enable = false;
    auto_enable = false;   // ← master switch 关：强制双关（启动残留收敛）
  } else if (v.proxy_auto_config) {
    sys_enable = false;
    auto_enable = true;
    // ← sysopt.rs:160-167：PAC 模式 + proxy_guard → GuardType::Autoproxy
    if (v.enable_proxy_guard) guard_type = GuardType::Autoproxy;
  } else {
    sys_enable = true;
    auto_enable = false;
    // ← sysopt.rs:168-175：纯全局 + proxy_guard → GuardType::Sysproxy
    if (v.enable_proxy_guard) guard_type = GuardType::Sysproxy;
  }
  sys.enable = sys_enable;
  auto_proxy.enable = auto_enable;

  // ← sysopt.rs:183-194 顺序规则写入（每步写后广播）
  for (ApplyStep step : proxy_apply_steps(sys_enable, auto_enable)) {
    Result<void> r = step == ApplyStep::Sysproxy ? access_->write_sysproxy(sys)
                                                 : access_->write_autoproxy(auto_proxy);
    if (r.is_err()) return r;
  }

  // ← sysopt.rs:181 set_guard_type（无论 guard 开关都转达；None 即停）
  auto& guard = SysproxyGuard::instance();
  guard.set_guard_type(guard_type, sys, auto_proxy);
  return Result<void>::ok();
}

Sysopt::VergeView make_verge_view(const config::IVerge& v,
                                  uint16_t clash_mixed_port,
                                  uint16_t pac_port) {
  Sysopt::VergeView view;
  view.enable_system_proxy = v.enable_system_proxy.value_or(false);
  view.proxy_auto_config = v.proxy_auto_config.value_or(false);
  view.enable_proxy_guard = v.enable_proxy_guard.value_or(false);
  view.proxy_guard_duration = v.proxy_guard_duration.value_or(30);
  view.use_default_bypass = v.use_default_bypass.value_or(true);
  view.system_proxy_bypass = v.system_proxy_bypass.value_or("");
  view.proxy_host = v.proxy_host.value_or("127.0.0.1");
  view.mixed_port = v.verge_mixed_port.value_or(0);
  // ← sysopt.rs:133-136：verge_mixed_port 缺失 → clash mixed_port 兜底
  if (view.mixed_port == 0) view.mixed_port = clash_mixed_port;
  view.pac_port = pac_port;
  return view;
}

Result<void> Sysopt::reset_sysproxy() {
  // ← sysopt.rs:201-207 CAS 防重入
  bool expected = false;
  if (!reset_flag_.compare_exchange_strong(expected, true)) {
    return Result<void>::ok();  // 已在重置中 → 幂等成功
  }
  struct ResetFlag {
    std::atomic<bool>& f;
    ~ResetFlag() { f.store(false); }
  } flag_guard{reset_flag_};

  std::lock_guard<std::mutex> lock(update_lock_);
  if (!access_) {
    // 同 update_sysproxy：测试环境留痕放行
    CWLOG_WARN("sysproxy: access 未装配，reset 留痕放行");
    return Result<void>::ok();
  }
  // ← sysopt.rs:217 close proxy guard（必须在锁内，Guard 停止后不再补写）
  SysproxyGuard::instance().set_guard_type(GuardType::None);
  // ← B-D40：对齐 Mini（M:core/sysopt.rs:217-235）reset 仅双关 enable，
  //   保留自身 ProxyServer/ProxyOverride（不读 OS 原值、不还原备份）。
  //   B-D41：write_sysproxy(false) 走 crate unset_proxy——仅 FLAGS=DIRECT，
  //   SERVER/BYPASS 注册表值原样保留（等价 Mini「保留 inner_proxy host/port」）。
  SysProxyState sys;
  sys.enable = false;
  AutoProxyState auto_proxy;
  auto_proxy.enable = false;
  auto r1 = access_->write_sysproxy(sys);
  auto r2 = access_->write_autoproxy(auto_proxy);
  if (r1.is_err()) return r1;
  if (r2.is_err()) return r2;
  return Result<void>::ok();
}

void Sysopt::refresh_guard(const VergeView& v) {
  // ← sysopt.rs:88-118 refresh_guard：
  //   sys off 或 guard off → stop；否则 set_interval(duration 或 30) + start
  auto& guard = SysproxyGuard::instance();
  if (!v.enable_system_proxy || !v.enable_proxy_guard) {
    guard.stop();
    return;
  }
  guard.set_interval(std::chrono::seconds(
      v.proxy_guard_duration > 0 ? v.proxy_guard_duration : 30));
  guard.start();
}

// ---------- WininetSysProxyAccess（生产；测试绝不使用） ----------
namespace {

constexpr wchar_t kInetSettingsKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";

void broadcast_proxy_change() {
  // ← sysproxy-rs f0775f6 src/windows.rs:169-176 notify_proxy_change：
  //   INTERNET_OPTION_PROXY_SETTINGS_CHANGED + REFRESH（B-D41 与 crate 同构）
  InternetSetOptionW(nullptr, INTERNET_OPTION_PROXY_SETTINGS_CHANGED, nullptr, 0);
  InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
}

// B-D41：与 sysproxy-rs（f0775f6 src/windows.rs）同构的写入路径——
// 不直接 RegSetValueExW，统一走 InternetSetOptionW(INTERNET_OPTION_PER_
// CONNECTION_OPTION)（LAN pszConnection=null + 每个 RAS 条目），由 WinInet
// 落注册表（FLAGS→ProxyEnable/AutoDetect、SERVER→ProxyServer、
// BYPASS→ProxyOverride），通知口径与读取路径（winreg 语义）一致。

// RAS 拨号/VPN 条目枚举（← windows.rs:287-359 get_ras_connections）
std::vector<std::wstring> enumerate_ras_names() {
  std::vector<std::wstring> names;
  DWORD bytes = 0, entries = 0;
  if (RasEnumEntriesW(nullptr, nullptr, nullptr, &bytes, &entries) !=
          ERROR_BUFFER_OVERFLOW ||
      entries == 0 || bytes == 0) {
    return names;  // 无 RAS 条目（常见形态，不报错）
  }
  std::vector<char> buf(bytes);
  auto* arr = reinterpret_cast<RASENTRYNAMEW*>(buf.data());
  arr->dwSize = sizeof(RASENTRYNAMEW);
  if (RasEnumEntriesW(nullptr, nullptr, arr, &bytes, &entries) != ERROR_SUCCESS) {
    return names;
  }
  for (DWORD i = 0; i < entries; ++i) names.emplace_back(arr[i].szEntryName);
  return names;
}

// 一组 PER_CONN 选项 → LAN + 全部 RAS 条目（← windows.rs:153-166 apply_option
// + 各 set/unset 函数里的 RAS 循环；任一失败返回 err，与 crate `?` 传播一致）
Result<void> apply_per_conn(INTERNET_PER_CONN_OPTIONW* opts, DWORD count) {
  INTERNET_PER_CONN_OPTION_LISTW list = {};
  list.dwSize = sizeof(list);
  list.dwOptionCount = count;
  list.pOptions = opts;
  list.pszConnection = nullptr;  // 局域网 LAN 代理设置
  if (!InternetSetOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION,
                          &list, sizeof(list))) {
    return Result<void>::err({kErrIo, "InternetSetOption per-conn (LAN) failed"});
  }
  for (const auto& name : enumerate_ras_names()) {  // 拨号连接/VPN 代理设置
    list.pszConnection = const_cast<LPWSTR>(name.c_str());
    if (!InternetSetOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION,
                            &list, sizeof(list))) {
      return Result<void>::err(
          {kErrIo, "InternetSetOption per-conn (RAS) failed"});
    }
  }
  return Result<void>::ok();
}

// ← windows.rs:110-151 set_global_proxy：FLAGS=PROXY|DIRECT + SERVER + BYPASS
Result<void> set_global_proxy(const std::wstring& server,
                              const std::wstring& bypass) {
  INTERNET_PER_CONN_OPTIONW opts[3] = {};
  opts[0].dwOption = INTERNET_PER_CONN_FLAGS;
  opts[0].Value.dwValue = PROXY_TYPE_PROXY | PROXY_TYPE_DIRECT;
  opts[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
  opts[1].Value.pszValue = const_cast<LPWSTR>(server.c_str());
  opts[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
  opts[2].Value.pszValue = const_cast<LPWSTR>(bypass.c_str());
  return apply_per_conn(opts, 3);
}

// ← windows.rs:69-104 set_auto_proxy：
//   FLAGS=AUTO_DETECT|AUTO_PROXY_URL|DIRECT + AUTOCONFIG_URL
Result<void> set_auto_proxy(const std::wstring& url) {
  INTERNET_PER_CONN_OPTIONW opts[2] = {};
  opts[0].dwOption = INTERNET_PER_CONN_FLAGS;
  opts[0].Value.dwValue =
      PROXY_TYPE_AUTO_DETECT | PROXY_TYPE_AUTO_PROXY_URL | PROXY_TYPE_DIRECT;
  opts[1].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
  opts[1].Value.pszValue = const_cast<LPWSTR>(url.c_str());
  return apply_per_conn(opts, 2);
}

// ← windows.rs:34-63 unset_proxy：仅 FLAGS=DIRECT（不动 SERVER/BYPASS 值）
Result<void> unset_proxy() {
  INTERNET_PER_CONN_OPTIONW opts[1] = {};
  opts[0].dwOption = INTERNET_PER_CONN_FLAGS;
  opts[0].Value.dwValue = PROXY_TYPE_DIRECT;
  return apply_per_conn(opts, 1);
}

// Internet Settings 写句柄：优先 KEY_SET_VALUE；被安全软件（360 等）以
// KEY_SET_VALUE 单独拒绝（ERROR_ACCESS_DENIED）时用 KEY_READ|KEY_WRITE
// 组合权限重试（KEY_WRITE 含 KEY_SET_VALUE，走同一 ACL 路径）。
LONG open_inet_settings_write(HKEY* out) {
  LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kInetSettingsKey, 0, KEY_SET_VALUE,
                          out);
  if (rc == ERROR_SUCCESS) return rc;
  LONG rc2 = RegOpenKeyExW(HKEY_CURRENT_USER, kInetSettingsKey, 0,
                           KEY_READ | KEY_WRITE, out);
  if (rc2 == ERROR_SUCCESS) return rc2;
  CWLOG_WARN("sysproxy: open write denied, KEY_SET_VALUE rc=" +
             std::to_string(rc) + " KEY_READ|KEY_WRITE rc=" + std::to_string(rc2));
  return rc2;
}

// RAS 条目同步已并入 apply_per_conn（B-D41：写路径与 sysproxy-rs 同构，
// LAN + 每个 RAS 条目统一经 PER_CONN_OPTION 写入）。

// B-D41：读回解析与 crate 同构（windows.rs:254-282 parse_proxy_address）——
// host:port 右起最后一个冒号拆分；[IPv6] 去方括号；无法解析端口 → 主机名+80。
// 返回是否解析出端口（false → port 用调用方缺省）。
void parse_proxy_address(const std::wstring& address, std::wstring& host,
                         uint16_t& port) {
  size_t colon = address.rfind(L':');
  if (colon != std::wstring::npos) {
    const std::wstring p = address.substr(colon + 1);
    bool digits = !p.empty();
    for (wchar_t c : p)
      if (c < L'0' || c > L'9') { digits = false; break; }
    if (digits) {
      unsigned long v = wcstoul(p.c_str(), nullptr, 10);
      if (v <= 65535) {
        host = address.substr(0, colon);
        // 去除 IPv6 方括号: "[::1]" → "::1"
        if (host.size() >= 2 && host.front() == L'[' && host.back() == L']')
          host = host.substr(1, host.size() - 2);
        port = static_cast<uint16_t>(v);
        return;
      }
    }
  }
  // 回退：无端口的主机名等边缘情况（crate URL 解析兜底 → 默认 80）
  host = address;
  port = 80;
}

class WininetSysProxyAccess : public ISysProxyAccess {
 public:
  Result<SysProxyState> read_sysproxy() override {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInetSettingsKey, 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
      return Result<SysProxyState>::err({kErrIo, "open Internet Settings failed"});
    }
    auto rd_str = [&](const wchar_t* name) -> std::wstring {
      wchar_t buf[2048] = {};
      DWORD size = sizeof(buf), type = 0;
      if (RegQueryValueExW(key, name, nullptr, &type,
                           reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS &&
          type == REG_SZ) {
        return buf;
      }
      return {};
    };
    auto rd_dword = [&](const wchar_t* name) -> DWORD {
      DWORD v = 0, size = sizeof(v), type = 0;
      if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&v),
                           &size) == ERROR_SUCCESS &&
          type == REG_DWORD) {
        return v;
      }
      return 0;
    };
    SysProxyState s;
    s.enable = rd_dword(L"ProxyEnable") != 0;
    // ← crate（windows.rs:180-222 get_system_proxy）读回口径：
    //   server 串含 '='（多协议 http=..;https=..）→ 优先 http= 分量；
    //   否则整体按 host:port 拆分；空串 → host 空、port 0
    std::wstring server = rd_str(L"ProxyServer");
    s.bypass = rd_str(L"ProxyOverride");
    RegCloseKey(key);
    if (!server.empty()) {
      std::wstring value = server;
      if (server.find(L'=') != std::wstring::npos) {
        // 优先查找 http 代理，缺省取第一段
        value.clear();
        size_t start = 0;
        while (start <= server.size()) {
          size_t semi = server.find(L';', start);
          std::wstring part = server.substr(
              start, semi == std::wstring::npos ? std::wstring::npos : semi - start);
          // trim（crate 对 part 做 trim 后比对前 5 字节 "http="）
          size_t b = part.find_first_not_of(L" \t");
          size_t e = part.find_last_not_of(L" \t");
          std::wstring t = b == std::wstring::npos
                               ? std::wstring()
                               : part.substr(b, e - b + 1);
          if (t.size() >= 5) {
            bool http_prefix = true;
            const wchar_t kHttp[] = L"http=";
            for (int i = 0; i < 5; ++i) {
              wchar_t c = t[i];
              if (c >= L'A' && c <= L'Z') c = c - L'A' + L'a';
              if (c != kHttp[i]) { http_prefix = false; break; }
            }
            if (http_prefix) { value = t.substr(5); break; }
          }
          if (value.empty()) value = t;  // or_else 第一段
          if (semi == std::wstring::npos) break;
          start = semi + 1;
        }
        size_t eq = value.find(L'=');
        if (eq != std::wstring::npos) value = value.substr(eq + 1);
      }
      parse_proxy_address(value, s.host, s.port);
    }
    return Result<SysProxyState>::ok(s);
  }

  // B-D41：写路径与 sysproxy-rs f0775f6 同构（windows.rs:225-230
  // Sysproxy::set_system_proxy）——enable → set_global_proxy（FLAGS=
  // PROXY|DIRECT + SERVER + BYPASS，LAN+RAS）；disable → unset_proxy
  // （仅 FLAGS=DIRECT，SERVER/BYPASS 值保留不动，与 Mini reset 语义一致）。
  // bypass 原样透传（Mini get_bypass 保证非空；crate 亦原样写入）。
  Result<void> write_sysproxy(const SysProxyState& s) override {
    const std::wstring server =
        s.host + (s.port ? L":" + std::to_wstring(s.port) : L"");
    Result<void> r =
        s.enable ? set_global_proxy(server, s.bypass) : unset_proxy();
    if (r.is_err()) return r;
    broadcast_proxy_change();
    return Result<void>::ok();
  }

  Result<AutoProxyState> read_autoproxy() override {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInetSettingsKey, 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
      return Result<AutoProxyState>::err({kErrIo, "open Internet Settings failed"});
    }
    wchar_t buf[2048] = {};
    DWORD size = sizeof(buf), type = 0;
    AutoProxyState a;
    // ← crate（windows.rs:238-240）：enable = AutoConfigURL 值存在
    //   （get_value is_ok），与串是否为空无关
    if (RegQueryValueExW(key, L"AutoConfigURL", nullptr, &type,
                         reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
      a.url = buf;
      a.enable = true;
    }
    RegCloseKey(key);
    return Result<AutoProxyState>::ok(a);
  }

  // B-D41：与 crate 同构（windows.rs:246-251 Autoproxy::set_auto_proxy）——
  // enable → set_auto_proxy（FLAGS=AUTO_DETECT|AUTO_PROXY_URL|DIRECT +
  // AUTOCONFIG_URL，LAN+RAS）；disable → unset_proxy（仅 FLAGS=DIRECT）。
  Result<void> write_autoproxy(const AutoProxyState& a) override {
    Result<void> r =
        (a.enable && !a.url.empty()) ? set_auto_proxy(a.url) : unset_proxy();
    if (r.is_err()) return r;
    broadcast_proxy_change();
    return Result<void>::ok();
  }

  bool persistent() const override { return true; }

  // 全量快照：保留原串（不拆 host:port），并带上 AutoDetect 与"键是否存在"
  Result<SysProxySnapshot> read_snapshot() override {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInetSettingsKey, 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
      return Result<SysProxySnapshot>::err({kErrIo, "open Internet Settings failed"});
    }
    auto rd_str = [&](const wchar_t* name, bool& present) -> std::wstring {
      wchar_t buf[2048] = {};
      DWORD size = sizeof(buf), type = 0;
      if (RegQueryValueExW(key, name, nullptr, &type,
                           reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS &&
          type == REG_SZ) {
        present = true;
        return buf;
      }
      present = false;
      return {};
    };
    auto rd_dword = [&](const wchar_t* name, bool& present) -> DWORD {
      DWORD v = 0, size = sizeof(v), type = 0;
      if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&v),
                           &size) == ERROR_SUCCESS &&
          type == REG_DWORD) {
        present = true;
        return v;
      }
      present = false;
      return 0;
    };
    SysProxySnapshot s;
    s.valid = true;
    bool enable_present = false;
    s.enable = rd_dword(L"ProxyEnable", enable_present) != 0;
    s.server = rd_str(L"ProxyServer", s.proxy_server_present);
    s.bypass = rd_str(L"ProxyOverride", s.proxy_override_present);
    s.auto_config_url = rd_str(L"AutoConfigURL", s.auto_config_present);
    s.auto_detect = rd_dword(L"AutoDetect", s.auto_detect_present) != 0;
    RegCloseKey(key);
    return Result<SysProxySnapshot>::ok(s);
  }

  // 逐值还原（含"原来不存在则删除键"）
  Result<void> write_snapshot(const SysProxySnapshot& s) override {
    HKEY key = nullptr;
    LONG rc = open_inet_settings_write(&key);
    if (rc != ERROR_SUCCESS) {
      return Result<void>::err({kErrIo, "open Internet Settings failed"});
    }
    DWORD enable = s.enable ? 1 : 0;
    RegSetValueExW(key, L"ProxyEnable", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&enable), sizeof(enable));
    auto put_or_del_sz = [&](const wchar_t* name, const std::wstring& v, bool present) {
      if (present) {
        RegSetValueExW(key, name, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(v.c_str()),
                       static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
      } else {
        RegDeleteValueW(key, name);
      }
    };
    put_or_del_sz(L"ProxyServer", s.server, s.proxy_server_present);
    put_or_del_sz(L"ProxyOverride", s.bypass, s.proxy_override_present);
    put_or_del_sz(L"AutoConfigURL", s.auto_config_url, s.auto_config_present);
    if (s.auto_detect_present) {
      DWORD ad = s.auto_detect ? 1 : 0;
      RegSetValueExW(key, L"AutoDetect", 0, REG_DWORD,
                     reinterpret_cast<const BYTE*>(&ad), sizeof(ad));
    } else {
      RegDeleteValueW(key, L"AutoDetect");
    }
    RegCloseKey(key);
    broadcast_proxy_change();
    return Result<void>::ok();
  }
};

}  // namespace

ISysProxyAccess* wininet_access() {
  static WininetSysProxyAccess s;
  return &s;
}

}  // namespace clashwin::system
