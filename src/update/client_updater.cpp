#include "update/client_updater.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "system/net_http.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/str_utils.h"
#include "util/utf.h"

namespace clashwin::update {

namespace {

using clashwin::utf::utf16_to_utf8;
using clashwin::utf::utf8_to_utf16;
using util::starts_with;
using util::to_lower;
using util::trim;

inline Error make_err(const std::string& msg, int code = 1) {
  return Error{code, msg};
}

class DefaultAppUpdateDownloader : public IAppUpdateDownloader {
 public:
  Result<std::string> fetch_manifest(const std::string& endpoint_url) override {
    const std::vector<system::ProxyType> proxy_types = {
        system::ProxyType::Localhost, system::ProxyType::System, system::ProxyType::None};

    for (auto pt : proxy_types) {
      system::HttpOptions opts;
      opts.proxy_type = pt;
      opts.timeout_seconds = 15;
      opts.user_agent = "ClashWin-ClientUpdater";
      auto res = system::http_get(endpoint_url, opts);
      if (res.is_ok() && res.value().is_success()) {
        return Result<std::string>::ok(res.value().body);
      }
    }
    return Result<std::string>::err(make_err("failed to fetch client update manifest across all proxy modes"));
  }

  Result<void> download_installer(
      const std::string& url, const std::wstring& dest_path,
      std::function<void(size_t downloaded, size_t total)> on_progress) override {
    const std::vector<system::ProxyType> proxy_types = {
        system::ProxyType::Localhost, system::ProxyType::System, system::ProxyType::None};

    for (auto pt : proxy_types) {
      system::HttpOptions opts;
      opts.proxy_type = pt;
      opts.timeout_seconds = 600;
      opts.user_agent = "ClashWin-ClientUpdater";
      opts.max_response_bytes = 200u << 20;  // 200MB installer limit
      auto res = system::http_get(url, opts);
      if (res.is_ok() && res.value().is_success()) {
        std::ofstream out(dest_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
          return Result<void>::err(make_err("failed to open destination installer file"));
        }
        out.write(res.value().body.data(), res.value().body.size());
        out.close();
        if (on_progress) {
          on_progress(res.value().body.size(), res.value().body.size());
        }
        return Result<void>::ok();
      }
    }
    return Result<void>::err(make_err("failed to download client installer across all proxy modes"));
  }
};

std::vector<int> parse_version_numbers(const std::string& ver_str) {
  std::string s = trim(ver_str);
  if (!s.empty() && (s.front() == 'v' || s.front() == 'V')) {
    s = s.substr(1);
  }
  // Trim pre-release tags like -beta, -rc, +build
  size_t tag_pos = s.find_first_of("-+");
  if (tag_pos != std::string::npos) {
    s = s.substr(0, tag_pos);
  }

  std::vector<int> nums;
  std::istringstream iss(s);
  std::string part;
  while (std::getline(iss, part, '.')) {
    try {
      nums.push_back(std::stoi(part));
    } catch (...) {
      nums.push_back(0);
    }
  }
  return nums;
}

}  // namespace

ClientUpdater::ClientUpdater()
    : downloader_(std::make_shared<DefaultAppUpdateDownloader>()) {}

ClientUpdater& ClientUpdater::instance() {
  static ClientUpdater inst;
  return inst;
}

void ClientUpdater::set_downloader(std::shared_ptr<IAppUpdateDownloader> downloader) {
  downloader_ = std::move(downloader);
}

void ClientUpdater::set_current_version(const std::string& version) {
  current_version_ = version;
}

bool ClientUpdater::is_version_newer(const std::string& latest, const std::string& current) {
  auto l_nums = parse_version_numbers(latest);
  auto c_nums = parse_version_numbers(current);

  size_t max_len = std::max(l_nums.size(), c_nums.size());
  for (size_t i = 0; i < max_len; ++i) {
    int l_val = (i < l_nums.size()) ? l_nums[i] : 0;
    int c_val = (i < c_nums.size()) ? c_nums[i] : 0;
    if (l_val > c_val) return true;
    if (l_val < c_val) return false;
  }
  return false;
}

Result<AppUpdateInfo> ClientUpdater::parse_manifest(
    const std::string& json_text, const std::string& current_version) {
  try {
    auto j = nlohmann::json::parse(json_text);
    AppUpdateInfo info;
    info.current_version = current_version;

    if (j.contains("version") && j["version"].is_string()) {
      info.latest_version = j["version"].get<std::string>();
    }
    if (j.contains("notes") && j["notes"].is_string()) {
      info.notes = j["notes"].get<std::string>();
    }
    if (j.contains("pub_date") && j["pub_date"].is_string()) {
      info.pub_date = j["pub_date"].get<std::string>();
    }

    // Platforms lookup (Tauri updater schema)
    if (j.contains("platforms") && j["platforms"].is_object()) {
      const auto& platforms = j["platforms"];
      for (auto it = platforms.begin(); it != platforms.end(); ++it) {
        std::string key = to_lower(it.key());
        if (key.find("windows") != std::string::npos || key.find("x86_64") != std::string::npos) {
          const auto& pobj = it.value();
          if (pobj.contains("url") && pobj["url"].is_string()) {
            info.download_url = pobj["url"].get<std::string>();
          }
          if (pobj.contains("signature") && pobj["signature"].is_string()) {
            info.signature = pobj["signature"].get<std::string>();
          }
          break;
        }
      }
      // If no matching platform was chosen, fallback to first entry
      if (info.download_url.empty() && !platforms.empty()) {
        const auto& pobj = platforms.begin().value();
        if (pobj.contains("url") && pobj["url"].is_string()) {
          info.download_url = pobj["url"].get<std::string>();
        }
        if (pobj.contains("signature") && pobj["signature"].is_string()) {
          info.signature = pobj["signature"].get<std::string>();
        }
      }
    }

    // Direct url fallback
    if (info.download_url.empty()) {
      if (j.contains("download_url") && j["download_url"].is_string()) {
        info.download_url = j["download_url"].get<std::string>();
      } else if (j.contains("url") && j["url"].is_string()) {
        info.download_url = j["url"].get<std::string>();
      }
    }
    if (info.signature.empty() && j.contains("signature") && j["signature"].is_string()) {
      info.signature = j["signature"].get<std::string>();
    }

    info.has_update = is_version_newer(info.latest_version, info.current_version);
    return Result<AppUpdateInfo>::ok(info);
  } catch (const std::exception& e) {
    return Result<AppUpdateInfo>::err(make_err(std::string("failed to parse manifest JSON: ") + e.what()));
  }
}

Result<AppUpdateInfo> ClientUpdater::check_app_update() {
  if (!downloader_) {
    return Result<AppUpdateInfo>::err(make_err("downloader not configured"));
  }
  const std::string manifest_url =
      "https://raw.githubusercontent.com/qiu-yuxiao/clash-win/dev/updater/app-update.json";
  auto res = downloader_->fetch_manifest(manifest_url);
  if (!res.is_ok()) {
    return Result<AppUpdateInfo>::err(res.error());
  }
  return parse_manifest(res.value(), current_version_);
}

Result<std::wstring> ClientUpdater::download_app_update(const std::string& download_url) {
  if (!downloader_) {
    return Result<std::wstring>::err(make_err("downloader not configured"));
  }
  wchar_t temp_dir[MAX_PATH];
  GetTempPathW(MAX_PATH, temp_dir);
  std::wstring dest_path = std::wstring(temp_dir) + L"clashwin_setup_update.exe";

  auto res = downloader_->download_installer(download_url, dest_path, nullptr);
  if (!res.is_ok()) {
    return Result<std::wstring>::err(res.error());
  }
  return Result<std::wstring>::ok(dest_path);
}

Result<void> ClientUpdater::install_app_update(const std::wstring& installer_path, bool silent) {
  std::error_code ec;
  if (!std::filesystem::exists(installer_path, ec)) {
    return Result<void>::err(make_err("installer file does not exist"));
  }

  SHELLEXECUTEINFOW sei{};
  sei.cbSize = sizeof(sei);
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  sei.lpVerb = L"open";
  sei.lpFile = installer_path.c_str();
  sei.lpParameters = silent ? L"/S" : L"";
  sei.nShow = SW_SHOWNORMAL;

  if (!ShellExecuteExW(&sei)) {
    return Result<void>::err(make_err("failed to launch installer process"));
  }

  if (sei.hProcess) {
    CloseHandle(sei.hProcess);
  }
  return Result<void>::ok();
}

}  // namespace clashwin::update
