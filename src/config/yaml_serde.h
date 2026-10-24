#pragma once
// serde_yaml_ng 兼容序列化层（T03 内部基建，T08 enhance 复用）
//
// 【为什么不用 YAML::Dump】vendored yaml-cpp 的 emitter：
//   1. map 下的 block sequence 会被多缩进 2 格（serde_yaml_ng/libyaml 的
//      dash 与父键同列，实机 config.yaml `dns-hijack:` 即证）；
//   2. null 默认输出 `~`（serde 输出 `null`）；
//   3. 多行字符串不支持 literal `|`（OnScalar 不读节点样式）。
// 三者都会破坏「与实机产物逐字节 diff 为空」的门禁，故按 libyaml 块布局
// 规则手写 dumper：
//   - map 嵌套 map：子键缩进 +2；
//   - map 值为 seq：dash 与键同列（不额外缩进）；
//   - seq 项为 map：首键紧跟 "- "，后续键与首键对齐；
//   - 多行字符串 → literal `|`（clip）/ `|-`（strip）；
//   - null → `null`；空集合 → `{}` / `[]`。
// 标量按文本直出（plain/双引号判定）；bool/数字文本 plain 形态与 serde 一致。
// 【局限】字符串值恰为数字/bool 文本时会按 plain 输出（serde 会加引号）——
// 当前配置域不存在该类字段，后续若引入需在此补引号策略。
#include <cstdint>
#include <optional>
#include <string>

#include <yaml-cpp/yaml.h>

#include "util/result.h"

namespace clashwin::config::serde {

// 序列化（含尾换行；空 map/seq 输出 "{}"/"[]"）
std::string dump(const YAML::Node& node);

// ← help.rs::save_yaml：prefix + "\n\n" + 正文；内容未变化跳过写；
// tmp 文件 + MoveFileExW(REPLACE_EXISTING) 原子替换；失败 tmp 留作 <path>.bak
Result<void> save_yaml_text(const std::wstring& path, const std::string& body,
                            const std::string& prefix);

// ---------- 标量节点构造 ----------
YAML::Node str_node(const std::string& v);
YAML::Node null_node();

// ---------- 键写入（保序：旧键原位覆盖，新键尾部追加） ----------
void set_str(YAML::Node& map, const std::string& key, const std::string& v);
void set_bool(YAML::Node& map, const std::string& key, bool v);
void set_u16(YAML::Node& map, const std::string& key, uint16_t v);
void set_u32(YAML::Node& map, const std::string& key, uint32_t v);
void set_i64(YAML::Node& map, const std::string& key, int64_t v);
void set_null(YAML::Node& map, const std::string& key);

// ---------- 取值（键缺失 / null / 类型不符 → nullopt，绝不抛异常） ----------
std::optional<std::string> opt_str(const YAML::Node& map, const std::string& key);
std::optional<bool> opt_bool(const YAML::Node& map, const std::string& key);
std::optional<uint16_t> opt_u16(const YAML::Node& map, const std::string& key);
std::optional<uint32_t> opt_u32(const YAML::Node& map, const std::string& key);
std::optional<uint64_t> opt_u64(const YAML::Node& map, const std::string& key);
std::optional<int64_t> opt_i64(const YAML::Node& map, const std::string& key);
std::optional<int32_t> opt_i32(const YAML::Node& map, const std::string& key);

// ← help.rs::read_mapping：读文件 → 校验顶层为 map（语法错误/顶层非 map → Error）
Result<YAML::Node> read_mapping_file(const std::wstring& path);

}  // namespace clashwin::config::serde
