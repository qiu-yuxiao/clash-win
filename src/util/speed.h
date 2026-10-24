#pragma once
// 字节速率格式化（← src-tauri/src/utils/speed.rs 逐行移植）
// 阈值 1000（显示层约定：保证显示值不超过三位数）；单位 B/s K/s M/s G/s T/s。
#include <cstdint>
#include <string>

namespace clashwin::util {

std::string format_bytes_per_second(uint64_t bytes_per_sec);

}  // namespace clashwin::util
