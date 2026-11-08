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
  if (raw.empty()) return false;
  // 取可执行文件路径：带引号取引号内；否则截到 ".exe" 末尾
  std::wstring path;
  if (raw.front() == L'"') {
    size_t end = raw.find(L'"', 1);
    if (end == std::wstring::npos) return false;
    path = raw.substr(1, end - 1);
  } else {
    size_t exe = raw.find(L".exe");
    path = exe == std::wstring::npos ? raw : raw.substr(0, exe + 4);
  }
  if (path.empty()) return false;
  DWORD attr = ::GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

// ← Mini: src-tauri/src/core/service.rs:446-452（已安装未运行 → 启动服务并等待）
Result<void> start_service() {
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
  if (is_admin) {
    d.use_service = false;  // 管理员自身可创建 TUN → Sidecar
    return d;
  }
  // TUN + 非管理员：必须走服务
  if (service_ready) {
    d.use_service = true;
    return d;
  }
  // 服务未就绪：明确告警（BUG-298 教训：禁止静默降级）→ Sidecar + 回退系统代理
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
