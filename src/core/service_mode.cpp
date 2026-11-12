// T07 服务模式实现（对照 ClashVerge core/service.rs + 04§6）
#include "core/service_mode.h"

#include <windows.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "config/config.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"

#include <chrono>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace clashwin::core::service_mode {

namespace {
using clashwin::Result;

std::string werr(const char* what, DWORD gle) {
  return std::string(what) + " failed, gle=" + std::to_string(gle);
}

// ← service.rs:139-145 system32_sc_exe：绝对路径调用，避免 PATH 劫持（F-07）
std::wstring system32_sc_exe() {
  wchar_t root[MAX_PATH] = {};
  DWORD n = ::GetEnvironmentVariableW(L"SystemRoot", root, MAX_PATH);
  std::wstring base = (n > 0 && n < MAX_PATH) ? std::wstring(root) : L"C:\\Windows";
  return base + L"\\System32\\sc.exe";
}

// ← service.rs:94-104：非提权走 ShellExecuteW runas（UAC），提权直接命令行
bool shell_runas(const std::wstring& file, const std::wstring& params) {
  SHELLEXECUTEINFOW sei{};
  sei.cbSize = sizeof(sei);
  sei.lpVerb = L"runas";
  sei.hwnd = ::GetForegroundWindow();
  sei.lpFile = file.c_str();
  sei.lpParameters = params.empty() ? nullptr : params.c_str();
  sei.nShow = SW_HIDE;
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  if (!::ShellExecuteExW(&sei)) return false;
  if (sei.hProcess) {
    ::WaitForSingleObject(sei.hProcess, 60000);
    DWORD code = 1;
    ::GetExitCodeProcess(sei.hProcess, &code);
    ::CloseHandle(sei.hProcess);
    return code == 0;
  }
  return true;
}
}  // namespace

// ---------- ServiceIpcAdapter ----------
Result<nlohmann::json> ServiceIpcAdapter::clash_start(const nlohmann::json& payload) {
  return ipc_->clash_start(payload);
}
Result<nlohmann::json> ServiceIpcAdapter::clash_stop() { return ipc_->clash_stop(); }
Result<std::string> ServiceIpcAdapter::magic_probe() { return ipc_->magic_probe(); }
bool ServiceIpcAdapter::pipe_present() {
  return mihomo::ServiceIpc::is_pipe_present(mihomo::ServiceIpc::kDefaultPipeName);
}
Result<nlohmann::json> ServiceIpcAdapter::get_clash_logs() { return ipc_->get_clash_logs(); }

// ---------- SCM ----------
std::recursive_mutex& service_operation_mutex() {
  static std::recursive_mutex mu;
  return mu;
}

std::wstring extract_service_exe_from_image_path(const std::wstring& raw) {
  auto trim_ws = [](std::wstring s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == L' ' || s[b] == L'\t')) ++b;
    size_t e = s.size();
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\0')) --e;
    return s.substr(b, e - b);
  };

  std::wstring s = trim_ws(raw);
  if (s.empty()) return {};
  if (s.rfind(L"\\??\\", 0) == 0) {
    s = trim_ws(s.substr(4));
  }
  if (s.empty()) return {};

  if (s.find(L'%') != std::wstring::npos) {
    DWORD needed = ::ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
    if (needed > 0) {
      std::wstring expanded(needed, L'\0');
      DWORD written = ::ExpandEnvironmentStringsW(s.c_str(), expanded.data(), needed);
      if (written > 0) {
        while (!expanded.empty() && expanded.back() == L'\0') expanded.pop_back();
        s = trim_ws(expanded);
      }
    }
  }
  if (s.empty()) return {};

  if (s.front() == L'"') {
    size_t end = s.find(L'"', 1);
    if (end == std::wstring::npos) return {};
    return trim_ws(s.substr(1, end - 1));
  }

  std::wstring lower = s;
  for (wchar_t& ch : lower) ch = static_cast<wchar_t>(::towlower(ch));
  size_t pos = 0;
  while ((pos = lower.find(L".exe", pos)) != std::wstring::npos) {
    size_t after = pos + 4;
    if (after == lower.size() || lower[after] == L' ' || lower[after] == L'\t') {
      return trim_ws(s.substr(0, after));
    }
    pos += 4;
  }
  return s;
}

ScmServiceRegistration inspect_service_registration() {
  const std::wstring reg_subkey =
      std::wstring(L"SYSTEM\\CurrentControlSet\\Services\\") + kServiceNameW;
  HKEY hkey = nullptr;
  LONG rc = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, reg_subkey.c_str(), 0, KEY_READ, &hkey);
  if (rc == ERROR_SUCCESS && hkey != nullptr) {
    DWORD start_type = 0;
    DWORD cb_start = sizeof(start_type);
    DWORD type_start = 0;
    bool has_start = (::RegQueryValueExW(hkey, L"Start", nullptr, &type_start,
                                         reinterpret_cast<LPBYTE>(&start_type),
                                         &cb_start) == ERROR_SUCCESS &&
                      type_start == REG_DWORD);

    wchar_t img_buf[2048] = {};
    DWORD cb_img = sizeof(img_buf) - sizeof(wchar_t);
    DWORD type_img = 0;
    std::wstring raw_img;
    if (::RegQueryValueExW(hkey, L"ImagePath", nullptr, &type_img,
                           reinterpret_cast<LPBYTE>(img_buf), &cb_img) == ERROR_SUCCESS &&
        (type_img == REG_SZ || type_img == REG_EXPAND_SZ) && cb_img >= sizeof(wchar_t)) {
      img_buf[cb_img / sizeof(wchar_t)] = L'\0';
      raw_img = img_buf;
    }
    ::RegCloseKey(hkey);

    if (has_start && start_type == SERVICE_DISABLED) {
      return ScmServiceRegistration::DisabledOrBroken;
    }
    std::wstring exe_path = extract_service_exe_from_image_path(raw_img);
    if (exe_path.empty()) {
      return ScmServiceRegistration::DisabledOrBroken;
    }
    DWORD attr = ::GetFileAttributesW(exe_path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      return ScmServiceRegistration::DisabledOrBroken;
    }
    return ScmServiceRegistration::InstalledAndEnabled;
  }
  if (rc == ERROR_FILE_NOT_FOUND || rc == ERROR_PATH_NOT_FOUND) {
    if (!is_service_installed()) {
      return ScmServiceRegistration::NotInstalled;
    }
  }

  SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return ScmServiceRegistration::NotInstalled;
  SC_HANDLE svc = ::OpenServiceW(scm, kServiceNameW, SERVICE_QUERY_CONFIG);
  if (!svc) {
    DWORD gle = ::GetLastError();
    ::CloseServiceHandle(scm);
    return (gle == ERROR_SERVICE_DOES_NOT_EXIST) ? ScmServiceRegistration::NotInstalled
                                                 : ScmServiceRegistration::DisabledOrBroken;
  }
  std::vector<BYTE> buf(8192);
  auto cfg = reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(buf.data());
  DWORD needed = 0;
  bool ok = ::QueryServiceConfigW(svc, cfg, static_cast<DWORD>(buf.size()), &needed) != FALSE;
  ::CloseServiceHandle(svc);
  ::CloseServiceHandle(scm);
  if (!ok) return ScmServiceRegistration::DisabledOrBroken;
  if (cfg->dwStartType == SERVICE_DISABLED) return ScmServiceRegistration::DisabledOrBroken;
  std::wstring exe = extract_service_exe_from_image_path(
      cfg->lpBinaryPathName ? cfg->lpBinaryPathName : L"");
  if (exe.empty()) return ScmServiceRegistration::DisabledOrBroken;
  DWORD attr = ::GetFileAttributesW(exe.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return ScmServiceRegistration::DisabledOrBroken;
  }
  return ScmServiceRegistration::InstalledAndEnabled;
}

// ← Mini: src-tauri/src/core/service.rs:445 is_service_installed()
bool is_service_installed() {
  SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  SC_HANDLE svc = ::OpenServiceW(scm, kServiceNameW, SERVICE_QUERY_STATUS);
  if (svc) ::CloseServiceHandle(svc);
  ::CloseServiceHandle(scm);
  return svc != nullptr;
}

// ← Mini: src-tauri/src/core/service.rs:429-455 refresh() 的 StartRequired 分支
//   （已安装但未运行 → 启动 + 等待就绪；本函数只回答 SCM STATE 是否为 RUNNING）
bool is_service_running() {
  SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  SC_HANDLE svc = ::OpenServiceW(scm, kServiceNameW, SERVICE_QUERY_STATUS);
  if (!svc) {
    ::CloseServiceHandle(scm);
    return false;
  }
  SERVICE_STATUS st{};
  bool running = false;
  if (::QueryServiceStatus(svc, &st)) running = st.dwCurrentState == SERVICE_RUNNING;
  ::CloseServiceHandle(svc);
  ::CloseServiceHandle(scm);
  return running;
}

// ← Mini: src-tauri/src/core/service.rs:470-475 ReinstallRequired / ForceReinstallRequired
//   （服务已注册但二进制不可用 → 重装）。本函数只做前置判定，重装由 cmd_service 走 installer。
bool is_service_binary_valid() {
  SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  SC_HANDLE svc = ::OpenServiceW(scm, kServiceNameW, SERVICE_QUERY_CONFIG);
  if (!svc) {
    ::CloseServiceHandle(scm);
    return false;
  }
  std::vector<BYTE> buf(8192);
  auto cfg = reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(buf.data());
  DWORD needed = 0;
  bool ok = ::QueryServiceConfigW(svc, cfg, static_cast<DWORD>(buf.size()), &needed) != FALSE;
  ::CloseServiceHandle(svc);
  ::CloseServiceHandle(scm);
  if (!ok) return false;
  std::wstring raw = cfg->lpBinaryPathName ? cfg->lpBinaryPathName : L"";
  std::wstring path = extract_service_exe_from_image_path(raw);
  if (path.empty()) return false;
  DWORD attr = ::GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

// ← Mini: src-tauri/src/core/service.rs:446-452（已安装未运行 → 启动服务并等待）
Result<void> start_service() {
  std::lock_guard<std::recursive_mutex> op_lock(service_operation_mutex());
  SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (scm) {
    SC_HANDLE svc = ::OpenServiceW(scm, kServiceNameW, SERVICE_START);
    if (svc) {
      BOOL ok = ::StartServiceW(svc, 0, nullptr);
      DWORD gle = ::GetLastError();
      ::CloseServiceHandle(svc);
      ::CloseServiceHandle(scm);
      if (ok || gle == ERROR_SERVICE_ALREADY_RUNNING) return Result<void>::ok();
      if (gle != ERROR_ACCESS_DENIED) {
        return Result<void>::err({2310, werr("StartServiceW", gle)});
      }
    } else {
      DWORD gle = ::GetLastError();
      ::CloseServiceHandle(scm);
      if (gle != ERROR_ACCESS_DENIED) {
        return Result<void>::err({2311, werr("OpenServiceW", gle)});
      }
    }
  }
  // 提权回退（UAC 弹窗；F-07：System32 绝对路径）
  std::wstring params = L"start " + std::wstring(kServiceNameW);
  if (!shell_runas(system32_sc_exe(), params)) {
  // runas check above
    return Result<void>::err({2312, "sc.exe start (runas) failed or cancelled"});
  }
  return Result<void>::ok();
}

Result<void> stop_service() {
  std::lock_guard<std::recursive_mutex> op_lock(service_operation_mutex());
  if (!is_service_installed() || !is_service_running()) {
    return Result<void>::ok();
  }
  CWLOG_INFO("stop_service: stopping clash_verge_service");
  SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (scm) {
    SC_HANDLE svc = ::OpenServiceW(scm, kServiceNameW, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (svc) {
      SERVICE_STATUS st{};
      BOOL ok = ::ControlService(svc, SERVICE_CONTROL_STOP, &st);
      DWORD gle = ::GetLastError();
      ::CloseServiceHandle(svc);
      ::CloseServiceHandle(scm);
      if (ok || gle == ERROR_SERVICE_NOT_ACTIVE) return Result<void>::ok();
      if (gle != ERROR_ACCESS_DENIED) {
        return Result<void>::err({2313, werr("ControlService(STOP)", gle)});
      }
    } else {
      DWORD gle = ::GetLastError();
      ::CloseServiceHandle(scm);
      if (gle == ERROR_SERVICE_DOES_NOT_EXIST) return Result<void>::ok();
      if (gle != ERROR_ACCESS_DENIED) {
        return Result<void>::err({2314, werr("OpenServiceW(STOP)", gle)});
      }
    }
  }
  // 提权回退（UAC 弹窗；F-07：System32 绝对路径）
  std::wstring params = L"stop " + std::wstring(kServiceNameW);
  if (!shell_runas(system32_sc_exe(), params)) {
    return Result<void>::err({2315, "sc.exe stop (runas) failed or cancelled"});
  }
  return Result<void>::ok();
}

// ---------- 就绪判定 / 等待 ----------
// ← Mini: src-tauri/src/core/service.rs:333 is_service_available()
//   Mini 的"就绪"= IPC 路径可访问；我方同义（管道存在 + magic 握手），
//   与 SCM STATE（is_service_running）分开判定，避免"已装未跑"被误判为就绪。
bool is_service_ready(IServiceIpc& ipc) {
  if (!ipc.pipe_present()) return false;
  auto probe = ipc.magic_probe();
  return probe.is_ok();
}

bool wait_for_service_ready(IServiceIpc& ipc, int budget_ms, int interval_ms) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budget_ms);
  for (;;) {
    if (is_service_ready(ipc)) return true;
    if (std::chrono::steady_clock::now() >= deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
  }
}

// ---------- payload ----------
nlohmann::json build_clash_config_payload(const std::string& core_path,
                                          const std::string& core_ipc_path,
                                          const std::string& config_path,
                                          const std::string& config_dir,
                                          const LogConfig& log_cfg) {
  // ← service.rs:231-239 + 04§6：ClashConfig{core_config, log_config}
  return nlohmann::json{
      {"core_config",
       {{"core_path", core_path},
        {"core_ipc_path", core_ipc_path},
        {"config_path", config_path},
        {"config_dir", config_dir}}},
      {"log_config",
       {{"directory", log_cfg.directory},
        {"max_log_size", log_cfg.max_log_size},
        {"max_log_files", log_cfg.max_log_files}}},
  };
}

LogConfig default_log_config() {
  LogConfig lc;
  lc.directory = clashwin::utf::utf16_to_utf8(clashwin::util::dirs::service_log_dir());
  // ← logger.rs:258-259：max_log_size = app_log_max_size(KB)×1024；max_log_files = count
  uint64_t size_kb = consts::kServiceLogSizeBytes / 1024;
  size_t files = consts::kServiceLogMaxFiles;
  try {
    auto verge = clashwin::config::Config::instance().verge().latest_arc();
    if (verge->app_log_max_size.has_value()) size_kb = *verge->app_log_max_size;
    if (verge->app_log_max_count.has_value())
      files = static_cast<size_t>(*verge->app_log_max_count);
  } catch (...) {
    // Config 未初始化（测试场景）→ 常量默认
  }
  lc.max_log_size = size_kb * 1024;
  lc.max_log_files = files;
  return lc;
}

// ---------- 启停 ----------
bool is_port_free(uint16_t port) {
  // ← service.rs:249-259 以 TcpListener::bind 探测端口释放
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int ok = ::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  ::closesocket(s);
  return ok == 0;
}

Result<void> start_with_service(IServiceIpc& ipc, const nlohmann::json& payload, uint16_t mixed_port,
                                int poll_ms, int max_wait_ms) {
  CWLOG_INFO("Trying to start core with existing service");
  // ← service.rs:241-243 先清理前次会话残留旧内核（崩溃退出时 clean_async 未执行）
  auto stop_r = ipc.clash_stop();
  if (stop_r.is_err()) {
    CWLOG_WARN("stop_clash before start failed (ignored): " + stop_r.error().message);
  }

  // ← service.rs L2-02：stop 后轮询端口释放（50ms × 最多 3s）
  int waited = 0;
  while (!is_port_free(mixed_port)) {
    if (waited >= max_wait_ms) {
      CWLOG_WARN("Waiting for port release timed out (" + std::to_string(max_wait_ms) +
                 "ms), continuing to start");
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
    waited += poll_ms;
  }

  // ← service.rs:261-269：连接失败 / code>0 都判失败
  auto resp = ipc.clash_start(payload);
  if (resp.is_err()) {
    return Result<void>::err({2313, "Cannot connect to Clash Verge Service: " + resp.error().message});
  }
  const auto& j = resp.value();
  // 两种返回：真实 ServiceIpc 已剥离 envelope（成功时 data 可能为 null）；
  // 测试 Mock 仍返回 {code,message}。仅当确有 code 字段时按业务码判定。
  int code = 0;
  if (j.is_object() && j.contains("code") && j["code"].is_number_integer()) {
    code = j["code"].get<int>();
  }
  if (code > 0) {
    std::string msg = j.value("message", "");
    CWLOG_ERROR("Failed to start core via service: " + msg);
    return Result<void>::err({2314, "start_clash failed (code=" + std::to_string(code) + "): " + msg});
  }
  CWLOG_INFO("Service started core successfully");
  return Result<void>::ok();
}

Result<void> stop_with_service(IServiceIpc& ipc) {
  CWLOG_INFO("Stopping core via service (IPC)");
  auto resp = ipc.clash_stop();
  if (resp.is_err()) {
    return Result<void>::err({2315, "Cannot connect to Clash Verge Service: " + resp.error().message});
  }
  const auto& j = resp.value();
  int code = 0;
  if (j.is_object() && j.contains("code") && j["code"].is_number_integer()) {
    code = j["code"].get<int>();
  }
  if (code > 0) {
    std::string msg = j.value("message", "");
    CWLOG_ERROR("Failed to stop core via service: " + msg);
    return Result<void>::err({2316, "stop_clash failed (code=" + std::to_string(code) + "): " + msg});
  }
  CWLOG_INFO("Service stopped core successfully");
  return Result<void>::ok();
}

// ---------- 启动模式决策 ----------
StartupDecision decide_startup_mode(bool needs_tun, bool is_admin, bool service_ready) {
  StartupDecision d;
  // ← lifecycle.rs:151-176 prepare_startup_mode
  if (!needs_tun) {
    d.use_service = false;  // 非 TUN → Sidecar
    return d;
  }
  // TUN 模式下：服务就绪则优先走服务（对齐 Clash Mini lifecycle.rs / service.rs）
  if (service_ready) {
    d.use_service = true;
    return d;
  }
  // 服务未就绪：若已有管理员权限，才可作为 fallback 临时以 Sidecar 运行 TUN
  if (is_admin) {
    d.use_service = false;
    return d;
  }
  // 服务未就绪且非管理员：明确告警（BUG-298 教训：禁止静默降级）→ Sidecar + 回退系统代理
  d.use_service = false;
  d.fallback_sysproxy = true;
  return d;
}

// ---------- 服务状态五态 ----------
ServiceState evaluate_state(IServiceProbe& probe) {
  if (!probe.installed()) return ServiceState::Missing;
  if (!probe.binary_valid()) return ServiceState::BinaryInvalid;
  // 回归点：SCM 非 RUNNING 时，即使管道还在也必须判 Stopped，不得判 ready。
  if (!probe.running()) return ServiceState::Stopped;
  if (!probe.pipe_ready()) return ServiceState::NotReady;
  return ServiceState::Ready;
}

}  // namespace clashwin::core::service_mode
