#pragma once
// 流量字节数 → 人类可读格式与实时流量曲线几何（← src/utils/parse-traffic.ts 逐规则移植）
// 返回 [数值串, 单位]；单位 ∈ B/KB/MB/GB/TB/PB/EB/ZB/YB。
// 语义：exp = num<1 ? 0 : min(floor(log2(num)/10), 8)；
//       dat = num / 1024^exp；dat ≥ 1000 → 取整，否则 3 位有效数字
//       （JS toPrecision(3) 语义，含指数形式边界）。
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace clashwin::util {

// ← src/utils/parse-traffic.ts:3-10（BUG-410）
inline constexpr int kTrafficGraphMinPoints = 30;
inline constexpr int kTrafficGraphMaxPoints = 60;
inline constexpr int kTrafficGraphBufferLen = kTrafficGraphMaxPoints + 3;
inline constexpr double kNarrowCanvasWidth = 275.0;
inline constexpr double kMaximizedCanvasWidth = 616.0;

// 根据画布逻辑宽度平滑映射可见时间窗口（≤275px 显示 30s，随拉宽或最大化线性扩展至 60s）
int resolve_traffic_visible_points(double css_width);

// 截取右侧最新 visible_points 秒视窗（保留 3 个边缘插值锚点），左端随宽度扩展自然揭露更早缓冲
template <typename T>
std::vector<T> slice_traffic_visible_buffer(const std::vector<T>& buffer,
                                            double visible_points) {
  const int rounded = std::isfinite(visible_points)
                          ? static_cast<int>(std::floor(visible_points + 0.5))
                          : kTrafficGraphMinPoints;
  const size_t clamped_visible = static_cast<size_t>(
      (std::clamp)(rounded, kTrafficGraphMinPoints, kTrafficGraphMaxPoints));
  const size_t needed = clamped_visible + 3u;
  if (buffer.size() > needed) {
    return std::vector<T>(
        buffer.end() - static_cast<std::ptrdiff_t>(needed), buffer.end());
  }
  return buffer;
}

// 分段对数纵轴坐标映射，钳制于 [2, height - 2] 防止 2.5px 线宽在底边/顶边被裁切
double compute_traffic_graph_y(double v, double height);

// 归一化横向点位计算：消除宽窄切换间的整数取整锯齿与右端留白
double compute_traffic_point_x(int index, double dx, double progress);

// num 非数值（nullopt/NaN）→ {"NaN", ""}
std::pair<std::string, std::string> parse_traffic(std::optional<double> num);

}  // namespace clashwin::util
