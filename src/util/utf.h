#pragma once
// UTF-8/UTF-16 互转：仅限 Win32 API 调用边界使用（内部一律 UTF-8）
#include <string>

namespace clashwin::utf {

// UTF-8 → UTF-16（失败返回空串）
std::wstring utf8_to_utf16(const std::string& s);

// UTF-16 → UTF-8（失败返回空串）
std::string utf16_to_utf8(const std::wstring& s);

}  // namespace clashwin::utf
