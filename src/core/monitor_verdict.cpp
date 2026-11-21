// CoreMonitor 节点存活分级裁决、黑洞滑窗、自愈限流与管道缺失快重启状态机
// （拆分自 src/core/monitor.cpp；← ClashVerge src-tauri/src/module/monitor/verdict.rs）
#include "core/monitor.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <thread>

#include "clashwin/core/constants.h"
#include "core/delay_manager.h"
#include "util/log.h"
#include "util/node.h"

namespace clashwin::core {

CoreMonitor::SelfHealAction CoreMonitor::evaluate_self_heal_counters(
    int& auto_select_fail_count, int& api_dead_heal_fail_count,
    bool api_alive, int restart_threshold, int alert_threshold) {
  if (auto_select_fail_count >= alert_threshold) {
    auto_select_fail_count = 0;
    api_dead_heal_fail_count = 0;
    return SelfHealAction::FireAlert;
  }
  if (api_dead_heal_fail_count >= restart_threshold) {
    api_dead_heal_fail_count = 0;
    if (!api_alive) {
      return SelfHealAction::RestartCore;
    }
  }
  return SelfHealAction::None;
}

CoreMonitor::SelfHealAction CoreMonitor::complete_restart_core_attempt(
    bool restart_ok, int& auto_select_fail_count,
    int& api_dead_heal_fail_count) {
  if (restart_ok) {
    return SelfHealAction::None;
  }
  auto_select_fail_count = 0;
  api_dead_heal_fail_count = 0;
  return SelfHealAction::FireAlert;
}

void CoreMonitor::fire_self_heal_alert(const Hooks& h) const {
  CWLOG_WARN("CoreMonitor: 连续 5 次自愈选点失败，触发系统网络警报");
  if (h.alert) {
    h.alert(
        L"Clash Win - 网络警报",
        L"后台自动优选节点连续 5 次失败，当前所有代理节点均已失效，无法正常连接网络。\n\n请检查您的网络连接或节点订阅状态。");
  }
}

void CoreMonitor::fire_core_restart_failed_alert(const Hooks& h) const {
  CWLOG_ERROR("CoreMonitor: 重启内核失败，立即升级为终态告警并重置本轮计数");
  if (h.alert) {
    h.alert(
        L"Clash Win - 网络警报",
        L"后台检测到代理内核通信异常且自动重启内核失败，当前无法正常恢复代理连接。\n\n请检查系统服务权限或手动重启 Clash Win。");
  }
}

bool CoreMonitor::is_mihomo_pipe_missing_confirmed(const Hooks& h) const {
  if (!h.expect_core_pipe_alive || !h.pipe_missing) return false;
  if (!h.expect_core_pipe_alive() || !h.pipe_missing()) return false;
  if (h.sleep_ms) {
    h.sleep_ms(kPipeMissingConfirmDelayMs);
  } else {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kPipeMissingConfirmDelayMs));
  }
  return h.expect_core_pipe_alive() && h.pipe_missing();
}

CoreMonitor::Action CoreMonitor::execute_core_restart_with_accounting(
    const Hooks& h, const std::string& reason_log, Action ok_action) {
  CWLOG_WARN(reason_log);
  const bool restart_ok = h.restart_core && h.restart_core().is_ok();
  int auto_fails = auto_select_fail_count_.load();
  int api_dead_fails = api_dead_heal_fail_count_.load();
  const SelfHealAction post =
      complete_restart_core_attempt(restart_ok, auto_fails, api_dead_fails);
  auto_select_fail_count_.store(auto_fails);
  api_dead_heal_fail_count_.store(api_dead_fails);
  if (restart_ok) {
    CWLOG_INFO(std::format(
        "CoreMonitor: 重启内核成功，API 通道应已恢复（保留告警计数={}）",
        auto_fails));
    return ok_action;
  }
  if (post == SelfHealAction::FireAlert) {
    fire_core_restart_failed_alert(h);
    return Action::Alerted;
  }
  return Action::Idle;
}

std::optional<CoreMonitor::Action> CoreMonitor::try_fast_restart_missing_pipe(
    const Hooks& h, const Params& p) {
  if (!h.pipe_missing || !h.expect_core_pipe_alive) {
    return std::nullopt;
  }
  const int64_t now =
      h.now_ms ? h.now_ms() : static_cast<int64_t>(GetTickCount64());
  const bool pipe_missing = h.pipe_missing();
  const bool expect_alive = h.expect_core_pipe_alive();
  const int64_t last = last_heal_ms_.load();
  const bool has_last = has_heal_attempt_.load() || (last != 0);
  std::optional<uint64_t> elapsed_secs;
  if (has_last) {
    elapsed_secs =
        (now > last) ? static_cast<uint64_t>((now - last) / 1000) : 0ULL;
  }
  if (!can_fast_restart_missing_pipe(pipe_missing, expect_alive,
                                     elapsed_secs)) {
    return std::nullopt;
  }
  if (!is_mihomo_pipe_missing_confirmed(h)) {
    return std::nullopt;
  }

  api_fail_count_.store(0);
  const int64_t after_confirm_now =
      h.now_ms ? h.now_ms() : static_cast<int64_t>(GetTickCount64());
  last_heal_ms_.store(after_confirm_now);
  has_heal_attempt_.store(true);
  last_tick_ms_.store(after_confirm_now);
  const int auto_fails = auto_select_fail_count_.fetch_add(1) + 1;
  if (auto_fails >= p.alert_fail_threshold) {
    auto_select_fail_count_.store(0);
    api_dead_heal_fail_count_.store(0);
    fire_self_heal_alert(h);
    return Action::Alerted;
  }
  return execute_core_restart_with_accounting(
      h,
      "CoreMonitor: 检测到内核命名管道已不存在（进程退出），跳过轮询空等立即重启内核",
      Action::Restarted);
}

CoreMonitor::Action CoreMonitor::heal(const std::string& uid,
                                      SelfHealOrigin origin,
                                      const std::string& dead_node) {
  Hooks h;
  Params p;
  {
    std::lock_guard<std::mutex> lock(m_);
    h = hooks_;
    p = params_;
  }
  const int64_t now = h.now_ms ? h.now_ms()
                               : static_cast<int64_t>(GetTickCount64());
  const int64_t last = last_heal_ms_.load();
  const bool has_last = has_heal_attempt_.load() || (last != 0);
  if (has_last && now - last < p.heal_cooldown_ms) {
    CWLOG_INFO(std::format(
        "CoreMonitor: 自愈选点在 {} 秒冷却中（已过 {} 秒），跳过本次",
        p.heal_cooldown_ms / 1000, (now - last) / 1000));
    return Action::Idle;
  }

  // S4 原节点确已死门检（ConfirmedDead 适用；NodeMigration / ApiChannelDown 跳过）
  if (self_heal_requires_dead_gate(origin) && !dead_node.empty() &&
      h.confirm_dead_gate && !h.confirm_dead_gate(dead_node)) {
    CWLOG_INFO("CoreMonitor: 自愈门检确认原节点未确死（" + dead_node +
               "），放弃本次切点");
    return Action::Idle;
  }

  // S4 / BUG-388 自愈切换速率限制（ConfirmedDead 豁免；NodeMigration / ApiChannelDown 受限）
  if (self_heal_subject_to_switch_rate_limit(origin)) {
    std::optional<int64_t> last_switch_age_secs;
    size_t recent_count = 0;
    {
      std::lock_guard<std::mutex> lock(switch_rate_m_);
      const int64_t win_ms = kAutoSwitchWindowSecs * 1000;
      recent_auto_switches_ms_.erase(
          std::remove_if(recent_auto_switches_ms_.begin(),
                         recent_auto_switches_ms_.end(),
                         [&](int64_t ts) { return (now - ts) >= win_ms; }),
          recent_auto_switches_ms_.end());
      if (!recent_auto_switches_ms_.empty()) {
        const int64_t delta_ms =
            (now >= recent_auto_switches_ms_.back())
                ? (now - recent_auto_switches_ms_.back())
                : 0;
        last_switch_age_secs = delta_ms / 1000;
      }
      recent_count = recent_auto_switches_ms_.size();
    }
    if (!auto_switch_permitted(last_switch_age_secs, recent_count)) {
      CWLOG_WARN("CoreMonitor: 自愈切换速率保护（间隔 < 120s 或 600s 内满 3 次），跳过本次切点");
      return Action::Idle;
    }
  }

  // BUG-388 API 通道恢复保活门检 / BUG-411 通道仍死直接免等
  bool api_confirmed_down = false;
  if (origin == SelfHealOrigin::ApiChannelDown) {
    if (h.api_channel_precheck) {
      const ApiChannelPrecheck pre = h.api_channel_precheck();
      if (pre == ApiChannelPrecheck::KeepAlive) {
        auto_select_fail_count_.store(0);
        api_dead_heal_fail_count_.store(0);
        emit_network_health_notice("network_health::ok");
        last_tick_ms_.store(now);
        CWLOG_INFO("CoreMonitor: API 通道自愈前复核已恢复且当前节点健康，保活现状并跳过切点");
        return Action::Idle;
      }
      if (pre == ApiChannelPrecheck::ConfirmedDown) {
        api_confirmed_down = true;
      }
    } else if (h.api_recovery_keep_alive && h.api_recovery_keep_alive()) {
      auto_select_fail_count_.store(0);
      api_dead_heal_fail_count_.store(0);
      emit_network_health_notice("network_health::ok");
      last_tick_ms_.store(now);
      CWLOG_INFO("CoreMonitor: API 通道恢复且当前活跃真节点健康，保活现状并跳过切点");
      return Action::Idle;
    }
  }

  const bool prev_has_attempt = has_heal_attempt_.exchange(true);
  last_heal_ms_.store(now);

  bool failed_this_round = false;
  if (api_confirmed_down) {
    CWLOG_WARN("CoreMonitor: API 通道仍不可达，跳过 30 秒就绪空等直接计入自愈失败次数");
    failed_this_round = true;
  } else {
    if ((!h.auto_select_for_heal && !h.auto_select_trigger) || uid.empty()) {
      return Action::Idle;
    }

    auto on_select_success = [&](std::string new_node) -> Action {
      // 自愈成功：清零双计数并广播恢复健康（BUG-318 / BUG-359）
      api_fail_count_.store(0);
      node_fail_count_.store(0);
      auto_select_fail_count_.store(0);
      api_dead_heal_fail_count_.store(0);
      emit_network_health_notice("network_health::ok");
      if (new_node.empty() && h.active_node_name) {
        new_node = h.active_node_name();
      }
      const bool switched =
          dead_node.empty() || new_node.empty() || dead_node != new_node;
      if (switched) {
        std::lock_guard<std::mutex> lock(switch_rate_m_);
        recent_auto_switches_ms_.push_back(now);
      }
      if (origin == SelfHealOrigin::NodeMigration) {
        if (h.on_node_migration_healed) {
          h.on_node_migration_healed();
        }
      } else if (!dead_node.empty() && !new_node.empty() &&
                 dead_node != new_node) {
        if (h.on_dead_node_healed) {
          h.on_dead_node_healed(dead_node);
        }
        CWLOG_INFO(std::format(
            "CoreMonitor: 自愈已切换 {} -> {}（已同步清理原故障节点存量代理连接）",
            dead_node, new_node));
      }
      return Action::Healed;
    };

    bool benign_abort = false;
    if (h.auto_select_for_heal) {
      const bool skip_wait_ready = (origin == SelfHealOrigin::ApiChannelDown);
      auto r = h.auto_select_for_heal(skip_wait_ready);
      if (r.is_ok() && r.value().selected) {
        return on_select_success(std::move(r.value().selected_node));
      }
      if (r.is_err() && is_benign_auto_select_abort(r.error().message)) {
        benign_abort = true;
      } else {
        if (r.is_ok()) {
          CWLOG_WARN("CoreMonitor: 自愈选点未选出可用节点（所有节点不可达）");
        } else {
          CWLOG_WARN("CoreMonitor: 自愈选点失败 (" + r.error().message + ")");
        }
        failed_this_round = true;
      }
    } else {
      auto r = h.auto_select_trigger();
      if (r.is_ok() && r.value()) {
        return on_select_success(std::string());
      }
      if ((r.is_ok() && !r.value()) ||
          (r.is_err() && is_benign_auto_select_abort(r.error().message))) {
        benign_abort = true;
      } else {
        failed_this_round = true;
      }
    }

    if (benign_abort) {
      // BUSY / MANUAL_PREEMPTED → 不计入失败、回退冷却时间戳允许下次重试
      last_heal_ms_.store(last);
      has_heal_attempt_.store(prev_has_attempt);
      return Action::Idle;
    }
  }

  if (!failed_this_round) {
    return Action::Idle;
  }

  // 自愈失败：递增双计数器 + 发射全节点不可达通知（BUG-318 / BUG-359）
  int auto_fails = auto_select_fail_count_.fetch_add(1) + 1;
  int api_dead_fails = api_dead_heal_fail_count_.fetch_add(1) + 1;
  emit_network_health_notice("network_health::all_dead");

  const bool need_api_check =
      (auto_fails < p.alert_fail_threshold &&
       api_dead_fails >= p.heal_fail_threshold);
  const bool api_ok =
      need_api_check ? (h.api_alive ? h.api_alive() : true) : true;
  if (need_api_check && api_ok) {
    CWLOG_WARN(std::format(
        "CoreMonitor: 连续 {} 次自愈选点失败但内核 API 可达（保留告警计数={}），跳过内核重启",
        api_dead_fails, auto_fails));
  }

  const SelfHealAction decision = evaluate_self_heal_counters(
      auto_fails, api_dead_fails, api_ok, p.heal_fail_threshold,
      p.alert_fail_threshold);
  auto_select_fail_count_.store(auto_fails);
  api_dead_heal_fail_count_.store(api_dead_fails);

  if (decision == SelfHealAction::FireAlert) {
    fire_self_heal_alert(h);
    return Action::Alerted;
  }

  if (decision == SelfHealAction::RestartCore) {
    return execute_core_restart_with_accounting(
        h,
        std::format(
            "CoreMonitor: 连续 {} 次自愈选点失败且内核 API 不可达，尝试重启内核（当前告警计数={}）",
            p.heal_fail_threshold, auto_fails),
        Action::RestartedOnHealFail);
  }

  return Action::Idle;
}

bool CoreMonitor::is_manual_switch_preempted(uint64_t start_epoch,
                                             uint64_t current_epoch) {
  return current_epoch != start_epoch;
}

bool CoreMonitor::is_benign_auto_select_abort(const std::string& err_msg) {
  return err_msg == "AUTO_SELECT_BUSY" ||
         err_msg == "AUTO_SELECT_MANUAL_PREEMPTED";
}

CoreMonitor::NodeStatus CoreMonitor::classify_node_status(
    bool alive, std::optional<uint32_t> last_delay,
    std::optional<uint64_t> age_secs) {
  if (!last_delay.has_value()) {
    return {NodeStatusKind::Unknown, 0};
  }
  const uint32_t delay = *last_delay;
  const bool valid_delay =
      (delay >= kNodeDelayMinMs && delay < kNodeDelayMaxMs);
  if (alive && valid_delay) {
    return {NodeStatusKind::Alive, delay};
  }
  if (!alive && valid_delay && age_secs.has_value() &&
      *age_secs < kSuspectWindowSecs) {
    return {NodeStatusKind::Suspect, 0};
  }
  return {NodeStatusKind::Dead, 0};
}

bool CoreMonitor::should_proceed_with_self_heal(const NodeStatus& status) {
  return status.kind == NodeStatusKind::Dead;
}

bool CoreMonitor::should_allow_https_migration(ActiveTrafficState traffic) {
  return traffic != ActiveTrafficState::Flowing;
}

void CoreMonitor::prune_brownout_episodes(std::vector<int64_t>& episodes_ms,
                                          int64_t now_ms) {
  episodes_ms.erase(
      std::remove_if(episodes_ms.begin(), episodes_ms.end(),
                     [now_ms](int64_t ts) {
                       const int64_t elapsed_secs =
                           (now_ms > ts) ? ((now_ms - ts) / 1000) : 0;
                       return elapsed_secs >= kBrownoutWindowSecs;
                     }),
      episodes_ms.end());
}

size_t CoreMonitor::record_brownout_episode(std::vector<int64_t>& episodes_ms,
                                            int64_t now_ms) {
  prune_brownout_episodes(episodes_ms, now_ms);
  episodes_ms.push_back(now_ms);
  return episodes_ms.size();
}

size_t CoreMonitor::brownout_episode_count_for_test() const {
  std::lock_guard<std::mutex> lock(brownout_m_);
  return recent_brownout_episodes_ms_.size();
}

CoreMonitor::SelfHealDecision CoreMonitor::decide_self_heal(
    const SelfHealTrigger& trigger, int node_dead_threshold,
    int api_fail_threshold, size_t brownout_confirm_fails) {
  switch (trigger.kind) {
    case SelfHealTrigger::Kind::NodeConfirmedDead:
      return (trigger.count >= static_cast<uint32_t>(node_dead_threshold) ||
              trigger.brownout_episodes >= brownout_confirm_fails)
                 ? SelfHealDecision::Allow
                 : SelfHealDecision::Wait;
    case SelfHealTrigger::Kind::NetworkRecovered:
      return SelfHealDecision::Wait;
    case SelfHealTrigger::Kind::ApiUnreachable:
      return (trigger.count >= static_cast<uint32_t>(api_fail_threshold) &&
              !trigger.api_alive_on_recheck)
                 ? SelfHealDecision::Allow
                 : SelfHealDecision::Wait;
  }
  return SelfHealDecision::Wait;
}

bool CoreMonitor::self_heal_requires_dead_gate(SelfHealOrigin origin) {
  return origin == SelfHealOrigin::ConfirmedDead;
}

bool CoreMonitor::self_heal_subject_to_switch_rate_limit(
    SelfHealOrigin origin) {
  return origin != SelfHealOrigin::ConfirmedDead;
}

bool CoreMonitor::auto_switch_permitted(
    std::optional<int64_t> last_switch_age_secs,
    size_t recent_switch_count) {
  if (last_switch_age_secs.has_value() &&
      *last_switch_age_secs < kMinAutoSwitchIntervalSecs) {
    return false;
  }
  return recent_switch_count < kMaxAutoSwitchesPerWindow;
}

bool CoreMonitor::should_keep_alive_on_api_recovery(bool is_valid_real_node,
                                                    const NodeStatus& status) {
  return is_valid_real_node && status.kind == NodeStatusKind::Alive;
}

bool CoreMonitor::persisted_exit_is_usable(
    const std::optional<std::string>& now,
    const std::vector<std::string>* all, const std::string& filter_text,
    bool confirmed_fake) {
  if (!now.has_value() || now->empty()) return false;
  if (*now == "DIRECT" || *now == "REJECT" || util::is_dummy_node(*now) ||
      confirmed_fake) {
    return false;
  }
  if (!util::match_node_filter(*now, util::effective_filter_text(filter_text))) {
    return false;
  }
  if (!all) return false;
  return std::find(all->begin(), all->end(), *now) != all->end();
}

bool CoreMonitor::should_migrate_filtered_out_active(
    const std::string& active_node, const std::vector<std::string>& members,
    const std::string& filter_text,
    const std::function<bool(const std::string&)>& is_fake) {
  const std::string effective = util::effective_filter_text(filter_text);
  if (effective.empty() || util::match_node_filter(active_node, effective)) {
    return false;
  }
  for (const auto& m : members) {
    if (m != active_node && m != "DIRECT" && m != "REJECT" &&
        !util::is_dummy_node(m) && (!is_fake || !is_fake(m)) &&
        util::match_node_filter(m, effective)) {
      return true;
    }
  }
  return false;
}

bool CoreMonitor::is_https_verify_backoff_active(
    std::optional<int64_t> elapsed_secs) {
  return elapsed_secs.has_value() &&
         *elapsed_secs < kHttpsVerifyAllFailedBackoffSecs;
}

}  // namespace clashwin::core
