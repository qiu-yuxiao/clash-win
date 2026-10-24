// parse_traffic 测试（← src/utils/parse-traffic.ts 格式化规则与边界）
#include "test_util.h"

#include <string>

#include "util/parse_traffic.h"

using clashwin::util::parse_traffic;

UT_TEST(parse_traffic_non_number) {
  auto r1 = parse_traffic(std::nullopt);  // TS: typeof num !== 'number'
  UT_CHECK_EQ(r1.first, "NaN");
  UT_CHECK_EQ(r1.second, "");

  auto r2 = parse_traffic(NAN);
  UT_CHECK_EQ(r2.first, "NaN");
  UT_CHECK_EQ(r2.second, "");
}

UT_TEST(parse_traffic_byte_boundaries) {
  auto r0 = parse_traffic(0.0);  // (0).toPrecision(3) = "0.00"
  UT_CHECK_EQ(r0.first, "0.00");
  UT_CHECK_EQ(r0.second, "B");

  auto r5 = parse_traffic(5.0);  // (5).toPrecision(3) = "5.00"
  UT_CHECK_EQ(r5.first, "5.00");
  UT_CHECK_EQ(r5.second, "B");

  auto r999 = parse_traffic(999.0);  // dat=999 <1000 → "999"
  UT_CHECK_EQ(r999.first, "999");
  UT_CHECK_EQ(r999.second, "B");

  auto r1000 = parse_traffic(1000.0);  // dat=1000 ≥1000 → toFixed(0)
  UT_CHECK_EQ(r1000.first, "1000");
  UT_CHECK_EQ(r1000.second, "B");

  auto r1023 = parse_traffic(1023.0);
  UT_CHECK_EQ(r1023.first, "1023");
  UT_CHECK_EQ(r1023.second, "B");
}

UT_TEST(parse_traffic_units) {
  auto kb = parse_traffic(1024.0);
  UT_CHECK_EQ(kb.first, "1.00");
  UT_CHECK_EQ(kb.second, "KB");

  auto mb = parse_traffic(1024.0 * 1024.0);  // 1MB
  UT_CHECK_EQ(mb.first, "1.00");
  UT_CHECK_EQ(mb.second, "MB");

  auto gb = parse_traffic(1024.0 * 1024.0 * 1024.0);  // 1GB
  UT_CHECK_EQ(gb.first, "1.00");
  UT_CHECK_EQ(gb.second, "GB");

  auto tb = parse_traffic(1099511627776.0);  // 1TB
  UT_CHECK_EQ(tb.first, "1.00");
  UT_CHECK_EQ(tb.second, "TB");

  auto frac = parse_traffic(1536.0);  // 1.5KB
  UT_CHECK_EQ(frac.first, "1.50");
  UT_CHECK_EQ(frac.second, "KB");

  auto large = parse_traffic(123456789.0);  // ≈117.7MB → 3 位有效
  UT_CHECK_EQ(large.first, "118");
  UT_CHECK_EQ(large.second, "MB");
}

UT_TEST(parse_traffic_negative_and_edge) {
  // 负数走 num<1 分支 → toPrecision(3)
  auto rn = parse_traffic(-5.0);
  UT_CHECK_EQ(rn.first, "-5.00");
  UT_CHECK_EQ(rn.second, "B");

  // 超大值：单位钳制 YB
  auto ry = parse_traffic(1e30);
  UT_CHECK_EQ(ry.second, "YB");
}
