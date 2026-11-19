// parse_traffic 测试（← src/utils/parse-traffic.ts 格式化规则与 BUG-410 几何不变量）
#include "test_util.h"

#include <cmath>
#include <numeric>
#include <string>
#include <vector>

#include "util/parse_traffic.h"

using clashwin::util::compute_traffic_graph_y;
using clashwin::util::compute_traffic_point_x;
using clashwin::util::kTrafficGraphBufferLen;
using clashwin::util::kTrafficGraphMaxPoints;
using clashwin::util::kTrafficGraphMinPoints;
using clashwin::util::parse_traffic;
using clashwin::util::resolve_traffic_visible_points;
using clashwin::util::slice_traffic_visible_buffer;

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

// ← src/utils/parse-traffic.test.ts:45-123（BUG-410）
UT_TEST(parse_traffic_bug410_graph_y_clamp_and_log_sensitivity) {
  for (const double height : {80.0, 99.0}) {
    UT_CHECK(compute_traffic_graph_y(0.0, height) == height - 2.0);
    UT_CHECK(compute_traffic_graph_y(10485760.0, height) == 2.0);
    UT_CHECK(compute_traffic_graph_y(50.0 * 1024.0 * 1024.0, height) == 2.0);

    for (const double bps :
         {0.0, 1.0, 10.0, 512.0, 10240.0, 102400.0, 1048576.0, 104857600.0}) {
      const double y = compute_traffic_graph_y(bps, height);
      UT_CHECK(y >= 2.0);
      UT_CHECK(y <= height - 2.0);
    }
  }

  // 分段对数纵轴对中小流量仍具备明显垂直抬升（不被历史高峰压扁为贴底直线）
  const double height = 99.0;
  const double baseline_y = compute_traffic_graph_y(0.0, height);
  const double small_bps_y = compute_traffic_graph_y(512.0, height);
  const double ten_kb_y = compute_traffic_graph_y(10.0 * 1024.0, height);
  UT_CHECK((baseline_y - small_bps_y) > 25.0);
  UT_CHECK((small_bps_y - ten_kb_y) > 10.0);
}

UT_TEST(parse_traffic_bug410_visible_points_and_horizontal_coverage) {
  UT_CHECK_EQ(kTrafficGraphMinPoints, 30);
  UT_CHECK_EQ(kTrafficGraphMaxPoints, 60);
  UT_CHECK_EQ(kTrafficGraphBufferLen, 63);

  UT_CHECK_EQ(resolve_traffic_visible_points(259.0), 30);
  UT_CHECK_EQ(resolve_traffic_visible_points(275.0), 30);

  const int w400 = resolve_traffic_visible_points(400.0);
  const int w500 = resolve_traffic_visible_points(500.0);
  UT_CHECK(w400 > 30);
  UT_CHECK(w400 < w500);
  UT_CHECK(w500 < 60);
  UT_CHECK_EQ(resolve_traffic_visible_points(616.0), 60);
  UT_CHECK_EQ(resolve_traffic_visible_points(800.0), 60);

  std::vector<int> buffer(static_cast<size_t>(kTrafficGraphBufferLen));
  std::iota(buffer.begin(), buffer.end(), 0);
  const auto narrow_slice = slice_traffic_visible_buffer(buffer, 30);
  const auto wide_slice = slice_traffic_visible_buffer(buffer, 60);
  UT_CHECK_EQ(narrow_slice.size(), size_t(33));
  UT_CHECK_EQ(wide_slice.size(), size_t(63));
  UT_CHECK_EQ(narrow_slice.back(), 62);
  UT_CHECK_EQ(wide_slice.back(), 62);
  UT_CHECK_EQ(narrow_slice.front(), 30);
  UT_CHECK_EQ(wide_slice.front(), 0);

  struct Scenario {
    double width;
    int visible_points;
  };
  const Scenario scenarios[] = {
      {259.0, kTrafficGraphMinPoints},
      {440.0, resolve_traffic_visible_points(440.0)},
      {616.0, resolve_traffic_visible_points(616.0)},
  };
  for (const auto& s : scenarios) {
    const auto sliced = slice_traffic_visible_buffer(buffer, s.visible_points);
    const double dx = s.width / static_cast<double>(s.visible_points);
    for (const double progress : {0.0, 0.5, 1.0}) {
      const double first_x = compute_traffic_point_x(0, dx, progress);
      const double last_x = compute_traffic_point_x(
          static_cast<int>(sliced.size()) - 1, dx, progress);
      UT_CHECK(first_x <= 0.0);
      UT_CHECK(last_x >= s.width - 1e-6);
    }
  }
}
