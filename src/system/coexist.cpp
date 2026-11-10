// ★ Mini/Win 互斥共存方针实现（← 总纲《移植基本准则.md》§5.2，用户 2026-11-09 改定）
// 取代旧「共存豁免 + refuse_service_mutation」设计（《Win和mini的差别.md》❌11/△3）。
// 纪律：按 exe 安装目录路径判定归属；不删对方磁盘文件；启动期禁止 UAC 弹窗
//       （无权限的停止/删除留待 install_service 的 UAC 路径）。
#include "system/coexist.h"

#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>  // ShellExecuteExW / SHELLEXECUTEINFOW / SEE_MASK_*（与项目其余源文件一致）
#include <bcrypt.h>    // BCrypt SHA256（← 总纲 §5.2.2 借用判定；与 core_updater.cpp 同法）

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <thread>
#include <vector>

#include "core/service_mode.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"

namespace clashwin::system::coexist {

namespace {

std::wstring lower(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
  return s;
}

// 解析服务 ImagePath 为可执行文件路径（镜像 service_mode.cpp 的解析规则：
// 带引号取引号内；否则截到最后一个 ".exe" 末尾）
std::wstring image_exe_path(const std::wstring& raw) {
  if (raw.empty()) return L"";
  if (raw.front() == L'"') {
    const size_t end = raw.find(L'"', 1);
    if (end == std::wstring::npos) return L"";
    return raw.substr(1, end - 1);
  }
  const size_t exe = raw.rfind(L".exe");
  return exe == std::wstring::npos ? raw : raw.substr(0, exe + 4);
}

// 两 App 共享组件的文件名集合：路径不属本方时出现 ⇒ 归属对方（或陈旧残留）。
bool is_shared_component_name(const std::wstring& file_lower) {
  static const wchar_t* kNames[] = {
      L"clash-mini.exe",        L"clash mini.exe",
      L"mini-mihomo.exe",       L"win-mihomo.exe",
      L"clash-verge-service.exe",
      L"clash-verge-service-install.exe",
      L"clash-verge-service-uninstall.exe",
      L"enableloopback.exe",    L"clash-verge.exe",
  };
  for (const wchar_t* n : kNames) {
    if (file_lower == n) return true;
  }
  return false;
}

std::wstring system32_sc_exe() {
  wchar_t root[MAX_PATH] = {};
  const DWORD n = ::GetEnvironmentVariableW(L"SystemRoot", root, MAX_PATH);
  const std::wstring base = (n > 0 && n < MAX_PATH) ? std::wstring(root) : L"C:\\Windows";
  return base + L"\\System32\\sc.exe";
}

// ← service_mode.cpp shell_runas 同构：UAC 提权跑命令（等待退出、校验退出码）
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

// 已注册 clash_verge_service 的 ImagePath 解析出的 exe 路径；缺失/查询失败 → 空。
std::wstring registered_service_exe() {
  SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return L"";
  SC_HANDLE svc = ::OpenServiceW(scm, core::service_mode::kServiceNameW,
                                 SERVICE_QUERY_CONFIG);
  if (!svc) {
    ::CloseServiceHandle(scm);
    return L"";
  }
  std::vector<BYTE> buf(8192);
  auto cfg = reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(buf.data());
  DWORD needed = 0;
  const bool ok =
      ::QueryServiceConfigW(svc, cfg, static_cast<DWORD>(buf.size()), &needed) != FALSE;
  const std::wstring raw = ok && cfg->lpBinaryPathName ? cfg->lpBinaryPathName : L"";
  ::CloseServiceHandle(svc);
  ::CloseServiceHandle(scm);
  return ok ? image_exe_path(raw) : L"";
}

// 文件 SHA256 → 小写 hex；打开/计算失败 → 空串（调用方按「不一致」处理）。
std::string file_sha256_hex(const std::wstring& path) {
  HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};
  std::string hex;
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  do {
    if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(
            &alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) break;
    DWORD obj_len = 0, cb = 0;
    if (!BCRYPT_SUCCESS(::BCryptGetProperty(
            alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj_len),
            sizeof(obj_len), &cb, 0))) break;
    std::vector<BYTE> hash_obj(obj_len);
    if (!BCRYPT_SUCCESS(::BCryptCreateHash(
            alg, &hash, hash_obj.data(), obj_len, nullptr, 0, 0))) break;
    std::vector<BYTE> buf(65536);
    DWORD read = 0;
    bool io_ok = true;
    while (::ReadFile(file, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr) &&
           read > 0) {
      if (!BCRYPT_SUCCESS(::BCryptHashData(hash, buf.data(), read, 0))) {
        io_ok = false;
        break;
      }
    }
    if (!io_ok) break;
    BYTE digest[32] = {};
    if (!BCRYPT_SUCCESS(::BCryptFinishHash(hash, digest, sizeof(digest), 0))) break;
    static constexpr char kHex[] = "0123456789abcdef";
    hex.resize(64);
    for (int i = 0; i < 32; ++i) {
      hex[static_cast<size_t>(i) * 2] = kHex[digest[i] >> 4];
      hex[static_cast<size_t>(i) * 2 + 1] = kHex[digest[i] & 0x0F];
    }
  } while (false);
  if (hash) ::BCryptDestroyHash(hash);
  if (alg) ::BCryptCloseAlgorithmProvider(alg, 0);
  ::CloseHandle(file);
  return hex;
}

}  // namespace

bool is_own_path(const std::wstring& exe_path) {
  if (exe_path.empty()) return false;
  // Windows 路径语义：大小写不敏感、正/反斜杠等价（服务 ImagePath 可能写 /）。
  auto norm = [](std::wstring s) {
    std::replace(s.begin(), s.end(), L'/', L'\\');
    return lower(std::move(s));
  };
  const std::wstring p = norm(exe_path);
  const std::wstring roots[] = {norm(util::dirs::exe_dir()),
                                norm(util::dirs::app_home_dir())};
  for (const std::wstring& raw_root : roots) {
    if (raw_root.empty()) continue;
    std::wstring root = raw_root;
    if (root.back() != L'\\') root += L'\\';
    if (p.starts_with(root)) return true;
  }
  return false;
}

bool service_image_is_own() {
  const std::wstring exe = registered_service_exe();
  if (exe.empty()) return false;
  return is_own_path(exe);
}

bool upstream_baseline_equals(std::string_view hex_lower_or_mixed) {
  const std::string_view baseline(core::service_mode::kServiceExeSha256);
  if (hex_lower_or_mixed.size() != baseline.size()) return false;
  for (size_t i = 0; i < baseline.size(); ++i) {
    char c = hex_lower_or_mixed[i];
    if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
    if (c != baseline[i]) return false;
  }
  return true;
}

bool service_image_matches_upstream() {
  const std::wstring exe = registered_service_exe();
  if (exe.empty() || !std::filesystem::exists(exe)) return false;
  return upstream_baseline_equals(file_sha256_hex(exe));
}

int kill_other_processes() {
  int killed = 0;
  const DWORD self_pid = ::GetCurrentProcessId();
  HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return 0;

  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  if (::Process32FirstW(snap, &pe)) {
    do {
      if (pe.th32ProcessID == self_pid || pe.th32ProcessID <= 4) continue;
      HANDLE p = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                               FALSE, pe.th32ProcessID);
      if (!p) continue;
      WCHAR path[MAX_PATH * 2] = {};
      DWORD size = MAX_PATH * 2;
      if (::QueryFullProcessImageNameW(p, 0, path, &size) && size > 0) {
        const std::wstring exe(path, size);
        const std::wstring file =
            lower(std::filesystem::path(exe).filename().wstring());
        const std::wstring dir =
            lower(std::filesystem::path(exe).parent_path().wstring());
        const bool own = is_own_path(exe);
        const bool shared = is_shared_component_name(file);
        const bool mini_dir = dir.find(L"clash mini") != std::wstring::npos ||
                              dir.find(L"clash-mini") != std::wstring::npos;
        if (!own && (shared || mini_dir)) {
          if (::TerminateProcess(p, 1)) {
            ++killed;
            CWLOG_INFO(std::format("coexist: 已终止对方进程 pid={} path={}",
                                   static_cast<long long>(pe.th32ProcessID),
                                   utf::utf16_to_utf8(exe)));
            ::WaitForSingleObject(p, 3000);  // 等退出，避免内核残留占用端口
          } else {
            CWLOG_WARN(std::format("coexist: 终止对方进程失败 pid={} gle={}",
                                   static_cast<long long>(pe.th32ProcessID),
                                   ::GetLastError()));
          }
        }
      }
      ::CloseHandle(p);
    } while (::Process32NextW(snap, &pe));
  }
  ::CloseHandle(snap);
  return killed;
}

bool stop_foreign_service_if_running() {
  namespace sm = core::service_mode;
  if (!sm::is_service_installed() || !sm::is_service_running()) return false;
  if (service_image_is_own()) return false;  // 自家服务不动

  SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  SC_HANDLE svc = ::OpenServiceW(scm, sm::kServiceNameW,
                                 SERVICE_STOP | SERVICE_QUERY_STATUS);
  if (!svc) {
    ::CloseServiceHandle(scm);
    return false;
  }
  SERVICE_STATUS st{};
  const BOOL ok = ::ControlService(svc, SERVICE_CONTROL_STOP, &st);
  const DWORD gle = ::GetLastError();
  ::CloseServiceHandle(svc);
  ::CloseServiceHandle(scm);
  if (!ok && gle != ERROR_SERVICE_NOT_ACTIVE) {
    // 启动期无权限（非管理员）：不弹 UAC，留待 install_service 接管路径
    CWLOG_WARN("coexist: 对方服务停止失败（无权限?），留待 install_service 接管");
    return false;
  }
  // 等待进入 STOPPED（最多 8s × 250ms）
  for (int i = 0; i < 32; ++i) {
    if (!sm::is_service_running()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  CWLOG_INFO("coexist: 已停止注册指向 Clash Mini 路径的 clash_verge_service（互斥方针）");
  return true;
}

bool delete_service_registration() {
  namespace sm = core::service_mode;
  if (!sm::is_service_installed()) return true;

  SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
  if (scm) {
    SC_HANDLE svc = ::OpenServiceW(scm, sm::kServiceNameW, DELETE);
    if (svc) {
      const BOOL ok = ::DeleteService(svc);
      const DWORD gle = ::GetLastError();
      ::CloseServiceHandle(svc);
      ::CloseServiceHandle(scm);
      if (ok) {
        CWLOG_INFO("coexist: clash_verge_service 注册已删除（接管前置）");
        return true;
      }
      if (gle != ERROR_ACCESS_DENIED) {
        CWLOG_WARN(std::format("coexist: DeleteService 失败 gle={}", gle));
        return false;
      }
    } else {
      ::CloseServiceHandle(scm);
    }
  }
  // 非管理员 → runas 提权 sc.exe delete（UAC；接管路径本就带 UAC）
  if (!shell_runas(system32_sc_exe(),
                   std::wstring(L"delete ") + sm::kServiceNameW)) {
    CWLOG_WARN("coexist: sc.exe delete (runas) 失败或被用户取消");
    return false;
  }
  return true;
}

}  // namespace clashwin::system::coexist
