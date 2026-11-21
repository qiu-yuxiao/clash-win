// Mihomo x64 PE 二进制完整性校验、自定义内核解析与官方资产安全校验
// （拆分自 src/core/core_updater.cpp；← ClashVerge src-tauri/src/core/core_updater/pe_verify.rs）
#include "clashwin/core/core_updater.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "util/log.h"
#include "util/str_utils.h"

namespace clashwin::core {

namespace {

using util::to_lower;
using util::trim;

inline Error make_err(const std::string& msg, int code = 1) {
  return Error{code, msg};
}

}  // namespace

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

// ← core_updater/pe_verify.rs parse_asset_sha256_digest（BUG-413）
std::optional<std::string> CoreUpdater::parse_asset_sha256_digest(
    const std::optional<std::string>& digest) {
  if (!digest.has_value()) return std::nullopt;
  std::string raw = trim(*digest);
  std::string hex_part = raw;
  if (raw.size() > 7 && to_lower(raw.substr(0, 7)) == "sha256:") {
    hex_part = trim(raw.substr(7));
  }
  if (hex_part.size() != 64) return std::nullopt;
  for (char c : hex_part) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
  }
  return to_lower(hex_part);
}

// ← core_updater/pe_verify.rs validate_official_mihomo_asset_url（BUG-413）
Result<void> CoreUpdater::validate_official_mihomo_asset_url(const std::string& url) {
  static const std::string kScheme = "https://";
  if (url.size() <= kScheme.size() || to_lower(url.substr(0, kScheme.size())) != kScheme) {
    return Result<void>::err(make_err("内核下载仅允许 HTTPS 协议: " + url));
  }
  for (char c : url) {
    if (static_cast<unsigned char>(c) <= 0x20 || c == '\\') {
      return Result<void>::err(make_err("内核下载 URL 含非法字符: " + url));
    }
  }
  const std::string rest = url.substr(kScheme.size());
  const size_t path_pos = rest.find_first_of("/?#");
  const std::string authority = rest.substr(0, path_pos);
  if (authority.find('@') != std::string::npos || authority.find(':') != std::string::npos) {
    return Result<void>::err(make_err("内核下载 URL 禁止包含非标准端口或用户信息: " + url));
  }
  if (to_lower(authority) != "github.com") {
    return Result<void>::err(make_err("内核下载来源域名非法（仅允许 github.com）: " + url));
  }
  if (path_pos == std::string::npos || rest[path_pos] != '/') {
    return Result<void>::err(make_err("内核下载路径不在官方 MetaCubeX/mihomo Release 目录内: " + url));
  }
  std::string path = rest.substr(path_pos);
  const size_t q = path.find_first_of("?#");
  if (q != std::string::npos) path = path.substr(0, q);
  static const std::string kPrefix = "/MetaCubeX/mihomo/releases/download/";
  if (path.compare(0, kPrefix.size(), kPrefix) != 0 || path.size() == kPrefix.size()) {
    return Result<void>::err(make_err("内核下载路径不在官方 MetaCubeX/mihomo Release 目录内: " + url));
  }
  size_t start = 0;
  while (start <= path.size()) {
    const size_t slash = path.find('/', start);
    const std::string seg =
        path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    const std::string lseg = to_lower(seg);
    if (seg == ".." || lseg == "%2e%2e" || lseg == ".%2e" || lseg == "%2e.") {
      return Result<void>::err(make_err("内核下载 URL 路径非法: " + url));
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  return Result<void>::ok();
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

}  // namespace clashwin::core
