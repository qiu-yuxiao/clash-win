// ★ Windows 轻量模式防强杀四重闭环守护实现（← Clash Mini src-tauri/src/process/guardian.rs，v3.2.5 BUG-316 + BUG-363）
#include "system/guardian.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <format>
#include <mutex>
#include <thread>

#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"

namespace clashwin::system::guardian {

namespace {

std::mutex g_guardian_mutex;
HANDLE g_exit_event = nullptr;
HANDLE g_lightweight_event = nullptr;
std::atomic<bool> g_supervisor_started{false};
SentinelWindowPair g_resident_sentinel{};

std::wstring current_module_path() {
  wchar_t exe_buf[MAX_PATH * 2] = {};
  const DWORD exe_len = ::GetModuleFileNameW(nullptr, exe_buf, MAX_PATH * 2);
  if (exe_len == 0 || exe_len >= MAX_PATH * 2) {
    return {};
  }
  return std::wstring(exe_buf, exe_len);
}

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

uint64_t filetime_to_u64(const FILETIME& ft) {
  return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) |
         static_cast<uint64_t>(ft.dwLowDateTime);
}

uint64_t file_size_from_attr(const WIN32_FILE_ATTRIBUTE_DATA& a) {
  return (static_cast<uint64_t>(a.nFileSizeHigh) << 32) |
         static_cast<uint64_t>(a.nFileSizeLow);
}

bool is_guardian_binary_up_to_date(const WIN32_FILE_ATTRIBUTE_DATA& src_attr,
                                   const std::filesystem::path& dst_path) {
  WIN32_FILE_ATTRIBUTE_DATA dst_attr{};
  if (!::GetFileAttributesExW(dst_path.c_str(), GetFileExInfoStandard, &dst_attr)) {
    return false;
  }
  if ((dst_attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return false;
  }
  if (file_size_from_attr(dst_attr) != file_size_from_attr(src_attr)) {
    return false;
  }
  const uint64_t t_src = filetime_to_u64(src_attr.ftLastWriteTime);
  const uint64_t t_dst = filetime_to_u64(dst_attr.ftLastWriteTime);
  const uint64_t diff = (t_src >= t_dst) ? (t_src - t_dst) : (t_dst - t_src);
  return diff <= 20'000'000ULL;  // <= 2s (100ns ticks)
}

void cleanup_stale_guard_staging(const std::filesystem::path& guard_dir) {
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(guard_dir, ec)) {
    if (ec) break;
    const std::wstring name = lower_w(entry.path().filename().wstring());
    if (name.starts_with(L"clashwin-guard.staging.")) {
      std::error_code rm_ec;
      std::filesystem::remove(entry.path(), rm_ec);
    }
  }
}

uint64_t query_process_uptime_secs(HANDLE h_proc) {
  FILETIME create_ft{}, exit_ft{}, kernel_ft{}, user_ft{};
  if (!::GetProcessTimes(h_proc, &create_ft, &exit_ft, &kernel_ft, &user_ft)) {
    return 0;
  }
  const uint64_t start = filetime_to_u64(create_ft);
  uint64_t end = filetime_to_u64(exit_ft);
  if (end == 0) {
    FILETIME now_ft{};
    ::GetSystemTimeAsFileTime(&now_ft);
    end = filetime_to_u64(now_ft);
  }
  if (end <= start) return 0;
  return (end - start) / 10'000'000ULL;
}

bool is_system_shutting_down() {
  return ::GetSystemMetrics(SM_SHUTTINGDOWN) != 0;
}

bool is_exit_event_signaled(HANDLE exit_event) {
  return exit_event != nullptr &&
         ::WaitForSingleObject(exit_event, 0) == WAIT_OBJECT_0;
}

// 在指定毫秒内等待主动退出事件；若收到退出事件或系统正在关机则立即返回 true（0ms 唤醒）
bool wait_exit_or_timeout(HANDLE exit_event, uint64_t timeout_ms) {
  const DWORD clamped = static_cast<DWORD>(
      std::min<uint64_t>(timeout_ms, static_cast<uint64_t>(INFINITE - 1)));
  if (exit_event != nullptr) {
    if (::WaitForSingleObject(exit_event, clamped) == WAIT_OBJECT_0) {
      return true;
    }
  } else {
    ::Sleep(clamped);
  }
  return is_exit_event_signaled(exit_event) || is_system_shutting_down();
}

// 扫描系统进程：若发现 NSIS 安装器/卸载器或对端 Clash Mini 正在运行，立即让位放弃拉起
// （← guardian.rs:318-335 + §5.2 互斥共存让位：用户主动启动 Clash Mini 时不反向抢跑）
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
          name == L"au_.exe" || name.find(L"un_a.exe") != std::wstring::npos ||
          name == L"clash-mini.exe" || name == L"clash mini.exe" ||
          name == L"clash-mini-guard.exe" || name == L"clash-mini-guard-alt.exe" ||
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

LRESULT CALLBACK sentinel_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCHITTEST) {
    return HTTRANSPARENT;
  }
  return ::DefWindowProcW(hwnd, msg, wp, lp);
}

POINT query_primary_workarea_origin() {
  RECT rc{};
  if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0) != FALSE) {
    return POINT{rc.left, rc.top};
  }
  return POINT{0, 0};
}

void pump_pending_window_messages() {
  MSG msg{};
  while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != FALSE) {
    ::TranslateMessage(&msg);
    ::DispatchMessageW(&msg);
  }
}

// 阻塞等待父进程退出或主动退出事件，同时按需响应哨兵窗口消息（平时 0% CPU 内核态挂起，IsHungAppWindow 恒 FALSE）
DWORD wait_parent_with_message_pump(HANDLE h_parent, HANDLE h_exit) {
  HANDLE handles[2] = {h_parent, h_exit};
  const DWORD count = h_exit ? 2u : 1u;
  for (;;) {
    const DWORD res = ::MsgWaitForMultipleObjects(
        count, handles, FALSE, INFINITE, QS_ALLINPUT);
    if (res == WAIT_OBJECT_0 + count) {
      pump_pending_window_messages();
    } else {
      return res;
    }
  }
}

int run_guardian_loop(const GuardianArgs& args) {
  // 1. 打开主进程句柄（仅请求同步等待与基本信息查询最小权限）
  HANDLE h_parent = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                  FALSE, args.parent_pid);
  if (!h_parent) {
    return 0;
  }

  // 2. 挂载伴生看门狗自身的在屏工作区哨兵窗口对，防止看门狗自身被第三方清理工具判为无窗口残留进程单杀
  auto guard_sentinel =
      create_sentinel_window_pair(L"ClashWinGuardHost", L"Clash Win Guard Host");
  pump_pending_window_messages();

  // 3. 打开/创建命名事件句柄（手动重置）
  const std::wstring exit_ev = intentional_exit_event_name(args.parent_pid);
  const std::wstring lw_ev = lightweight_mode_event_name(args.parent_pid);
  HANDLE h_exit = ::OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, exit_ev.c_str());
  if (!h_exit) {
    h_exit = ::CreateEventW(nullptr, TRUE, FALSE, exit_ev.c_str());
  }
  HANDLE h_lw = ::OpenEventW(SYNCHRONIZE, FALSE, lw_ev.c_str());
  if (!h_lw) {
    h_lw = ::CreateEventW(nullptr, TRUE, FALSE, lw_ev.c_str());
  }

  // 主动收缩工作集，使伴生守护进程常驻私有工作集收敛至约 1 MB 左右
  ::SetProcessWorkingSetSize(::GetCurrentProcess(), static_cast<SIZE_T>(-1),
                             static_cast<SIZE_T>(-1));

  // 4. 阻塞等待（0% CPU）并按需排空哨兵窗消息队列
  const DWORD wait = wait_parent_with_message_pump(h_parent, h_exit);
  if (guard_sentinel.has_value()) {
    destroy_sentinel_window_pair(*guard_sentinel);
  }

  const bool intentional =
      h_exit != nullptr &&
      (wait == WAIT_OBJECT_0 + 1 ||
       ::WaitForSingleObject(h_exit, 0) == WAIT_OBJECT_0);
  if (intentional || wait != WAIT_OBJECT_0 || is_system_shutting_down()) {
    if (h_lw) ::CloseHandle(h_lw);
    if (h_exit) ::CloseHandle(h_exit);
    ::CloseHandle(h_parent);
    return 0;
  }

  DWORD exit_code = 0;
  ::GetExitCodeProcess(h_parent, &exit_code);
  const uint64_t uptime_secs = query_process_uptime_secs(h_parent);
  const bool in_lightweight =
      h_lw != nullptr && (::WaitForSingleObject(h_lw, 0) == WAIT_OBJECT_0);

  if (h_lw) ::CloseHandle(h_lw);
  if (h_exit) ::CloseHandle(h_exit);
  ::CloseHandle(h_parent);

  // 5. 同步追加崩溃/强杀取证日志（补全强杀无遗言盲区）
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
            "[Guardian] 主进程在 {} 秒内连续快速异常退出已达上限 ({} 次)，停止自动拉起以防死循环",
            kStableUptimeResetSecs, kMaxRapidRespawnGen));
    return 0;
  }
  const uint32_t next_gen = *next_gen_opt;

  // 6. 安全缓冲期：避开卸载器删盘、安装器升级、对端互斥接管或系统注销瞬态
  ::Sleep(kRespawnSafetyDelayMs);

  if (is_system_shutting_down()) {
    append_forensic_log(args.log_dir, "INFO",
                        "[Guardian] 检测到系统正在关机/注销，放弃拉起");
    return 0;
  }

  std::filesystem::path target_exe = args.target_exe;
  if (target_exe.empty()) {
    const std::wstring cur = current_module_path();
    if (cur.empty()) return 0;
    target_exe = std::filesystem::path(cur);
  }
  std::error_code ec;
  if (!std::filesystem::exists(target_exe, ec)) {
    append_forensic_log(
        args.log_dir, "INFO",
        std::format("[Guardian] 目标主程序不存在 ({})，放弃拉起",
                    utf::utf16_to_utf8(target_exe.wstring())));
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

  // 7. 静默自愈拉起目标主进程（--guardian-respawn 直接静默进轻量托盘并复用存活服务内核）
  std::wstring cmd =
      quote_cmd_arg(target_exe.wstring()) +
      std::format(
          L" --guardian-respawn --guardian-gen {} --prev-pid {} --prev-exit {}",
          next_gen, args.parent_pid, exit_code);
  const std::filesystem::path work_dir = target_exe.parent_path();
  const wchar_t* work_dir_ptr =
      (!work_dir.empty() && std::filesystem::is_directory(work_dir, ec))
          ? work_dir.c_str()
          : nullptr;

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, work_dir_ptr, &si, &pi)) {
    append_forensic_log(
        args.log_dir, "WARN",
        std::format(
            "[Guardian] 已静默自愈拉起主进程恢复托盘与后台防断流守护 (新 PID={}, 代数={}/{}, target={})",
            pi.dwProcessId, next_gen, kMaxRapidRespawnGen,
            utf::utf16_to_utf8(target_exe.wstring())));
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
  } else {
    append_forensic_log(
        args.log_dir, "ERROR",
        std::format("[Guardian] 自愈拉起主进程失败: gle={}", ::GetLastError()));
  }
  return 0;
}

std::filesystem::path resolve_guardian_executable(
    const std::filesystem::path& main_exe) {
  const auto guard_dir = resolve_isolated_guardian_dir();
  if (auto isolated = sync_guardian_binary_to_dir(main_exe, guard_dir)) {
    return *isolated;
  }
  CWLOG_WARN(std::format(
      "[Guardian] 同步物理隔离看门狗二进制失败，降级使用主程序路径 ({})",
      utf::utf16_to_utf8(main_exe.wstring())));
  return main_exe;
}

bool spawn_companion_child(const std::filesystem::path& guard_exe,
                           const std::filesystem::path& main_exe,
                           uint32_t pid,
                           const std::filesystem::path& log_dir,
                           uint16_t singleton_port,
                           uint32_t generation,
                           PROCESS_INFORMATION* out_pi) {
  if (!out_pi) return false;
  *out_pi = PROCESS_INFORMATION{};

  const std::wstring cmd_base =
      quote_cmd_arg(guard_exe.wstring()) +
      std::format(
          L" --guardian --parent-pid {} --target-exe {} --log-dir {} --singleton-port {} --gen {}",
          pid, quote_cmd_arg(main_exe.wstring()),
          quote_cmd_arg(log_dir.wstring()), singleton_port, generation);

  std::error_code ec;
  const std::filesystem::path guard_work_dir = guard_exe.parent_path();
  const wchar_t* work_dir_ptr =
      (!guard_work_dir.empty() && std::filesystem::is_directory(guard_work_dir, ec))
          ? guard_work_dir.c_str()
          : nullptr;

  STARTUPINFOW si{};
  si.cb = sizeof(si);

  // 优先尝试脱离当前 Job Object，防止外部按进程组清理时将伴生进程一并连坐强杀；
  // 若宿主 Job 不允许 breakaway，自动回退普通无窗口拉起。
  std::wstring cmd1 = cmd_base;
  if (::CreateProcessW(nullptr, cmd1.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, nullptr,
                       work_dir_ptr, &si, out_pi)) {
    ::CloseHandle(out_pi->hThread);
    out_pi->hThread = nullptr;
    return true;
  }

  std::wstring cmd2 = cmd_base;
  if (::CreateProcessW(nullptr, cmd2.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, work_dir_ptr, &si, out_pi)) {
    ::CloseHandle(out_pi->hThread);
    out_pi->hThread = nullptr;
    return true;
  }
  return false;
}

void run_companion_supervisor(std::filesystem::path main_exe,
                              uint32_t pid,
                              std::filesystem::path log_dir,
                              uint16_t singleton_port,
                              uint32_t initial_gen,
                              HANDLE exit_event) {
  const auto session_boot = std::chrono::steady_clock::now();
  uint32_t rebuild_count = 0;
  uint32_t rapid_deaths = 0;

  for (;;) {
    if (is_exit_event_signaled(exit_event) || is_system_shutting_down()) {
      break;
    }

    const uint64_t session_uptime = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - session_boot)
            .count());
    const uint32_t effective_gen =
        (session_uptime >= kStableUptimeResetSecs) ? 0u : initial_gen;
    const std::filesystem::path guard_exe = resolve_guardian_executable(main_exe);

    PROCESS_INFORMATION pi{};
    if (!spawn_companion_child(guard_exe, main_exe, pid, log_dir, singleton_port,
                               effective_gen, &pi)) {
      ++rapid_deaths;
      const uint64_t retry_ms = compute_guard_rebuild_backoff_ms(rapid_deaths);
      CWLOG_WARN(std::format(
          "[Guardian] 启动伴生看门狗失败 (gle={})，将在 {}ms 后重试",
          ::GetLastError(), retry_ms));
      if (rapid_deaths >= 5 || wait_exit_or_timeout(exit_event, retry_ms)) {
        break;
      }
      continue;
    }

    const uint32_t guard_pid = pi.dwProcessId;
    const auto guard_spawned_at = std::chrono::steady_clock::now();
    if (rebuild_count == 0) {
      CWLOG_INFO(std::format(
          "[Guardian] 伴生看门狗已就绪 (guardian_pid={}, parent_pid={}, gen={}, guard_exe={})",
          guard_pid, pid, effective_gen,
          utf::utf16_to_utf8(guard_exe.wstring())));
    } else {
      CWLOG_WARN(std::format(
          "[Guardian] 伴生看门狗已自动重建恢复 (guardian_pid={}, parent_pid={}, 第{}次重建)",
          guard_pid, pid, rebuild_count));
    }

    HANDLE wait_handles[2] = {pi.hProcess, exit_event};
    const DWORD wait_count = exit_event ? 2u : 1u;
    const DWORD wait_res =
        ::WaitForMultipleObjects(wait_count, wait_handles, FALSE, INFINITE);

    if (wait_res != WAIT_OBJECT_0 || is_exit_event_signaled(exit_event) ||
        is_system_shutting_down()) {
      // 正常退出路径：立即释放句柄退场，0ms 唤醒，绝不阻塞主进程退出
      ::CloseHandle(pi.hProcess);
      break;
    }

    DWORD exit_code = 0;
    ::GetExitCodeProcess(pi.hProcess, &exit_code);
    ::CloseHandle(pi.hProcess);

    const uint64_t guard_uptime = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - guard_spawned_at)
            .count());
    if (guard_uptime >= kGuardRapidDeathWindowSecs) {
      rapid_deaths = 0;
    } else {
      ++rapid_deaths;
    }
    const uint64_t backoff_ms = compute_guard_rebuild_backoff_ms(rapid_deaths);

    CWLOG_WARN(std::format(
        "[Guardian] 检测到伴生看门狗进程意外终止 (guardian_pid={}, 存活={}s, ExitCode={})，将在 {}ms 后自动重建守护",
        guard_pid, guard_uptime, format_exit_code_diagnosis(exit_code),
        backoff_ms));

    if (wait_exit_or_timeout(exit_event, backoff_ms)) {
      break;
    }
    if (is_installer_or_peer_running()) {
      CWLOG_INFO(
          "[Guardian] 检测到安装/卸载程序或对端客户端正在运行，停止重建伴生看门狗");
      break;
    }
    ++rebuild_count;
  }
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
  std::optional<std::filesystem::path> target_exe;
  std::optional<std::filesystem::path> log_dir;
  uint16_t singleton_port = 33335;
  uint32_t generation = 0;

  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == L"--parent-pid" && i + 1 < args.size()) {
      parent_pid = parse_u32_w(args[++i]);
    } else if (args[i] == L"--target-exe" && i + 1 < args.size()) {
      const std::wstring& val = args[++i];
      if (!val.empty()) {
        target_exe = std::filesystem::path(val);
      }
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

  if (!target_exe.has_value()) {
    if (!args.empty() && !args[0].empty()) {
      target_exe = std::filesystem::path(args[0]);
    } else {
      const std::wstring cur = current_module_path();
      if (!cur.empty()) {
        target_exe = std::filesystem::path(cur);
      }
    }
  }

  if (!parent_pid.has_value() || *parent_pid == 0 || !target_exe.has_value() ||
      target_exe->empty() || !log_dir.has_value() || log_dir->empty()) {
    return std::nullopt;
  }
  return GuardianArgs{
      .parent_pid = *parent_pid,
      .target_exe = *target_exe,
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

uint64_t compute_guard_rebuild_backoff_ms(uint32_t rapid_deaths) {
  switch (rapid_deaths) {
    case 0:
      return kGuardRebuildBaseDelayMs;
    case 1:
      return 2000;
    case 2:
      return 5000;
    case 3:
      return 15000;
    default:
      return 30000;
  }
}

std::filesystem::path resolve_isolated_guardian_dir() {
  wchar_t* env_val = nullptr;
  size_t len = 0;
  if (_wdupenv_s(&env_val, &len, L"LOCALAPPDATA") == 0 && env_val) {
    std::wstring local_app(env_val, len > 0 ? len - 1 : 0);
    std::free(env_val);
    if (!local_app.empty()) {
      return std::filesystem::path(local_app) / util::dirs::kAppId / L"guard";
    }
  }
  PWSTR known = nullptr;
  if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known)) &&
      known) {
    std::filesystem::path p =
        std::filesystem::path(known) / util::dirs::kAppId / L"guard";
    ::CoTaskMemFree(known);
    return p;
  }
  std::error_code ec;
  auto tmp = std::filesystem::temp_directory_path(ec);
  if (tmp.empty()) tmp = L"C:\\Windows\\Temp";
  return tmp / util::dirs::kAppId / L"guard";
}

std::optional<std::filesystem::path> sync_guardian_binary_to_dir(
    const std::filesystem::path& main_exe,
    const std::filesystem::path& guard_dir) {
  if (main_exe.empty() || guard_dir.empty()) return std::nullopt;

  WIN32_FILE_ATTRIBUTE_DATA src_attr{};
  if (!::GetFileAttributesExW(main_exe.c_str(), GetFileExInfoStandard, &src_attr)) {
    return std::nullopt;
  }
  if ((src_attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return std::nullopt;
  }
  const uint64_t src_size = file_size_from_attr(src_attr);
  if (src_size == 0) {
    return std::nullopt;
  }

  std::error_code ec;
  std::filesystem::create_directories(guard_dir, ec);
  if (!std::filesystem::is_directory(guard_dir, ec)) {
    return std::nullopt;
  }
  cleanup_stale_guard_staging(guard_dir);

  const std::filesystem::path primary_guard =
      guard_dir / kIsolatedGuardianBinaryName;
  if (is_guardian_binary_up_to_date(src_attr, primary_guard)) {
    return primary_guard;
  }

  const std::filesystem::path staging =
      guard_dir /
      std::format(L"clashwin-guard.staging.{}.exe", ::GetCurrentProcessId());
  if (!::CopyFileW(main_exe.c_str(), staging.c_str(), FALSE)) {
    std::filesystem::remove(staging, ec);
    return std::nullopt;
  }

  WIN32_FILE_ATTRIBUTE_DATA stg_attr{};
  if (!::GetFileAttributesExW(staging.c_str(), GetFileExInfoStandard, &stg_attr) ||
      file_size_from_attr(stg_attr) != src_size) {
    std::filesystem::remove(staging, ec);
    return std::nullopt;
  }

  constexpr DWORD kMoveFlags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;
  if (::MoveFileExW(staging.c_str(), primary_guard.c_str(), kMoveFlags) != FALSE) {
    return primary_guard;
  }

  // 若上一代看门狗正处于微秒级退出尚未释放映像锁，短暂避让 80ms 后重试一次，仍占用则落入备用隔离文件名
  ::Sleep(80);
  if (::MoveFileExW(staging.c_str(), primary_guard.c_str(), kMoveFlags) != FALSE) {
    return primary_guard;
  }

  const std::filesystem::path fallback_guard =
      guard_dir / kIsolatedGuardianFallbackName;
  if (is_guardian_binary_up_to_date(src_attr, fallback_guard)) {
    std::filesystem::remove(staging, ec);
    return fallback_guard;
  }
  if (::MoveFileExW(staging.c_str(), fallback_guard.c_str(), kMoveFlags) != FALSE) {
    return fallback_guard;
  }
  std::filesystem::remove(staging, ec);
  return std::nullopt;
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
      if (code >= 0xC0000000u) {
        return std::format("{} (0x{:08X}: Windows NTSTATUS 异常终止)", code, code);
      }
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
    const wchar_t* class_name,
    const wchar_t* title) {
  if (!class_name || !*class_name) return std::nullopt;
  const wchar_t* effective_title =
      (title && *title) ? title : L"Clash Win Tray Host";
  const POINT work_origin = query_primary_workarea_origin();

  HINSTANCE hinst = ::GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.lpfnWndProc = sentinel_wnd_proc;
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

  // 1. 隐藏 owner 窗口（不带 WS_VISIBLE，置于屏外）：用于持有 sentinel，使其天然从任务栏和 Alt+Tab 隐身
  HWND owner_hwnd = ::CreateWindowExW(
      0, class_name, L"ClashWinResidentOwner", WS_POPUP, -32000, -32000, 1, 1,
      nullptr, nullptr, hinst, nullptr);
  if (!owner_hwnd) {
    CWLOG_WARN(std::format(
        "[Guardian] 创建哨兵 owner 窗口失败: gle={}", ::GetLastError()));
    return std::nullopt;
  }

  // 2. 落在主显示器工作区左上角（work_origin）的 1×1 穿透被拥有窗口：
  //    - 含 WS_VISIBLE → IsWindowVisible(sentinel) != 0
  //    - 落在真实显示器工作区内 → MonitorFromWindow(sentinel, MONITOR_DEFAULTTONULL) != NULL
  //    - LWA_ALPHA = 1（1/255 肉眼不可见）+ WM_NCHITTEST -> HTTRANSPARENT 完全穿透
  //    - 不含 WS_EX_TOOLWINDOW / WS_EX_TOPMOST / WS_EX_APPWINDOW → 任务栏与 Alt+Tab 均不可见
  HWND sentinel_hwnd = ::CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE, class_name,
      effective_title, WS_POPUP | WS_VISIBLE, work_origin.x, work_origin.y, 1,
      1, owner_hwnd, nullptr, hinst, nullptr);
  if (!sentinel_hwnd) {
    CWLOG_WARN(std::format(
        "[Guardian] 创建哨兵可见窗口失败: gle={}", ::GetLastError()));
    ::DestroyWindow(owner_hwnd);
    return std::nullopt;
  }

  (void)::SetLayeredWindowAttributes(
      sentinel_hwnd, 0, kSentinelVisibleAlpha, LWA_ALPHA);
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
  if (auto pair = create_sentinel_window_pair(
          L"ClashWinResidentSentinelClass", L"Clash Win Tray Host")) {
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

  if (g_supervisor_started.exchange(true, std::memory_order_acq_rel)) {
    return;
  }

  const std::wstring cur_exe = current_module_path();
  if (cur_exe.empty()) {
    g_supervisor_started.store(false, std::memory_order_release);
    CWLOG_WARN("[Guardian] 获取当前可执行文件路径失败");
    return;
  }

  const uint32_t pid = ::GetCurrentProcessId();
  HANDLE exit_ev = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_guardian_mutex);
    exit_ev = g_exit_event;
  }

  try {
    std::thread(run_companion_supervisor, std::filesystem::path(cur_exe), pid,
                log_dir, singleton_port, current_gen, exit_ev)
        .detach();
  } catch (const std::exception& e) {
    g_supervisor_started.store(false, std::memory_order_release);
    CWLOG_WARN(std::format(
        "[Guardian] 创建伴生看门狗监督线程失败: {}", e.what()));
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
