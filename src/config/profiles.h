#pragma once
// IProfiles 实现（profiles.yaml）+ profiles/ 目录文件管理
// ← config/profiles.rs + config.rs::ensure_default_profile_items
#include "clashwin/config/models.h"

namespace clashwin::config::profiles {

std::wstring profiles_path();

// ← profiles.rs::new：读 profiles.yaml + 迁移 + 补缺失 uid；失败 → default
IProfiles load();

// 全局保护件 uid（config.rs::ensure_default_profile_items）
inline constexpr const char* kGlobalMergeUid = "Merge";
inline constexpr const char* kGlobalScriptUid = "Script";

// ← config.rs::ensure_default_profile_items：确保全局 Merge/Script 存在
Result<void> ensure_default_profile_items(IProfiles& p);

// append_item 的独立落点（供 prfitem.cpp ensure_enhance_items 调用，
// 避免循环依赖：prfitem.cpp → profiles.cpp 单向）
Result<void> append_item_safe(PrfItem& item);

// 注册当前装配期 profiles 实例（Config::init 设置；T09 改为 Draft 门面）
void set_profiles_instance(IProfiles* p);

// 序列化（serde 布局；供测试 diff）
std::string dump_profiles(const IProfiles& p);

Result<void> save_profiles(const IProfiles& p);

// ← profiles.rs::is_profile_file：^[RL]\w+\.yaml$ / ^m\w+\.yaml$ /
//   ^s\w+\.js$ / ^[rpg]\w+\.yaml$
bool is_profile_file(const std::string& filename);

// 全局保护件文件名（cleanup 时永不删除）
bool is_protected_global_file(const std::string& filename);

}  // namespace clashwin::config::profiles
