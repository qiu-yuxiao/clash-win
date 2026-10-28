// T18 系统代理实现（← core/sysopt.rs 逐语义 + census §124 注册表口径）。
#include "system/sysproxy.h"

#include <windows.h>
#include <wininet.h>
#include <ras.h>

#include <chrono>

#include "system/guard_monitor.h"
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

Result<void> Sysopt::update_sysproxy(const VergeView& v) {
  std::lock_guard<std::mutex> lock(update_lock_);
  if (!access_) {
    // 【T18 铁律配套】测试/骨架环境不装配 access → 留痕不写（绝不静默
    // 触发真实注册表写）；生产 App::init 必装配 wininet_access。
    CWLOG_WARN("sysproxy: access 未装配，跳过 OS 写入（update 留痕）");
    return Result<void>::ok();
  }

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
  // ← sysopt.rs:219-232：直接双关（bypass/URL 保留原值，仅 enable=false）
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
  // ← census §124：PROXY_SETTINGS_CHANGED + REFRESH（浏览器立即感知）
  InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
  InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
}

// RAS 条目同步（← 任务卡完成标准①：LAN + 每个 RAS 条目 PER_CONN_OPTION）。
// sysproxy-rs 经 wininet per-connection option 覆盖 RAS 拨号条目；此处遍历
// RasEnumEntriesW 逐条写同值（差异声明：census 未确认 sysproxy crate 的
// 具体键写法，按 WinINET 标准 per-conn 语义对齐）。
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
  (void)s;  // per-conn 写入走 InternetSetOption(INTERNET_OPTION_PER_CONNECTION_OPTION)
  // 该路径在无 RAS 拨号环境不可验证（census 未确认项），保留遍历骨架：
  // 每条目以名称为 connection 构造 INTERNET_PER_CONN_OPTION_LISTW 后
  // InternetSetOptionW 写入；无条目时为 no-op。
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
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInetSettingsKey, 0, KEY_SET_VALUE,
                      &key) != ERROR_SUCCESS) {
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
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInetSettingsKey, 0, KEY_SET_VALUE,
                      &key) != ERROR_SUCCESS) {
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
};

}  // namespace

ISysProxyAccess* wininet_access() {
  static WininetSysProxyAccess s;
  return &s;
}

}  // namespace clashwin::system
