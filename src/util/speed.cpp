#include "util/speed.h"

#include <bit>
#include <cmath>
#include <cstdio>

namespace clashwin::util {

namespace {
// ← speed.rs:4-14
constexpr double kDisplayThreshold = 1000.0;
const char* const kSpeedUnits[] = {"B/s", "K/s", "M/s", "G/s", "T/s"};
constexpr int kUnitCount = 5;

const double kScales[] = {
    1.0,
    1024.0,
    1024.0 * 1024.0,
    1024.0 * 1024.0 * 1024.0,
    1024.0 * 1024.0 * 1024.0 * 1024.0,
};
}  // namespace

std::string format_bytes_per_second(uint64_t bytes_per_sec) {
  if (bytes_per_sec < static_cast<uint64_t>(kDisplayThreshold)) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lluB/s",
                  static_cast<unsigned long long>(bytes_per_sec));
    return buf;
  }

  // ← ilog2()/10：bit_width-1 等价
  int unit_index =
      static_cast<int>((bytes_per_sec <= 0 ? 0 : std::bit_width(bytes_per_sec) - 1) / 10);
  if (unit_index > kUnitCount - 1) unit_index = kUnitCount - 1;

  double value = static_cast<double>(bytes_per_sec) / kScales[unit_index];

  if (std::round(value) >= kDisplayThreshold && unit_index < kUnitCount - 1) {
    ++unit_index;
    value = static_cast<double>(bytes_per_sec) / kScales[unit_index];
  }

  char buf[32];
  if (value < 9.95) {
    std::snprintf(buf, sizeof(buf), "%.1f%s", value, kSpeedUnits[unit_index]);
  } else {
    std::snprintf(buf, sizeof(buf), "%.0f%s", std::round(value),
                  kSpeedUnits[unit_index]);
  }
  return buf;
}

}  // namespace clashwin::util
