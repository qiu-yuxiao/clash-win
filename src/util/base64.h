#pragma once
// 鲁棒 Base64 解码（← universal_parser.rs::decode_base64_robust）
// 顺序：standard → urlsafe → 补 padding 后 standard → 补 padding 后 urlsafe；
// 解码前剥离全部空白（含换行）；全部失败返回 nullopt。
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace clashwin::util {

// standard/urlsafe 兼容、自动剥空白、缺省 padding 补齐
std::optional<std::vector<uint8_t>> decode_base64_robust(std::string_view s);

}  // namespace clashwin::util
