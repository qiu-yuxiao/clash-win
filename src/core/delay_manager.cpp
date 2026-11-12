// 延迟管理器实现（T16）：流程逐段对照 monitor.rs trigger_backend_auto_select_inner。
#include "delay_manager.h"

#include <windows.h>

#include "util/log.h"

namespace clashwin::core {

DelayManager& DelayManager::instance() {
  static DelayManager m;
  m.hooks_.now_ms = [] { return static_cast<int64_t>(GetTickCount64()); };
  return m;
}

void DelayManager::set_hooks(DelayProbeHooks hooks) {
  if (!hooks.now_ms) hooks.now_ms = [] { return static_cast<int64_t>(GetTickCount64()); };
  hooks_ = std::move(hooks);
}

namespace {
// 未测得 → 0（Timeout 语义；display 全量）
std::vector<std::pair<std::string, uint32_t>> build_display(
    const std::vector<std::string>& nodes,
    const std::map<std::string, uint32_t>& delays) {
  std::vector<std::pair<std::string, uint32_t>> display;
  display.reserve(nodes.size());
  for (const auto& n : nodes) {
    auto it = delays.find(n);
    display.emplace_back(n, it != delays.end() ? it->second : 0);
  }
  return display;
}
}  // namespace

DelayManager::GroupOutcome DelayManager::test_group(
    const std::string& group, const std::vector<std::string>& nodes,
    bool allow_cache_reuse,
    std::optional<std::vector<std::pair<std::string, uint32_t>>> cached) {
  GroupOutcome out;
  if (nodes.empty()) return out;

  // 1. 子集测速新鲜缓存复用（<30s 且多节点子集；单点测速强制实时探活 ← monitor.rs:922-966）
  if (allow_cache_reuse && nodes.size() > 1 && cached && !cached->empty()) {
    std::map<std::string, uint32_t> delays;
    for (const auto& [n, d] : *cached) delays[n] = d;
    CWLOG_INFO("DelayManager: 复用新鲜测量组缓存（" +
               std::to_string(cached->size()) + " 项），跳过拨测");
    out.display = build_display(nodes, delays);
    out.injected = true;
    out.source = DelayResultSource::FreshMetricsCache;
    if (hooks_.emit_results) {
      hooks_.emit_results(group, out.display, /*from_probe=*/false);
    }
    return out;
  }

  // 2. 测量组群测（单调用；BUG-344：彻底废除 120s 陈旧 history 兜底与未执行伪造 0ms）
  std::optional<std::map<std::string, uint32_t>> probed;
  if (hooks_.group_probe) {
    probed = hooks_.group_probe(group + "__METRICS");
  }
  if (!probed.has_value() || probed->empty()) {
    CWLOG_WARN("DelayManager: 测量组拨测未取得有效实时数据（待测节点数=" +
               std::to_string(nodes.size()) +
               "）→ 判定未执行，严禁伪造 Timeout");
    return out;  // display={}, injected=false, source=NotExecuted
  }

  // 3. 完成注入（未测得=0 Timeout，fromProbe=true）
  out.display = build_display(nodes, *probed);
  out.injected = true;
  out.source = DelayResultSource::LiveProbe;
  if (hooks_.emit_results) {
    hooks_.emit_results(group, out.display, /*from_probe=*/true);
  }
  return out;
}

DelayManager::GroupOutcome DelayManager::test_single(const std::string& group,
                                                     const std::string& node) {
  // 单点禁用缓存复用（强即时探活意图 ← monitor.rs 单点语义注释）
  return test_group(group, {node}, false, std::nullopt);
}

}  // namespace clashwin::core
