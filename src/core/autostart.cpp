#include "core/autostart.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "config/config.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/str_utils.h"
#include "util/utf.h"

namespace clashwin::core {

namespace {

class WindowsAutostartAccess : public IAutostartAccess {
 public:
  Result<bool> is_enabled() override {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeySubPath, 0, KEY_READ, &key) != ERROR_SUCCESS) {
      return Result<bool>::ok(false);
    }
    DWORD type = 0, size = 0;
    LONG rc = ::RegQueryValueExW(key, kRunValueName, nullptr, &type, nullptr, &size);
    ::RegCloseKey(key);
    return Result<bool>::ok(rc == ERROR_SUCCESS);
  }

  Result<void> enable(const std::wstring& cmd) override {
    HKEY key = nullptr;
    LONG rc = ::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeySubPath, 0, KEY_SET_VALUE, &key);
    if (rc != ERROR_SUCCESS) {
      return Result<void>::err({static_cast<int>(rc), "RegOpenKeyExW failed"});
    }
    rc = ::RegSetValueExW(key, kRunValueName, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(cmd.c_str()),
                          static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    ::RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
      return Result<void>::err({static_cast<int>(rc), "RegSetValueExW failed"});
    }
    // ★ ❌6：同步写 StartupApproved\Run=启用，覆盖任务管理器禁用
    reconcile_startup_approved(true);
    return Result<void>::ok();
  }

  Result<void> disable() override {
    HKEY key = nullptr;
    LONG rc = ::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeySubPath, 0, KEY_SET_VALUE, &key);
    if (rc != ERROR_SUCCESS) {
      return Result<void>::ok();
    }
    ::RegDeleteValueW(key, kRunValueName);
    ::RegCloseKey(key);
    // ★ ❌6：自启关闭时一并清除 StartupApproved 批准位，保持与 Run 键一致
    reconcile_startup_approved(false);
    return Result<void>::ok();
  }

  Result<bool> delete_scheduled_task(const std::wstring& task_name) override {
    std::wstring cmd = L"schtasks.exe /Delete /TN \"" + task_name + L"\" /F";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
      ::WaitForSingleObject(pi.hProcess, 3000);
      DWORD exit_code = 1;
      ::GetExitCodeProcess(pi.hProcess, &exit_code);
      ::CloseHandle(pi.hThread);
      ::CloseHandle(pi.hProcess);
      return Result<bool>::ok(exit_code == 0);
    }
    return Result<bool>::ok(false);
  }

  std::wstring current_exe_path() override {
    wchar_t buf[MAX_PATH];
    DWORD len = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
      return std::wstring(buf, len);
    }
    return util::dirs::exe_dir() + L"\\clash-win.exe";
  }

  bool is_admin() override {
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD len = 0;
    const BOOL ok = ::GetTokenInformation(token, TokenElevation, &elevation,
                                          sizeof(elevation), &len);
    ::CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
  }

  std::optional<std::filesystem::path> system_tasks_dir() override {
    wchar_t buf[MAX_PATH];
    const DWORD n = ::GetEnvironmentVariableW(L"SystemRoot", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::nullopt;
    return std::filesystem::path(std::wstring(buf, n)) / L"System32" / L"Tasks";
  }

  // ★ ❌6 StartupApproved\Run\<name>：REG_BINARY，首字节 0x02=批准启用 / 0x03=禁用，
  //   余 11 字节为变更时间戳（置 0）。启用自启时写「批准启用」，使配置在任务管理器
  //   禁用后胜出重新启用；关闭自启时删除该批准位，保持与 Run 键一致。
  //   ← Mini §2.10.1（enable 时同步写 StartupApproved\Run=启用）。
  void reconcile_startup_approved(bool enabled) {
    HKEY key = nullptr;
    LONG rc = ::RegCreateKeyExW(HKEY_CURRENT_USER, kStartupApprovedKeySubPath, 0,
                                nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                                nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) return;
    if (enabled) {
      BYTE blob[12] = {0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
      ::RegSetValueExW(key, kRunValueName, 0, REG_BINARY, blob, sizeof(blob));
    } else {
      ::RegDeleteValueW(key, kRunValueName);
    }
    ::RegCloseKey(key);
  }
};

}  // namespace

IAutostartAccess* windows_autostart_access() {
  static WindowsAutostartAccess s_instance;
  return &s_instance;
}

AutostartManager& AutostartManager::instance() {
  static AutostartManager s_manager;
  return s_manager;
}

AutostartManager::AutostartManager() : access_(windows_autostart_access()) {}

void AutostartManager::set_access(IAutostartAccess* access) {
  access_ = access ? access : windows_autostart_access();
}

IAutostartAccess* AutostartManager::access() {
  return access_;
}

Result<void> AutostartManager::update_launch(std::optional<bool> enable_auto_launch) {
  if (!access_) return Result<void>::err({-1, "AutostartAccess not set"});
  bool is_enable = false;
  if (enable_auto_launch.has_value()) {
    is_enable = *enable_auto_launch;
  } else {
    auto verge = config::Config::instance().verge().latest_arc();
    if (verge && verge->enable_auto_launch.has_value()) {
      is_enable = *verge->enable_auto_launch;
    }
  }

  if (is_enable) {
    std::wstring exe = access_->current_exe_path();
    std::wstring cmd;
    if (!exe.empty() && exe.front() == L'\"' && exe.back() == L'\"') {
      cmd = exe + L" --silent";
    } else {
      cmd = L"\"" + exe + L"\" --silent";
    }
    CWLOG_INFO("AutostartManager::update_launch: enabling with cmd=" +
               utf::utf16_to_utf8(cmd));
    return access_->enable(cmd);
  } else {
    auto current = access_->is_enabled();
    if (current.is_ok() && current.value()) {
      CWLOG_INFO("AutostartManager::update_launch: disabling");
      return access_->disable();
    }
    return Result<void>::ok();
  }
}

Result<bool> AutostartManager::get_launch_status() {
  if (!access_) return Result<bool>::err({-1, "AutostartAccess not set"});
  return access_->is_enabled();
}

bool should_skip_cleanup_from_marker(const std::optional<std::string>& marker_content,
                                     bool is_admin) {
  // ← autostart.rs should_skip_cleanup_from_marker（BUG-414）
  if (!marker_content.has_value()) return false;
  const std::string m = util::trim(*marker_content);
  if (m == kMarkerDone) return true;
  if (m == kMarkerPendingAdmin) return !is_admin;
  return false;
}

bool is_task_file_definitely_absent(const std::optional<std::filesystem::path>& tasks_dir,
                                    const std::wstring& task_name) {
  // ← autostart.rs is_task_file_definitely_absent（BUG-414）
  if (!tasks_dir.has_value()) return false;
  std::error_code ec;
  if (!std::filesystem::is_directory(*tasks_dir, ec) || ec) return false;
  const auto st = std::filesystem::symlink_status(*tasks_dir / task_name, ec);
  if (ec) {
    return ec == std::errc::no_such_file_or_directory;
  }
  return st.type() == std::filesystem::file_type::not_found;
}

void AutostartManager::cleanup_legacy_scheduled_tasks() {
  if (!access_) return;
  std::wstring home = util::dirs::app_home_dir();
  std::wstring marker = home + L"\\" + kAutostartMigrationMarker;

  std::optional<std::string> existing_marker;
  {
    std::ifstream ifs(std::filesystem::path(marker), std::ios::binary);
    if (ifs.is_open()) {
      existing_marker = std::string((std::istreambuf_iterator<char>(ifs)),
                                    std::istreambuf_iterator<char>());
    }
  }
  const bool is_admin = access_->is_admin();
  if (should_skip_cleanup_from_marker(existing_marker, is_admin)) {
    return;
  }

  const auto tasks_dir = access_->system_tasks_dir();
  bool all_clean = true;
  for (const auto* name : kLegacyTaskNames) {
    if (is_task_file_definitely_absent(tasks_dir, name)) {
      continue;
    }
    auto r = access_->delete_scheduled_task(name);
    if (r.is_ok() && r.value()) {
      CWLOG_INFO("AutostartManager: cleaned legacy task " + utf::utf16_to_utf8(name));
    } else if (is_task_file_definitely_absent(tasks_dir, name)) {
      CWLOG_DEBUG("AutostartManager: legacy task already absent " + utf::utf16_to_utf8(name));
    } else {
      all_clean = false;
      CWLOG_WARN("AutostartManager: legacy task cannot be deleted now (may need admin): " +
                 utf::utf16_to_utf8(name));
    }
  }

  // 确保父目录存在
  std::error_code ec;
  std::filesystem::create_directories(home, ec);

  const std::string next_marker = all_clean ? kMarkerDone : kMarkerPendingAdmin;
  HANDLE h = ::CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    ::WriteFile(h, next_marker.data(), static_cast<DWORD>(next_marker.size()), &written, nullptr);
    ::CloseHandle(h);
    CWLOG_INFO("AutostartManager: migration marker written: " + next_marker);
  } else {
    CWLOG_WARN("AutostartManager: failed to write migration marker, gle=" +
               std::to_string(::GetLastError()));
  }
}

Result<void> AutostartManager::reconcile_on_startup() {
  cleanup_legacy_scheduled_tasks();
  return update_launch(std::nullopt);
}

void AutostartManager::reset_state() {
  access_ = windows_autostart_access();
}

}  // namespace clashwin::core
