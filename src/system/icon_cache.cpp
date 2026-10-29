#include "system/icon_cache.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

#include "config/ssrf.h"
#include "system/net_http.h"
#include "util/dirs.h"
#include "util/utf.h"

namespace clashwin::system {

namespace {
namespace errs {
constexpr int kBadArgs = 2901;
constexpr int kIo = 2906;
}  // namespace errs
}  // namespace

Result<std::vector<uint8_t>> DefaultIconCacheFetcher::fetch(const std::string& url) {
  HttpOptions opts;
  opts.timeout_seconds = 15;
  auto res = http_get(url, opts);
  if (!res.is_ok()) {
    return Result<std::vector<uint8_t>>::err(res.error());
  }
  if (!res.value().is_success()) {
    return Result<std::vector<uint8_t>>::err({errs::kIo, "HTTP " + std::to_string(res.value().status)});
  }
  const auto& body = res.value().body;
  std::vector<uint8_t> bytes(body.begin(), body.end());
  return Result<std::vector<uint8_t>>::ok(std::move(bytes));
}

IconCache::IconCache() : fetcher_(std::make_shared<DefaultIconCacheFetcher>()) {}

IconCache& IconCache::instance() {
  static IconCache s_instance;
  return s_instance;
}

void IconCache::set_fetcher(std::shared_ptr<IIconCacheFetcher> fetcher) {
  fetcher_ = std::move(fetcher);
}

std::shared_ptr<IIconCacheFetcher> IconCache::fetcher() const {
  return fetcher_;
}

bool IconCache::is_valid_icon_name(const std::string& name) {
  std::string_view s = name;
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
  if (s.empty()) return false;
  if (s.find('/') != std::string_view::npos || s.find('\\') != std::string_view::npos || s.find("..") != std::string_view::npos) return false;
  for (char c : s) {
    if (c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == ':') return false;
    if (static_cast<unsigned char>(c) < 32) return false;
  }
  return true;
}

bool IconCache::is_ssrf_safe_url(const std::string& url) {
  auto parts = parse_http_url(url);
  if (!parts.is_ok()) return false;
  if (parts.value().scheme != "http" && parts.value().scheme != "https") return false;
  auto r = config::ssrf::validate_url_no_ssrf(parts.value());
  return r.is_ok();
}

bool IconCache::is_supported_icon_content(const std::vector<uint8_t>& bytes) {
  if (bytes.empty()) return false;

  // Trim UTF-8 BOM
  size_t start = 0;
  if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
    start = 3;
  }
  // Skip ASCII whitespace
  while (start < bytes.size() && (bytes[start] == ' ' || bytes[start] == '\t' || bytes[start] == '\r' || bytes[start] == '\n')) {
    start++;
  }

  // Check prefix text
  size_t end = std::min(bytes.size(), start + 2048);
  std::string prefix;
  for (size_t i = start; i < end; ++i) {
    prefix.push_back(static_cast<char>(std::tolower(bytes[i])));
  }

  // Reject HTML
  if (prefix.rfind("<!doctype html", 0) == 0 ||
      prefix.rfind("<html", 0) == 0 ||
      prefix.rfind("<head", 0) == 0) {
    return false;
  }

  // SVG check
  if (prefix.rfind("<svg", 0) == 0 ||
      ((prefix.rfind("<?xml", 0) == 0 || prefix.rfind("<!doctype svg", 0) == 0) &&
       prefix.find("<svg") != std::string::npos)) {
    return true;
  }

  // PNG magic: 0x89 0x50 0x4E 0x47 0x0D 0x0A 0x1A 0x0A
  if (bytes.size() >= 8 &&
      bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47 &&
      bytes[4] == 0x0D && bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A) {
    return true;
  }

  // ICO magic: 0x00 0x00 0x01 0x00
  if (bytes.size() >= 4 &&
      bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0x01 && bytes[3] == 0x00) {
    return true;
  }

  // JPEG magic: 0xFF 0xD8 0xFF
  if (bytes.size() >= 3 &&
      bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
    return true;
  }

  // GIF magic: "GIF87a" or "GIF89a"
  if (bytes.size() >= 6 &&
      bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == '8' &&
      (bytes[4] == '7' || bytes[4] == '9') && bytes[5] == 'a') {
    return true;
  }

  // WebP magic: "RIFF" .... "WEBP"
  if (bytes.size() >= 12 &&
      bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
      bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P') {
    return true;
  }

  return false;
}

Result<std::string> IconCache::download_icon_cache(const std::string& url, const std::string& name) {
  if (!is_valid_icon_name(name)) {
    return Result<std::string>::err({errs::kBadArgs, "invalid icon cache file name"});
  }
  if (!is_ssrf_safe_url(url)) {
    return Result<std::string>::err({errs::kBadArgs, "cannot download icon: SSRF protection or invalid scheme"});
  }

  std::wstring home = util::dirs::app_home_dir();
  std::wstring cache_dir = home + L"\\icons\\cache";
  std::error_code ec;
  std::filesystem::create_directories(cache_dir, ec);

  std::wstring target_path = cache_dir + L"\\" + utf::utf8_to_utf16(name);
  if (std::filesystem::exists(target_path, ec) && std::filesystem::file_size(target_path, ec) > 0) {
    return Result<std::string>::ok(utf::utf16_to_utf8(target_path));
  }

  std::wstring temp_path = cache_dir + L"\\" + utf::utf8_to_utf16(name) + L".downloading";

  if (!fetcher_) {
    return Result<std::string>::err({errs::kIo, "no icon fetcher configured"});
  }
  auto res = fetcher_->fetch(url);
  if (!res.is_ok()) {
    return Result<std::string>::err(res.error());
  }

  const auto& bytes = res.value();
  if (!is_supported_icon_content(bytes)) {
    std::filesystem::remove(temp_path, ec);
    return Result<std::string>::err({errs::kIo, "Downloaded content is not a valid image: " + url});
  }

  {
    std::ofstream out(temp_path, std::ios::binary);
    if (!out.is_open()) {
      return Result<std::string>::err({errs::kIo, "failed to create temporary download file"});
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
  }

  std::filesystem::rename(temp_path, target_path, ec);
  if (ec) {
    std::filesystem::remove(temp_path, ec);
    if (!std::filesystem::exists(target_path, ec)) {
      return Result<std::string>::err({errs::kIo, "failed to finalize icon cache file"});
    }
  }

  return Result<std::string>::ok(utf::utf16_to_utf8(target_path));
}

Result<std::string> IconCache::copy_icon_file(const std::string& src_path, const IconInfo& info) {
  if (!is_valid_icon_name(info.name) || !is_valid_icon_name(info.current_t)) {
    return Result<std::string>::err({errs::kBadArgs, "invalid icon name or current_t"});
  }
  if (!info.previous_t.empty() && !is_valid_icon_name(info.previous_t)) {
    return Result<std::string>::err({errs::kBadArgs, "invalid previous_t"});
  }

  std::wstring src_wpath = utf::utf8_to_utf16(src_path);
  std::error_code ec;
  if (!std::filesystem::exists(src_wpath, ec)) {
    return Result<std::string>::err({errs::kIo, "file not found"});
  }

  std::wstring home = util::dirs::app_home_dir();
  std::wstring icons_dir = home + L"\\icons";
  std::filesystem::create_directories(icons_dir, ec);

  std::filesystem::path p(src_wpath);
  std::string ext = p.extension().string();
  if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
  if (ext.empty()) ext = "ico";

  std::string dest_name = info.name + "-" + info.current_t + "." + ext;
  std::wstring dest_wpath = icons_dir + L"\\" + utf::utf8_to_utf16(dest_name);

  if (!info.previous_t.empty()) {
    std::wstring prev_png = icons_dir + L"\\" + utf::utf8_to_utf16(info.name + "-" + info.previous_t + ".png");
    std::filesystem::remove(prev_png, ec);
    std::wstring prev_ico = icons_dir + L"\\" + utf::utf8_to_utf16(info.name + "-" + info.previous_t + ".ico");
    std::filesystem::remove(prev_ico, ec);
  }

  std::filesystem::copy_file(src_wpath, dest_wpath, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    return Result<std::string>::err({errs::kIo, "failed to copy icon file: " + ec.message()});
  }

  return Result<std::string>::ok(utf::utf16_to_utf8(dest_wpath));
}

Result<void> IconCache::cleanup_icon_cache(size_t max_files) {
  std::wstring home = util::dirs::app_home_dir();
  std::wstring cache_dir = home + L"\\icons\\cache";
  std::error_code ec;
  if (!std::filesystem::exists(cache_dir, ec)) {
    return Result<void>::ok();
  }

  struct Entry {
    std::filesystem::path path;
    std::filesystem::file_time_type mtime;
  };
  std::vector<Entry> entries;

  for (const auto& entry : std::filesystem::directory_iterator(cache_dir, ec)) {
    if (entry.is_regular_file(ec)) {
      entries.push_back({entry.path(), entry.last_write_time(ec)});
    }
  }

  if (entries.size() > max_files) {
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
      return a.mtime < b.mtime;
    });
    size_t to_delete = entries.size() - max_files;
    for (size_t i = 0; i < to_delete; ++i) {
      std::filesystem::remove(entries[i].path, ec);
    }
  }

  return Result<void>::ok();
}

}  // namespace clashwin::system
