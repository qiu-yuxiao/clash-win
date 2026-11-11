// ★ Windows 双层防误杀与零断流自愈守护实现（← Clash Mini src-tauri/src/utils/guardian.rs，v3.2.5 BUG-316）
#include "system/guardian.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <format>
#include <mutex>

#include "util/log.h"
#include "util/utf.h"

namespace clashwin::system::guardian {

namespace {

std::mutex g_guardian_mutex;
HANDLE g_exit_event = nullptr;
HANDLE g_lightweight_event = nullptr;
SentinelWindowPair g_resident_sentinel{};

std::vector<std::wstring> current_command_line_args() {
  std::vector<std::wstring> out;
  int argc = 0;
  LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  if (!argv) return out;
  out.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    if (argv[i]) out.emplace_back(argv[i]);
  }
  ::LocalFree(argv);
  return out;
}

std::optional<uint32_t> parse_u32_w(const std::wstring& s) {
  if (s.empty()) return std::nullopt;
  wchar_t* end = nullptr;
  errno = 0;
  const unsigned long v = std::wcstoul(s.c_str(), &end, 10);
  if (errno != 0 || end == s.c_str() || *end != L'\0' || v > 0xFFFFFFFFUL) {
    return std::nullopt;
  }
  return static_cast<uint32_t>(v);
}

std::wstring lower_w(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return s;
}

std::wstring quote_cmd_arg(const std::wstring& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    return arg;
  }
  std::wstring out = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashes;
    } else if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
      backslashes = 0;
    } else {
      out.append(backslashes, L'\\');
      out.push_back(c);
      backslashes = 0;
    }
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

uint64_t query_process_uptime_secs(HANDLE h_proc) {
  FILETIME create_ft{}, exit_ft{}, kernel_ft{}, user_ft{};
  if (!::GetProcessTimes(h_proc, &create_ft, &exit_ft, &kernel_ft, &user_ft)) {
    return 0;
  }
  const auto to_u64 = [](const FILETIME& ft) -> uint64_t {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) |
           static_cast<uint64_t>(ft.dwLowDateTime);
  };
  const uint64_t start = to_u64(create_ft);
  uint64_t end = to_u64(exit_ft);
  if (end == 0) {
    FILETIME now_ft{};
    ::GetSystemTimeAsFileTime(&now_ft);
    end = to_u64(now_ft);
  }
  if (end <= start) return 0;
  return (end - start) / 10'000'000ULL;
}

// 扫描系统进程：若发现 NSIS 安装器/卸载器或对端 Clash Mini 正在运行，立即让位放弃拉起
// （← guardian.rs:524-563 + §5.2 互斥共存让位：用户主动启动 Clash Mini 时不反向抢跑）
bool is_installer_or_peer_running() {
  HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;

  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;
  if (::Process32FirstW(snap, &entry)) {
    do {
      const std::wstring name = lower_w(entry.szExeFile);
      if (name == L"uninstall.exe" || name == L"un_a.exe" ||
          name == L"clash-mini.exe" || name == L"clash mini.exe" ||
          name.starts_with(L"clash.mini_") ||
          name.starts_with(L"clash-mini-setup") ||
          name.starts_with(L"clashwin_") ||
          name.starts_with(L"clashwin-") ||
          (name.find(L"setup") != std::wstring::npos &&
           (name.find(L"clash") != std::wstring::npos ||
            name.find(L"mini") != std::wstring::npos))) {
        found = true;
        break;
      }
    } while (::Process32NextW(snap, &entry));
  }
  ::CloseHandle(snap);
  return found;
}

// 检测单实例端口是否空闲（与 SingletonServer 一致采用 SO_EXCLUSIVEADDRUSE，测完即关）
bool is_singleton_port_free(uint16_t port) {
  WSADATA wsa{};
  if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) {
    ::WSACleanup();
    return false;
  }
  int opt = 1;
  ::setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  const bool free_ok =
      (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
  ::closesocket(s);
  ::WSACleanup();
  return free_ok;
}

int run_guardian_loop(const GuardianArgs& args) {
  // 1. 打开主进程句柄（仅请求同步等待与基本信息查询最小权限）
  HANDLE h_parent = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                  FALSE, args.parent_pid);
  if (!h_parent) {
    return 0;
  }

  // 2. 打开/创建命名事件句柄（手动重置）
  const std::wstring exit_ev = intentional_exit_event_name(args.parent_pid);
  const std::wstring lw_ev = lightweight_mode_event_name(args.parent_pid);
  HANDLE h_exit = ::CreateEventW(nullptr, TRUE, FALSE, exit_ev.c_str());
  HANDLE h_lw = ::CreateEventW(nullptr, TRUE, FALSE, lw_ev.c_str());
  if (!h_exit || !h_lw) {
    if (h_exit) ::CloseHandle(h_exit);
    if (h_lw) ::CloseHandle(h_lw);
    ::CloseHandle(h_parent);
    return 0;
  }

  // 主动收缩工作集，使伴生守护进程常驻私有工作集收敛至约 1 MB 左右
  ::SetProcessWorkingSetSize(::GetCurrentProcess(), static_cast<SIZE_T>(-1),
                             static_cast<SIZE_T>(-1));

  // 3. 阻塞等待（0% CPU）：[0] 主进程终止，或 [1] 主进程主动发出正常退出通知
  HANDLE handles[2] = {h_parent, h_exit};
  const DWORD wait = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);

  if (wait == WAIT_OBJECT_0 + 1) {
    // 正常退出信号触发，0ms 同步退出
    ::CloseHandle(h_lw);
    ::CloseHandle(h_exit);
    ::CloseHandle(h_parent);
    return 0;
  }

  if (wait == WAIT_OBJECT_0) {
    // 双重确认：若主进程在终止瞬间已置位主动退出事件，仍按正常退出处理
    if (::WaitForSingleObject(h_exit, 0) == WAIT_OBJECT_0) {
      ::CloseHandle(h_lw);
      ::CloseHandle(h_exit);
      ::CloseHandle(h_parent);
      return 0;
    }

    DWORD exit_code = 0;
    ::GetExitCodeProcess(h_parent, &exit_code);
    const uint64_t uptime_secs = query_process_uptime_secs(h_parent);
    const bool in_lightweight = (::WaitForSingleObject(h_lw, 0) == WAIT_OBJECT_0);

    ::CloseHandle(h_lw);
    ::CloseHandle(h_exit);
    ::CloseHandle(h_parent);

    // 4. 同步追加崩溃/强杀取证日志（补全强杀无遗言盲区）
    append_forensic_log(
        args.log_dir, "ERROR",
        std::format(
            "[Guardian] 检测到主进程 (PID={}) 非正常终止！退出码={}，存活时长={}s，轻量模式={}，自愈代数={}",
            args.parent_pid, format_exit_code_diagnosis(exit_code), uptime_secs,
            in_lightweight, args.generation));

    // 仅在后台轻量/托盘隐身态执行无感自愈拉起；前台可见态异常退出只留痕不弹鬼窗
    if (!in_lightweight) {
      return 0;
    }

    const auto next_gen_opt = compute_next_generation(args.generation, uptime_secs);
    if (!next_gen_opt.has_value()) {
      append_forensic_log(
          args.log_dir, "ERROR",
          std::format(
              "[Guardian] 短时间内连续快速异常退出已达上限 ({} 次)，停止自动拉起以防死循环",
              kMaxRapidRespawnGen));
      return 0;
    }
    const uint32_t next_gen = *next_gen_opt;

    // 5. 安全缓冲期：避开卸载器删盘、安装器升级、对端互斥接管或系统注销瞬态
    ::Sleep(kRespawnSafetyDelayMs);

    if (::GetSystemMetrics(SM_SHUTTINGDOWN) != 0) {
      append_forensic_log(args.log_dir, "INFO",
                          "[Guardian] 检测到系统正在关机/注销，放弃拉起");
      return 0;
    }

    wchar_t exe_buf[MAX_PATH * 2] = {};
    const DWORD exe_len = ::GetModuleFileNameW(nullptr, exe_buf, MAX_PATH * 2);
    if (exe_len == 0 || exe_len >= MAX_PATH * 2) {
      return 0;
    }
    const std::filesystem::path exe_path(std::wstring(exe_buf, exe_len));
    std::error_code ec;
    if (!std::filesystem::exists(exe_path, ec)) {
      append_forensic_log(args.log_dir, "INFO",
                          "[Guardian] 主程序文件已不存在（可能已被卸载），放弃拉起");
      return 0;
    }

    if (is_installer_or_peer_running()) {
      append_forensic_log(
          args.log_dir, "INFO",
          "[Guardian] 检测到安装/卸载程序或对端客户端正在运行，让位放弃拉起");
      return 0;
    }

    if (!is_singleton_port_free(args.singleton_port)) {
      append_forensic_log(
          args.log_dir, "INFO",
          std::format("[Guardian] 单实例端口 {} 已被新实例占用，无需重复拉起",
                      args.singleton_port));
      return 0;
    }

    // 6. 静默自愈拉起主进程（--guardian-respawn 直接静默进轻量托盘并复用存活服务内核）
    std::wstring cmd =
        quote_cmd_arg(exe_path.wstring()) +
        std::format(
            L" --guardian-respawn --guardian-gen {} --prev-pid {} --prev-exit {}",
            next_gen, args.parent_pid, exit_code);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
      append_forensic_log(
          args.log_dir, "WARN",
          std::format(
              "[Guardian] 已静默自愈拉起主进程恢复托盘与后台防断流守护 (新 PID={}, 代数={}/{})",
              pi.dwProcessId, next_gen, kMaxRapidRespawnGen));
      ::CloseHandle(pi.hProcess);
      ::CloseHandle(pi.hThread);
    } else {
      append_forensic_log(
          args.log_dir, "ERROR",
          std::format("[Guardian] 自愈拉起主进程失败: gle={}", ::GetLastError()));
    }
    return 0;
  }

  ::CloseHandle(h_lw);
  ::CloseHandle(h_exit);
  ::CloseHandle(h_parent);
  return 0;
}

}  // namespace

std::wstring intentional_exit_event_name(uint32_t pid) {
  return std::format(L"Local\\ClashWinIntentionalExit_{}", pid);
}

std::wstring lightweight_mode_event_name(uint32_t pid) {
  return std::format(L"Local\\ClashWinInLightweight_{}", pid);
}

std::optional<GuardianArgs> parse_guardian_args(
    const std::vector<std::wstring>& args) {
  bool is_guardian = false;
  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == L"--guardian") {
      is_guardian = true;
      break;
    }
  }
  if (!is_guardian) return std::nullopt;

  std::optional<uint32_t> parent_pid;
  std::optional<std::filesystem::path> log_dir;
  uint16_t singleton_port = 33335;
  uint32_t generation = 0;

  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == L"--parent-pid" && i + 1 < args.size()) {
      parent_pid = parse_u32_w(args[++i]);
    } else if (args[i] == L"--log-dir" && i + 1 < args.size()) {
      log_dir = std::filesystem::path(args[++i]);
    } else if (args[i] == L"--singleton-port" && i + 1 < args.size()) {
      if (auto p = parse_u32_w(args[++i]); p && *p > 0 && *p < 65536) {
        singleton_port = static_cast<uint16_t>(*p);
      }
    } else if (args[i] == L"--gen" && i + 1 < args.size()) {
      if (auto g = parse_u32_w(args[++i])) {
        generation = *g;
      }
    }
  }

  if (!parent_pid.has_value() || *parent_pid == 0 || !log_dir.has_value() ||
      log_dir->empty()) {
    return std::nullopt;
  }
  return GuardianArgs{
      .parent_pid = *parent_pid,
      .log_dir = *log_dir,
      .singleton_port = singleton_port,
      .generation = generation,
  };
}

std::optional<RespawnInfo> parse_respawn_info(
    const std::vector<std::wstring>& args) {
  if (!is_guardian_respawn_args(args)) return std::nullopt;

  RespawnInfo info{};
  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == L"--guardian-gen" && i + 1 < args.size()) {
      if (auto g = parse_u32_w(args[++i])) info.generation = *g;
    } else if (args[i] == L"--prev-pid" && i + 1 < args.size()) {
      if (auto p = parse_u32_w(args[++i])) info.prev_pid = *p;
    } else if (args[i] == L"--prev-exit" && i + 1 < args.size()) {
      if (auto e = parse_u32_w(args[++i])) info.prev_exit = *e;
    }
  }
  return info;
}

bool is_guardian_respawn_args(const std::vector<std::wstring>& args) {
  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == L"--guardian-respawn") return true;
  }
  return false;
}

bool is_guardian_respawn() {
  return is_guardian_respawn_args(current_command_line_args());
}

std::optional<uint32_t> compute_next_generation(uint32_t current_gen,
                                                uint64_t uptime_secs) {
  const uint32_t next =
      (uptime_secs >= kStableUptimeResetSecs) ? 1u : (current_gen + 1u);
  if (next > kMaxRapidRespawnGen) return std::nullopt;
  return next;
}

std::string format_exit_code_diagnosis(uint32_t code) {
  switch (code) {
    case 0:
      return "0 (STATUS_SUCCESS / 未经正常退出通道直接终止)";
    case 1:
      return "1 (TerminateProcess 外部强杀或未捕获异常终止)";
    case 99:
      return "99 (0x63: 命中 360加速球/第三方内存清理 TerminateProcess 强杀特征码)";
    case 0xC0000005u:
      return "0xC0000005 (STATUS_ACCESS_VIOLATION 内存访问违例)";
    case 0xC00000FDu:
      return "0xC00000FD (STATUS_STACK_OVERFLOW 栈溢出)";
    case 0xC0000409u:
      return "0xC0000409 (STATUS_STACK_BUFFER_OVERRUN / abort 快速失败)";
    case 0xC0000374u:
      return "0xC0000374 (STATUS_HEAP_CORRUPTION 堆损坏)";
    case 0x40010004u:
      return "0x40010004 (DBG_TERMINATE_PROCESS 调试器/任务管理器终止)";
    default:
      return std::format("{} (0x{:08X})", code, code);
  }
}

void append_forensic_log(const std::filesystem::path& log_dir,
                         const char* level,
                         const std::string& message) {
  std::error_code ec;
  std::filesystem::create_directories(log_dir, ec);
  const std::filesystem::path log_file = log_dir / L"clashwin.log";

  HANDLE h = ::CreateFileW(log_file.c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;

  SYSTEMTIME t{};
  ::GetLocalTime(&t);
  char header[64] = {};
  std::snprintf(header, sizeof(header), "[%04u-%02u-%02u %02u:%02u:%02u.%03u][%s] ",
                t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                t.wMilliseconds, level ? level : "INFO");
  const std::string line = std::string(header) + "[System] " + message + "\r\n";
  DWORD written = 0;
  ::WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
  ::FlushFileBuffers(h);
  ::CloseHandle(h);
}

std::optional<SentinelWindowPair> create_sentinel_window_pair(
    const wchar_t* class_name) {
  if (!class_name || !*class_name) return std::nullopt;

  HINSTANCE hinst = ::GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.lpfnWndProc = ::DefWindowProcW;
  wc.hInstance = hinst;
  wc.lpszClassName = class_name;
  if (::RegisterClassExW(&wc) == 0) {
    const DWORD gle = ::GetLastError();
    if (gle != ERROR_CLASS_ALREADY_EXISTS) {
      CWLOG_WARN(std::format(
          "[Guardian] 注册防清理哨兵窗口类失败: gle={}", gle));
      return std::nullopt;
    }
  }

  // 1. 隐藏 owner 窗口（不带 WS_VISIBLE）：用于持有 sentinel，使其天然从任务栏和 Alt+Tab 隐身
  HWND owner_hwnd = ::CreateWindowExW(
      0, class_name, L"ClashWinResidentOwner", WS_POPUP, -32000, -32000, 1, 1,
      nullptr, nullptr, hinst, nullptr);
  if (!owner_hwnd) {
    CWLOG_WARN(std::format(
        "[Guardian] 创建哨兵 owner 窗口失败: gle={}", ::GetLastError()));
    return std::nullopt;
  }

  // 2. 可见但完全透明（alpha=0）的 1×1 屏幕外被拥有窗口：
  //    - 含 WS_VISIBLE → IsWindowVisible(sentinel) != 0
  //    - 不含 WS_EX_TOOLWINDOW / WS_EX_TOPMOST → 不被 360 判为悬浮球/工具条跳过
  //    - 被隐藏 owner 拥有且不含 WS_EX_APPWINDOW → 任务栏与 Alt+Tab 均完全不可见
  HWND sentinel_hwnd = ::CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE, class_name,
      L"Clash Win", WS_POPUP | WS_VISIBLE, -32000, -32000, 1, 1, owner_hwnd,
      nullptr, hinst, nullptr);
  if (!sentinel_hwnd) {
    CWLOG_WARN(std::format(
        "[Guardian] 创建哨兵可见窗口失败: gle={}", ::GetLastError()));
    ::DestroyWindow(owner_hwnd);
    return std::nullopt;
  }

  (void)::SetLayeredWindowAttributes(sentinel_hwnd, 0, 0, LWA_ALPHA);
  return SentinelWindowPair{
      .owner_hwnd = owner_hwnd,
      .sentinel_hwnd = sentinel_hwnd,
  };
}

void destroy_sentinel_window_pair(SentinelWindowPair& pair) {
  if (pair.sentinel_hwnd) {
    ::DestroyWindow(static_cast<HWND>(pair.sentinel_hwnd));
    pair.sentinel_hwnd = nullptr;
  }
  if (pair.owner_hwnd) {
    ::DestroyWindow(static_cast<HWND>(pair.owner_hwnd));
    pair.owner_hwnd = nullptr;
  }
}

void init_resident_sentinel_window() {
  std::lock_guard<std::mutex> lock(g_guardian_mutex);
  if (g_resident_sentinel.sentinel_hwnd) return;
  if (auto pair = create_sentinel_window_pair(L"ClashWinResidentSentinelClass")) {
    g_resident_sentinel = *pair;
    CWLOG_INFO(std::format(
        "[Guardian] 已创建零渲染常驻防清理哨兵窗口 (owner=0x{:X}, sentinel=0x{:X})",
        reinterpret_cast<uintptr_t>(g_resident_sentinel.owner_hwnd),
        reinterpret_cast<uintptr_t>(g_resident_sentinel.sentinel_hwnd)));
  }
}

void destroy_resident_sentinel_window() {
  std::lock_guard<std::mutex> lock(g_guardian_mutex);
  destroy_sentinel_window_pair(g_resident_sentinel);
}

bool ensure_guardian_events(bool initial_lightweight) {
  std::lock_guard<std::mutex> lock(g_guardian_mutex);
  if (g_exit_event && g_lightweight_event) {
    if (initial_lightweight) {
      ::SetEvent(g_lightweight_event);
    }
    return true;
  }
  const uint32_t pid = ::GetCurrentProcessId();
  const std::wstring exit_ev = intentional_exit_event_name(pid);
  const std::wstring lw_ev = lightweight_mode_event_name(pid);

  HANDLE h_exit = ::CreateEventW(nullptr, TRUE, FALSE, exit_ev.c_str());
  HANDLE h_lw =
      ::CreateEventW(nullptr, TRUE, initial_lightweight ? TRUE : FALSE, lw_ev.c_str());
  if (!h_exit || !h_lw) {
    CWLOG_WARN(std::format(
        "[Guardian] 创建内核事件失败: gle={}", ::GetLastError()));
    if (h_exit) ::CloseHandle(h_exit);
    if (h_lw) ::CloseHandle(h_lw);
    return false;
  }
  g_exit_event = h_exit;
  g_lightweight_event = h_lw;
  return true;
}

void start_guardian_companion(const std::filesystem::path& log_dir,
                              uint16_t singleton_port,
                              bool initial_lightweight) {
  if (!ensure_guardian_events(initial_lightweight)) {
    return;
  }

  if (const char* disable = std::getenv("CLASHWIN_DISABLE_GUARDIAN");
      disable && std::string_view(disable) == "1") {
    CWLOG_INFO(
        "[Guardian] 已通过环境变量 CLASHWIN_DISABLE_GUARDIAN=1 禁用伴生看门狗");
    return;
  }

  const auto args = current_command_line_args();
  uint32_t current_gen = 0;
  if (const auto respawn = parse_respawn_info(args)) {
    current_gen = respawn->generation;
    CWLOG_WARN(std::format(
        "[Guardian] 本实例由伴生看门狗自动自愈拉起 (gen={}, 前序 PID={}, 前序退出码={})",
        respawn->generation, respawn->prev_pid,
        format_exit_code_diagnosis(respawn->prev_exit)));
  }

  wchar_t exe_buf[MAX_PATH * 2] = {};
  const DWORD exe_len = ::GetModuleFileNameW(nullptr, exe_buf, MAX_PATH * 2);
  if (exe_len == 0 || exe_len >= MAX_PATH * 2) {
    CWLOG_WARN("[Guardian] 获取当前可执行文件路径失败");
    return;
  }

  const uint32_t pid = ::GetCurrentProcessId();
  std::wstring cmd =
      quote_cmd_arg(std::wstring(exe_buf, exe_len)) +
      std::format(L" --guardian --parent-pid {} --log-dir {} --singleton-port {} --gen {}",
                  pid, quote_cmd_arg(log_dir.wstring()), singleton_port,
                  current_gen);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  // 优先尝试脱离当前 Job Object，防止外部按进程组清理时将伴生进程一并连坐强杀；
  // 若宿主 Job 不允许 breakaway（ERROR_ACCESS_DENIED），自动回退普通无窗口拉起。
  BOOL spawned = ::CreateProcessW(
      nullptr, cmd.data(), nullptr, nullptr, FALSE,
      CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, nullptr, nullptr, &si, &pi);
  if (!spawned) {
    std::wstring cmd_fallback = cmd;
    spawned = ::CreateProcessW(nullptr, cmd_fallback.data(), nullptr, nullptr,
                               FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                               &pi);
  }

  if (spawned) {
    CWLOG_INFO(std::format(
        "[Guardian] 伴生看门狗进程已就绪 (guardian_pid={}, parent_pid={}, gen={})",
        pi.dwProcessId, pid, current_gen));
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
  } else {
    CWLOG_WARN(std::format(
        "[Guardian] 拉起伴生看门狗进程失败: gle={}", ::GetLastError()));
  }
}

void set_lightweight_mode_signal(bool in_lightweight) {
  std::lock_guard<std::mutex> lock(g_guardian_mutex);
  if (!g_lightweight_event) return;
  if (in_lightweight) {
    ::SetEvent(g_lightweight_event);
  } else {
    ::ResetEvent(g_lightweight_event);
  }
}

void notify_intentional_exit() {
  std::lock_guard<std::mutex> lock(g_guardian_mutex);
  if (g_exit_event) {
    ::SetEvent(g_exit_event);
  }
}

bool signal_foreign_guardian_exit(uint32_t target_pid) {
  if (target_pid == 0) return false;
  bool signaled_any = false;
  const std::wstring names[2] = {
      std::format(L"Local\\ClashMiniIntentionalExit_{}", target_pid),
      std::format(L"Local\\ClashWinIntentionalExit_{}", target_pid),
  };
  for (const auto& name : names) {
    HANDLE h = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (h) {
      if (::SetEvent(h)) signaled_any = true;
      ::CloseHandle(h);
    }
  }
  return signaled_any;
}

std::optional<int> maybe_run_guardian_mode() {
  const auto args = current_command_line_args();
  bool has_guardian_flag = false;
  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == L"--guardian") {
      has_guardian_flag = true;
      break;
    }
  }
  if (!has_guardian_flag) return std::nullopt;

  const auto parsed = parse_guardian_args(args);
  if (!parsed.has_value()) {
    return 2;
  }
  return run_guardian_loop(*parsed);
}

}  // namespace clashwin::system::guardian
