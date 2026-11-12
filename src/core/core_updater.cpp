#include "core/core_updater.h"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

#include "clashwin/config/constants.h"
#include "events/event_bus.h"
#include "system/net_http.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/str_utils.h"
#include "util/utf.h"

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")

namespace clashwin::core {

namespace {

using clashwin::utf::utf16_to_utf8;
using clashwin::utf::utf8_to_utf16;
using util::ends_with;
using util::starts_with;
using util::to_lower;
using util::trim;

inline Error make_err(const std::string& msg, int code = 1) {
  return Error{code, msg};
}

class DefaultCoreUpdateDownloader : public ICoreUpdateDownloader {
 public:
  Result<std::string> fetch_latest_release_json() override {
    const std::string url = "https://api.github.com/repos/MetaCubeX/mihomo/releases/latest";
    const std::vector<system::ProxyType> proxy_types = {
        system::ProxyType::Localhost, system::ProxyType::System, system::ProxyType::None};

    for (auto pt : proxy_types) {
      system::HttpOptions opts;
      opts.proxy_type = pt;
      opts.timeout_seconds = 15;
      opts.user_agent = "ClashWin-CoreUpdater";
      auto res = system::http_get(url, opts);
      if (res.is_ok() && res.value().is_success()) {
        return Result<std::string>::ok(res.value().body);
      }
    }
    return Result<std::string>::err(make_err("failed to query GitHub release API across all proxy modes"));
  }

  Result<std::string> fetch_checksum_content(const std::string& url) override {
    const std::vector<system::ProxyType> proxy_types = {
        system::ProxyType::Localhost, system::ProxyType::System, system::ProxyType::None};

    for (auto pt : proxy_types) {
      system::HttpOptions opts;
      opts.proxy_type = pt;
      opts.timeout_seconds = 20;
      opts.user_agent = "ClashWin-CoreUpdater";
      auto res = system::http_get(url, opts);
      if (res.is_ok() && res.value().is_success()) {
        return Result<std::string>::ok(res.value().body);
      }
    }
    return Result<std::string>::err(make_err("failed to download checksums file across all proxy modes"));
  }

  Result<void> download_asset(
      const std::string& url, const std::wstring& dest_path,
      std::function<void(uint64_t downloaded, uint64_t total)> on_progress) override {
    const std::vector<system::ProxyType> proxy_types = {
        system::ProxyType::Localhost, system::ProxyType::System, system::ProxyType::None};

    for (auto pt : proxy_types) {
      auto r = download_via_winhttp(url, dest_path, pt, on_progress);
      if (r.is_ok()) return r;
    }
    return Result<void>::err(make_err("所有网络连接（代理/直连）均下载失败"));
  }

 private:
  Result<void> download_via_winhttp(
      const std::string& url, const std::wstring& dest_path, system::ProxyType proxy_type,
      std::function<void(uint64_t downloaded, uint64_t total)> on_progress) {
    auto parsed = system::parse_http_url(url);
    if (!parsed.is_ok()) return Result<void>::err(make_err("invalid download url"));

    HINTERNET session = WinHttpOpen(
        L"ClashWin-CoreUpdater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return Result<void>::err(make_err("WinHttpOpen failed"));

    std::wstring proxy_str;
    if (proxy_type == system::ProxyType::Localhost) {
      proxy_str = L"127.0.0.1:" + std::to_wstring(static_cast<uint32_t>(config::consts::kDefaultMixed));
    } else if (proxy_type == system::ProxyType::System) {
      auto sys = system::read_system_proxy();
      if (sys.has_value()) proxy_str = utf8_to_utf16(*sys);
    }

    std::string current_url = url;
    int redirects = 0;
    while (redirects < 10) {
      auto p = system::parse_http_url(current_url);
      if (!p.is_ok()) {
        WinHttpCloseHandle(session);
        return Result<void>::err(make_err("parse redirect failed"));
      }
      auto parts = p.value();
      HINTERNET connect = WinHttpConnect(
          session, utf8_to_utf16(parts.host).c_str(), parts.effective_port(), 0);
      if (!connect) {
        WinHttpCloseHandle(session);
        return Result<void>::err(make_err("WinHttpConnect failed"));
      }

      const bool is_https = (parts.scheme == "https");
      HINTERNET request = WinHttpOpenRequest(
          connect, L"GET", utf8_to_utf16(parts.request_target()).c_str(),
          nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
          is_https ? WINHTTP_FLAG_SECURE : 0);
      if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return Result<void>::err(make_err("WinHttpOpenRequest failed"));
      }

      if (!proxy_str.empty()) {
        WINHTTP_PROXY_INFO pi{};
        pi.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        pi.lpszProxy = const_cast<LPWSTR>(proxy_str.c_str());
        WinHttpSetOption(request, WINHTTP_OPTION_PROXY, &pi, sizeof(pi));
      }

      DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
      WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
      WinHttpSetTimeouts(request, 15000, 15000, 30000, 30000);

      if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
          !WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return Result<void>::err(make_err("WinHttp request failed"));
      }

      DWORD status_code = 0;
      DWORD size = sizeof(status_code);
      WinHttpQueryHeaders(request,
                          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &size,
                          WINHTTP_NO_HEADER_INDEX);

      if (status_code == 301 || status_code == 302 || status_code == 303 ||
          status_code == 307 || status_code == 308) {
        wchar_t loc[2048] = {0};
        DWORD loc_size = sizeof(loc);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION,
                                WINHTTP_HEADER_NAME_BY_INDEX, loc, &loc_size,
                                WINHTTP_NO_HEADER_INDEX)) {
          current_url = utf16_to_utf8(loc);
          WinHttpCloseHandle(request);
          WinHttpCloseHandle(connect);
          redirects++;
          continue;
        }
      }

      if (status_code != 200) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return Result<void>::err(make_err("HTTP error: " + std::to_string(status_code)));
      }

      // Read Content-Length
      uint64_t total_size = 0;
      wchar_t cl_buf[64] = {0};
      DWORD cl_size = sizeof(cl_buf);
      if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH,
                              WINHTTP_HEADER_NAME_BY_INDEX, cl_buf, &cl_size,
                              WINHTTP_NO_HEADER_INDEX)) {
        total_size = std::wcstoull(cl_buf, nullptr, 10);
      }

      // Write stream to file
      std::ofstream out(dest_path, std::ios::binary);
      if (!out.is_open()) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return Result<void>::err(make_err("failed to open output destination file"));
      }

      uint64_t downloaded = 0;
      std::vector<char> buffer(65536);
      DWORD bytes_read = 0;
      while (WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read) &&
             bytes_read > 0) {
        out.write(buffer.data(), bytes_read);
        downloaded += bytes_read;
        if (on_progress) {
          on_progress(downloaded, total_size);
        }
      }
      out.close();

      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      WinHttpCloseHandle(session);
      return Result<void>::ok();
    }

    WinHttpCloseHandle(session);
    return Result<void>::err(make_err("too many redirects"));
  }
};

}  // namespace

CoreUpdater::CoreUpdater()
    : downloader_(std::make_shared<DefaultCoreUpdateDownloader>()) {}

CoreUpdater& CoreUpdater::instance() {
  static CoreUpdater inst;
  return inst;
}

void CoreUpdater::set_downloader(std::shared_ptr<ICoreUpdateDownloader> downloader) {
  downloader_ = std::move(downloader);
}

void CoreUpdater::set_core_api_client(ICoreApiClient* client) {
  api_client_ = client;
}

void CoreUpdater::set_lifecycle_ops(CoreOps ops) {
  lifecycle_ops_ = std::move(ops);
}

void CoreUpdater::reset_lifecycle_ops() { lifecycle_ops_.reset(); }

void CoreUpdater::emit_progress(const std::string& status, uint32_t progress,
                               const std::string& message) {
  CoreUpgradeProgress p{status, progress, message};
  events::EventBus::instance().publish("core-upgrade-progress", p.to_json());
}

std::optional<GithubAsset> CoreUpdater::match_target_asset(const GithubRelease& release) {
  const std::string target_prefix = "mihomo-windows-amd64";
  const std::string exact_gz = "mihomo-windows-amd64.exe.gz";
  const std::string exact_zip = target_prefix + "-" + release.tag_name + ".zip";

  // Exact matching first
  for (const auto& a : release.assets) {
    std::string name_lower = to_lower(a.name);
    if (name_lower == to_lower(exact_zip) || name_lower == exact_gz) {
      return a;
    }
  }

  // Fallback matching
  for (const auto& a : release.assets) {
    std::string name_lower = to_lower(a.name);
    if (starts_with(name_lower, target_prefix) &&
        (ends_with(name_lower, ".zip") || ends_with(name_lower, ".gz"))) {
      return a;
    }
  }

  return std::nullopt;
}

std::optional<std::string> CoreUpdater::find_checksum_url(const GithubRelease& release) {
  const std::vector<std::string> checksum_names = {
      "checksums.txt", "sha256sums.txt", "sha256sum.txt"};
  for (const auto& a : release.assets) {
    std::string lower = to_lower(a.name);
    for (const auto& name : checksum_names) {
      if (lower == name) {
        return a.browser_download_url;
      }
    }
  }
  return std::nullopt;
}

std::optional<std::string> CoreUpdater::parse_checksum_for_asset(
    const std::string& checksum_text, const std::string& asset_name) {
  std::istringstream stream(checksum_text);
  std::string line;
  const std::string target = to_lower(asset_name);

  while (std::getline(stream, line)) {
    std::string l = trim(line);
    if (l.empty() || l[0] == '#') continue;

    std::istringstream ls(l);
    std::string sha;
    std::string filename;
    if (!(ls >> sha >> filename)) continue;

    if (sha.size() != 64) continue;
    if (starts_with(filename, "*")) {
      filename = filename.substr(1);
    }
    // Remove directory path if present
    size_t slash = filename.find_last_of("/\\");
    if (slash != std::string::npos) {
      filename = filename.substr(slash + 1);
    }

    if (to_lower(filename) == target) {
      return to_lower(sha);
    }
  }
  return std::nullopt;
}

Result<std::string> CoreUpdater::compute_file_sha256(const std::wstring& file_path) {
  HANDLE hFile = CreateFileW(file_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    return Result<std::string>::err(make_err("failed to open file for SHA256 computation"));
  }

  BCRYPT_ALG_HANDLE hAlg = nullptr;
  NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
  if (!BCRYPT_SUCCESS(status)) {
    CloseHandle(hFile);
    return Result<std::string>::err(make_err("BCryptOpenAlgorithmProvider failed"));
  }

  DWORD cbHashObject = 0, cbData = 0;
  BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&cbHashObject, sizeof(DWORD), &cbData, 0);
  std::vector<BYTE> hash_obj(cbHashObject);

  BCRYPT_HASH_HANDLE hHash = nullptr;
  status = BCryptCreateHash(hAlg, &hHash, hash_obj.data(), cbHashObject, nullptr, 0, 0);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    CloseHandle(hFile);
    return Result<std::string>::err(make_err("BCryptCreateHash failed"));
  }

  std::vector<BYTE> buffer(65536);
  DWORD bytes_read = 0;
  while (ReadFile(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr) &&
         bytes_read > 0) {
    BCryptHashData(hHash, buffer.data(), bytes_read, 0);
  }
  CloseHandle(hFile);

  BYTE hash[32] = {0};
  BCryptFinishHash(hHash, hash, sizeof(hash), 0);
  BCryptDestroyHash(hHash);
  BCryptCloseAlgorithmProvider(hAlg, 0);

  char hex[65] = {0};
  for (int i = 0; i < 32; ++i) {
    sprintf_s(&hex[i * 2], 3, "%02x", hash[i]);
  }
  return Result<std::string>::ok(std::string(hex));
}

CoreBinaryValidation CoreUpdater::inspect_mihomo_pe_binary(const std::wstring& file_path) {
  WIN32_FILE_ATTRIBUTE_DATA fad{};
  if (!::GetFileAttributesExW(file_path.c_str(), GetFileExInfoStandard, &fad)) {
    const DWORD gle = ::GetLastError();
    if (gle == ERROR_FILE_NOT_FOUND || gle == ERROR_PATH_NOT_FOUND) {
      return {CoreBinaryValidationKind::IoUnavailable, "file not found"};
    }
    return {CoreBinaryValidationKind::IoUnavailable,
            "failed to stat file, gle=" + std::to_string(gle)};
  }
  if ((fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return {CoreBinaryValidationKind::Corrupted, "path is a directory, not a PE binary"};
  }

  HANDLE hFile = ::CreateFileW(
      file_path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    const DWORD gle = ::GetLastError();
    return {CoreBinaryValidationKind::IoUnavailable,
            "failed to open file for PE inspection, gle=" + std::to_string(gle)};
  }

  LARGE_INTEGER li_size{};
  if (!::GetFileSizeEx(hFile, &li_size)) {
    const DWORD gle = ::GetLastError();
    ::CloseHandle(hFile);
    return {CoreBinaryValidationKind::IoUnavailable,
            "GetFileSizeEx failed, gle=" + std::to_string(gle)};
  }
  const uint64_t file_len = static_cast<uint64_t>(li_size.QuadPart);
  if (file_len < kMinValidCoreBytes) {
    ::CloseHandle(hFile);
    return {CoreBinaryValidationKind::Corrupted,
            "file size " + std::to_string(file_len) + " bytes < minimum 5 MiB"};
  }

  BYTE header[4096] = {};
  const DWORD initial_read =
      static_cast<DWORD>((std::min)(file_len, static_cast<uint64_t>(sizeof(header))));
  DWORD bytes_read = 0;
  if (!::ReadFile(hFile, header, initial_read, &bytes_read, nullptr)) {
    const DWORD gle = ::GetLastError();
    ::CloseHandle(hFile);
    return {CoreBinaryValidationKind::IoUnavailable,
            "ReadFile failed during PE inspection, gle=" + std::to_string(gle)};
  }
  if (bytes_read < 64) {
    ::CloseHandle(hFile);
    return {CoreBinaryValidationKind::Corrupted, "DOS header truncated (< 64 bytes)"};
  }
  if (header[0] != 'M' || header[1] != 'Z') {
    ::CloseHandle(hFile);
    return {CoreBinaryValidationKind::Corrupted, "missing MZ DOS signature"};
  }

  auto rd_u16 = [](const BYTE* p) -> uint16_t {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
  };
  auto rd_i32 = [](const BYTE* p) -> int32_t {
    uint32_t v = static_cast<uint32_t>(p[0]) |
                 (static_cast<uint32_t>(p[1]) << 8) |
                 (static_cast<uint32_t>(p[2]) << 16) |
                 (static_cast<uint32_t>(p[3]) << 24);
    return static_cast<int32_t>(v);
  };

  const int32_t e_lfanew = rd_i32(header + 0x3C);
  if (e_lfanew < 64) {
    ::CloseHandle(hFile);
    return {CoreBinaryValidationKind::Corrupted,
            "invalid e_lfanew offset: " + std::to_string(e_lfanew)};
  }
  const uint64_t pe_off = static_cast<uint64_t>(e_lfanew);
  if (pe_off + 26ULL > file_len) {
    ::CloseHandle(hFile);
    return {CoreBinaryValidationKind::Corrupted, "e_lfanew points past EOF"};
  }

  BYTE nt_buf[26] = {};
  const BYTE* nt_ptr = nullptr;
  if (pe_off + 26ULL <= bytes_read) {
    nt_ptr = header + pe_off;
  } else {
    LARGE_INTEGER seek_pos{};
    seek_pos.QuadPart = static_cast<LONGLONG>(pe_off);
    DWORD nt_read = 0;
    if (!::SetFilePointerEx(hFile, seek_pos, nullptr, FILE_BEGIN) ||
        !::ReadFile(hFile, nt_buf, sizeof(nt_buf), &nt_read, nullptr)) {
      const DWORD gle = ::GetLastError();
      ::CloseHandle(hFile);
      return {CoreBinaryValidationKind::IoUnavailable,
              "failed reading NT header at e_lfanew, gle=" + std::to_string(gle)};
    }
    if (nt_read < sizeof(nt_buf)) {
      ::CloseHandle(hFile);
      return {CoreBinaryValidationKind::Corrupted, "NT header truncated"};
    }
    nt_ptr = nt_buf;
  }
  ::CloseHandle(hFile);

  if (nt_ptr[0] != 'P' || nt_ptr[1] != 'E' || nt_ptr[2] != 0 || nt_ptr[3] != 0) {
    return {CoreBinaryValidationKind::Corrupted, "missing PE\\0\\0 signature"};
  }

  const BYTE* coff = nt_ptr + 4;
  const uint16_t machine = rd_u16(coff + 0);
  const uint16_t num_sections = rd_u16(coff + 2);
  const uint16_t size_of_opt_hdr = rd_u16(coff + 16);
  const uint16_t characteristics = rd_u16(coff + 18);
  const uint16_t opt_magic = rd_u16(nt_ptr + 24);

  if (machine != 0x8664) {
    return {CoreBinaryValidationKind::Corrupted,
            "invalid COFF Machine (expected AMD64 0x8664)"};
  }
  if (num_sections < 1 || num_sections > 96) {
    return {CoreBinaryValidationKind::Corrupted,
            "invalid COFF NumberOfSections: " + std::to_string(num_sections)};
  }
  if (size_of_opt_hdr < 112) {
    return {CoreBinaryValidationKind::Corrupted,
            "OptionalHeader too small: " + std::to_string(size_of_opt_hdr)};
  }
  if ((characteristics & 0x0002) == 0) {
    return {CoreBinaryValidationKind::Corrupted,
            "COFF IMAGE_FILE_EXECUTABLE_IMAGE flag missing"};
  }
  if (opt_magic != 0x020B) {
    return {CoreBinaryValidationKind::Corrupted,
            "invalid OptionalHeader Magic (expected PE32+ 0x020B)"};
  }

  const uint64_t section_table_end =
      pe_off + 24ULL + static_cast<uint64_t>(size_of_opt_hdr) +
      static_cast<uint64_t>(num_sections) * 40ULL;
  if (section_table_end > file_len) {
    return {CoreBinaryValidationKind::Corrupted,
            "PE section table extends past EOF"};
  }

  return {CoreBinaryValidationKind::Valid, ""};
}

Result<void> CoreUpdater::validate_mihomo_pe_binary(const std::wstring& file_path) {
  auto v = inspect_mihomo_pe_binary(file_path);
  if (v.is_valid()) return Result<void>::ok();
  return Result<void>::err(make_err(v.reason));
}

std::optional<std::wstring> CoreUpdater::resolve_usable_custom_core(
    const std::wstring& cores_dir) {
  if (cores_dir.empty()) return std::nullopt;
  const std::wstring custom = cores_dir + L"\\win-mihomo.exe";
  WIN32_FILE_ATTRIBUTE_DATA fad{};
  if (!::GetFileAttributesExW(custom.c_str(), GetFileExInfoStandard, &fad)) {
    const DWORD gle = ::GetLastError();
    if (gle == ERROR_FILE_NOT_FOUND || gle == ERROR_PATH_NOT_FOUND) {
      return std::nullopt;
    }
    CWLOG_WARN("custom core win-mihomo.exe stat unavailable (gle=" +
               std::to_string(gle) + "), keeping file and falling back to sidecar");
    return std::nullopt;
  }

  const auto check = inspect_mihomo_pe_binary(custom);
  if (check.kind == CoreBinaryValidationKind::Valid) {
    return custom;
  }
  if (check.kind == CoreBinaryValidationKind::Corrupted) {
    CWLOG_WARN("custom core win-mihomo.exe is corrupted (" + check.reason +
               "), deleting broken binary and falling back to sidecar");
    std::error_code ec;
    std::filesystem::remove_all(custom, ec);
    return std::nullopt;
  }

  CWLOG_WARN("custom core win-mihomo.exe temporarily unavailable (" + check.reason +
             "), keeping file and falling back to sidecar");
  return std::nullopt;
}

namespace {

Result<void> write_buffer_to_staging(const std::wstring& staging_path,
                                     const char* data,
                                     size_t len) {
  HANDLE h = ::CreateFileW(staging_path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return Result<void>::err(
        make_err("failed to create staging core file, gle=" + std::to_string(::GetLastError())));
  }
  size_t offset = 0;
  while (offset < len) {
    const DWORD chunk = static_cast<DWORD>((std::min)(len - offset, static_cast<size_t>(1048576)));
    DWORD written = 0;
    if (!::WriteFile(h, data + offset, chunk, &written, nullptr) || written != chunk) {
      const DWORD gle = ::GetLastError();
      ::CloseHandle(h);
      std::error_code ec;
      std::filesystem::remove(staging_path, ec);
      return Result<void>::err(
          make_err("WriteFile to staging core failed, gle=" + std::to_string(gle)));
    }
    offset += written;
  }
  ::FlushFileBuffers(h);
  ::CloseHandle(h);
  return Result<void>::ok();
}

Result<void> flush_and_commit_staging(const std::wstring& staging_path,
                                      const std::wstring& target_exe_path) {
  {
    HANDLE h = ::CreateFileW(staging_path.c_str(), GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      ::FlushFileBuffers(h);
      ::CloseHandle(h);
    }
  }

  auto pe_ok = CoreUpdater::validate_mihomo_pe_binary(staging_path);
  if (!pe_ok.is_ok()) {
    std::error_code ec;
    std::filesystem::remove(staging_path, ec);
    return Result<void>::err(
        make_err("extracted core failed PE validation: " + pe_ok.error().message));
  }

  DWORD last_err = 0;
  for (int attempt = 0; attempt < 10; ++attempt) {
    if (::MoveFileExW(staging_path.c_str(), target_exe_path.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      return Result<void>::ok();
    }
    last_err = ::GetLastError();
    if (last_err != ERROR_ACCESS_DENIED && last_err != ERROR_SHARING_VIOLATION) {
      break;
    }
    if (attempt + 1 < 10) {
      ::Sleep(100);
    }
  }

  std::error_code ec;
  std::filesystem::remove(staging_path, ec);
  return Result<void>::err(make_err(
      "failed to atomically replace win-mihomo.exe from staging, gle=" +
      std::to_string(last_err)));
}

}  // namespace

Result<void> CoreUpdater::extract_core_binary(const std::wstring& archive_path,
                                            const std::wstring& target_exe_path) {
  // Ensure target parent directory exists
  std::filesystem::path target_path(target_exe_path);
  if (target_path.has_parent_path()) {
    std::filesystem::create_directories(target_path.parent_path());
  }
  const std::wstring staging_path = target_exe_path + L".staging";
  {
    std::error_code cleanup_ec;
    std::filesystem::remove(staging_path, cleanup_ec);
  }

  // Attempt direct uncompressed PKZIP extraction (STORE, compression method = 0)
  std::ifstream arch(archive_path, std::ios::binary);
  if (arch.is_open()) {
    uint32_t sig = 0;
    arch.read(reinterpret_cast<char*>(&sig), 4);
    if (sig == 0x04034b50) {
      // PKZIP Local File Header found
      while (sig == 0x04034b50) {
        uint16_t ver = 0, flags = 0, method = 0, mod_time = 0, mod_date = 0;
        uint32_t crc = 0, comp_size = 0, uncomp_size = 0;
        uint16_t fname_len = 0, extra_len = 0;
        arch.read(reinterpret_cast<char*>(&ver), 2);
        arch.read(reinterpret_cast<char*>(&flags), 2);
        arch.read(reinterpret_cast<char*>(&method), 2);
        arch.read(reinterpret_cast<char*>(&mod_time), 2);
        arch.read(reinterpret_cast<char*>(&mod_date), 2);
        arch.read(reinterpret_cast<char*>(&crc), 4);
        arch.read(reinterpret_cast<char*>(&comp_size), 4);
        arch.read(reinterpret_cast<char*>(&uncomp_size), 4);
        arch.read(reinterpret_cast<char*>(&fname_len), 2);
        arch.read(reinterpret_cast<char*>(&extra_len), 2);

        std::string fname(fname_len, '\0');
        arch.read(&fname[0], fname_len);
        if (extra_len > 0) arch.seekg(extra_len, std::ios::cur);

        std::string lower_fname = to_lower(fname);
        if (method == 0 && lower_fname.find("mihomo") != std::string::npos &&
            ends_with(lower_fname, ".exe")) {
          // Extract directly to .staging, flush, validate PE64, then atomic MoveFileExW
          std::vector<char> data(uncomp_size);
          if (uncomp_size > 0) {
            arch.read(data.data(), uncomp_size);
          }
          arch.close();
          auto wr = write_buffer_to_staging(staging_path, data.data(), data.size());
          if (!wr.is_ok()) return wr;
          return flush_and_commit_staging(staging_path, target_exe_path);
        }

        // Skip payload
        arch.seekg(comp_size, std::ios::cur);

        // Read next signature
        if (!arch.read(reinterpret_cast<char*>(&sig), 4)) break;
      }
    }
    arch.close();
  }

  // For Deflate ZIP or GZ, unpack using C:\Windows\System32\tar.exe
  std::filesystem::path temp_extract_dir = target_path.parent_path() / L".extract_tmp";
  std::error_code ec;
  std::filesystem::remove_all(temp_extract_dir, ec);
  std::filesystem::create_directories(temp_extract_dir, ec);

  std::wstring cmd = L"\"C:\\Windows\\System32\\tar.exe\" -xf \"" +
                     archive_path + L"\" -C \"" + temp_extract_dir.wstring() + L"\"";

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};

  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');

  BOOL cp = CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  if (!cp) {
    std::filesystem::remove_all(temp_extract_dir, ec);
    return Result<void>::err(make_err("failed to launch tar.exe for extraction"));
  }

  WaitForSingleObject(pi.hProcess, 10000);
  DWORD exit_code = 1;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  if (exit_code != 0) {
    std::filesystem::remove_all(temp_extract_dir, ec);
    return Result<void>::err(make_err("tar.exe extraction exited with error: " + std::to_string(exit_code)));
  }

  // Locate the extracted mihomo executable
  std::filesystem::path found_exe;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(temp_extract_dir, ec)) {
    if (entry.is_regular_file()) {
      std::string fn = to_lower(entry.path().filename().string());
      if (fn.find("mihomo") != std::string::npos && ends_with(fn, ".exe")) {
        found_exe = entry.path();
        break;
      }
    }
  }

  if (found_exe.empty()) {
    std::filesystem::remove_all(temp_extract_dir, ec);
    return Result<void>::err(make_err("在压缩包内未找到 win-mihomo 程序二进制"));
  }

  // Stage to target.staging first, remove temp dir, then flush + PE validate + atomic replace
  std::filesystem::copy_file(found_exe, staging_path,
                             std::filesystem::copy_options::overwrite_existing, ec);
  std::filesystem::remove_all(temp_extract_dir, ec);

  if (ec) {
    std::error_code rm_ec;
    std::filesystem::remove(staging_path, rm_ec);
    return Result<void>::err(make_err("failed to copy extracted binary to staging: " + ec.message()));
  }

  return flush_and_commit_staging(staging_path, target_exe_path);
}

Result<GithubRelease> CoreUpdater::check_latest_release() {
  if (!downloader_) {
    return Result<GithubRelease>::err(make_err("downloader not configured"));
  }
  auto json_res = downloader_->fetch_latest_release_json();
  if (!json_res.is_ok()) {
    return Result<GithubRelease>::err(json_res.error());
  }

  try {
    auto j = nlohmann::json::parse(json_res.value());
    GithubRelease release = j.get<GithubRelease>();
    return Result<GithubRelease>::ok(release);
  } catch (const std::exception& e) {
    return Result<GithubRelease>::err(make_err(std::string("failed to parse GitHub release JSON: ") + e.what()));
  }
}

Result<void> CoreUpdater::start_core_upgrade(const GithubRelease& release) {
  emit_progress("checking", 5, "正在检测适合您系统架构的内核包...");

  auto asset_opt = match_target_asset(release);
  if (!asset_opt.has_value()) {
    const std::string err_msg = "在 GitHub Release 中未找到适合您系统架构的内核包";
    emit_progress("error", 0, err_msg);
    return Result<void>::err(make_err(err_msg));
  }

  const GithubAsset asset = *asset_opt;
  std::wstring cores_dir = util::dirs::cores_dir();
  std::filesystem::create_directories(cores_dir);

  std::wstring temp_download_path = cores_dir + L"\\win-mihomo.download.tmp";
  std::wstring target_exe_path = cores_dir + L"\\win-mihomo.exe";

  emit_progress("downloading", 10, "开始下载: " + asset.name);

  uint32_t last_pct = 10;
  auto on_progress = [this, &asset, &last_pct](uint64_t downloaded, uint64_t total) {
    if (total > 0) {
      // src: core_updater.rs:269 ((d/t)*80.0) as u32 + 10（截断，不设上限，
      // 满下载时 90；随后 verifying 固定 85 —— 与 Mini 行为一致）
      uint32_t pct = 10 + static_cast<uint32_t>((static_cast<double>(downloaded) /
                                                 static_cast<double>(total)) * 80.0);
      if (pct != last_pct) {
        last_pct = pct;
        char msg_buf[256];
        sprintf_s(msg_buf, "正在下载 %s: %.1f MB / %.1f MB",
                  asset.name.c_str(),
                  static_cast<double>(downloaded) / 1048576.0,
                  static_cast<double>(total) / 1048576.0);
        emit_progress("downloading", pct, msg_buf);
      }
    }
  };

  auto dl_res = downloader_->download_asset(asset.browser_download_url, temp_download_path, on_progress);
  if (!dl_res.is_ok()) {
    std::error_code ec;
    std::filesystem::remove(temp_download_path, ec);
    emit_progress("error", 0, dl_res.error().message);
    return dl_res;
  }

  // Stage 3: Verifying (85%)
  emit_progress("verifying", 85, "正在校验下载文件完整性...");

  auto checksum_url_opt = find_checksum_url(release);
  if (checksum_url_opt.has_value()) {
    auto cs_content_res = downloader_->fetch_checksum_content(*checksum_url_opt);
    if (cs_content_res.is_ok()) {
      auto expected_sha_opt = parse_checksum_for_asset(cs_content_res.value(), asset.name);
      if (expected_sha_opt.has_value()) {
        auto actual_sha_res = compute_file_sha256(temp_download_path);
        if (!actual_sha_res.is_ok() ||
            to_lower(actual_sha_res.value()) != to_lower(*expected_sha_opt)) {
          std::error_code ec;
          std::filesystem::remove(temp_download_path, ec);
          std::string err_msg = "完整性校验失败: sha256 mismatch (expected " +
                                *expected_sha_opt + ", got " +
                                (actual_sha_res.is_ok() ? actual_sha_res.value() : "error") + ")";
          emit_progress("error", 0, err_msg);
          return Result<void>::err(make_err(err_msg));
        }
      }
    }
  }

  // Snapshot active proxy group (BUG-335: fallback to persisted non-dummy selection)
  std::optional<std::string> saved_proxy_now;
  if (api_client_) {
    saved_proxy_now = CoreManager::instance().snapshot_proxy_group_now(*api_client_);
  }
  if (!saved_proxy_now.has_value()) {
    saved_proxy_now = CoreManager::instance().read_persisted_proxy_selection("PROXY");
  }

  // Stop core before replacing binary (BUG-346: wait up to 3000ms for file handle release)
  if (lifecycle_ops_.has_value() && lifecycle_ops_->stop) {
    lifecycle_ops_->stop();
  } else {
    CoreManager::instance().stop();
  }
  if (lifecycle_ops_.has_value()) {
    if (lifecycle_ops_->wait_cores_exit) {
      lifecycle_ops_->wait_cores_exit(3000);
    }
  } else {
    CoreManager::instance().wait_for_win_cores_exit(3000);
  }

  // Stage 5: Extracting (90%)
  emit_progress("extracting", 90, "正在解压并替换内核程序...");
  auto extract_res = extract_core_binary(temp_download_path, target_exe_path);

  // Clean up temp file
  std::error_code ec;
  std::filesystem::remove(temp_download_path, ec);

  if (!extract_res.is_ok()) {
    // Recovery restart
    if (lifecycle_ops_.has_value() && lifecycle_ops_->start) {
      lifecycle_ops_->start();
    } else {
      CoreManager::instance().start_auto(util::dirs::clash_mini_path());
    }
    if (saved_proxy_now.has_value() && api_client_) {
      CoreManager::instance().restore_proxy_group_now(*api_client_, *saved_proxy_now);
    }
    emit_progress("error", 0, "解压替换失败: " + extract_res.error().message);
    return extract_res;
  }

  // Restart core
  Result<void> start_res = Result<void>::ok();
  if (lifecycle_ops_.has_value() && lifecycle_ops_->start) {
    start_res = lifecycle_ops_->start();
  } else {
    start_res = CoreManager::instance().start_auto(util::dirs::clash_mini_path());
  }

  if (!start_res.is_ok()) {
    std::string err_msg = "内核二进制已更新，但重启失败: " + start_res.error().message +
                          "。请手动重启 Clash Win。";
    emit_progress("error", 0, err_msg);
    return Result<void>::err(make_err(err_msg));
  }

  // Restore proxy group
  if (saved_proxy_now.has_value() && api_client_) {
    CoreManager::instance().restore_proxy_group_now(*api_client_, *saved_proxy_now);
  }

  emit_progress("done", 100, "内核更新成功！核心已成功重启。");
  return Result<void>::ok();
}

}  // namespace clashwin::core
