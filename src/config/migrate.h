#pragma once
// 配置版本迁移框架（← config/migrate.rs 全量对齐）
// 约定：
// - 各配置文件顶层带 config_version；缺失视为 v1；
// - 读取时逐级迁移、写入时落当前版本；迁移函数为纯函数（无 IO）；
// - 迁移前先备份为 <name>.bak.v<from>；备份失败则中止本次迁移；
// - 版本高于支持值（用户降级）→ 只告警、不迁移、不写回。
#include <string>

#include <yaml-cpp/yaml.h>

#include "clashwin/config/constants.h"
#include "clashwin/config/models.h"
#include "util/result.h"

namespace clashwin::config::migrate {

// ← migrate.rs::detect_config_version（缺失视为 v1；注意必须在模板合并前调用）
uint32_t detect_version(const YAML::Node& map);

// ← migrate.rs::backup_before_migration：<name>.bak.v<from>；
// 已存在则跳过覆盖（保留最早那份），返回备份路径；失败返回 nullopt
std::optional<std::wstring> backup_before_migration(const std::wstring& path,
                                                    uint32_t from_version);

// ← migrate.rs::migrate_verge（纯函数，返回迁移后版本号）
uint32_t migrate_verge(IVerge& cfg, uint32_t from_version);
// ← migrate.rs::migrate_clash（纯函数，对 Mapping 执行）
uint32_t migrate_clash(YAML::Node& map, uint32_t from_version);
// ← migrate.rs::migrate_profiles（当前无破坏性变更，骨架保留）
uint32_t migrate_profiles(IProfiles& profiles, uint32_t from_version);

// 各文件当前版本号
constexpr uint32_t current_verge_version() { return consts::kCurrentVergeVersion; }
constexpr uint32_t current_clash_version() { return consts::kCurrentClashVersion; }
constexpr uint32_t current_profiles_version() {
  return consts::kCurrentProfilesVersion;
}

}  // namespace clashwin::config::migrate
