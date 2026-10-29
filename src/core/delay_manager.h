#pragma once
// 延迟管理器（T16）——单点/群组测速 + backend-delay-results 批量注入。
// 语义对照（只读参考）：
// - 失败/超时口径：DELAY_ERROR_VALUE=10000（cmd/clash.rs:71）、
//   探针超时=NODE_DELAY_MAX_MS=5000（v3.1.1 BUG-304：2000→5000，
//   链路拥塞期 2~5s 的「慢而可用」节点不再被误判为断流；与判死阈值统一）
// - 群测走测量组 PROXY__METRICS group delay 单调用（monitor.rs：TUN 下对活跃
//   节点自测会回环返回 timeout，是断流根因——禁止逐节点自测路径）
// - 未测得 → 0（Timeout 语义，monitor.rs:958-963 注释：与前端 "Timeout" 一致）
// - display 全量；candidates = [NODE_DELAY_MIN(30), NODE_DELAY_MAX(5000))
// - -2 testing 中间态：群测发起→逐节点置 -2→完成置实际值（T13 已支持显示）
// 数据源注入 DelayProbeHooks（测试 mock；生产绑 MihomoClient/NodeData）。
#include <windows.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace clashwin::core {

inline constexpr uint32_t kDelayErrorValue = 10000;   // ← DELAY_ERROR_VALUE
inline constexpr uint32_t kNodeDelayMinMs = 30;       // ← NODE_DELAY_MIN_MS
inline constexpr uint32_t kNodeDelayMaxMs = 5000;     // ← NODE_DELAY_MAX_MS/探针超时（BUG-304）
inline constexpr uint32_t kMetricsReuseMaxAgeSecs = 30;   // ← 子集缓存新鲜阈值
inline constexpr uint32_t kFallbackHistoryMaxAgeSecs = 120;  // ← history 兜底阈值

// 延迟探针钩子（生产 = MihomoClient group_delay/delay_proxy_by_name/get_proxies）
struct DelayProbeHooks {
  // 测量组群测（单调用，内核拨测每成员）→ {name: delay}；失败 → nullopt
  std::function<std::optional<std::map<std::string, uint32_t>>(
      const std::string& metrics_group)>
      group_probe;
  // history 兜底读取（alive 且 <max_age 秒）→ {name: delay}；空 = 无新鲜数据
  std::function<std::map<std::string, uint32_t>()> fresh_history;
  // backend-delay-results 批量注入（payload {group, results} 组装+发布）
  std::function<void(const std::string& group,
                     const std::vector<std::pair<std::string, uint32_t>>& results)>
      emit_results;
  std::function<int64_t()> now_ms;  // 缺省 GetTickCount64（.cpp 构造赋值）
};

class DelayManager {
 public:
  static DelayManager& instance();

  void set_hooks(DelayProbeHooks hooks);
  DelayProbeHooks& hooks() { return hooks_; }

  // 群组测速（← trigger_backend_auto_select_inner 测量段）：
  // 1. 发起 → 逐节点置 -2（testing）批量注入
  // 2. 测量组群测（单调用）；复用新鲜缓存时跳过（reuse_cache 语义）
  // 3. 兜底 history（alive+新鲜）；全空 + 单点语义 → 视为未执行
  // 4. 完成注入实际值（未测得 = 0 Timeout）
  // 返回 display 结果（全量节点，含 0）；injected=false = 测速未执行
  struct GroupOutcome {
    std::vector<std::pair<std::string, uint32_t>> display;
    bool injected = false;  // false = 测速未执行（严禁伪造成 Timeout）
  };
  GroupOutcome test_group(
      const std::string& group, const std::vector<std::string>& nodes,
      bool allow_cache_reuse,
      std::optional<std::vector<std::pair<std::string, uint32_t>>> cached =
          std::nullopt);

  // 单点测速（显式探活，禁用缓存复用 ← monitor.rs「单点测速语义必须真实探测」）
  // 测量组未返回数据 → injected=false（测速未执行，严禁伪造 Timeout）
  GroupOutcome test_single(const std::string& group, const std::string& node);

 private:
  DelayProbeHooks hooks_;
};

}  // namespace clashwin::core
