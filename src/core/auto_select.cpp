// AUTO_SELECT 实现（T16）：流程逐段对照 monitor.rs:745-1090。
#include "auto_select.h"

#include <windows.h>

#include <algorithm>
#include <limits>

#include "core/monitor.h"
#include "switching_lock.h"
#include "events/event_bus.h"
#include "events/notice.h"
#include "ui/i18n_text.h"
#include "util/log.h"
#include "util/node.h"

namespace clashwin::core {

AutoSelect& AutoSelect::instance() {
  static AutoSelect s;
  return s;
}

void AutoSelect::set_hooks(AutoSelectHooks hooks) { hooks_ = std::move(hooks); }

void AutoSelect::sort_display_delays(
    std::vector<std::pair<std::string, uint32_t>>& display, int sort_type) {
  if (sort_type == 2) {
    std::stable_sort(display.begin(), display.end(),
                     [](const auto& a, const auto& b) {
                       return a.first < b.first;
                     });
    return;
  }
  // BUG-322 / BUG-349: 0ms 代表测速超时（Timeout），按延迟升序排列时必须排在所有真实延迟之后
  std::stable_sort(display.begin(), display.end(),
                   [](const auto& a, const auto& b) {
                     const uint32_t ka =
                         (a.second == 0) ? (std::numeric_limits<uint32_t>::max)()
                                         : a.second;
                     const uint32_t kb =
                         (b.second == 0) ? (std::numeric_limits<uint32_t>::max)()
                                         : b.second;
                     return ka < kb;
                   });
}

// 纯择优：candidates=[30,5000) → 失败服务器避让（安全阀：全候选在失败
// 服务器上则放弃避让）→ 最小延迟（← monitor.rs:1000-1064）
std::optional<std::string> AutoSelect::pick_fastest(
    const std::vector<std::pair<std::string, uint32_t>>& display,
    const std::function<std::vector<std::string>()>& failed_servers,
    const std::function<std::map<std::string, std::string>()>&
        node_server_map,
    const std::function<bool(const std::string&)>& is_verified_true) {
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

  // 优先从 verified_true 候选中择优；若全员未验真则保底回退全候选（← monitor.rs:207）
  if (is_verified_true) {
    std::vector<std::pair<std::string, uint32_t>> verified;
    for (const auto& c : candidates) {
      if (is_verified_true(c.first)) verified.push_back(c);
    }
    if (!verified.empty()) {
      candidates = std::move(verified);
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
    // 子集校验：selected 需匹配 filter_text（支持多关键词/OR/排除语法）
    if (util::match_node_filter(*selected, filter_text_lower)) {
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
  const uint64_t start_manual_epoch =
      CoreMonitor::instance().manual_switch_epoch();
  // T18 转正：内核就绪等待（← monitor.rs trigger_backend_auto_select 的
  //   wait_for_clash_ready 前置；skip_wait_ready 时跳过——静默测速场景已就绪）。
  //   等待失败（超时）不阻断，与原版「尽力而为」一致。
  if (!skip_wait_ready && hooks_.wait_ready) {
    if (!hooks_.wait_ready()) {
      CWLOG_WARN("AutoSelect: 内核就绪等待超时，继续触发（尽力而为）");
      // ← profile-coordination.ts:35-43：10s 内 PROXY 组仍无真实节点 →
      //   showNotice.info(clashNotReady)，随后照旧继续自动选点
      events::NoticeService::instance().push(
          "info", ui::tr8("shared.feedback.notifications.clashNotReady"));
    }
  }

  // 1. 节点子集确定（← :1923-1956）：显式子集剔除 dummy/假节点；全量走组信息+filter
  const bool is_frontend_subset = node_names.has_value() && !node_names->empty();
  auto is_excluded = [&](const std::string& n) {
    return n.empty() || (hooks_.is_dummy && hooks_.is_dummy(n)) ||
           (hooks_.is_confirmed_fake && hooks_.is_confirmed_fake(n));
  };
  std::vector<std::string> nodes;
  if (is_frontend_subset) {
    for (const auto& n : *node_names) {
      if (!is_excluded(n)) nodes.push_back(n);
    }
  } else if (hooks_.group_nodes) {
    auto all = hooks_.group_nodes("PROXY");
    const std::string filter = hooks_.filter_text ? hooks_.filter_text() : "";
    for (const auto& n : all) {
      if (is_excluded(n)) continue;
      if (util::match_node_filter(n, filter)) {
        nodes.push_back(n);
      }
    }
  }
  if (nodes.empty()) {
    out.display = {};
    out.source = DelayResultSource::NotExecuted;
    return out;
  }

  // 2. 群测（BUG-344：< 10s 测量组实时快照 / < 30s 子集保鲜；严禁未执行伪造 0ms）
  bool allow_cache_reuse = is_frontend_subset && nodes.size() > 1;
  std::optional<std::vector<std::pair<std::string, uint32_t>>> cached_subset;
  if (!is_frontend_subset && nodes.size() > 1) {
    if (auto recent = CoreMonitor::try_reuse_metrics_probe(
            static_cast<int64_t>(GetTickCount64()),
            CoreMonitor::kSelfHealSnapshotReuseMs, nodes, true)) {
      std::vector<std::pair<std::string, uint32_t>> snap;
      snap.reserve(nodes.size());
      for (const auto& n : nodes) {
        auto it = recent->find(n);
        if (it != recent->end()) snap.emplace_back(n, it->second);
      }
      if (!snap.empty()) {
        allow_cache_reuse = true;
        cached_subset = std::move(snap);
      }
    }
  }
  if (!cached_subset && allow_cache_reuse && hooks_.fresh_metrics_subset) {
    cached_subset = hooks_.fresh_metrics_subset(nodes);
  }
  auto& dm = DelayManager::instance();
  auto outcome = dm.test_group("PROXY", nodes, allow_cache_reuse, cached_subset,
                               is_frontend_subset);
  out.source = outcome.source;
  if (!outcome.injected) {
    // 拨测未执行 → 空 display（严禁伪造 Timeout 0ms）
    if (select) events::EventBus::refresh_proxies();
    return out;
  }
  out.display = outcome.display;

  // 3. sort_type 仅影响展示序（0=读取持久配置或默认按延迟；1=延迟升序且 0ms 垫底；2=名称）
  int effective_sort = sort_type;
  if (effective_sort == 0) {
    effective_sort =
        (hooks_.saved_sort_type ? hooks_.saved_sort_type(profile_uid)
                                : std::nullopt)
            .value_or(1);
    if (effective_sort <= 0) effective_sort = 1;
  }
  sort_display_delays(out.display, effective_sort);

  // 4. select：验真兜底 → 过滤重检 → 择优 → 双重校验 current_uid → 手动抢占检查 → 连续性幂等判断 → 切换 → 回写
  if (select) {
    // 冷启动/全员未验真兜底（← monitor.rs:2199-2206）
    if (hooks_.run_https_verify) {
      auto is_ver = [&](const std::string& n) {
        return hooks_.is_verified_true ? hooks_.is_verified_true(n)
                                       : util::is_verified_true_node(n);
      };
      bool has_reachable = false;
      bool any_verified = false;
      for (const auto& entry : out.display) {
        if (entry.second >= kNodeDelayMinMs && entry.second < kNodeDelayMaxMs) {
          has_reachable = true;
          if (is_ver(entry.first)) {
            any_verified = true;
            break;
          }
        }
      }
      if (has_reachable && !any_verified) {
        hooks_.run_https_verify(nodes);
      }
    }

    const std::string latest_filter =
        hooks_.filter_text ? hooks_.filter_text() : "";
    std::vector<std::pair<std::string, uint32_t>> matched_display;
    matched_display.reserve(out.display.size());
    for (const auto& entry : out.display) {
      if (hooks_.is_confirmed_fake && hooks_.is_confirmed_fake(entry.first)) {
        continue;
      }
      if (util::match_node_filter(entry.first, latest_filter)) {
        matched_display.push_back(entry);
      }
    }
    auto fastest = pick_fastest(
        matched_display, hooks_.failed_servers, hooks_.node_server_map,
        hooks_.is_verified_true);
    if (!fastest) {
      events::EventBus::refresh_proxies();
      return out;
    }
    auto cur = hooks_.current_uid ? hooks_.current_uid() : std::nullopt;
    if (cur.has_value() && !profile_uid.empty() && *cur != profile_uid) {
      // 活动配置已被更改 → 舍弃切换（← monitor.rs:2185）
      CWLOG_INFO("AutoSelect: 活动配置已更改，舍弃切换");
      return out;
    }
    // 【BUG-388 · 手动切点抢占护栏】测速期间用户手动切换过节点 → 禁止覆盖
    const uint64_t cur_manual_epoch =
        CoreMonitor::instance().manual_switch_epoch();
    if (CoreMonitor::is_manual_switch_preempted(start_manual_epoch,
                                                 cur_manual_epoch)) {
      CWLOG_INFO("AutoSelect: 选点期间检测到用户手动切点，放弃覆盖当前选择");
      out.preempted = true;
      events::EventBus::refresh_proxies();
      return out;
    }
    const bool already_current =
        hooks_.current_proxy_now &&
        hooks_.current_proxy_now("PROXY") == *fastest;
    if (already_current) {
      CWLOG_INFO("AutoSelect: 最优节点已是当前出口，保持现状（不发起切换写入）: " +
                 *fastest);
    }
    if (already_current ||
        (hooks_.select_node && hooks_.select_node("PROXY", *fastest))) {
      out.selected = true;
      out.selected_node = *fastest;
      CoreMonitor::instance().emit_network_health_notice("network_health::ok");
      // 【关键修复】回写 profile.selected（filterText 子集校验 ← :2250）
      if (util::match_node_filter(*fastest, latest_filter)) {
        if (hooks_.write_selected) hooks_.write_selected(profile_uid, *fastest);
      } else {
        CWLOG_WARN("AutoSelect: 最快节点越界 filterText 子集，不写回 selected");
      }
      events::EventBus::refresh_proxies();
    }
  } else {
    // 仅测速模式（如前端闪电测速 / 唤醒静默测速）：存在任一有效延迟即解除断网/全死横幅（BUG-359）
    size_t reachable = 0;
    bool any_ok = false;
    const auto active_node = hooks_.current_proxy_now
                                 ? hooks_.current_proxy_now("PROXY")
                                 : std::nullopt;
    std::optional<uint32_t> active_delay;
    for (const auto& [name, d] : out.display) {
      if (CoreMonitor::is_valid_probe_delay(d)) {
        ++reachable;
      }
      if (!any_ok && d > 0 && d < kNodeDelayMaxMs) {
        any_ok = true;
        CoreMonitor::instance().emit_network_health_notice(
            "network_health::ok");
      }
      if (active_node.has_value() && name == *active_node) {
        active_delay = d;
      }
    }
    const char* src_name =
        (out.source == DelayResultSource::LiveProbe)
            ? "LiveProbe"
            : ((out.source == DelayResultSource::FreshMetricsCache)
                   ? "FreshMetricsCache"
                   : "NotExecuted");
    CWLOG_INFO(std::string("[后台监测] 纯测速刷新完成（不切换节点）：待测 ") +
               std::to_string(nodes.size()) + " 个，可达 " +
               std::to_string(reachable) + " 个，source=" + src_name +
               "，当前节点=" + active_node.value_or("None") + "（延迟=" +
               (active_delay.has_value()
                    ? (std::to_string(*active_delay) + "ms")
                    : std::string("None")) +
               "）");
  }
  return out;
}

// 唤醒静默测速（T16⑤ / BUG-402）：5min 冷却 + 复用 running_ 互斥；传入当前过滤子集以复用 <30s 快照
bool AutoSelect::silent_test_on_wakeup(
    const std::string& profile_uid,
    std::optional<std::vector<std::string>> node_names) {
  int64_t now = static_cast<int64_t>(GetTickCount64());
  constexpr int64_t kCooldownMs = 5 * 60 * 1000;
  int64_t last = last_wakeup_ms_.load();
  if (last != 0 && now - last < kCooldownMs) {
    CWLOG_INFO("AutoSelect: 唤醒测速冷却中（剩余 " +
               std::to_string((kCooldownMs - (now - last)) / 1000) + "s）");
    return false;
  }
  // 仅测速不切换（select=false）；互斥由 trigger 内 Guard 保证
  // BUG-402：传入过滤子集 `node_names` 允许复用后端 <30s 测量组快照，sort_type=0 按持久排序状态
  const int sort_type = (node_names.has_value() && !node_names->empty()) ? 0 : 1;
  auto r = trigger(profile_uid, std::move(node_names), sort_type,
                   /*select=*/false, /*skip_wait_ready=*/true);
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
