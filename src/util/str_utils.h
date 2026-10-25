#pragma once
// 字符串工具（← utils/help.rs、config/prfitem.rs::fix_dirty_url、
//              utils/resolve/scheme.rs::decode_subscription_url）
// 内部一律 UTF-8；percent-decode 为 UTF-8 lossy 语义（非法字节→U+FFFD）。
#include <string>
#include <string_view>
#include <vector>

#include "util/result.h"

namespace clashwin::util {

// 去除两端 ASCII 空白
std::string trim(std::string_view s);

// 按单字符分隔（保留空段，Rust str::split 语义）
std::vector<std::string> split(std::string_view s, char delim);
// 同上但跳过空段
std::vector<std::string> split_no_empty(std::string_view s, char delim);

bool starts_with(std::string_view s, std::string_view prefix);
bool ends_with(std::string_view s, std::string_view suffix);
std::string to_lower(std::string_view s);

// percent-decode（%XX；非法序列原样保留；UTF-8 lossy：非法字节→U+FFFD）
std::string percent_decode(std::string_view s);

// 最多解码 2 次、结果稳定即停（← scheme.rs 防双解码语义）
std::string percent_decode_max2(std::string_view s);

// Url::parse 的轻量替身：scheme 语法合法（[A-Za-z][A-Za-z0-9+.-]*: 且有后继）
// 即视为 URL。用于 decode_subscription_url 的"已解码则跳过"判定。
bool is_probably_url(std::string_view s);

// ← scheme.rs::decode_subscription_url：已是 URL 原样返回；
// 否则最多解码 2 次，中途成为 URL 或结果稳定即停。
std::string decode_subscription_url(std::string_view raw_url);

// ← prfitem.rs::fix_dirty_url：查询参数误挂在 path 上（…/path&a=1）时
// 把首个 '&' 改写为 '?'。已有 '?' 或不含 '&' 的 URL 原样返回
// （scheme/host 归一小写，与 Url::parse 的规范化对齐）。
// 注意：不重排 path（/../ 解析、默认端口剥离属 T06 网络层的完整 URL 解析器）。
Result<std::string> fix_dirty_url(std::string_view input);

// ← String::from_utf8 严格 UTF-8 校验（T06 共享：uri_list/net_http）
bool is_valid_utf8(std::string_view s);

// 标准 Base64 编码（含 '=' padding；net_http Basic 认证/测试用）
std::string encode_base64(const uint8_t* data, size_t len);
std::string encode_base64(std::string_view s);

}  // namespace clashwin::util
