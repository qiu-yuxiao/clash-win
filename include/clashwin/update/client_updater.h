#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "clashwin/config/constants.h"
#include "util/result.h"
#include "nlohmann/json.hpp"

namespace clashwin::update {

struct AppUpdateInfo {
  bool has_update = false;
  std::string current_version;
  std::string latest_version;
  std::string notes;
  std::string pub_date;
  std::string download_url;
  std::string signature;
  std::string sha256;  // ← tauri updater 平台节点 sha256（Mini pubkey 验签前用于完整性兜底）
  size_t file_size = 0;

  nlohmann::json to_json() const;
};

inline nlohmann::json AppUpdateInfo::to_json() const {
  return nlohmann::json{
      {"has_update", has_update},
      {"current_version", current_version},
      {"latest_version", latest_version},
      {"notes", notes},
      {"pub_date", pub_date},
      {"download_url", download_url},
      {"signature", signature},
      {"sha256", sha256},
      {"file_size", file_size},
  };
}

struct IAppUpdateDownloader {
  virtual ~IAppUpdateDownloader() = default;
  virtual Result<std::string> fetch_manifest(const std::string& endpoint_url) = 0;
  virtual Result<void> download_installer(
      const std::string& url, const std::wstring& dest_path,
      std::function<void(size_t downloaded, size_t total)> on_progress) = 0;
};

class ClientUpdater {
 public:
  static ClientUpdater& instance();

  void set_downloader(std::shared_ptr<IAppUpdateDownloader> downloader);
  void set_current_version(const std::string& version);
  const std::string& current_version() const { return current_version_; }

  Result<AppUpdateInfo> check_app_update();
  Result<std::wstring> download_app_update(const std::string& download_url,
                                           const std::string& expected_sha256 = "",
                                           const std::string& signature_b64 = "");
  Result<void> install_app_update(const std::wstring& installer_path, bool silent = false);

  // Pure helpers (fully testable)
  static bool is_version_newer(const std::string& latest, const std::string& current);
  static Result<AppUpdateInfo> parse_manifest(const std::string& json_text,
                                             const std::string& current_version);
  // CNG SHA-256 流水线；文件不存在/读取失败返回空串
  static std::string compute_sha256_hex(const std::wstring& file_path);

 private:
  ClientUpdater();
  ~ClientUpdater() = default;

  std::shared_ptr<IAppUpdateDownloader> downloader_;
  std::string current_version_ = config::consts::kAppVersion;  // 单一事实源（← constants.h kAppVersion，与 Mini 同为 3.2.2）
};

// minisign 验签（B-D44 收敛，1:1 对齐 tauri-plugin-updater 2.10.1）：
// 公钥取 config::kUpdatePubkeyFileB64（Mini tauri.conf.json:32）；
// pubkey_file_b64_override 仅供测试注入（nullptr = 用 config 公钥）。
// 语义见 src/update/client_updater.cpp verify_with_pubkey 注释。
bool verify_installer_signature(const std::vector<uint8_t>& file_bytes,
                                const std::string& signature_b64,
                                std::string* err,
                                const char* pubkey_file_b64_override = nullptr);

// 测试注入：替换验签公钥（nullptr/空 = 恢复 config 公钥）
void set_verify_pubkey_for_test(const char* pubkey_file_b64);

}  // namespace clashwin::update
