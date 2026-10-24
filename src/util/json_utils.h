#pragma once
// nlohmann/json 安全取值/类型转换助手（← crates/tauri-plugin-mihomo/src/utils.rs、
// mihomo/models.rs 侧的 JSON 载荷取值习惯）
// 约定：类型不匹配一律回退默认值/空 optional，绝不抛异常。
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace clashwin::util {

// 取整型字段（key 不存在或类型不符 → def）
int64_t get_or_int(const nlohmann::json& j, std::string_view key, int64_t def);

// 取字符串字段（key 不存在或非字符串 → def）
std::string get_or_string(const nlohmann::json& j, std::string_view key,
                          std::string_view def);

// 取布尔字段（key 不存在或非布尔 → def）
bool get_or_bool(const nlohmann::json& j, std::string_view key, bool def);

// try_get：key 存在且类型匹配才返回值
std::optional<int64_t> try_get_int(const nlohmann::json& j,
                                   std::string_view key);
std::optional<std::string> try_get_string(const nlohmann::json& j,
                                          std::string_view key);

// 字符串 ↔ wstring 边界（内部 UTF-8，Win32 显示层 UTF-16）：
// 字段为字符串时经 utf8↔utf16 转换；不存在/类型不符 → def
std::wstring get_or_wstring(const nlohmann::json& j, std::string_view key,
                            const std::wstring& def);

}  // namespace clashwin::util
