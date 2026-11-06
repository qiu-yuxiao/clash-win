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
  //   旧的 sysproxy_backup.json 全量还原已随 update 一同移除。
  SysProxyState sys;
  sys.enable = false;
  auto cur = access_->read_sysproxy();
  if (cur.is_ok()) {
    sys.host = cur.value().host;
    sys.port = cur.value().port;
    sys.bypass = cur.value().bypass;
  }
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
  // ← census §124：PROXY_SETTINGS_CHANGED + REFRESH（浏览器立即感知）
  InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
  InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
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

// RAS 条目同步（← 任务卡完成标准①：LAN + 每个 RAS 条目 PER_CONN_OPTION）。
// sysproxy-rs 经 wininet per-connection option 覆盖 RAS 拨号条目；此处遍历
// RasEnumEntriesW 逐条以条目名为 connection 构造 INTERNET_PER_CONN_OPTION_LISTW
// 后 InternetSetOptionW 写入，与 LAN 注册表写入（write_sysproxy）保持同值。
// 无 RAS 拨号条目时为 no-op（常见形态，不报错）。
void sync_ras_entries(const SysProxyState& s) {
  DWORD entries = 0, bytes = 0;
  if (RasEnumEntriesW(nullptr, nullptr, nullptr, &bytes, &entries) !=
          ERROR_BUFFER_OVERFLOW ||
      entries == 0 || bytes == 0) {
    return;  // 无 RAS 条目（常见形态）
  }
  std::vector<char> buf(bytes);
  auto* names = reinterpret_cast<RASENTRYNAMEW*>(buf.data());
  names->dwSize = sizeof(RASENTRYNAMEW);
  if (RasEnumEntriesW(nullptr, nullptr, names, &bytes, &entries) != ERROR_SUCCESS) {
    return;
  }
  const std::wstring server = s.host + (s.port ? L":" + std::to_wstring(s.port) : L"");
  const std::wstring bypass = s.bypass.empty() ? kDefaultBypass : s.bypass;
  INTERNET_PER_CONN_OPTIONW opt[3] = {};
  opt[0].dwOption = INTERNET_PER_CONN_FLAGS;
  opt[0].Value.dwValue = s.enable ? (PROXY_TYPE_PROXY | PROXY_TYPE_DIRECT)
                                  : PROXY_TYPE_DIRECT;
  opt[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
  opt[1].Value.pszValue = const_cast<LPWSTR>(server.c_str());
  opt[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
  opt[2].Value.pszValue = const_cast<LPWSTR>(bypass.c_str());
  INTERNET_PER_CONN_OPTION_LISTW list = {};
  list.dwSize = sizeof(list);
  list.dwOptionCount = 3;
  list.pOptions = opt;
  for (DWORD i = 0; i < entries; ++i) {
    list.pszConnection = const_cast<LPWSTR>(names[i].szEntryName);
    InternetSetOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list,
                       sizeof(list));
  }
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
    // ← network.rs get_sys_proxy：server = "host:port"（读回拆分）
    std::wstring server = rd_str(L"ProxyServer");
    s.bypass = rd_str(L"ProxyOverride");
    RegCloseKey(key);
    size_t colon = server.rfind(L':');
    if (colon != std::wstring::npos) {
      s.host = server.substr(0, colon);
      s.port = static_cast<uint16_t>(_wtoi(server.substr(colon + 1).c_str()));
    } else {
      s.host = server;
    }
    return Result<SysProxyState>::ok(s);
  }

  Result<void> write_sysproxy(const SysProxyState& s) override {
    HKEY key = nullptr;
    LONG rc = open_inet_settings_write(&key);
    if (rc != ERROR_SUCCESS) {
      return Result<void>::err({kErrIo, "open Internet Settings failed"});
    }
    DWORD enable = s.enable ? 1 : 0;
    // ← network.rs server 串口径 "host:port"
    std::wstring server =
        s.host + (s.port ? L":" + std::to_wstring(s.port) : L"");
    RegSetValueExW(key, L"ProxyEnable", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&enable), sizeof(enable));
    RegSetValueExW(key, L"ProxyServer", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(server.c_str()),
                   static_cast<DWORD>((server.size() + 1) * sizeof(wchar_t)));
    RegSetValueExW(key, L"ProxyOverride", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(s.bypass.c_str()),
                   static_cast<DWORD>((s.bypass.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    sync_ras_entries(s);
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
    if (RegQueryValueExW(key, L"AutoConfigURL", nullptr, &type,
                         reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
      a.url = buf;
      a.enable = !a.url.empty();
    }
    RegCloseKey(key);
    return Result<AutoProxyState>::ok(a);
  }

  Result<void> write_autoproxy(const AutoProxyState& a) override {
    HKEY key = nullptr;
    LONG rc = open_inet_settings_write(&key);
    if (rc != ERROR_SUCCESS) {
      return Result<void>::err({kErrIo, "open Internet Settings failed"});
    }
    if (a.enable && !a.url.empty()) {
      RegSetValueExW(key, L"AutoConfigURL", 0, REG_SZ,
                     reinterpret_cast<const BYTE*>(a.url.c_str()),
                     static_cast<DWORD>((a.url.size() + 1) * sizeof(wchar_t)));
    } else {
      RegDeleteValueW(key, L"AutoConfigURL");  // ← 清 PAC（键删除）
    }
    RegCloseKey(key);
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
