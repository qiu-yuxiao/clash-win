#include "util/parse_traffic.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

namespace clashwin::util {

namespace {

const char* const kUnits[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
constexpr int kUnitCount = 9;

// JS Number.prototype.toFixed(0)（正数域；≥1e21 时 JS 退回 toString，不移植）
std::string js_to_fixed0(double v) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.0f", v);
  return buf;
}

// JS Number.prototype.toPrecision(3)
// 规则（ECMA-262 / V8 DoubleToPrecisionCString）：
//   先取 3 位有效数字（十进制进位同步调整指数），设 e = 取整后值的十进制指数：
//   e < -6 或 e >= 3 → 指数形式 "d.DDe±X"；否则定点。
std::string js_to_precision3(double x) {
  if (std::isnan(x)) return "NaN";
  bool neg = x < 0;
  double a = neg ? -x : x;
  if (a == 0) return "0.00";  // ±0 → "0.00"（JS 无 -0 符号）

  // %.*e 给出 3 位有效数字 + 已随进位调整的指数
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.2e", a);
  // 形如 "d.DDe±XX"
  char digits[3] = {buf[0], buf[2], buf[3]};
  int e = std::atoi(buf + 5);

  std::string m;
  if (e < -6 || e >= 3) {
    m = std::string(1, digits[0]) + "." + digits[1] + digits[2] + "e" +
        (e < 0 ? "-" : "+") + std::to_string(e < 0 ? -e : e);
  } else if (e >= 2) {
    // e == p-1：整数 = 3 位数字 + (e-2) 个 0
    m = std::string(1, digits[0]) + digits[1] + digits[2];
    for (int i = 0; i < e - 2; ++i) m += '0';
  } else if (e >= 0) {
    m = std::string(1, digits[0]);
    if (e >= 1) m += digits[1];
    m += '.';
    if (e == 0) {
      m += digits[1];
      m += digits[2];
    } else {
      m += digits[2];
    }
  } else {
    m = "0.";
    for (int i = 0; i < -e - 1; ++i) m += '0';
    m += digits[0];
    m += digits[1];
    m += digits[2];
  }
  return neg ? "-" + m : m;
}

}  // namespace

std::pair<std::string, std::string> parse_traffic(std::optional<double> num) {
  // ← parse-traffic.ts:3-12
  if (!num.has_value() || std::isnan(*num)) return {"NaN", ""};
  const double v = *num;

  int exp = 0;
  if (v >= 1) {
    double e = std::floor(std::log2(v) / 10.0);
    if (e > kUnitCount - 1) e = kUnitCount - 1;
    if (e < 0) e = 0;
    exp = static_cast<int>(e);
  }
  const double dat = v / std::pow(1024.0, exp);

  std::string ret = dat >= 1000.0 ? js_to_fixed0(dat) : js_to_precision3(dat);
  return {ret, kUnits[exp]};
}

}  // namespace clashwin::util
