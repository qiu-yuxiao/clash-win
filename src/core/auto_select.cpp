// AUTO_SELECT 实现（T16）：流程逐段对照 monitor.rs:745-1090。
#include "auto_select.h"

#include <windows.h>

#include <algorithm>

#include "switching_lock.h"
#include "util/log.h"

namespace clashwin::core {

AutoSelect& AutoSelect::instance() {
  static AutoSelect s;
  return s;
}

void AutoSelect::set_hooks(AutoSelectHooks hooks) { hooks_ = std::move(hooks); }

namespace {
std::string to_lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
  return s;
}
}  // namespace

// 纯择优：candidates=[30,2000) → 失败服务器避让（安全阀：全候选在失败
// 服务器上则放弃避让）→ 最小延迟（← monitor.rs:1000-1064）
std::optional<std::string> AutoSelect::pick_fastest(
    const std::vector<std::pair<std::string, uint32_t>>& display,
    const std::function<std::vector<std::string>()>& failed_servers,
    const std::function<std::map<std::string, std::string>()>&
        node_server_map) {
  std::vector<std::pair<std::string, uint32_t>> candidates;
  for (const auto& [name, d] : display) {
    if (d >= kNodeDelayMinMs && d < kNodeDelayMaxMs)
      candidates.emplace_back(name, d);
  }
  if (candidates.empty()) return std::nullopt;

  // 失败服务器避让（安全阀）
  if (failed_servers && node_server_map) {
    auto failed = failed_servers();
    if (!failed.empty()) {
      auto server_map = node_server_map();
      if (!server_map.empty()) {
        std::vector<std::pair<std::string, uint32_t>> kept;
        for (const auto& c : candidates) {
          auto it = server_map.find(c.first);
          bool on_failed =
              it != server_map.end() &&
              std::find(failed.begin(), failed.end(), it->second) !=
                  failed.end();
          if (!on_failed) kept.push_back(c);
        }
        if (!kept.empty() && kept.size() < candidates.size()) {
          CWLOG_INFO("AutoSelect: 失败服务器避让，剔除 " +
                     std::to_string(candidates.size() - kept.size()) + " 个候选");
          candidates = std::move(kept);
        } else if (kept.empty()) {
          CWLOG_WARN("AutoSelect: 全部候选都在失败服务器上，放弃避让");
        }
      }
    }
  }

  auto best = std::min_element(candidates.begin(), candidates.end(),
                               [](const auto& a, const auto& b) {
                                 return a.second < b.second;
                               });
  return best != candidates.end() ? std::optional{best->first} : std::nullopt;
}

// 激活校准防线（← use-profiles activateSelected：子集校验+失败回写）
std::string AutoSelect::calibrate_selected(
    const std::string& uid, const std::string& filter_text_lower,
    const std::string& kernel_now,
    const std::function<std::optional<std::string>(const std::string&)>&
        read_selected,
    const std::function<bool(const std::string&, const std::string&)>&
        write_selected) {
  (void)uid;
  auto selected = read_selected ? read_selected(uid) : std::nullopt;
  if (selected) {
    // 子集校验：selected 需匹配 filter_text（子串语义，lowercase）
    if (filter_text_lower.empty() ||
        to_lower(*selected).find(filter_text_lower) != std::string::npos) {
      return *selected;  // 子集内 → 正常
    }
    // 越界（被污染的持久选择）→ 不写回内核，回写实际节点
    CWLOG_WARN("AutoSelect: selected 越界过滤词子集，回写内核实际节点: " +
               kernel_now);
  }
  // 失败回写：持久 selected 缺失/越界 → 内核实际节点为权威
  if (write_selected) write_selected(uid, kernel_now);
  return kernel_now;
}

AutoSelect::Outcome AutoSelect::trigger(
    const std::string& profile_uid,
    std::optional<std::vector<std::string>> node_names, int sort_type,
    bool select, bool skip_wait_ready) {
  Outcome out;
  // 【锁差异声明】RAII Guard（见 auto_select.h 头注：原版 BUG-121 卡死教训，
  // 本实现任何路径必释放）；skip_wait_ready 时也先置位（与原版一致：
  // 互斥先于就绪等待）
  struct Guard {
    std::atomic<bool>& f;
    bool held = false;
    explicit Guard(std::atomic<bool>& flag) : f(flag) {
      held = !f.exchange(true);
    }
    ~Guard() {
      if (held) f.store(false);  // Drop Guard：任何路径必释放
    }
  } guard(running_);
  if (!guard.held) {
    CWLOG_INFO("AutoSelect: 已在运行中，返回繁忙（AUTO_SELECT_BUSY）");
    out.busy = true;
    return out;
  }
  // T18 转正：内核就绪等待（← monitor.rs trigger_backend_auto_select 的
  //   wait_for_clash_ready 前置；skip_wait_ready 时跳过——静默测速场景已就绪）。
  //   等待失败（超时）不阻断，与原版「尽力而为」一致。
  if (!skip_wait_ready && hooks_.wait_ready) {
    if (!hooks_.wait_ready()) {
      CWLOG_WARN("AutoSelect: 内核就绪等待超时，继续触发（尽力而为）");
    }
  }

  // 1. 节点子集确定（← :826-844）：显式子集剔除 dummy；全量走组信息+filter
  //    （组信息读取由调用方经 node_names 传入——本层不做 IO，保持可测）
  std::vector<std::string> nodes;
  if (node_names && !node_names->empty()) {
    for (const auto& n : *node_names)
      if (!hooks_.is_dummy || !hooks_.is_dummy(n)) nodes.push_back(n);
  }
  if (nodes.empty()) {
    out.display = {};
    return out;
  }

  // 2. 群测（DelayManager：testing 中间态 → 测量组 → 注入）
  auto& dm = DelayManager::instance();
  auto outcome = dm.test_group("PROXY", nodes, /*allow_cache_reuse=*/true);
  if (!outcome.injected && nodes.size() == 1) {
    // 单点未执行 → 空 display（严禁伪造 Timeout）
    return out;
  }
  out.display = outcome.display;

  // 3. sort_type 仅影响展示序（0=head state 由调用方换算；1=延迟 2=名称）
  if (sort_type == 2) {
    std::sort(out.display.begin(), out.display.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
  } else {
    std::stable_sort(out.display.begin(), out.display.end(),
                     [](const auto& a, const auto& b) {
                       return a.second < b.second;
                     });
  }

  // 4. select：双重校验 current_uid → 择优 → 切换 → 回写（子集校验）
  if (select) {
    auto fastest = pick_fastest(
        out.display, hooks_.failed_servers, hooks_.node_server_map);
    if (!fastest) return out;
    auto cur = hooks_.current_uid ? hooks_.current_uid() : std::nullopt;
    if (cur != std::optional{profile_uid}) {
      // 活动配置已被更改 → 舍弃切换（← monitor.rs:1046-1056）
      CWLOG_INFO("AutoSelect: 活动配置已更改，舍弃切换");
      return out;
    }
    if (hooks_.select_node && hooks_.select_node("PROXY", *fastest)) {
      out.selected = true;
      // 【关键修复】回写 profile.selected（filterText 子集校验 ← :1080-1096）
      std::string filter = hooks_.filter_text ? to_lower(hooks_.filter_text())
                                              : "";
      if (filter.empty() || to_lower(*fastest).find(filter) != std::string::npos) {
        if (hooks_.write_selected) hooks_.write_selected(profile_uid, *fastest);
      } else {
        CWLOG_WARN("AutoSelect: 最快节点越界 filterText 子集，不写回 selected");
      }
    }
  }
  return out;
}

// 唤醒静默测速（T16⑤）：5min 冷却 + 复用 running_ 互斥；T19 挂触发
bool AutoSelect::silent_test_on_wakeup(const std::string& profile_uid) {
  int64_t now = static_cast<int64_t>(GetTickCount64());
  constexpr int64_t kCooldownMs = 5 * 60 * 1000;
  int64_t last = last_wakeup_ms_.load();
  if (last != 0 && now - last < kCooldownMs) {
    CWLOG_INFO("AutoSelect: 唤醒测速冷却中（剩余 " +
               std::to_string((kCooldownMs - (now - last)) / 1000) + "s）");
    return false;
  }
  // 仅测速不切换（select=false）；互斥由 trigger 内 Guard 保证
  auto r = trigger(profile_uid, std::nullopt, 1, /*select=*/false,
                   /*skip_wait_ready=*/true);
  if (r.busy) return false;  // 忙 → 不更新冷却（下次唤醒再试）
  last_wakeup_ms_.store(now);
  return true;
}

int64_t AutoSelect::wakeup_cooldown_remaining_ms() const {
  constexpr int64_t kCooldownMs = 5 * 60 * 1000;
  int64_t now = static_cast<int64_t>(GetTickCount64());
  int64_t last = last_wakeup_ms_.load();
  if (last == 0) return 0;
  int64_t remain = kCooldownMs - (now - last);
  return remain > 0 ? remain : 0;
}

}  // namespace clashwin::core
