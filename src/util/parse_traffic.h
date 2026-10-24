#pragma once
// 流量字节数 → 人类可读格式（← src/utils/parse-traffic.ts 逐规则移植）
// 返回 [数值串, 单位]；单位 ∈ B/KB/MB/GB/TB/PB/EB/ZB/YB。
// 语义：exp = num<1 ? 0 : min(floor(log2(num)/10), 8)；
//       dat = num / 1024^exp；dat ≥ 1000 → 取整，否则 3 位有效数字
//       （JS toPrecision(3) 语义，含指数形式边界）。
#include <optional>
#include <string>
#include <utility>

namespace clashwin::util {

// num 非数值（nullopt/NaN）→ {"NaN", ""}
std::pair<std::string, std::string> parse_traffic(std::optional<double> num);

}  // namespace clashwin::util
