#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/core_manager.h"
#include "util/result.h"
#include "nlohmann/json.hpp"

namespace clashwin::core {

struct GithubAsset {
  std::string name;
  std::string browser_download_url;
};

inline void to_json(nlohmann::json& j, const GithubAsset& a) {
  j = nlohmann::json{{"name", a.name}, {"browser_download_url", a.browser_download_url}};
}

inline void from_json(const nlohmann::json& j, GithubAsset& a) {
  if (j.contains("name") && j["name"].is_string()) {
    a.name = j["name"].get<std::string>();
  }
  if (j.contains("browser_download_url") && j["browser_download_url"].is_string()) {
    a.browser_download_url = j["browser_download_url"].get<std::string>();
  }
}

struct GithubRelease {
  std::string tag_name;
  std::vector<GithubAsset> assets;
};

inline void to_json(nlohmann::json& j, const GithubRelease& r) {
  j = nlohmann::json{{"tag_name", r.tag_name}, {"assets", r.assets}};
}

inline void from_json(const nlohmann::json& j, GithubRelease& r) {
  if (j.contains("tag_name") && j["tag_name"].is_string()) {
    r.tag_name = j["tag_name"].get<std::string>();
  }
  if (j.contains("assets") && j["assets"].is_array()) {
    r.assets = j["assets"].get<std::vector<GithubAsset>>();
  }
}

struct CoreUpgradeProgress {
  std::string status;  // checking, downloading, verifying, extracting, done, error
  uint32_t progress = 0;  // 0..100
  std::string message;

  nlohmann::json to_json() const {
    return {{"status", status}, {"progress", progress}, {"message", message}};
  }
};

// Mockable downloader interface for 100% offline deterministic testing
struct ICoreUpdateDownloader {
  virtual ~ICoreUpdateDownloader() = default;
  virtual Result<std::string> fetch_latest_release_json() = 0;
  virtual Result<std::string> fetch_checksum_content(const std::string& url) = 0;
  virtual Result<void> download_asset(
      const std::string& url, const std::wstring& dest_path,
      std::function<void(uint64_t downloaded, uint64_t total)> on_progress) = 0;
};

class CoreUpdater {
 public:
  static CoreUpdater& instance();

  // Dependency injection for unit testing
  void set_downloader(std::shared_ptr<ICoreUpdateDownloader> downloader);
  void set_core_api_client(ICoreApiClient* client);
  void set_lifecycle_ops(CoreOps ops);
  void reset_lifecycle_ops();

  // Commands
  Result<GithubRelease> check_latest_release();
  Result<void> start_core_upgrade(const GithubRelease& release);

  // Pure helper functions (fully testable)
  static std::optional<GithubAsset> match_target_asset(const GithubRelease& release);
  static std::optional<std::string> find_checksum_url(const GithubRelease& release);
  static std::optional<std::string> parse_checksum_for_asset(const std::string& checksum_text,
                                                            const std::string& asset_name);
  static Result<std::string> compute_file_sha256(const std::wstring& file_path);
  static Result<void> extract_core_binary(const std::wstring& archive_path,
                                         const std::wstring& target_exe_path);

  void emit_progress(const std::string& status, uint32_t progress, const std::string& message);

 private:
  CoreUpdater();
  ~CoreUpdater() = default;

  std::shared_ptr<ICoreUpdateDownloader> downloader_;
  ICoreApiClient* api_client_ = nullptr;
  std::optional<CoreOps> lifecycle_ops_;
};

}  // namespace clashwin::core
