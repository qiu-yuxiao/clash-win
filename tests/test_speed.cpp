// speed 测试（← src-tauri/src/utils/speed.rs 内置测试全量移植）
#include "test_util.h"

#include "util/speed.h"

using clashwin::util::format_bytes_per_second;

UT_TEST(speed_byte_boundaries) {
  // ← speed.rs test_format_handles_byte_boundaries
  UT_CHECK_EQ(format_bytes_per_second(0), "0B/s");
  UT_CHECK_EQ(format_bytes_per_second(999), "999B/s");
  // 1000 ≥ 阈值即升档（显示值不超过三位数）
  UT_CHECK_EQ(format_bytes_per_second(1000), "1.0K/s");
  UT_CHECK_EQ(format_bytes_per_second(1024), "1.0K/s");
}

UT_TEST(speed_decimal_and_integer_rules) {
  // ← speed.rs test_format_handles_decimal_and_integer_rules
  UT_CHECK_EQ(format_bytes_per_second(9 * 1024), "9.0K/s");
  UT_CHECK_EQ(format_bytes_per_second(10 * 1024 - 1), "10K/s");
  UT_CHECK_EQ(format_bytes_per_second(10 * 1024), "10K/s");
  UT_CHECK_EQ(format_bytes_per_second(123 * 1024), "123K/s");
}

UT_TEST(speed_unit_promotion_after_rounding) {
  // ← speed.rs test_format_handles_unit_promotion_after_rounding
  // 999.5K/s 四舍五入为 1000 → 升档 1.0M/s
  UT_CHECK_EQ(format_bytes_per_second(999 * 1024 + 512), "1.0M/s");
  UT_CHECK_EQ(format_bytes_per_second(1024 * 1024), "1.0M/s");
  UT_CHECK_EQ(format_bytes_per_second(1536 * 1024), "1.5M/s");
}
