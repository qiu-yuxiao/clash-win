#pragma once
// ← ClashVerge src-tauri/src/enhance/field.rs 全量移植
// use_lowercase：仅保留字符串键并 ASCII 转小写（serde_yaml Value::as_str 语义：
// 数字/bool 键一律丢弃）；use_sort：HANDLE_FIELDS → 中间段 → DEFAULT_FIELDS。
//
// 【G3 实证】实机两次生成（clash-win.yaml vs clash-win-check.yaml）中间段
// （非 HANDLE、非 DEFAULT 的键）顺序互不相同——Rust HashSet 随机遍历序，属
// 实机运行时不确定性。本移植取「输入插入序」这一确定序；逐行 diff 归因时
// 该段顺序差异视为预期。另据实机 golden：DEFAULT_FIELDS 不含 rules
// （实机产物中 rules 落在随机中间段、位于 proxies 之前），与 ClashVerge
// 工作区源码的 5 项版本不同，故此处按实机行为取 4 项。
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace clashwin::enhance::field {

extern const char* const kHandleFields[];   // 12 项（field.rs:5-18）
extern const char* const kDefaultFields[];  // 4 项（实机对齐，见文件头）

// 是否字符串键（serde_yaml Value::as_str == Some）：tag "!" 或
// 「非数字/bool/null 文本的 plain」；程序构造（tag 空）视为字符串。
bool is_string_key(const YAML::Node& key);

// ← field.rs::use_lowercase：新 map，字符串键 ASCII 小写，其余键丢弃
YAML::Node use_lowercase(const YAML::Node& config);

// ← field.rs::use_sort：HANDLE_FIELDS（有则按表序）→ 中间键（输入插入序）
// → DEFAULT_FIELDS（按表序）。原地语义：返回新节点。
YAML::Node use_sort(const YAML::Node& config);

// ← field.rs::use_keys：全部字符串键（小写化）
std::vector<std::string> use_keys(const YAML::Node& config);

}  // namespace clashwin::enhance::field
