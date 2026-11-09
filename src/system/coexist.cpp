// ★ Mini/Win 互斥共存方针实现（← 总纲《移植基本准则.md》§5.2，用户 2026-11-09 改定）
// 取代旧「共存豁免 + refuse_service_mutation」设计（《Win和mini的差别.md》❌11/△3）。
// 纪律：按 exe 安装目录路径判定归属；不删对方磁盘文件；启动期禁止 UAC 弹窗
//       （无权限的停止/删除留待 install_service 的 UAC 路径）。
#include "system/coexist.h"

#include <windows.h>
#include <tlhelp32.h>

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
      L"mini-mihomo.exe",       L"clash-verge-service.exe",
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

}  // namespace

bool is_own_path(const std::wstring& exe_path) {
  if (exe_path.empty()) return false;
  const std::wstring p = lower(exe_path);
  const std::wstring roots[] = {lower(util::dirs::exe_dir()),
                                lower(util::dirs::app_home_dir())};
  for (const std::wstring& raw_root : roots) {
    if (raw_root.empty()) continue;
    std::wstring root = raw_root;
    if (root.back() != L'\\' && root.back() != L'/') root += L'\\';
    if (p.starts_with(root)) return true;
  }
  return false;
}

bool service_image_is_own() {
  SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) return false;
  SC_HANDLE svc = ::OpenServiceW(scm, core::service_mode::kServiceNameW,
                                 SERVICE_QUERY_CONFIG);
  if (!svc) {
    ::CloseServiceHandle(scm);
    return false;
  }
  std::vector<BYTE> buf(8192);
  auto cfg = reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(buf.data());
  DWORD needed = 0;
  const bool ok =
      ::QueryServiceConfigW(svc, cfg, static_cast<DWORD>(buf.size()), &needed) != FALSE;
  const std::wstring raw = ok && cfg->lpBinaryPathName ? cfg->lpBinaryPathName : L"";
  ::CloseServiceHandle(svc);
  ::CloseServiceHandle(scm);
  if (!ok || raw.empty()) return false;
  return is_own_path(image_exe_path(raw));
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
