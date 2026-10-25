#pragma once
// ← ClashVerge src-tauri/src/enhance/merge.rs::use_merge 全量移植
// merge 字段先 use_lowercase，再 deep_merge 覆盖 config（非 map 值整体覆盖）。
#include <yaml-cpp/yaml.h>

namespace clashwin::enhance {

YAML::Node use_merge(const YAML::Node& merge, const YAML::Node& config);

}  // namespace clashwin::enhance
