#include "clashwin/core/core_updater.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "clashwin/config/constants.h"
#include "config/config.h"
#include "events/event_bus.h"
#include "system/net_http.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/str_utils.h"
#include "util/utf.h"

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

uint16_t resolve_verge_mixed_port() {
  try {
    auto v = config::Config::instance().verge().latest_arc();
    if (v && v->verge_mixed_port && *v->verge_mixed_port > 0) {
      return *v->verge_mixed_port;
    }
  } catch (...) {
  }
  return config::consts::kDefaultMixed;
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
      opts.localhost_mixed_port = []() -> std::optional<uint16_t> {
        return resolve_verge_mixed_port();
      };
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
      opts.max_response_bytes = 1024 * 1024;  // BUG-413：校验和文件上限 1 MiB
      opts.localhost_mixed_port = []() -> std::optional<uint16_t> {
        return resolve_verge_mixed_port();
      };
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
      system::HttpOptions opts;
      opts.proxy_type = pt;
      opts.timeout_seconds = 300;
      opts.user_agent = "ClashWin-CoreUpdater";
      opts.max_response_bytes = kMaxCoreArchiveBytes;  // BUG-413
      opts.localhost_mixed_port = []() -> std::optional<uint16_t> {
        return resolve_verge_mixed_port();
      };
      auto r = system::http_download_file(url, dest_path, opts, on_progress);
      if (r.is_ok()) return r;
    }
    return Result<void>::err(make_err("所有网络连接（代理/直连）均下载失败"));
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
          // BUG-413：解压体积上限，超限或为空直接拒绝（不分配内存）
          if (uncomp_size == 0 || static_cast<uint64_t>(uncomp_size) > kMaxCoreExtractedBytes) {
            arch.close();
            return Result<void>::err(make_err(
                "解压后的内核文件体积异常或超过安全上限 (" + std::to_string(uncomp_size) +
                " bytes > " + std::to_string(kMaxCoreExtractedBytes) + " bytes)，已中止"));
          }
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

  // BUG-413：解压产物体积上限
  {
    std::error_code sz_ec;
    const uint64_t extracted_size = std::filesystem::file_size(found_exe, sz_ec);
    if (sz_ec || extracted_size == 0 || extracted_size > kMaxCoreExtractedBytes) {
      std::filesystem::remove_all(temp_extract_dir, ec);
      return Result<void>::err(make_err(
          "解压后的内核文件体积异常或超过安全上限 (" + std::to_string(extracted_size) +
          " bytes > " + std::to_string(kMaxCoreExtractedBytes) + " bytes)，已中止"));
    }
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
  if (is_exiting_check_ && is_exiting_check_()) {
    return Result<GithubRelease>::err(make_err("application exiting"));
  }
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
  auto check_exiting = [&](const char* stage) -> bool {
    if (is_exiting_check_ && is_exiting_check_()) {
      CWLOG_INFO(std::string("CoreUpdater: aborted during ") + stage +
                 " (application exiting)");
      return true;
    }
    return false;
  };
  if (check_exiting("start")) {
    return Result<void>::err(make_err("application exiting"));
  }
  emit_progress("checking", 5, "正在检测适合您系统架构的内核包...");

  auto asset_opt = match_target_asset(release);
  if (!asset_opt.has_value()) {
    const std::string err_msg = "在 GitHub Release 中未找到适合您系统架构的内核包";
    emit_progress("error", 0, err_msg);
    return Result<void>::err(make_err(err_msg));
  }

  const GithubAsset asset = *asset_opt;

  // BUG-413：下载地址白名单 + 声明体积上限（下载前拒绝）
  {
    auto url_ok = validate_official_mihomo_asset_url(asset.browser_download_url);
    if (!url_ok.is_ok()) {
      emit_progress("error", 0, url_ok.error().message);
      return url_ok;
    }
    if (asset.size.has_value() && (*asset.size == 0 || *asset.size > kMaxCoreArchiveBytes)) {
      const std::string err_msg = "内核压缩包声明体积异常或超过安全上限 (" +
                                  std::to_string(*asset.size) + " bytes)，已拒绝下载";
      emit_progress("error", 0, err_msg);
      return Result<void>::err(make_err(err_msg));
    }
  }

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
  if (check_exiting("download")) {
    std::error_code ec;
    std::filesystem::remove(temp_download_path, ec);
    return Result<void>::err(make_err("application exiting"));
  }
  if (!dl_res.is_ok()) {
    std::error_code ec;
    std::filesystem::remove(temp_download_path, ec);
    emit_progress("error", 0, dl_res.error().message);
    return dl_res;
  }

  // Stage 3: Verifying (85%)
  emit_progress("verifying", 85, "正在校验下载文件完整性...");

  auto fail_verify = [&](const std::string& err_msg) -> Result<void> {
    std::error_code ec;
    std::filesystem::remove(temp_download_path, ec);
    emit_progress("error", 0, err_msg);
    return Result<void>::err(make_err(err_msg));
  };

  // BUG-413：实际下载体积上限 + 与声明体积严格一致
  {
    std::error_code sz_ec;
    const uint64_t archive_size = std::filesystem::file_size(temp_download_path, sz_ec);
    if (sz_ec || archive_size == 0 || archive_size > kMaxCoreArchiveBytes) {
      return fail_verify("内核压缩包体积异常或超过安全上限 (" + std::to_string(archive_size) +
                         " bytes)，已中止升级");
    }
    if (asset.size.has_value() && archive_size != *asset.size) {
      return fail_verify("内核压缩包体积与官方声明不一致 (expected " +
                         std::to_string(*asset.size) + ", got " +
                         std::to_string(archive_size) + ")，已中止升级");
    }
  }

  // BUG-413 Fail-Closed：优先 asset.digest，缺失时回退校验和文件；两者皆无一律拒绝
  std::optional<std::string> expected_sha = parse_asset_sha256_digest(asset.digest);
  if (!expected_sha.has_value()) {
    auto checksum_url_opt = find_checksum_url(release);
    if (checksum_url_opt.has_value()) {
      auto cs_url_ok = validate_official_mihomo_asset_url(*checksum_url_opt);
      if (!cs_url_ok.is_ok()) {
        return fail_verify(cs_url_ok.error().message);
      }
      auto cs_content_res = downloader_->fetch_checksum_content(*checksum_url_opt);
      if (cs_content_res.is_ok() && cs_content_res.value().size() <= 1024 * 1024) {
        expected_sha = parse_asset_sha256_digest(
            parse_checksum_for_asset(cs_content_res.value(), asset.name));
      }
    }
  }
  if (!expected_sha.has_value()) {
    return fail_verify("缺少官方 SHA-256 校验摘要 (" + asset.name + ")，已拒绝未校验的内核升级");
  }
  {
    auto actual_sha_res = compute_file_sha256(temp_download_path);
    if (!actual_sha_res.is_ok() || to_lower(actual_sha_res.value()) != *expected_sha) {
      return fail_verify("完整性校验失败: sha256 mismatch (expected " + *expected_sha +
                         ", got " +
                         (actual_sha_res.is_ok() ? actual_sha_res.value() : "error") + ")");
    }
    CWLOG_INFO("CoreUpdater: integrity check passed for " + asset.name + " (sha256=" +
               *expected_sha + ")");
  }
  if (check_exiting("verify")) {
    std::error_code ec;
    std::filesystem::remove(temp_download_path, ec);
    return Result<void>::err(make_err("application exiting"));
  }

  // Snapshot compliant active proxy group (BUG-335 + BUG-405)
  std::optional<std::string> saved_proxy_now;
  if (api_client_) {
    saved_proxy_now = CoreManager::instance().resolve_desired_proxy_selection(*api_client_);
  } else {
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
    if (api_client_) {
      CoreManager::instance().ensure_proxy_group_selection(*api_client_, saved_proxy_now);
    } else {
      CoreManager::instance().spawn_ensure_proxy_group_selection(saved_proxy_now);
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

  // Ensure compliant proxy group selection & clean excluded connections (BUG-405 + BUG-407)
  if (api_client_) {
    CoreManager::instance().ensure_proxy_group_selection(*api_client_, saved_proxy_now);
  } else {
    CoreManager::instance().spawn_ensure_proxy_group_selection(saved_proxy_now);
  }

  emit_progress("done", 100, "内核更新成功！核心已成功重启。");
  return Result<void>::ok();
}

}  // namespace clashwin::core
