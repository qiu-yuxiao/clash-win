// ★ Windows 防误杀与自愈守护闭环实现（← Clash Mini src-tauri/src/process/guardian.rs，v3.3.0）
#include "system/guardian.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwctype>
#include <format>
#include <fstream>
#include <mutex>
#include <thread>

#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

namespace clashwin::system::guardian {

namespace {

std::mutex g_guardian_mutex;
HANDLE g_exit_event = nullptr;
HANDLE g_lightweight_event = nullptr;
std::atomic<bool> g_supervisor_started{false};
std::atomic<bool> g_lightweight_active{false};
std::atomic<uint64_t> g_atomic_temp_seq{1};
std::atomic<uint64_t> g_last_main_heartbeat_ms{0};
SentinelWindowPair g_resident_sentinel{};

// 会话级运行时诊断快照（A3/A6）
std::filesystem::path g_session_log_dir;
std::filesystem::path g_session_guard_dir;
std::string g_session_id;
std::string g_session_epoch;
uint16_t g_session_singleton_port = 33335;
uint32_t g_session_initial_gen = 0;
std::atomic<bool> g_session_initialized{false};
std::atomic<uint32_t> g_active_guard_pid{0};
std::atomic<int> g_parent_in_job_state{-1};   // -1=未知 0=false 1=true
std::atomic<int> g_guard_in_job_state{-1};    // -1=未知 0=false 1=true
std::atomic<bool> g_breakaway_degraded{false};

uint64_t current_unix_ms() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

uint64_t current_unix_secs() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

std::string format_local_timestamp() {
  SYSTEMTIME t{};
  ::GetLocalTime(&t);
  char buf[48] = {};
  std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u.%03u",
                t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                t.wMilliseconds);
  return std::string(buf);
}

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

std::optional<uint64_t> parse_u64_sv(std::string_view s) {
  if (s.empty()) return std::nullopt;
  uint64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return std::nullopt;
    const uint64_t digit = static_cast<uint64_t>(c - '0');
    if (v > (UINT64_MAX - digit) / 10ULL) return std::nullopt;
    v = v * 10ULL + digit;
  }
  return v;
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

std::optional<std::string> file_sha256_hex(const std::filesystem::path& p) {
  HANDLE h = ::CreateFileW(p.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return std::nullopt;

  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::optional<std::string> result;

  if (NT_SUCCESS(::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM,
                                               nullptr, 0))) {
    DWORD obj_len = 0;
    DWORD data_len = 0;
    if (NT_SUCCESS(::BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
                                       reinterpret_cast<PUCHAR>(&obj_len),
                                       sizeof(obj_len), &data_len, 0)) &&
        obj_len > 0) {
      std::vector<UCHAR> hash_obj(obj_len);
      if (NT_SUCCESS(::BCryptCreateHash(alg, &hash, hash_obj.data(), obj_len,
                                        nullptr, 0, 0))) {
        std::array<UCHAR, 32768> buf{};
        DWORD read_bytes = 0;
        bool ok = true;
        while (::ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()),
                          &read_bytes, nullptr)) {
          if (read_bytes == 0) break;
          if (!NT_SUCCESS(::BCryptHashData(hash, buf.data(), read_bytes, 0))) {
            ok = false;
            break;
          }
        }
        std::array<UCHAR, 32> digest{};
        if (ok &&
            NT_SUCCESS(::BCryptFinishHash(hash, digest.data(),
                                          static_cast<ULONG>(digest.size()), 0))) {
          static constexpr char kHex[] = "0123456789abcdef";
          std::string hex;
          hex.reserve(64);
          for (UCHAR b : digest) {
            hex.push_back(kHex[(b >> 4) & 0xF]);
            hex.push_back(kHex[b & 0xF]);
          }
          result = std::move(hex);
        }
      }
    }
  }

  if (hash) ::BCryptDestroyHash(hash);
  if (alg) ::BCryptCloseAlgorithmProvider(alg, 0);
  ::CloseHandle(h);
  return result;
}

bool is_guardian_binary_up_to_date(uint64_t src_size,
                                    const std::string& src_sha256,
                                    const std::filesystem::path& dst_path) {
  WIN32_FILE_ATTRIBUTE_DATA dst_attr{};
  if (!::GetFileAttributesExW(dst_path.c_str(), GetFileExInfoStandard, &dst_attr)) {
    return false;
  }
  if ((dst_attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return false;
  }
  if (file_size_from_attr(dst_attr) != src_size) {
    return false;
  }
  const auto dst_sha = file_sha256_hex(dst_path);
  return dst_sha.has_value() && *dst_sha == src_sha256;
}

bool atomic_replace_file_with_retry(const std::filesystem::path& tmp_path,
                                    const std::filesystem::path& target_path) {
  constexpr DWORD kMoveFlags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (::MoveFileExW(tmp_path.c_str(), target_path.c_str(), kMoveFlags) != FALSE) {
      return true;
    }
    const DWORD gle = ::GetLastError();
    if (gle == ERROR_SHARING_VIOLATION || gle == ERROR_ACCESS_DENIED) {
      ::Sleep(10u << attempt);
      continue;
    }
    return false;
  }
  return false;
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

std::optional<bool> query_process_in_job(HANDLE h_proc) {
  BOOL in_job = FALSE;
  if (::IsProcessInJob(h_proc, nullptr, &in_job) != FALSE) {
    return in_job != FALSE;
  }
  return std::nullopt;
}

bool is_system_shutting_down() {
  return ::GetSystemMetrics(SM_SHUTTINGDOWN) != 0;
}

bool is_exit_event_signaled(HANDLE exit_event) {
  return exit_event != nullptr &&
         ::WaitForSingleObject(exit_event, 0) == WAIT_OBJECT_0;
}

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

// 扫描系统进程：仅检测真正安装/卸载器，严格跳过当前 PID，绝不匹配 clashwin.exe / clashwin-guard.exe
bool is_installer_or_uninstaller_running() {
  HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;

  const DWORD self_pid = ::GetCurrentProcessId();
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;
  if (::Process32FirstW(snap, &entry)) {
    do {
      if (entry.th32ProcessID == self_pid) continue;
      const std::wstring name = lower_w(entry.szExeFile);
      if (name == L"clashwin.exe" || name == L"clashwin-guard.exe" ||
          name == L"clashwin-guard-alt.exe") {
        continue;
      }
      const bool is_uninstaller =
          (name == L"uninstall.exe" || name == L"au_.exe" ||
           name == L"un_a.exe" || name.find(L"un_a.exe") != std::wstring::npos);
      const bool is_clash_setup =
          (name.find(L"setup") != std::wstring::npos &&
           (name.find(L"clash") != std::wstring::npos ||
            name.find(L"mini") != std::wstring::npos));
      if (is_uninstaller || is_clash_setup || name.starts_with(L"clash.mini_") ||
          name.starts_with(L"clash-mini-setup") ||
          name.starts_with(L"clashwin_")) {
        found = true;
        break;
      }
    } while (::Process32NextW(snap, &entry));
  }
  ::CloseHandle(snap);
  return found;
}

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

void write_guardian_fallback_log(const std::string& line, const std::string& err) {
  const std::wstring full = utf::utf8_to_utf16(
      "[ClashWin Guardian Log Fallback] (err=" + err + ") " + line);
  ::OutputDebugStringW(full.c_str());

  std::error_code ec;
  auto tmp = std::filesystem::temp_directory_path(ec);
  if (tmp.empty()) return;
  const std::filesystem::path fb = tmp / L"clashwin-guard-fallback.log";
  HANDLE h = ::CreateFileW(fb.c_str(), FILE_APPEND_DATA | SYNCHRONIZE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    std::string out = "[fallback err=" + err + "] " + line;
    DWORD written = 0;
    ::WriteFile(h, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
    ::FlushFileBuffers(h);
    ::CloseHandle(h);
  }
}

bool signal_event_with_retry(HANDLE h_ev) {
  if (!h_ev) return false;
  for (int i = 0; i < 3; ++i) {
    ::SetEvent(h_ev);
    if (::WaitForSingleObject(h_ev, 0) == WAIT_OBJECT_0) {
      return true;
    }
    ::Sleep(5);
  }
  return ::WaitForSingleObject(h_ev, 0) == WAIT_OBJECT_0;
}

void write_main_heartbeat_now(const std::filesystem::path& guard_dir,
                              const std::string& session_id,
                              const std::string& session_epoch,
                              uint32_t parent_pid,
                              uint32_t gen) {
  if (guard_dir.empty()) return;
  std::optional<uint32_t> gp;
  const uint32_t raw_gp = g_active_guard_pid.load(std::memory_order_acquire);
  if (raw_gp > 0) gp = raw_gp;

  const nlohmann::json j = {
      {"pid", parent_pid},
      {"session_id", session_id},
      {"session_epoch", session_epoch},
      {"generation", gen},
      {"lightweight", g_lightweight_active.load(std::memory_order_acquire)},
      {"guard_pid", gp ? nlohmann::json(*gp) : nlohmann::json(nullptr)},
      {"timestamp_unix_ms", current_unix_ms()},
      {"local_time", format_local_timestamp()},
  };
  (void)write_json_atomic(guard_dir / kMainHeartbeatFilename, j);
  g_last_main_heartbeat_ms.store(current_unix_ms(), std::memory_order_release);
}

void write_guard_heartbeat(const GuardianArgs& args,
                           std::string_view state,
                           std::optional<bool> guard_in_job) {
  const std::filesystem::path guard_dir =
      args.guard_dir.empty() ? resolve_isolated_guardian_dir() : args.guard_dir;
  if (guard_dir.empty()) return;

  const nlohmann::json j = {
      {"guard_pid", ::GetCurrentProcessId()},
      {"parent_pid", args.parent_pid},
      {"session_id", args.session_id},
      {"session_epoch", args.session_epoch},
      {"generation", args.generation},
      {"broker", args.broker.empty() ? kBrokerBreakawayJob : args.broker},
      {"guard_in_job", guard_in_job ? nlohmann::json(*guard_in_job) : nlohmann::json(nullptr)},
      {"breakaway_degraded", guard_in_job.value_or(true)},
      {"state", std::string(state)},
      {"timestamp_unix_ms", current_unix_ms()},
      {"local_time", format_local_timestamp()},
  };
  (void)write_json_atomic(guard_dir / kGuardHeartbeatFilename, j);
}

void write_intentional_exit_phase(std::string_view phase, std::string_view reason) {
  HANDLE h_ev = nullptr;
  std::filesystem::path guard_dir;
  std::filesystem::path log_dir;
  std::string session_id;
  uint32_t gen = 0;
  {
    std::lock_guard<std::mutex> lock(g_guardian_mutex);
    h_ev = g_exit_event;
    guard_dir = g_session_guard_dir;
    log_dir = g_session_log_dir;
    session_id = g_session_id;
    gen = g_session_initial_gen;
  }

  const bool ev_ok = signal_event_with_retry(h_ev);
  if (!guard_dir.empty() && !session_id.empty()) {
    bool wrote = false;
    for (int i = 0; i < 3; ++i) {
      if (write_intentional_exit_proof_in(guard_dir, ::GetCurrentProcessId(),
                                          session_id, phase, reason,
                                          current_unix_ms())) {
        wrote = true;
        break;
      }
      ::Sleep(10u << i);
    }
    if (!log_dir.empty()) {
      append_structured_guardian_log(
          log_dir, wrote && ev_ok ? "INFO" : "WARN", session_id,
          ::GetCurrentProcessId(),
          g_active_guard_pid.load() > 0
              ? std::optional<uint32_t>(g_active_guard_pid.load())
              : std::nullopt,
          gen,
          phase == "confirmed" ? "INTENTIONAL_EXIT_CONFIRMED"
                               : "INTENTIONAL_EXIT_PREPARED",
          std::format("phase={}, reason={}, event_ok={}, proof_ok={}", phase,
                      reason, ev_ok, wrote));
    }
  }
}

DWORD wait_parent_with_heartbeat(HANDLE h_parent,
                                 HANDLE h_exit,
                                 const GuardianArgs& args,
                                 std::optional<bool> guard_in_job) {
  HANDLE handles[2] = {h_parent, h_exit};
  const DWORD count = h_exit ? 2u : 1u;
  uint64_t last_hb_ms = current_unix_ms();

  for (;;) {
    const uint64_t now = current_unix_ms();
    const uint64_t elapsed = (now >= last_hb_ms) ? (now - last_hb_ms) : 0;
    const DWORD wait_ms = (elapsed >= kHeartbeatIntervalMs)
                              ? 0
                              : static_cast<DWORD>(kHeartbeatIntervalMs - elapsed);

    const DWORD res =
        ::MsgWaitForMultipleObjects(count, handles, FALSE, wait_ms, QS_ALLINPUT);
    if (res == WAIT_OBJECT_0 + count) {
      pump_pending_window_messages();
      continue;
    }
    if (res == WAIT_TIMEOUT) {
      write_guard_heartbeat(args, "watching", guard_in_job);
      last_hb_ms = current_unix_ms();
      continue;
    }
    return res;
  }
}

int run_guardian_loop(const GuardianArgs& args) {
  const std::filesystem::path guard_dir =
      args.guard_dir.empty() ? resolve_isolated_guardian_dir() : args.guard_dir;
  const std::string session_id =
      args.session_id.empty() ? std::format("pid-{}", args.parent_pid) : args.session_id;
  const std::string epoch_id =
      args.session_epoch.empty() ? session_id : args.session_epoch;
  const uint32_t guard_pid = ::GetCurrentProcessId();

  HANDLE h_parent = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                  FALSE, args.parent_pid);
  if (!h_parent) {
    append_structured_guardian_log(
        args.log_dir, "WARN", session_id, args.parent_pid, guard_pid,
        args.generation, "COMPANION_OPEN_PARENT_FAILED",
        std::format("OpenProcess 失败 (gle={})，退出守护", ::GetLastError()));
    return 0;
  }

  auto guard_sentinel =
      create_sentinel_window_pair(L"ClashWinGuardHost", L"Clash Win Guard Host");
  pump_pending_window_messages();

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

  ::SetProcessWorkingSetSize(::GetCurrentProcess(), static_cast<SIZE_T>(-1),
                             static_cast<SIZE_T>(-1));

  const auto guard_in_job = query_process_in_job(::GetCurrentProcess());
  append_structured_guardian_log(
      args.log_dir, "INFO", session_id, args.parent_pid, guard_pid,
      args.generation, "COMPANION_ONLINE",
      std::format("伴生看门狗已上线 (broker={}, guard_in_job={}, gen={})",
                  args.broker.empty() ? kBrokerBreakawayJob : args.broker,
                  guard_in_job.has_value() ? (*guard_in_job ? "true" : "false")
                                           : "unknown",
                  args.generation));
  write_guard_heartbeat(args, "watching", guard_in_job);

  const DWORD wait = wait_parent_with_heartbeat(h_parent, h_exit, args, guard_in_job);
  if (guard_sentinel.has_value()) {
    destroy_sentinel_window_pair(*guard_sentinel);
  }

  const bool ev_signaled_now =
      h_exit != nullptr &&
      (wait == WAIT_OBJECT_0 + 1 || ::WaitForSingleObject(h_exit, 0) == WAIT_OBJECT_0);
  if (ev_signaled_now || wait != WAIT_OBJECT_0 || is_system_shutting_down()) {
    append_structured_guardian_log(
        args.log_dir, "INFO", session_id, args.parent_pid, guard_pid,
        args.generation, "COMPANION_EXIT_EVENT_SIGNALED",
        std::format("收到主动退出事件或系统关机信号 (wait={}, shutting_down={})，看门狗安静退场",
                    wait, is_system_shutting_down()));
    write_guard_heartbeat(args, "intentional_exit", guard_in_job);
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
  ::CloseHandle(h_parent);

  // T=0 双通道检查：若内核事件在父进程退出窗口内刚好置位或 intentional_exit.json 有效，直接退出
  const auto check0 = evaluate_intentional_exit_proof_in(
      guard_dir, args.parent_pid, session_id, current_unix_ms());
  if ((h_exit != nullptr && ::WaitForSingleObject(h_exit, 0) == WAIT_OBJECT_0) ||
      check0.status == IntentionalProofStatus::ValidPrepared ||
      check0.status == IntentionalProofStatus::ValidConfirmed) {
    if (h_exit) ::CloseHandle(h_exit);
    append_structured_guardian_log(
        args.log_dir, "INFO", session_id, args.parent_pid, guard_pid,
        args.generation, "PARENT_EXIT_INTENTIONAL_T0",
        std::format("T=0 核实为主动退出 ({}), 放弃自愈拉起", check0.reason));
    write_guard_heartbeat(args, "intentional_exit", guard_in_job);
    return 0;
  }

  append_structured_guardian_log(
      args.log_dir, "ERROR", session_id, args.parent_pid, guard_pid,
      args.generation, "PARENT_ABNORMAL_EXIT_DETECTED",
      std::format("检测到主进程异常终止! exit={}, uptime={}s, lightweight={}, gen={}, mode={}",
                  format_exit_code_diagnosis(exit_code), uptime_secs,
                  in_lightweight, args.generation,
                  in_lightweight ? "silent" : "gui"));

  // 安全缓冲期 3500ms（避开卸载/升级/关机瞬态）
  ::Sleep(kRespawnSafetyDelayMs);

  // 缓冲期后二次核对事件与带外退出凭证 + 系统关机 + 安装/卸载器
  const auto check1 = evaluate_intentional_exit_proof_in(
      guard_dir, args.parent_pid, session_id, current_unix_ms());
  const bool ev_after_delay =
      h_exit != nullptr && ::WaitForSingleObject(h_exit, 0) == WAIT_OBJECT_0;
  if (h_exit) ::CloseHandle(h_exit);

  if (ev_after_delay ||
      check1.status == IntentionalProofStatus::ValidPrepared ||
      check1.status == IntentionalProofStatus::ValidConfirmed ||
      is_system_shutting_down()) {
    append_structured_guardian_log(
        args.log_dir, "INFO", session_id, args.parent_pid, guard_pid,
        args.generation, "PARENT_EXIT_INTENTIONAL_AFTER_DELAY",
        std::format("3500ms 安全延时后确认为主动退出/关机 (ev={}, proof={})",
                    ev_after_delay, check1.reason));
    write_guard_heartbeat(args, "intentional_exit", guard_in_job);
    return 0;
  }
  if (check1.status == IntentionalProofStatus::Rejected) {
    append_structured_guardian_log(
        args.log_dir, "WARN", session_id, args.parent_pid, guard_pid,
        args.generation, "INTENTIONAL_EXIT_PROOF_REJECTED", check1.detail);
  }

  if (is_installer_or_uninstaller_running()) {
    append_structured_guardian_log(
        args.log_dir, "INFO", session_id, args.parent_pid, guard_pid,
        args.generation, "RESPAWN_YIELD_INSTALLER",
        "检测到安装器或卸载器正在运行，让位放弃拉起");
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
    append_structured_guardian_log(
        args.log_dir, "INFO", session_id, args.parent_pid, guard_pid,
        args.generation, "RESPAWN_TARGET_MISSING",
        std::format("目标主程序不存在 ({})，放弃拉起",
                    utf::utf16_to_utf8(target_exe.wstring())));
    return 0;
  }

  if (!is_singleton_port_free(args.singleton_port)) {
    append_structured_guardian_log(
        args.log_dir, "INFO", session_id, args.parent_pid, guard_pid,
        args.generation, "RESPAWN_PORT_OCCUPIED",
        std::format("单实例端口 {} 已被新实例占用，无需重复拉起", args.singleton_port));
    return 0;
  }

  // Fail-Closed Boot/Epoch 累计熔断判决并持久化
  const std::string boot_fp = current_boot_fingerprint();
  const auto budget_res = evaluate_and_record_respawn_in(
      guard_dir, epoch_id, args.generation, args.parent_pid, current_unix_ms(),
      boot_fp, /*commit=*/true);
  if (budget_res.is_err()) {
    append_structured_guardian_log(
        args.log_dir, "ERROR", session_id, args.parent_pid, guard_pid,
        args.generation, "RESPAWN_STATE_FAIL_CLOSED", budget_res.error().message);
    write_guard_heartbeat(args, "fail_closed", guard_in_job);
    return 0;
  }
  if (!budget_res.value().has_value()) {
    append_structured_guardian_log(
        args.log_dir, "WARN", session_id, args.parent_pid, guard_pid,
        args.generation, "RESPAWN_CIRCUIT_BREAKER_OPEN",
        std::format("同一开机/会话纪元已达到自愈次数上限 ({})，触发累计熔断，停止自愈拉起",
                    kMaxRapidRespawnGen));
    write_guard_heartbeat(args, "circuit_breaker_open", guard_in_job);
    return 0;
  }
  const uint32_t next_gen = *budget_res.value();

  // A5 形态保真：轻量模式崩溃传 `--silent` 恢复托盘，非轻量前台崩溃以可见 GUI 恢复
  std::wstring cmd =
      quote_cmd_arg(target_exe.wstring()) +
      std::format(
          L" --guardian-respawn --guardian-gen {} --prev-pid {} --prev-exit {} --session-epoch {}",
          next_gen, args.parent_pid, exit_code,
          quote_cmd_arg(utf::utf8_to_utf16(epoch_id)));
  if (in_lightweight) {
    cmd += L" --silent";
  }

  const std::filesystem::path work_dir = target_exe.parent_path();
  const wchar_t* work_dir_ptr =
      (!work_dir.empty() && std::filesystem::is_directory(work_dir, ec))
          ? work_dir.c_str()
          : nullptr;

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  const DWORD flags = CREATE_BREAKAWAY_FROM_JOB | (in_lightweight ? CREATE_NO_WINDOW : 0u);
  if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, flags,
                       nullptr, work_dir_ptr, &si, &pi)) {
    append_structured_guardian_log(
        args.log_dir, "WARN", session_id, args.parent_pid, guard_pid,
        next_gen, "RESPAWN_MAIN_SUCCESS",
        std::format("已通过 Breakaway 自愈拉起主进程 (new_pid={}, gen={}/{}, mode={}, target={})",
                    pi.dwProcessId, next_gen, kMaxRapidRespawnGen,
                    in_lightweight ? "silent" : "gui",
                    utf::utf16_to_utf8(target_exe.wstring())));
    write_guard_heartbeat(args, "respawned", guard_in_job);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
  } else {
    append_structured_guardian_log(
        args.log_dir, "ERROR", session_id, args.parent_pid, guard_pid,
        next_gen, "RESPAWN_MAIN_BREAKAWAY_FAILED",
        std::format("Breakaway 自愈拉起主进程失败 (gle={})，禁止降级同 Job",
                    ::GetLastError()));
  }
  return 0;
}

std::filesystem::path resolve_guardian_executable(
    const std::filesystem::path& main_exe,
    const std::filesystem::path& guard_dir,
    const std::filesystem::path& log_dir,
    const std::string& session_id,
    uint32_t gen) {
  if (auto isolated = sync_guardian_binary_to_dir(main_exe, guard_dir)) {
    return *isolated;
  }
  append_structured_guardian_log(
      log_dir, "WARN", session_id, ::GetCurrentProcessId(), std::nullopt, gen,
      "SUPERVISOR_BINARY_SYNC_DEGRADED",
      std::format("同步物理隔离看门狗二进制失败，降级使用主程序路径 ({})",
                  utf::utf16_to_utf8(main_exe.wstring())));
  return main_exe;
}

bool spawn_companion_child(const std::filesystem::path& guard_exe,
                           const std::filesystem::path& main_exe,
                           uint32_t pid,
                           const std::filesystem::path& log_dir,
                           const std::filesystem::path& guard_dir,
                           uint16_t singleton_port,
                           uint32_t generation,
                           const std::string& session_id,
                           const std::string& session_epoch,
                           PROCESS_INFORMATION* out_pi) {
  if (!out_pi) return false;
  *out_pi = PROCESS_INFORMATION{};

  const std::wstring cmd_base =
      quote_cmd_arg(guard_exe.wstring()) +
      std::format(
          L" --guardian --parent-pid {} --target-exe {} --log-dir {} --guard-dir {} "
          L"--singleton-port {} --gen {} --session-id {} --session-epoch {} --broker {}",
          pid, quote_cmd_arg(main_exe.wstring()),
          quote_cmd_arg(log_dir.wstring()), quote_cmd_arg(guard_dir.wstring()),
          singleton_port, generation,
          quote_cmd_arg(utf::utf8_to_utf16(session_id)),
          quote_cmd_arg(utf::utf8_to_utf16(session_epoch)),
          utf::utf8_to_utf16(kBrokerBreakawayJob));

  std::error_code ec;
  const std::filesystem::path guard_work_dir = guard_exe.parent_path();
  const wchar_t* work_dir_ptr =
      (!guard_work_dir.empty() && std::filesystem::is_directory(guard_work_dir, ec))
          ? guard_work_dir.c_str()
          : nullptr;

  STARTUPINFOW si{};
  si.cb = sizeof(si);

  // P0-1 / P0-4：严格单通道 CREATE_BREAKAWAY_FROM_JOB，禁止无 Breakaway 降级同 Job
  std::wstring cmd = cmd_base;
  if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, nullptr,
                       work_dir_ptr, &si, out_pi)) {
    const auto p_in_job = query_process_in_job(::GetCurrentProcess());
    const auto g_in_job = query_process_in_job(out_pi->hProcess);
    if (p_in_job.has_value()) {
      g_parent_in_job_state.store(*p_in_job ? 1 : 0, std::memory_order_release);
    }
    if (g_in_job.has_value()) {
      g_guard_in_job_state.store(*g_in_job ? 1 : 0, std::memory_order_release);
      g_breakaway_degraded.store(*g_in_job, std::memory_order_release);
    }
    ::CloseHandle(out_pi->hThread);
    out_pi->hThread = nullptr;
    return true;
  }
  return false;
}

void run_companion_supervisor(std::filesystem::path main_exe,
                              uint32_t pid,
                              std::filesystem::path log_dir,
                              std::filesystem::path guard_dir,
                              uint16_t singleton_port,
                              uint32_t initial_gen,
                              std::string session_id,
                              std::string session_epoch,
                              HANDLE exit_event) {
  // RAII 确保无论何种路径退出（连续失败/安装器超时/异常），均重置 g_supervisor_started 以便巡检重武装（A6）
  struct RunningResetGuard {
    ~RunningResetGuard() {
      g_active_guard_pid.store(0, std::memory_order_release);
      g_supervisor_started.store(false, std::memory_order_release);
    }
  } reset_guard;

  uint32_t rebuild_count = 0;
  uint32_t rapid_deaths = 0;

  for (;;) {
    if (is_exit_event_signaled(exit_event) || is_system_shutting_down()) {
      break;
    }

    const std::filesystem::path guard_exe = resolve_guardian_executable(
        main_exe, guard_dir, log_dir, session_id, initial_gen);

    PROCESS_INFORMATION pi{};
    if (!spawn_companion_child(guard_exe, main_exe, pid, log_dir, guard_dir,
                               singleton_port, initial_gen, session_id,
                               session_epoch, &pi)) {
      ++rapid_deaths;
      const uint64_t retry_ms = compute_guard_rebuild_backoff_ms(rapid_deaths);
      append_structured_guardian_log(
          log_dir, "WARN", session_id, pid, std::nullopt, initial_gen,
          "SUPERVISOR_SPAWN_FAILED",
          std::format("Breakaway 启动伴生看门狗失败 (gle={})，将在 {}ms 后重试 ({}/5)",
                      ::GetLastError(), retry_ms, rapid_deaths));
      if (rapid_deaths >= 5 || wait_exit_or_timeout(exit_event, retry_ms)) {
        break;
      }
      continue;
    }

    const uint32_t guard_pid = pi.dwProcessId;
    g_active_guard_pid.store(guard_pid, std::memory_order_release);
    const auto guard_spawned_at = std::chrono::steady_clock::now();

    append_structured_guardian_log(
        log_dir, rebuild_count == 0 ? "INFO" : "WARN", session_id, pid, guard_pid,
        initial_gen,
        rebuild_count == 0 ? "SUPERVISOR_COMPANION_STARTED"
                           : "SUPERVISOR_COMPANION_REBUILT",
        std::format("guard_exe={}, guard_in_job={}, rebuilds={}",
                    utf::utf16_to_utf8(guard_exe.wstring()),
                    g_guard_in_job_state.load(), rebuild_count));
    write_main_heartbeat_now(guard_dir, session_id, session_epoch, pid, initial_gen);

    HANDLE wait_handles[2] = {pi.hProcess, exit_event};
    const DWORD wait_count = exit_event ? 2u : 1u;
    const DWORD wait_res =
        ::WaitForMultipleObjects(wait_count, wait_handles, FALSE, INFINITE);

    if (wait_res != WAIT_OBJECT_0 || is_exit_event_signaled(exit_event) ||
        is_system_shutting_down()) {
      ::CloseHandle(pi.hProcess);
      break;
    }

    DWORD exit_code = 0;
    ::GetExitCodeProcess(pi.hProcess, &exit_code);
    ::CloseHandle(pi.hProcess);
    g_active_guard_pid.store(0, std::memory_order_release);

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

    append_structured_guardian_log(
        log_dir, "WARN", session_id, pid, guard_pid, initial_gen,
        "SUPERVISOR_COMPANION_EXITED",
        std::format("伴生看门狗意外终止 (uptime={}s, exit={})，将在 {}ms 后自动重建",
                    guard_uptime, format_exit_code_diagnosis(exit_code), backoff_ms));

    if (wait_exit_or_timeout(exit_event, backoff_ms)) {
      break;
    }

    // 若安装器在位，最长退避等待 120s 后决定继续还是让 RAII 清零等待巡检
    uint64_t waited_installer_ms = 0;
    while (is_installer_or_uninstaller_running() && waited_installer_ms < 120000ULL) {
      if (wait_exit_or_timeout(exit_event, 2000)) {
        return;
      }
      waited_installer_ms += 2000;
    }
    if (is_installer_or_uninstaller_running()) {
      append_structured_guardian_log(
          log_dir, "INFO", session_id, pid, std::nullopt, initial_gen,
          "SUPERVISOR_YIELD_INSTALLER",
          "安装/卸载程序持续运行超时 120s，监督线程暂停等待巡检重武装");
      break;
    }
    ++rebuild_count;
  }
}

void launch_supervisor_thread_locked() {
  if (!g_session_initialized.load(std::memory_order_acquire)) return;
  if (g_supervisor_started.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  const std::wstring cur_exe = current_module_path();
  if (cur_exe.empty()) {
    g_supervisor_started.store(false, std::memory_order_release);
    return;
  }

  std::filesystem::path log_dir;
  std::filesystem::path guard_dir;
  uint16_t singleton_port = 33335;
  uint32_t initial_gen = 0;
  std::string session_id;
  std::string session_epoch;
  HANDLE exit_ev = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_guardian_mutex);
    log_dir = g_session_log_dir;
    guard_dir = g_session_guard_dir;
    singleton_port = g_session_singleton_port;
    initial_gen = g_session_initial_gen;
    session_id = g_session_id;
    session_epoch = g_session_epoch;
    exit_ev = g_exit_event;
  }

  try {
    std::thread(run_companion_supervisor, std::filesystem::path(cur_exe),
                ::GetCurrentProcessId(), std::move(log_dir),
                std::move(guard_dir), singleton_port, initial_gen,
                std::move(session_id), std::move(session_epoch), exit_ev)
        .detach();
  } catch (const std::exception& e) {
    g_supervisor_started.store(false, std::memory_order_release);
    CWLOG_WARN(std::format("[Guardian] 创建伴生看门狗监督线程失败: {}", e.what()));
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
  std::filesystem::path guard_dir;
  uint16_t singleton_port = 33335;
  uint32_t generation = 0;
  std::string session_id;
  std::string session_epoch;
  std::string broker;

  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == L"--parent-pid" && i + 1 < args.size()) {
      parent_pid = parse_u32_w(args[++i]);
    } else if (args[i] == L"--target-exe" && i + 1 < args.size()) {
      const std::wstring& val = args[++i];
      if (!val.empty()) target_exe = std::filesystem::path(val);
    } else if (args[i] == L"--log-dir" && i + 1 < args.size()) {
      log_dir = std::filesystem::path(args[++i]);
    } else if (args[i] == L"--guard-dir" && i + 1 < args.size()) {
      const std::wstring& val = args[++i];
      if (!val.empty()) guard_dir = std::filesystem::path(val);
    } else if (args[i] == L"--singleton-port" && i + 1 < args.size()) {
      if (auto p = parse_u32_w(args[++i]); p && *p > 0 && *p < 65536) {
        singleton_port = static_cast<uint16_t>(*p);
      }
    } else if (args[i] == L"--gen" && i + 1 < args.size()) {
      if (auto g = parse_u32_w(args[++i])) generation = *g;
    } else if (args[i] == L"--session-id" && i + 1 < args.size()) {
      session_id = utf::utf16_to_utf8(args[++i]);
    } else if (args[i] == L"--session-epoch" && i + 1 < args.size()) {
      session_epoch = utf::utf16_to_utf8(args[++i]);
    } else if (args[i] == L"--broker" && i + 1 < args.size()) {
      broker = utf::utf16_to_utf8(args[++i]);
    }
  }

  if (!target_exe.has_value()) {
    if (!args.empty() && !args[0].empty()) {
      target_exe = std::filesystem::path(args[0]);
    } else {
      const std::wstring cur = current_module_path();
      if (!cur.empty()) target_exe = std::filesystem::path(cur);
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
      .guard_dir = guard_dir,
      .singleton_port = singleton_port,
      .generation = generation,
      .session_id = session_id,
      .session_epoch = session_epoch,
      .broker = broker,
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
    } else if (args[i] == L"--session-epoch" && i + 1 < args.size()) {
      info.session_epoch = utf::utf16_to_utf8(args[++i]);
    } else if (args[i] == L"--silent") {
      info.silent = true;
    }
  }
  return info;
}

std::optional<std::string> parse_respawn_session_epoch(
    const std::vector<std::wstring>& args) {
  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == L"--session-epoch" && i + 1 < args.size() &&
        !args[i + 1].empty()) {
      return utf::utf16_to_utf8(args[i + 1]);
    }
  }
  return std::nullopt;
}

bool is_guardian_respawn_args(const std::vector<std::wstring>& args) {
  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == L"--guardian-respawn") return true;
  }
  return false;
}

bool is_guardian_silent_respawn_args(const std::vector<std::wstring>& args) {
  bool respawn = false;
  bool silent = false;
  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == L"--guardian-respawn") respawn = true;
    if (args[i] == L"--silent") silent = true;
  }
  return respawn && silent;
}

bool is_guardian_respawn() {
  return is_guardian_respawn_args(current_command_line_args());
}

bool is_guardian_silent_respawn() {
  return is_guardian_silent_respawn_args(current_command_line_args());
}

std::optional<uint32_t> compute_next_generation(uint32_t current_gen) {
  if (current_gen >= kMaxRapidRespawnGen) return std::nullopt;
  return current_gen + 1u;
}

std::string current_boot_fingerprint() {
  const uint64_t now_s = current_unix_secs();
  const uint64_t up_s = ::GetTickCount64() / 1000ULL;
  const uint64_t boot_s = (now_s >= up_s) ? (now_s - up_s) : 0ULL;
  return std::format("boot:{}", boot_s);
}

bool boot_fingerprints_match(std::string_view saved, std::string_view current) {
  constexpr std::string_view kPrefix = "boot:";
  if (saved.starts_with(kPrefix) && current.starts_with(kPrefix)) {
    const auto a = parse_u64_sv(saved.substr(kPrefix.size()));
    const auto b = parse_u64_sv(current.substr(kPrefix.size()));
    if (a.has_value() && b.has_value()) {
      const uint64_t diff = (*a >= *b) ? (*a - *b) : (*b - *a);
      return diff <= 30ULL;
    }
  }
  return saved == current;
}

std::optional<uint32_t> evaluate_respawn_budget(
    const RespawnSessionState& state,
    uint32_t cli_gen,
    std::string_view expected_epoch_id,
    std::string_view boot_fingerprint) {
  if (!state.boot_fingerprint.empty() &&
      !boot_fingerprints_match(state.boot_fingerprint, boot_fingerprint)) {
    return 1u;
  }
  if (!state.session_epoch_id.empty() && !expected_epoch_id.empty() &&
      state.session_epoch_id != expected_epoch_id) {
    // 已有后续新的干净冷启动接管状态文件，旧纪元停止动作
    return std::nullopt;
  }
  const uint32_t effective_prior =
      (state.respawn_count == 0 && state.session_epoch_id.empty())
          ? cli_gen
          : std::max(state.respawn_count, cli_gen);
  return compute_next_generation(effective_prior);
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

bool write_json_atomic(const std::filesystem::path& target,
                       const nlohmann::json& payload) {
  if (target.empty()) return false;
  std::error_code ec;
  const auto parent = target.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (!std::filesystem::is_directory(parent, ec)) return false;
  }

  const uint64_t seq = g_atomic_temp_seq.fetch_add(1, std::memory_order_relaxed);
  const std::filesystem::path tmp =
      target.parent_path() /
      std::format(L"{}.tmp.{}.{}", target.filename().wstring(),
                  ::GetCurrentProcessId(), seq);

  const std::string text = payload.dump(2) + "\n";
  HANDLE h = ::CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return false;
  }

  DWORD written = 0;
  const BOOL w_ok =
      ::WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
  const BOOL f_ok = ::FlushFileBuffers(h);
  ::CloseHandle(h);

  if (!w_ok || !f_ok || written != text.size()) {
    std::filesystem::remove(tmp, ec);
    return false;
  }

  if (!atomic_replace_file_with_retry(tmp, target)) {
    std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
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
  const auto src_sha256 = file_sha256_hex(main_exe);
  if (!src_sha256.has_value()) {
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
  if (is_guardian_binary_up_to_date(src_size, *src_sha256, primary_guard)) {
    return primary_guard;
  }

  const uint64_t seq = g_atomic_temp_seq.fetch_add(1, std::memory_order_relaxed);
  const std::filesystem::path staging =
      guard_dir / std::format(L"clashwin-guard.staging.{}.{}.exe",
                              ::GetCurrentProcessId(), seq);
  if (!::CopyFileW(main_exe.c_str(), staging.c_str(), FALSE)) {
    std::filesystem::remove(staging, ec);
    return std::nullopt;
  }

  // 刷盘确保副本落盘完整
  HANDLE h_stg = ::CreateFileW(staging.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h_stg != INVALID_HANDLE_VALUE) {
    ::FlushFileBuffers(h_stg);
    ::CloseHandle(h_stg);
  }

  WIN32_FILE_ATTRIBUTE_DATA stg_attr{};
  if (!::GetFileAttributesExW(staging.c_str(), GetFileExInfoStandard, &stg_attr) ||
      file_size_from_attr(stg_attr) != src_size) {
    std::filesystem::remove(staging, ec);
    return std::nullopt;
  }

  if (atomic_replace_file_with_retry(staging, primary_guard)) {
    return primary_guard;
  }

  const std::filesystem::path fallback_guard =
      guard_dir / kIsolatedGuardianFallbackName;
  if (is_guardian_binary_up_to_date(src_size, *src_sha256, fallback_guard)) {
    std::filesystem::remove(staging, ec);
    return fallback_guard;
  }
  if (atomic_replace_file_with_retry(staging, fallback_guard)) {
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

  const std::string ts = format_local_timestamp();
  const std::string line =
      std::format("[{}][{}] [System] {}\r\n", ts, level ? level : "INFO", message);
  DWORD written = 0;
  ::WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
  ::FlushFileBuffers(h);
  ::CloseHandle(h);
}

void append_structured_guardian_log(const std::filesystem::path& log_dir,
                                    const char* level,
                                    std::string_view session_id,
                                    uint32_t parent_pid,
                                    std::optional<uint32_t> guard_pid,
                                    uint32_t gen,
                                    std::string_view event,
                                    std::string_view message) {
  const std::string gp_str =
      guard_pid.has_value() ? std::to_string(*guard_pid) : "-";
  const std::string line = std::format(
      "[{}] {} | session={} | parent_pid={} | guard_pid={} | gen={} | event={} | {}\r\n",
      format_local_timestamp(), level ? level : "INFO", session_id, parent_pid,
      gp_str, gen, event, message);

  if (log_dir.empty()) {
    write_guardian_fallback_log(line, "log_dir empty");
    return;
  }

  std::error_code ec;
  std::filesystem::create_directories(log_dir, ec);
  const std::filesystem::path log_file = log_dir / kGuardianLogFilename;
  HANDLE h = ::CreateFileW(log_file.c_str(), FILE_APPEND_DATA | SYNCHRONIZE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    const std::string err = std::format("CreateFileW gle={}", ::GetLastError());
    CWLOG_WARN("[Guardian] 写 logs/guardian.log 失败: " + err);
    write_guardian_fallback_log(line, err);
    return;
  }
  DWORD written = 0;
  const BOOL ok = ::WriteFile(h, line.data(), static_cast<DWORD>(line.size()),
                              &written, nullptr);
  ::FlushFileBuffers(h);
  ::CloseHandle(h);
  if (!ok || written != line.size()) {
    write_guardian_fallback_log(line, std::format("WriteFile gle={}", ::GetLastError()));
  }
}

bool write_intentional_exit_proof_in(const std::filesystem::path& guard_dir,
                                     uint32_t pid,
                                     std::string_view session_id,
                                     std::string_view phase,
                                     std::string_view reason,
                                     uint64_t now_unix_ms) {
  if (guard_dir.empty() || session_id.empty()) return false;
  const nlohmann::json j = {
      {"pid", pid},
      {"session_id", std::string(session_id)},
      {"phase", std::string(phase)},
      {"reason", std::string(reason)},
      {"timestamp_unix_ms", now_unix_ms},
  };
  return write_json_atomic(guard_dir / kIntentionalExitFilename, j);
}

IntentionalProofCheck evaluate_intentional_exit_proof_in(
    const std::filesystem::path& guard_dir,
    uint32_t parent_pid,
    std::string_view session_id,
    uint64_t now_unix_ms) {
  const std::filesystem::path path = guard_dir / kIntentionalExitFilename;
  std::error_code ec;
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) {
    if (!std::filesystem::exists(path, ec)) {
      return IntentionalProofCheck{IntentionalProofStatus::NotPresent, {}, {}};
    }
    return IntentionalProofCheck{IntentionalProofStatus::Rejected, {},
                                 "无法打开 intentional_exit.json"};
  }

  nlohmann::json j;
  try {
    ifs >> j;
  } catch (const std::exception& e) {
    ifs.close();
    std::filesystem::remove(path, ec);
    return IntentionalProofCheck{
        IntentionalProofStatus::Rejected, {},
        std::format("退出凭证 JSON 损坏并已清理: {}", e.what())};
  }
  ifs.close();

  const uint32_t proof_pid = j.value("pid", 0u);
  const std::string proof_sid = j.value("session_id", std::string{});
  const std::string proof_phase = j.value("phase", std::string{});
  const std::string proof_reason = j.value("reason", std::string{});
  const uint64_t proof_ts = j.value("timestamp_unix_ms", uint64_t{0});

  if (proof_pid != parent_pid || proof_sid != session_id) {
    return IntentionalProofCheck{
        IntentionalProofStatus::Rejected, proof_reason,
        std::format("退出凭证身份不匹配 (expected pid={}/session={}, got pid={}/session={})",
                    parent_pid, session_id, proof_pid, proof_sid)};
  }

  if (proof_ts > now_unix_ms + 5000ULL) {
    std::filesystem::remove(path, ec);
    return IntentionalProofCheck{
        IntentionalProofStatus::Rejected, proof_reason,
        std::format("退出凭证时间戳超前异常并已清理 (proof_ts={}, now={})",
                    proof_ts, now_unix_ms)};
  }

  const uint64_t ttl = (proof_phase == "confirmed")
                           ? kIntentionalExitConfirmedTtlMs
                           : kIntentionalExitProofTtlMs;
  const uint64_t age = (now_unix_ms >= proof_ts) ? (now_unix_ms - proof_ts) : 0ULL;
  if (age > ttl) {
    std::filesystem::remove(path, ec);
    return IntentionalProofCheck{
        IntentionalProofStatus::Rejected, proof_reason,
        std::format("退出凭证已过 TTL 有效期并清理 (phase={}, age={}ms > ttl={}ms)",
                    proof_phase, age, ttl)};
  }

  return IntentionalProofCheck{
      proof_phase == "confirmed" ? IntentionalProofStatus::ValidConfirmed
                                 : IntentionalProofStatus::ValidPrepared,
      proof_reason, {}};
}

void clear_intentional_exit_proof_in(const std::filesystem::path& guard_dir) {
  const std::filesystem::path path = guard_dir / kIntentionalExitFilename;
  std::error_code ec;
  if (!std::filesystem::remove(path, ec) && std::filesystem::exists(path, ec)) {
    ::Sleep(15);
    std::filesystem::remove(path, ec);
  }
}

void reset_respawn_state_for_clean_start_in(const std::filesystem::path& guard_dir,
                                            const std::filesystem::path& log_dir,
                                            std::string_view session_epoch_id,
                                            std::string_view boot_fingerprint) {
  if (guard_dir.empty()) return;
  const std::filesystem::path path = guard_dir / kRespawnStateFilename;
  RespawnSessionState prev{};
  std::error_code ec;
  if (std::ifstream ifs(path, std::ios::binary); ifs.is_open()) {
    try {
      nlohmann::json pj;
      ifs >> pj;
      prev.session_epoch_id = pj.value("session_epoch_id", std::string{});
      prev.boot_fingerprint = pj.value("boot_fingerprint", std::string{});
      prev.respawn_count = pj.value("respawn_count", 0u);
    } catch (...) {
    }
  }

  const uint32_t my_pid = ::GetCurrentProcessId();
  if (prev.respawn_count > 0 && !log_dir.empty()) {
    append_structured_guardian_log(
        log_dir, "INFO", session_epoch_id, my_pid, std::nullopt, 0,
        "RESPAWN_BUDGET_RESET_ON_CLEAN_START",
        std::format("干净冷启动复位前序累计复活计数 (cleared_count={}, old_epoch={}, old_boot={})",
                    prev.respawn_count, prev.session_epoch_id,
                    prev.boot_fingerprint));
  }

  const nlohmann::json state_j = {
      {"session_epoch_id", std::string(session_epoch_id)},
      {"boot_fingerprint", std::string(boot_fingerprint)},
      {"respawn_count", 0u},
      {"last_respawn_unix_ms", 0ULL},
      {"last_parent_pid", my_pid},
  };
  if (!write_json_atomic(path, state_j) && !log_dir.empty()) {
    append_structured_guardian_log(
        log_dir, "WARN", session_epoch_id, my_pid, std::nullopt, 0,
        "RESPAWN_STATE_RESET_FAILED", "干净冷启动写入 respawn_state.json 失败");
  }
}

Result<std::optional<uint32_t>> evaluate_and_record_respawn_in(
    const std::filesystem::path& guard_dir,
    std::string_view epoch_id,
    uint32_t cli_gen,
    uint32_t parent_pid,
    uint64_t now_unix_ms,
    std::string_view boot_fingerprint,
    bool commit) {
  const std::filesystem::path path = guard_dir / kRespawnStateFilename;
  RespawnSessionState state{};
  {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
      std::error_code ec;
      if (std::filesystem::exists(path, ec)) {
        return Result<std::optional<uint32_t>>::err(
            Error{1, "读取 respawn_state.json 失败"});
      }
      if (cli_gen > 0) {
        return Result<std::optional<uint32_t>>::err(Error{
            2, std::format("复活状态文件缺失且非干净冷启动 (cli_gen={})，fail-closed 拒绝拉起",
                           cli_gen)});
      }
    } else {
      try {
        nlohmann::json j;
        ifs >> j;
        state.session_epoch_id = j.value("session_epoch_id", std::string{});
        state.boot_fingerprint = j.value("boot_fingerprint", std::string{});
        state.respawn_count = j.value("respawn_count", 0u);
        state.last_respawn_unix_ms = j.value("last_respawn_unix_ms", uint64_t{0});
        state.last_parent_pid = j.value("last_parent_pid", 0u);
      } catch (const std::exception& e) {
        return Result<std::optional<uint32_t>>::err(
            Error{3, std::format("respawn_state.json 解析损坏: {}", e.what())});
      }
    }
  }  // 提前释放文件句柄，避免 Windows 上后续 atomic_replace_file_with_retry 因同进程占用失败

  const auto next_gen =
      evaluate_respawn_budget(state, cli_gen, epoch_id, boot_fingerprint);
  if (!next_gen.has_value()) {
    return Result<std::optional<uint32_t>>::ok(std::nullopt);
  }

  if (commit) {
    const std::string resolved_epoch =
        (!boot_fingerprints_match(state.boot_fingerprint, boot_fingerprint) ||
         state.session_epoch_id.empty())
            ? std::string(epoch_id)
            : state.session_epoch_id;
    const nlohmann::json updated = {
        {"session_epoch_id", resolved_epoch},
        {"boot_fingerprint", std::string(boot_fingerprint)},
        {"respawn_count", *next_gen},
        {"last_respawn_unix_ms", now_unix_ms},
        {"last_parent_pid", parent_pid},
    };
    if (!write_json_atomic(path, updated)) {
      return Result<std::optional<uint32_t>>::err(
          Error{4, "写入 respawn_state.json 失败 (Fail-Closed 拒绝拉起)"});
    }
  }

  return Result<std::optional<uint32_t>>::ok(next_gen);
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

  HWND owner_hwnd = ::CreateWindowExW(
      0, class_name, L"ClashWinResidentOwner", WS_POPUP, -32000, -32000, 1, 1,
      nullptr, nullptr, hinst, nullptr);
  if (!owner_hwnd) {
    CWLOG_WARN(std::format(
        "[Guardian] 创建哨兵 owner 窗口失败: gle={}", ::GetLastError()));
    return std::nullopt;
  }

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
  g_lightweight_active.store(initial_lightweight, std::memory_order_release);
  if (g_exit_event && g_lightweight_event) {
    if (initial_lightweight) {
      ::SetEvent(g_lightweight_event);
    } else {
      ::ResetEvent(g_lightweight_event);
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
  std::string inherited_epoch;
  if (const auto respawn = parse_respawn_info(args)) {
    current_gen = respawn->generation;
    inherited_epoch = respawn->session_epoch;
    CWLOG_WARN(std::format(
        "[Guardian] 本实例由伴生看门狗自愈拉起 (gen={}, 形态={}, 前序 PID={}, 前序退出码={})",
        respawn->generation, respawn->silent ? "silent" : "gui", respawn->prev_pid,
        format_exit_code_diagnosis(respawn->prev_exit)));
  }

  const std::filesystem::path guard_dir = resolve_isolated_guardian_dir();
  const uint32_t my_pid = ::GetCurrentProcessId();
  const uint64_t now_ms = current_unix_ms();
  const std::string session_id = std::format("s-{}-{}", my_pid, now_ms);
  const std::string boot_fp = current_boot_fingerprint();
  const std::string session_epoch =
      !inherited_epoch.empty() ? inherited_epoch : session_id;

  clear_intentional_exit_proof_in(guard_dir);
  if (current_gen == 0) {
    reset_respawn_state_for_clean_start_in(guard_dir, log_dir, session_epoch, boot_fp);
  }

  {
    std::lock_guard<std::mutex> lock(g_guardian_mutex);
    g_session_log_dir = log_dir;
    g_session_guard_dir = guard_dir;
    g_session_singleton_port = singleton_port;
    g_session_initial_gen = current_gen;
    g_session_id = session_id;
    g_session_epoch = session_epoch;
  }
  g_session_initialized.store(true, std::memory_order_release);

  launch_supervisor_thread_locked();
}

void ensure_guardian_supervisor() {
  if (const char* disable = std::getenv("CLASHWIN_DISABLE_GUARDIAN");
      disable && std::string_view(disable) == "1") {
    return;
  }
  if (!g_session_initialized.load(std::memory_order_acquire)) return;
  if (g_supervisor_started.load(std::memory_order_acquire)) return;
  HANDLE h_exit = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_guardian_mutex);
    h_exit = g_exit_event;
  }
  if (is_exit_event_signaled(h_exit) || is_system_shutting_down()) return;
  launch_supervisor_thread_locked();
}

void record_main_heartbeat_if_due() {
  if (!g_session_initialized.load(std::memory_order_acquire)) return;
  const uint64_t now_ms = current_unix_ms();
  const uint64_t last_ms = g_last_main_heartbeat_ms.load(std::memory_order_acquire);
  if (last_ms > 0 && now_ms >= last_ms && (now_ms - last_ms) < kHeartbeatIntervalMs) {
    return;
  }

  std::filesystem::path guard_dir;
  std::string sid;
  std::string epoch;
  uint32_t gen = 0;
  {
    std::lock_guard<std::mutex> lock(g_guardian_mutex);
    guard_dir = g_session_guard_dir;
    sid = g_session_id;
    epoch = g_session_epoch;
    gen = g_session_initial_gen;
  }
  write_main_heartbeat_now(guard_dir, sid, epoch, ::GetCurrentProcessId(), gen);
}

void set_lightweight_mode_signal(bool in_lightweight) {
  g_lightweight_active.store(in_lightweight, std::memory_order_release);
  HANDLE h_lw = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_guardian_mutex);
    h_lw = g_lightweight_event;
  }
  if (!h_lw) return;
  // A5 含回读核对
  for (int i = 0; i < 3; ++i) {
    if (in_lightweight) {
      ::SetEvent(h_lw);
      if (::WaitForSingleObject(h_lw, 0) == WAIT_OBJECT_0) break;
    } else {
      ::ResetEvent(h_lw);
      if (::WaitForSingleObject(h_lw, 0) == WAIT_TIMEOUT) break;
    }
  }
}

void notify_intentional_exit() {
  notify_intentional_exit_with_reason("intentional_exit");
}

void notify_intentional_exit_with_reason(std::string_view reason) {
  write_intentional_exit_phase("prepared", reason);
}

void confirm_intentional_exit_with_reason(std::string_view reason) {
  write_intentional_exit_phase("confirmed", reason);
}

void cancel_intentional_exit(std::string_view reason) {
  HANDLE h_ev = nullptr;
  std::filesystem::path guard_dir;
  std::filesystem::path log_dir;
  std::string sid;
  uint32_t gen = 0;
  {
    std::lock_guard<std::mutex> lock(g_guardian_mutex);
    h_ev = g_exit_event;
    guard_dir = g_session_guard_dir;
    log_dir = g_session_log_dir;
    sid = g_session_id;
    gen = g_session_initial_gen;
  }
  if (h_ev) ::ResetEvent(h_ev);
  if (!guard_dir.empty()) clear_intentional_exit_proof_in(guard_dir);
  if (!log_dir.empty() && !sid.empty()) {
    append_structured_guardian_log(
        log_dir, "INFO", sid, ::GetCurrentProcessId(),
        g_active_guard_pid.load() > 0
            ? std::optional<uint32_t>(g_active_guard_pid.load())
            : std::nullopt,
        gen, "INTENTIONAL_EXIT_CANCELLED",
        std::format("撤销退出凭证并重新确认看门狗监督 (reason={})", reason));
  }
  ensure_guardian_supervisor();
}

std::string get_diagnostic_summary() {
  if (!g_session_initialized.load(std::memory_order_acquire)) {
    return "未初始化（测试环境或已通过 CLASHWIN_DISABLE_GUARDIAN 禁用）";
  }
  const int p_job = g_parent_in_job_state.load(std::memory_order_acquire);
  const int g_job = g_guard_in_job_state.load(std::memory_order_acquire);
  const uint32_t g_pid = g_active_guard_pid.load(std::memory_order_acquire);
  const bool sup_up = g_supervisor_started.load(std::memory_order_acquire);
  const auto p_str = [](int v) -> const char* {
    return v < 0 ? "unknown" : (v > 0 ? "true" : "false");
  };
  return std::format(
      "broker={}, supervisor={}, guard_pid={}, gen={}, parent_in_job={}, guard_in_job={}, breakaway_degraded={}, lightweight={}",
      kBrokerBreakawayJob, sup_up ? "running" : "stopped",
      g_pid > 0 ? std::to_string(g_pid) : "none", g_session_initial_gen,
      p_str(p_job), p_str(g_job),
      g_breakaway_degraded.load(std::memory_order_acquire) ? "true" : "false",
      g_lightweight_active.load(std::memory_order_acquire) ? "true" : "false");
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
