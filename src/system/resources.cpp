#include "system/resources.h"

#include <filesystem>
#include <utility>
#include "util/log.h"
#include "util/utf.h"

namespace clashwin::system {

Result<void> init_resources(const std::wstring& app_home_dir,
                            const std::wstring& app_resources_dir) {
  if (app_home_dir.empty() || app_resources_dir.empty()) {
    return Result<void>::ok();
  }

  std::error_code ec;
  std::filesystem::path home(app_home_dir);
  std::filesystem::path res(app_resources_dir);

  if (!std::filesystem::exists(home, ec)) {
    std::filesystem::create_directories(home, ec);
    if (ec) {
      CWLOG_ERROR("init_resources: failed to create home dir " + utf::utf16_to_utf8(home.wstring()) + ": " + ec.message());
      return Result<void>::ok();
    }
  }

  // 资源映射列表：src_rel, dest_rel
  const std::pair<const wchar_t*, const wchar_t*> files[] = {
      {L"Country.mmdb", L"Country.mmdb"},
      {L"geosite.dat", L"geosite.dat"},
      {L"gfwlist.yaml", L"ruleset\\gfwlist.yaml"},
  };

  for (const auto& [src_rel, dest_rel] : files) {
    auto src_path = res / src_rel;
    auto dest_path = home / dest_rel;

    if (!std::filesystem::exists(src_path, ec) || std::filesystem::file_size(src_path, ec) == 0) {
      continue;
    }

    bool should_copy = false;
    if (!std::filesystem::exists(dest_path, ec) || std::filesystem::file_size(dest_path, ec) == 0) {
      should_copy = true;
    } else {
      auto src_time = std::filesystem::last_write_time(src_path, ec);
      if (!ec) {
        auto dest_time = std::filesystem::last_write_time(dest_path, ec);
        if (!ec && src_time > dest_time) {
          should_copy = true;
        }
      }
    }

    if (should_copy) {
      std::filesystem::create_directories(dest_path.parent_path(), ec);
      std::filesystem::copy_file(src_path, dest_path, std::filesystem::copy_options::overwrite_existing, ec);
      if (ec) {
        CWLOG_WARN("init_resources: copy failed for " + utf::utf16_to_utf8(dest_rel) + ": " + ec.message());
      } else {
        CWLOG_INFO("init_resources: released " + utf::utf16_to_utf8(dest_rel));
      }
    }
  }

  return Result<void>::ok();
}

}  // namespace clashwin::system
