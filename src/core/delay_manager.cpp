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

  // 1. testing 中间态：逐节点置 -2（T13 apply_delay_results 透传 -2 →
  //    UI "testing"；完成帧覆盖实际值）
  if (hooks_.emit_results) {
    std::vector<std::pair<std::string, uint32_t>> testing;
    testing.reserve(nodes.size());
    for (const auto& n : nodes) testing.emplace_back(n, static_cast<uint32_t>(-2));
    hooks_.emit_results(group, testing);
  }

  // 2. 测量组群测（单调用；fresh 缓存复用跳过拨测 ← 子集测速降频）
  std::map<std::string, uint32_t> delays;
  bool reused = false;
  if (allow_cache_reuse && cached && !cached->empty()) {
    for (const auto& [n, d] : *cached) delays[n] = d;
    reused = true;
    CWLOG_INFO("DelayManager: 复用新鲜缓存（" + std::to_string(cached->size()) +
               " 项），跳过拨测");
  } else if (hooks_.group_probe) {
    if (auto r = hooks_.group_probe(group + "__METRICS")) delays = *r;
  }

  // 3. 单点语义：测量组未返回数据 → 判定「测速未执行」，严禁伪造 Timeout
  bool single = nodes.size() == 1;
  if (single && delays.empty() && !reused) {
    CWLOG_WARN("DelayManager: 单点测速未取得测量组数据 → 判定未执行");
    return out;  // injected=false
  }

  // 4. 兜底：group delay 全空 → history（alive+新鲜）
  if (!reused && delays.empty() && hooks_.fresh_history) {
    delays = hooks_.fresh_history();
  }

  // 5. 完成注入（未测得=0）
  out.display = build_display(nodes, delays);
  out.injected = true;
  if (hooks_.emit_results) hooks_.emit_results(group, out.display);
  return out;
}

DelayManager::GroupOutcome DelayManager::test_single(const std::string& group,
                                                     const std::string& node) {
  // 单点禁用缓存复用（强即时探活意图 ← monitor.rs 单点语义注释）
  return test_group(group, {node}, false, std::nullopt);
}

}  // namespace clashwin::core
