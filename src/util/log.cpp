#include "util/log.h"
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <vector>

namespace clashwin {

namespace fs = std::filesystem;

namespace {
std::mutex g_mutex;  // 单例构造前即使用，静态生存期安全
constexpr size_t kMaxPendingLines = 512;

std::string lower_ascii(std::string s) {
  for (char& ch : s) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return s;
}

bool iequals_w(const std::wstring& a, const std::wstring& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (towlower(a[i]) != towlower(b[i])) return false;
  }
  return true;
}

HANDLE open_append_log(const std::wstring& path, uint64_t* out_size) {
  HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
  if (out_size) {
    LARGE_INTEGER sz{};
    *out_size = GetFileSizeEx(h, &sz) && sz.QuadPart > 0
                    ? static_cast<uint64_t>(sz.QuadPart)
                    : 0ull;
  }
  return h;
}
}  // namespace

Logger& Logger::instance() {
  static Logger inst;
  return inst;
}

int Logger::parse_level(const char* level) {
  if (!level) return 2;
  std::string l = lower_ascii(level);
  if (l == "trace") return 0;
  if (l == "debug") return 1;
  if (l == "info") return 2;
  if (l == "warn" || l == "warning") return 3;
  if (l == "error") return 4;
  return 2;
}

int Logger::parse_level_config(const std::string& level_str) {
  std::string l = lower_ascii(level_str);
  if (l.empty() || l == "info") return 2;
  if (l == "trace") return 0;
  if (l == "debug") return 1;
  if (l == "warn" || l == "warning") return 3;
  if (l == "error") return 4;
  if (l == "silent" || l == "off" || l == "none") return 5;
  return 2;
}

int Logger::auto_clean_days(int mode) {
  switch (mode) {
    case 1: return 1;
    case 2: return 7;
    case 3: return 30;
    case 4: return 90;
    default: return 0;
  }
}

void Logger::init(const std::wstring& home_dir) {
  std::lock_guard lk(g_mutex);
  if (inited_) return;
  if (home_dir.empty()) return;

  base_dir_ = home_dir;
  logs_dir_ = home_dir + L"\\logs";
  latest_path_ = logs_dir_ + L"\\latest.log";

  std::error_code ec;
  fs::create_directories(fs::path(logs_dir_), ec);

  // ← src-tauri/src/core/logger.rs：清理遗留 temp 快照目录
  fs::path temp_dir = fs::path(home_dir) / L"temp";
  if (fs::exists(temp_dir, ec)) {
    fs::remove_all(temp_dir, ec);
  }

  uint64_t sz = 0;
  HANDLE h = open_append_log(latest_path_, &sz);
  if (h == INVALID_HANDLE_VALUE) return;  // 静默降级：日志不可用不影响主流程
  file_ = h;
  current_bytes_ = sz;
  inited_ = true;

  // 刷新 init() 前的早期缓冲行
  if (!pending_.empty()) {
    auto buffered = std::move(pending_);
    pending_.clear();
    for (const auto& item : buffered) {
      if (item.level < min_level_) continue;
      rotate_if_needed_locked(item.text.size());
      if (file_) {
        DWORD written = 0;
        if (WriteFile(file_, item.text.data(),
                      static_cast<DWORD>(item.text.size()), &written, nullptr)) {
          current_bytes_ += written;
        }
      }
    }
  }
}

void Logger::configure(uint64_t max_size_kb, size_t max_count,
                       const std::string& level_str, int auto_clean_mode,
                       bool run_cleanup) {
  std::lock_guard lk(g_mutex);
  max_size_bytes_ = max_size_kb * 1024ull;
  max_count_ = max_count;
  min_level_ = parse_level_config(level_str);
  auto_clean_mode_ = auto_clean_mode;
  if (inited_) {
    rotate_if_needed_locked(0);
    if (run_cleanup) {
      clean_expired_logs_locked();
    }
  }
}

void Logger::rotate_if_needed_locked(size_t incoming_bytes) {
  if (!inited_ || !file_ || max_size_bytes_ == 0 || current_bytes_ == 0) return;
  if (current_bytes_ + incoming_bytes <= max_size_bytes_) return;

  FlushFileBuffers(file_);
  CloseHandle(file_);
  file_ = nullptr;

  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t stamp[64];
  swprintf_s(stamp, L"%04u-%02u-%02u_%02u-%02u-%02u",
             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  std::wstring archive = logs_dir_ + L"\\" + stamp + L".log";
  std::error_code ec;
  for (int idx = 1; fs::exists(fs::path(archive), ec) && idx <= 99; ++idx) {
    archive = logs_dir_ + L"\\" + stamp + L"_" + std::to_wstring(idx) + L".log";
  }
  MoveFileExW(latest_path_.c_str(), archive.c_str(), MOVEFILE_REPLACE_EXISTING);

  prune_rotated_logs_locked();

  uint64_t sz = 0;
  HANDLE h = open_append_log(latest_path_, &sz);
  if (h != INVALID_HANDLE_VALUE) {
    file_ = h;
    current_bytes_ = sz;
  } else {
    current_bytes_ = 0;
  }
}

void Logger::prune_rotated_logs_locked() {
  if (logs_dir_.empty()) return;
  std::error_code ec;
  struct Entry {
    fs::path path;
    fs::file_time_type mtime;
  };
  std::vector<Entry> rotated;
  for (const auto& de : fs::directory_iterator(fs::path(logs_dir_), ec)) {
    if (ec) break;
    if (!de.is_regular_file(ec)) continue;
    const auto fn = de.path().filename().wstring();
    if (de.path().extension() != L".log") continue;
    if (iequals_w(fn, L"latest.log")) continue;
    auto mt = de.last_write_time(ec);
    if (!ec) rotated.push_back({de.path(), mt});
  }
  if (rotated.size() <= max_count_) return;
  std::sort(rotated.begin(), rotated.end(),
            [](const Entry& a, const Entry& b) { return a.mtime < b.mtime; });
  const size_t remove_count = rotated.size() - max_count_;
  for (size_t i = 0; i < remove_count; ++i) {
    fs::remove(rotated[i].path, ec);
  }
}

void Logger::clean_expired_logs_locked() {
  const int days = auto_clean_days(auto_clean_mode_);
  if (days <= 0 || logs_dir_.empty()) return;

  const auto cutoff =
      fs::file_time_type::clock::now() - std::chrono::hours(24ll * days);
  const std::vector<fs::path> dirs = {
      fs::path(logs_dir_),
      fs::path(logs_dir_) / L"sidecar",
      fs::path(logs_dir_) / L"service",
  };
  std::error_code ec;
  for (const auto& d : dirs) {
    if (!fs::exists(d, ec) || !fs::is_directory(d, ec)) continue;
    for (const auto& de : fs::directory_iterator(d, ec)) {
      if (ec) break;
      if (!de.is_regular_file(ec)) continue;
      if (de.path().extension() != L".log") continue;
      const auto fn = de.path().filename().wstring();
      if (iequals_w(fn, L"latest.log") ||
          iequals_w(fn, L"sidecar_latest.log") ||
          iequals_w(fn, L"service_latest.log")) {
        continue;
      }
      auto mt = de.last_write_time(ec);
      if (!ec && mt < cutoff) {
        fs::remove(de.path(), ec);
      }
    }
  }
}

void Logger::write(const char* level, const std::string& msg) {
  std::lock_guard lk(g_mutex);
  const int lvl = parse_level(level);
  SYSTEMTIME t;
  GetLocalTime(&t);
  char line[64];
  snprintf(line, sizeof(line), "[%04u-%02u-%02u %02u:%02u:%02u.%03u][%s] ",
           t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
           t.wMilliseconds, level ? level : "INFO");
  std::string text = line + msg + "\r\n";

  if (!inited_) {
    if (pending_.size() < kMaxPendingLines) {
      pending_.push_back({lvl, std::move(text)});
    }
    return;
  }
  if (lvl < min_level_) return;

  rotate_if_needed_locked(text.size());
  if (!file_) return;

  DWORD written = 0;
  if (WriteFile(file_, text.data(), static_cast<DWORD>(text.size()), &written,
                nullptr)) {
    current_bytes_ += written;
  }
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
  current_bytes_ = 0;
  pending_.clear();
}

}  // namespace clashwin
