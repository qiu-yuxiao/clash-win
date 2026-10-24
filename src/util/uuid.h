#pragma once
// uid 生成（← utils/help.rs::get_uid：nanoid!(11, ALPHABET62)，前缀 + 11 位）
// ALPHABET = 0-9 a-z A-Z（62 字符）；前缀 R/L/m/s/r/p/g 由 PrfItem 类型决定（T03）。
#include <string>
#include <string_view>

namespace clashwin::util {

// 生成 prefix + 11 位随机串；线程安全
std::string gen_uid(std::string_view prefix);

}  // namespace clashwin::util
