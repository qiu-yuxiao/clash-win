#include "util/log.h"
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <mutex>

namespace clashwin {

namespace {
std::mutex g_mutex;  // 单例构造前即使用，静态生存期安全
}  // namespace

Logger& Logger::instance() {
  static Logger inst;
  return inst;
}

void Logger::init(const std::wstring& exe_dir) {
  std::lock_guard lk(g_mutex);
  if (inited_) return;
  const std::wstring dir = exe_dir + L"\\logs";
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(dir), ec);
  const std::wstring path = dir + L"\\clashwin.log";
  HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;  // 静默降级：日志不可用不影响主流程
  file_ = h;
  inited_ = true;
}

void Logger::write(const char* level, const std::string& msg) {
  std::lock_guard lk(g_mutex);
  if (!inited_) return;
  SYSTEMTIME t;
  GetLocalTime(&t);
  char line[64];
  snprintf(line, sizeof(line), "[%04u-%02u-%02u %02u:%02u:%02u.%03u][%s] ",
           t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
           t.wMilliseconds, level);
  const std::string text = line + msg + "\r\n";
  DWORD written = 0;
  WriteFile(file_, text.data(), static_cast<DWORD>(text.size()), &written,
            nullptr);
}

void Logger::flush() {
  std::lock_guard lk(g_mutex);
  if (inited_ && file_) FlushFileBuffers(file_);
}

void Logger::shutdown() {
  std::lock_guard lk(g_mutex);
  if (inited_ && file_) {
    FlushFileBuffers(file_);
    CloseHandle(file_);
  }
  file_ = nullptr;
  inited_ = false;
}

}  // namespace clashwin
