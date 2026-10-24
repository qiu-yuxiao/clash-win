#include "migrate.h"

#include <windows.h>

#include "verge.h"
#include "util/str_utils.h"
#include "util/utf.h"
#include "util/log.h"
#include "yaml_serde.h"

namespace clashwin::config::migrate {

using serde::opt_str;

uint32_t detect_version(const YAML::Node& map) {
  // ← migrate.rs::detect_config_version
  uint32_t v = 0;
  const YAML::Node n = map["config_version"];
  if (n && n.IsScalar() && !n.IsNull()) {
    try {
      v = static_cast<uint32_t>(std::stoull(util::trim(n.Scalar())));
    } catch (...) {
      v = 0;
    }
  }
  return v == 0 ? consts::kImplicitLegacyVersion : v;
}

std::optional<std::wstring> backup_before_migration(const std::wstring& path,
                                                    uint32_t from_version) {
  size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return std::nullopt;
  std::wstring name = path.substr(slash + 1);
  std::wstring backup =
      path.substr(0, slash + 1) + name + L".bak.v" + std::to_wstring(from_version);
  if (GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES) {
    // 同一版本只备份一次，避免重复启动时反复覆盖（保留最早那份原始配置）
    CWLOG_INFO("config migration backup already exists, skip: " +
               utf::utf16_to_utf8(backup));
    return backup;
  }
  if (CopyFileW(path.c_str(), backup.c_str(), TRUE)) {
    CWLOG_INFO("config backed up before migration: " + utf::utf16_to_utf8(backup));
    return backup;
  }
  CWLOG_WARN("config migration backup failed, caller should abort migration");
  return std::nullopt;
}

// ---------- verge ----------
namespace {
// v1→v2：历史遗留 start_page "/home" 归一化为 "/"
void migrate_verge_v1_to_v2(IVerge& cfg) {
  if (cfg.start_page && *cfg.start_page == "/home") {
    cfg.start_page = "/";
    CWLOG_INFO("verge v1->v2: start_page \"/home\" normalized to \"/\"");
  }
}
// v2→v3：日志保留个数旧默认 8 → 40（用户自定义值一律尊重）
void migrate_verge_v2_to_v3(IVerge& cfg) {
  if (cfg.app_log_max_count &&
      *cfg.app_log_max_count == consts::kVergeOldLogCount) {
    cfg.app_log_max_count = consts::kAppLogMaxCount;
    CWLOG_INFO("verge v2->v3: app_log_max_count 8 -> 40");
  }
}
}  // namespace

uint32_t migrate_verge(IVerge& cfg, uint32_t from_version) {
  uint32_t version = from_version < consts::kImplicitLegacyVersion
                         ? consts::kImplicitLegacyVersion
                         : from_version;
  if (version > current_verge_version()) {
    CWLOG_WARN("verge.yaml version " + std::to_string(version) +
               " newer than supported, keep as-is");
    return version;
  }
  while (version < current_verge_version()) {
    switch (version) {
      case 1: migrate_verge_v1_to_v2(cfg); break;
      case 2: migrate_verge_v2_to_v3(cfg); break;
      default:
        CWLOG_WARN("missing verge.yaml v" + std::to_string(version) +
                   " migration, stop");
        return version;
    }
    ++version;
  }
  return version;
}

// ---------- clash ----------
namespace {
// v1→v2：tun.stack 旧默认 "system" 归一化为 "gvisor"
void migrate_clash_v1_to_v2(YAML::Node& map) {
  YAML::Node tun = map["tun"];
  if (!tun || !tun.IsMap()) return;
  YAML::Node stack = tun["stack"];
  if (stack && stack.IsScalar() && stack.Scalar() == "system") {
    tun["stack"] = consts::kDefaultStack;
    CWLOG_INFO(std::string("clash v1->v2: tun.stack \"system\" -> \"") +
               consts::kDefaultStack + "\"");
  }
}
}  // namespace

uint32_t migrate_clash(YAML::Node& map, uint32_t from_version) {
  uint32_t version = from_version < consts::kImplicitLegacyVersion
                         ? consts::kImplicitLegacyVersion
                         : from_version;
  if (version > current_clash_version()) {
    CWLOG_WARN("clash.yaml version " + std::to_string(version) +
               " newer than supported, keep as-is");
    return version;
  }
  while (version < current_clash_version()) {
    switch (version) {
      case 1: migrate_clash_v1_to_v2(map); break;
      default:
        CWLOG_WARN("missing clash.yaml v" + std::to_string(version) +
                   " migration, stop");
        return version;
    }
    ++version;
  }
  return version;
}

// ---------- profiles ----------
uint32_t migrate_profiles(IProfiles& profiles, uint32_t from_version) {
  (void)profiles;  // 当前无破坏性变更（骨架保留，便于后续扩展）
  uint32_t version = from_version < consts::kImplicitLegacyVersion
                         ? consts::kImplicitLegacyVersion
                         : from_version;
  if (version > current_profiles_version()) {
    CWLOG_WARN("profiles.yaml version " + std::to_string(version) +
               " newer than supported, keep as-is");
    return version;
  }
  // 当前版本即 v1，尚无破坏性变更；骨架与 verge 对称，便于后续扩展
  return version;
}

}  // namespace clashwin::config::migrate
