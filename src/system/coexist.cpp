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
#include "system/guardian.h"
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

// 本进程是否以管理员身份运行：用 TokenElevation 判定（比 IsUserAnAdmin 更稳，
// 不受 shellapi.h 版本宏影响）。
bool is_elevated() {
  HANDLE token = nullptr;
  if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION elev{};
  DWORD size = 0;
  const bool ok = ::GetTokenInformation(token, TokenElevation, &elev,
                                       sizeof(elev), &size) != FALSE;
  ::CloseHandle(token);
  return ok && elev.TokenIsElevated != 0;
}

// 同步跑一段 PowerShell，返回其标准输出（用于日志/计数）；任何失败返回空串。
std::string run_powershell_capture(const std::wstring& script) {
  const std::wstring cmd =
      L"powershell.exe -NoProfile -NonInteractive -Command \"" + script + L"\"";
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE hRead = nullptr, hWrite = nullptr;
  if (!::CreatePipe(&hRead, &hWrite, &sa, 0)) return {};
  PROCESS_INFORMATION pi{};
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.hStdOutput = hWrite;
  si.hStdError = hWrite;
  si.dwFlags |= STARTF_USESTDHANDLES;
  const BOOL ok = ::CreateProcessW(nullptr, const_cast<wchar_t*>(cmd.c_str()),
                                   nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                   nullptr, nullptr, &si, &pi);
  if (hWrite) { ::CloseHandle(hWrite); hWrite = nullptr; }
  if (!ok) {
    if (hRead) ::CloseHandle(hRead);
    return {};
  }
  ::CloseHandle(pi.hThread);
  std::string out;
  char buf[1024];
  DWORD rd = 0;
  while (::ReadFile(hRead, buf, sizeof(buf) - 1, &rd, nullptr) && rd > 0) {
    out.append(buf, static_cast<size_t>(rd));
  }
  ::CloseHandle(hRead);
  ::WaitForSingleObject(pi.hProcess, 20000);
  DWORD code = 1;
  ::GetExitCodeProcess(pi.hProcess, &code);
  ::CloseHandle(pi.hProcess);
  (void)code;
  return out;
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
      // 早期按文件名粗判（szExeFile 无需先 OpenProcess 即可得）：共享组件名 ⇒ 对方进程。
      // 用于 OpenProcess 失败（对方以管理员提权运行）时仍能识别并明确告警，避免静默漏杀。
      const std::wstring fname_low =
          lower(std::filesystem::path(pe.szExeFile).filename().wstring());
      const bool fname_shared = is_shared_component_name(fname_low);
      HANDLE p = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                               FALSE, pe.th32ProcessID);
      if (!p) {
        if (fname_shared) {
          // 关键可见性：对方在 TUN 模式下以管理员身份运行，当前（非提权）上下文无权终止，
          // 会在提权路径（安装/TUN 接管/服务）重试。此前此处直接静默跳过，正是「两内核同跑」的根因。
          CWLOG_WARN(std::format(
              "coexist: 对方进程 pid={} ({}) 无法打开句柄（可能以管理员提权运行），"
              "当前上下文无权终止；将在提权路径（安装/TUN/服务）重试",
              static_cast<long long>(pe.th32ProcessID),
              utf::utf16_to_utf8(pe.szExeFile)));
        }
        continue;
      }
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
          // 互斥接管前先礼貌置位对方 v3.2.5+ 伴生看门狗的 IntentionalExit 事件，
          // 使其 0ms 同步退出而不误记崩溃取证日志或尝试反向拉起。
          (void)guardian::signal_foreign_guardian_exit(pe.th32ProcessID);
          if (::TerminateProcess(p, 1)) {
            ++killed;
            CWLOG_INFO(std::format("coexist: 已终止对方进程 pid={} path={}",
                                   static_cast<long long>(pe.th32ProcessID),
                                   utf::utf16_to_utf8(exe)));
            ::WaitForSingleObject(p, 3000);  // 等退出，避免内核残留占用端口
          } else {
            const DWORD gle = ::GetLastError();
            if (gle == ERROR_ACCESS_DENIED) {
              // 目标提权运行且 TerminateProcess 被拒：在提权上下文（如 TUN 接管）会再次执行本函数并成功。
              CWLOG_WARN(std::format(
                  "coexist: 终止对方进程 pid={} 被拒（ERROR_ACCESS_DENIED，对方提权运行）；"
                  "将在提权路径重试", static_cast<long long>(pe.th32ProcessID)));
            } else {
              CWLOG_WARN(std::format("coexist: 终止对方进程失败 pid={} gle={}",
                                     static_cast<long long>(pe.th32ProcessID), gle));
            }
          }
        }
      }
      ::CloseHandle(p);
    } while (::Process32NextW(snap, &pe));
  }
  ::CloseHandle(snap);
  return killed;
}

// ★ 杀对方规则补上的收尾：清理对方遗留的 Wintun/Mihomo 虚拟网卡（孤儿网卡）。
// 背景：对方（Clash Mini）若曾在 TUN 模式下被强杀，其 mihomo 来不及优雅拆除 TUN，
//       会留下一张孤儿 Wintun 网卡 + 全流量路由；本方随后开 TUN 时若同名网卡已存在，
//       内核复用脏孤儿失败 → 全流量接管断网（即 0.2.3 的回归：Mini 在 TUN 被装 Win 后切 TUN 断网）。
// 安全边界：共存铁律保证对方已死，清理的一定是「无活进程撑着的」孤儿网卡；
//           不碰对方任何磁盘文件，符合「不删对方磁盘文件」方针。
// 触发时机：仅在已提权时执行（TUN 必走提权服务；启动期未提权则跳过，
//           与「启动期禁止 UAC 弹窗」方针一致；TUN 接管路径必提权，届时自然清掉）。
void remove_orphan_wintun_adapters() {
  if (!is_elevated()) {
    CWLOG_INFO(
        "coexist: 当前未提权，跳过 Wintun 孤儿网卡清理（待 TUN/服务接管路径提权后执行）");
    return;
  }
  // -IncludeHidden：孤儿 Wintun 卡常为隐藏适配器，普通 Get-NetAdapter 看不到，会漏清。
  // 仅匹配 Wintun/Mihomo 虚拟网卡，不误伤物理网卡。
  const std::wstring script =
      L"(Get-NetAdapter -IncludeHidden -ErrorAction SilentlyContinue | "
      L"Where-Object { $_.InterfaceDescription -match 'Wintun|Mihomo' } | "
      L"ForEach-Object { $_ | Remove-NetAdapter -Confirm:$false -Force -ErrorAction SilentlyContinue; $true }).Count";
  const std::string out = run_powershell_capture(script);
  int n = 0;
  try {
    const size_t p = out.find_first_of("0123456789");
    if (p != std::string::npos) n = std::stoi(out.substr(p));
  } catch (...) { n = 0; }
  if (n > 0) {
    CWLOG_INFO(std::format(
        "coexist: 已清理 {} 张对方遗留的 Wintun/Mihomo 孤儿网卡", n));
  } else {
    CWLOG_INFO("coexist: 未发现需清理的 Wintun/Mihomo 孤儿网卡");
  }
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
