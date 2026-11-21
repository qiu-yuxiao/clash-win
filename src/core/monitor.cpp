// CoreMonitor 守护线程生命周期、状态复位、单步体检循环与就绪等待
// （其余子职责拆分至 monitor_probe.cpp / monitor_verdict.cpp / monitor_observer.cpp；
//   ← ClashVerge src-tauri/src/module/monitor/mod.rs & daemon.rs）
#include "core/monitor.h"

#include <windows.h>

#include <chrono>
#include <exception>
#include <format>
#include <thread>

#include "events/event_bus.h"
#include "util/log.h"

namespace clashwin::core {

CoreMonitor& CoreMonitor::instance() {
  static CoreMonitor s;
  return s;
}

void CoreMonitor::set_hooks(Hooks h) {
  std::lock_guard<std::mutex> lock(m_);
  hooks_ = std::move(h);
}

void CoreMonitor::set_params(Params p) {
  std::lock_guard<std::mutex> lock(m_);
  params_ = p;
}

int CoreMonitor::current_interval_secs() const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_));
  if (hooks_.network_online && !hooks_.network_online()) {
    return params_.offline_interval_secs;
  }
  if (api_fail_count_.load() > 0 || node_fail_count_.load() > 0) {
    return params_.retry_interval_secs;
  }
  return params_.normal_interval_secs;
}

void CoreMonitor::run_supervised_daemon(
    const char* task_name, const std::function<bool()>& is_stopping,
    const std::function<void(uint32_t)>& backoff_sleep_ms,
    const std::function<void()>& on_reset,
    const std::function<void()>& daemon_body) {
  const char* label = (task_name && *task_name) ? task_name : "CoreMonitor";
  while (!is_stopping || !is_stopping()) {
    try {
      if (daemon_body) daemon_body();
      break;
    } catch (const std::exception& ex) {
      CWLOG_ERROR(std::format(
          "{}: [后台监测] 守护循环未预期异常（{}），重置状态并于 {}ms 后拉起恢复",
          label, ex.what(), kSupervisorRestartDelayMs));
    } catch (...) {
      CWLOG_ERROR(std::format(
          "{}: [后台监测] 守护循环未知异常，重置状态并于 {}ms 后拉起恢复",
          label, kSupervisorRestartDelayMs));
    }
    if (on_reset) on_reset();
    if (is_stopping && is_stopping()) break;
    if (backoff_sleep_ms) {
      backoff_sleep_ms(kSupervisorRestartDelayMs);
    } else {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(kSupervisorRestartDelayMs));
    }
  }
}

void CoreMonitor::reset_runtime_loop_state() {
  api_fail_count_.store(0);
  node_fail_count_.store(0);
  auto_select_fail_count_.store(0);
  api_dead_heal_fail_count_.store(0);
  was_offline_.store(false);
  last_heal_ms_.store(0);
  has_heal_attempt_.store(false);
  last_tick_ms_.store(0);
  last_https_verify_ms_.store(-1);
  reset_flowing_traffic_state();
  reset_kernel_log_observer();
  last_summary_log_ms_.store(-1);
  daemon_start_ms_.store(-1);
  manual_switch_epoch_.store(0, std::memory_order_release);
  last_node_change_ms_.store(0);
  has_node_change_.store(false);
  manual_switch_wake_.store(false);
  manual_grace_unmatched_.store(false);
  {
    std::lock_guard<std::mutex> lock(manual_m_);
    pending_manual_switch_ms_ = -1;
  }
  {
    std::lock_guard<std::mutex> lock(last_node_m_);
    last_active_node_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(switch_rate_m_);
    recent_auto_switches_ms_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(brownout_m_);
    recent_brownout_episodes_ms_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(health_m_);
    last_notified_health_ = "network_health::ok";
  }
}

void CoreMonitor::start() {
  if (running_.exchange(true)) return;
  worker_ = std::thread([this] {
    bool is_first_run = true;
    run_supervised_daemon(
        "CoreMonitor",
        [this] { return !running_.load(); },
        [this](uint32_t ms) {
          std::unique_lock<std::mutex> lock(m_);
          cv_.wait_for(lock, std::chrono::milliseconds(ms),
                       [this] { return !running_.load(); });
        },
        [this] { reset_runtime_loop_state(); },
        [this, &is_first_run] {
          while (running_.load()) {
            if (is_first_run) {
              is_first_run = false;
              // 首次启动时等待内核就绪
              for (int i = 0; i < 50 && running_.load(); ++i) {
                if (hooks_.api_alive && hooks_.api_alive()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
              }
              if (hooks_.reconcile_running_mode) {
                hooks_.reconcile_running_mode();
              }
              // 若无窗口可见（托盘启动），先按过滤边界恢复 persisted selected，再执行 Continuity-First 探测/选点
              if (hooks_.is_headless && hooks_.is_headless()) {
                std::string uid =
                    hooks_.current_uid ? hooks_.current_uid() : "";
                if (!uid.empty()) {
                  if (hooks_.restore_selected_nodes) {
                    hooks_.restore_selected_nodes(uid);
                  }
                  const bool keep_persisted =
                      hooks_.persisted_exit_usable &&
                      hooks_.persisted_exit_usable();
                  if (keep_persisted && hooks_.auto_select_with_mode) {
                    hooks_.auto_select_with_mode(/*select=*/false);
                    // BUG-417：纯测速发现持久节点超时且同组有健康候选 → 预置第 1 次失败，3s 快速复核
                    if (hooks_.take_boot_probe_failure_seed &&
                        hooks_.take_boot_probe_failure_seed()) {
                      const std::string cur_active =
                          hooks_.active_node_name ? hooks_.active_node_name() : std::string();
                      {
                        std::lock_guard<std::mutex> lock(last_node_m_);
                        last_active_node_ = cur_active;
                      }
                      node_fail_count_.store(1);
                      CWLOG_INFO("[后台监测] 启动纯测速发现持久节点 " + cur_active +
                                 " 超时不可达且同组存在健康候选，计入第 1 次失败并进入 3s 快速复核");
                    }
                  } else if (!keep_persisted && hooks_.auto_select_trigger) {
                    hooks_.auto_select_trigger();
                  }
                }
              }
            }
            int interval = current_interval_secs();
            // 分片等待（stop/kick 即时退出；manual_switch_wake 仅吸收宽限期时间戳后继续休眠）
            for (int waited = 0; running_.load() && waited < interval;
                 waited += 1) {
              std::unique_lock<std::mutex> lock(m_);
              cv_.wait_for(lock, std::chrono::seconds(1), [this] {
                return !running_.load() || kick_.load() ||
                       manual_switch_wake_.load();
              });
              if (!running_.load()) break;
              if (manual_switch_wake_.exchange(false)) {
                const int64_t now =
                    hooks_.now_ms ? hooks_.now_ms()
                                  : static_cast<int64_t>(GetTickCount64());
                absorb_pending_manual_switch(now);
              }
              if (kick_.exchange(false)) break;
            }
            if (!running_.load()) break;
            tick_once();
          }
        });
  });
}

void CoreMonitor::stop() {
  if (!running_.exchange(false)) return;
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void CoreMonitor::kick() {
  kick_.store(true);
  cv_.notify_all();
}

void CoreMonitor::notify_manual_node_switched() {
  manual_switch_epoch_.fetch_add(1, std::memory_order_acq_rel);
  // BUG-415：手动切点重置建连失败追踪（从切换时刻起算，不回放旧告警）
  reset_kernel_observer_for_manual_switch();
  int64_t now = 0;
  {
    std::lock_guard<std::mutex> lock(m_);
    now = hooks_.now_ms ? hooks_.now_ms()
                        : static_cast<int64_t>(GetTickCount64());
  }
  {
    std::lock_guard<std::mutex> lock(manual_m_);
    pending_manual_switch_ms_ = now;
  }
  manual_switch_wake_.store(true);
  cv_.notify_all();
}

void CoreMonitor::absorb_pending_manual_switch(int64_t fallback_now_ms) {
  int64_t ts = -1;
  {
    std::lock_guard<std::mutex> lock(manual_m_);
    ts = pending_manual_switch_ms_;
    pending_manual_switch_ms_ = -1;
  }
  if (ts < 0) return;
  const int64_t effective_ts = (ts >= 0) ? ts : fallback_now_ms;
  last_node_change_ms_.store(effective_ts);
  has_node_change_.store(true);
  manual_grace_unmatched_.store(true);
  node_fail_count_.store(0);
  {
    std::lock_guard<std::mutex> lock(brownout_m_);
    recent_brownout_episodes_ms_.clear();
  }
  CWLOG_INFO("CoreMonitor: 吸收前端手动节点切换事件，立即刷新 20s 切换宽限期");
}

void CoreMonitor::emit_network_health_notice(const std::string& health_msg) {
  {
    std::lock_guard<std::mutex> lock(health_m_);
    if (last_notified_health_ == health_msg) return;
    last_notified_health_ = health_msg;
  }
  const char* status =
      (health_msg == "network_health::ok") ? "info" : "warning";
  events::EventBus::notice_message(status, health_msg);
}

std::string CoreMonitor::last_notified_health_for_test() const {
  std::lock_guard<std::mutex> lock(health_m_);
  return last_notified_health_;
}

void CoreMonitor::reset_state() {
  reset_runtime_loop_state();
  clear_server_failures();
}

// ★ §5.1 切换宽限期判定（← Mini monitor.rs:1383-1396，v3.2.4 BUG-315 = 20s）
//   切换后 20s 内：失败判定只记日志、不计入连续失败次数。
bool CoreMonitor::in_switch_grace() const {
  if (!has_node_change_.load()) return false;
  int64_t changed = last_node_change_ms_.load();
  int64_t now = hooks_.now_ms ? hooks_.now_ms()
                              : static_cast<int64_t>(GetTickCount64());
  int64_t grace;
  {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_));
    grace = params_.switch_grace_ms;
  }
  return (now - changed) < grace;
}

CoreMonitor::Action CoreMonitor::tick_once() {
  Hooks h;
  Params p;
  {
    std::lock_guard<std::mutex> lock(m_);
    h = hooks_;
    p = params_;
  }
  const auto now_fn = [&]() -> int64_t {
    return h.now_ms ? h.now_ms() : static_cast<int64_t>(GetTickCount64());
  };
  const int64_t now_tick = now_fn();
  last_tick_ms_.store(now_tick);

  // BUG-336 Checkpoint #1：任何提前返回（进程未跑 / 物理断网 / API 失败）前先吸收手动切换信号
  absorb_pending_manual_switch(now_tick);

  // 1. 进程级直检：进程死 → 若接入管道探测则走防抖/保护/冷却快重启，否则沿用直连重启
  if (h.core_alive && !h.core_alive()) {
    if (h.pipe_missing && h.expect_core_pipe_alive) {
      if (auto act = try_fast_restart_missing_pipe(h, p)) {
        return *act;
      }
      return Action::Idle;
    }
    CWLOG_WARN("CoreMonitor: 内核进程静默退出（无退出日志）→ 自动重启");
    if (h.restart_core && h.restart_core().is_ok()) return Action::Restarted;
    return Action::Idle;
  }

  // BUG-417：服务内核实际在线但 running_mode 被瞬时超时回滚为 NotRunning 时自动对账
  if (h.reconcile_running_mode) h.reconcile_running_mode();
  // BUG-416：在线探针前同步 Service 模式内核增量日志（TUN 网卡丢失/切换 → 强制在线复核）
  if (h.sync_kernel_logs) h.sync_kernel_logs();

  // 2. 物理网络离线：跳过检测并通知前端离线横幅（BUG-359）
  bool online = h.network_online ? h.network_online() : true;
  if (!online) {
    was_offline_.store(true);
    emit_network_health_notice("network_health::offline");
    maybe_log_steady_state_summary(now_fn(), false);
    return Action::Idle;
  }
  maybe_log_steady_state_summary(now_fn(), true);
  // 离线 → 在线转移（S3 统一裁决 NetworkRecovered = Wait）：
  // 复位计数并通知 ok，交由本周期后续正常体检路径重新测量，绝不直接切点
  if (was_offline_.exchange(false)) {
    api_fail_count_.store(0);
    node_fail_count_.store(0);
    {
      std::lock_guard<std::mutex> lock(brownout_m_);
      recent_brownout_episodes_ms_.clear();
    }
    emit_network_health_notice("network_health::ok");
    CWLOG_INFO("CoreMonitor: 物理网络恢复，交由常规体检路径重新测量（不立即自愈）");
  }

  // 3. API 存活：优先缺管秒级快重启（BUG-411），否则连续 3 次不可达且复核仍不可达 → 自愈
  bool api_ok = h.api_alive ? h.api_alive() : true;
  if (!api_ok) {
    if (auto act = try_fast_restart_missing_pipe(h, p)) {
      return *act;
    }
    int fails = api_fail_count_.fetch_add(1) + 1;
    if (fails >= p.api_fail_threshold) {
      const bool api_recheck = h.api_alive ? h.api_alive() : false;
      api_fail_count_.store(0);
      if (decide_self_heal(
              {SelfHealTrigger::Kind::ApiUnreachable,
               static_cast<uint32_t>(fails), api_recheck},
              p.node_dead_threshold, p.api_fail_threshold) ==
          SelfHealDecision::Allow) {
        std::string uid = h.current_uid ? h.current_uid() : std::string();
        return heal(uid, SelfHealOrigin::ApiChannelDown, {});
      }
      CWLOG_INFO("CoreMonitor: API 复核已恢复可达，复位计数交下一周期重测");
    }
    return Action::Idle;
  }
  api_fail_count_.store(0);

  // 定期真 HTTPS 验真（每 180s 周期一次；有未决节点才实际发包）
  if (h.periodic_https_verify) {
    const int64_t last_v = last_https_verify_ms_.load();
    if (last_v < 0 || (now_tick - last_v) >= kHttpsVerifyIntervalSecs * 1000) {
      last_https_verify_ms_.store(now_tick);
      h.periodic_https_verify();
    }
  }

  // BUG-336 Checkpoint #2：观测 active_node_name 前再次吸收
  absorb_pending_manual_switch(now_fn());

  // ★ §5.1 活跃节点切换检测（← Mini monitor.rs:1358-1370）
  std::string observed_active;
  if (h.active_node_name) {
    observed_active = h.active_node_name();
    bool changed = false;
    std::string prev_active;
    if (!observed_active.empty()) {
      std::lock_guard<std::mutex> lock(last_node_m_);
      if (observed_active != last_active_node_) {
        prev_active = last_active_node_;
        last_active_node_ = observed_active;
        changed = true;
      }
    }
    if (changed) {
      // BUG-417：仅真实换节点（Some(old) -> Some(new)）且无手动预置宽限期时开启 20s 宽限期；
      // 守护首次观测（None -> Some）严禁开启伪宽限期或清除冷启动预置的失败计数。
      const bool had_manual_grace =
          manual_grace_unmatched_.exchange(false) && in_switch_grace();
      const bool had_prev_node = !prev_active.empty();
      const bool arm_grace = should_arm_switch_grace_on_node_change(
          had_prev_node ? std::optional<std::string>(prev_active) : std::nullopt,
          observed_active, had_manual_grace);
      if (arm_grace) {
        last_node_change_ms_.store(now_fn());
        has_node_change_.store(true);
      }
      if (had_prev_node) {
        node_fail_count_.store(0);
        std::lock_guard<std::mutex> lock(brownout_m_);
        recent_brownout_episodes_ms_.clear();
      }
      CWLOG_INFO(std::format(
          "CoreMonitor: 活跃节点变化 {} -> {}{}",
          had_prev_node ? prev_active : std::string("None"), observed_active,
          arm_grace ? std::format("，起算 {}s 切换宽限期", p.switch_grace_ms / 1000)
                    : std::string("（首次观测或已在宽限期内，不重新起算）")));
    }
  }

  // ★ §5.4 第 3 道铁锁：假/广告/验真失败迁移节点 → 直接判需迁移（NodeMigration）
  if (h.is_dummy_active) {
    if (h.is_dummy_active()) {
      CWLOG_WARN("CoreMonitor: 活跃节点为伪节点/已确认假节点/待迁移节点，触发不可信迁移");
      node_fail_count_.store(0);
      std::string uid = h.current_uid ? h.current_uid() : std::string();
      if (!uid.empty()) {
        return heal(uid, SelfHealOrigin::NodeMigration, observed_active);
      }
      return Action::Idle;
    }
  }

  // ★ BUG-405：活跃出口已被当前过滤词排除且存在合规候选 → 立即触发迁移（NodeMigration，受 20s 宽限期保护）
  if (h.should_migrate_filtered_out && h.should_migrate_filtered_out()) {
    absorb_pending_manual_switch(now_fn());
    if (in_switch_grace()) {
      CWLOG_INFO("CoreMonitor: 活跃节点越界过滤词迁移处于 20s 切换宽限期内，暂缓切点");
      return Action::Idle;
    }
    CWLOG_WARN("CoreMonitor: 当前活跃节点已被过滤词排除，触发越界迁移 (NodeMigration)");
    node_fail_count_.store(0);
    std::string uid = h.current_uid ? h.current_uid() : std::string();
    if (!uid.empty()) {
      return heal(uid, SelfHealOrigin::NodeMigration, observed_active);
    }
    return Action::Idle;
  }

  // ★ §5.8 测速失真复核（← Mini monitor.rs:1526-1670）：不可用或主 URL 失真 → 本轮不判死
  if (h.probe_metrics_with_fallback) {
    if (!h.probe_metrics_with_fallback()) {
      CWLOG_WARN(
          "CoreMonitor: 测速链路不可用或主测速地址失真，本轮不判定节点死活");
      node_fail_count_.store(0);
      auto_select_fail_count_.store(0);
      api_dead_heal_fail_count_.store(0);
      return Action::Idle;
    }
  }

  // ★ ShouldMigrate：Alive 但真 HTTPS 验真失败 → 切往已验真节点（在失真复核之后且受 20s 宽限期约束）
  if (h.should_migrate_untrusted && h.should_migrate_untrusted()) {
    absorb_pending_manual_switch(now_fn());
    if (in_switch_grace()) {
      CWLOG_INFO("CoreMonitor: 活跃节点 HTTPS 验真迁移处于 20s 切换宽限期内，暂缓切点");
      return Action::Idle;
    }
    CWLOG_WARN("CoreMonitor: 活跃节点真 HTTPS 验真失败，触发不可信迁移 (ShouldMigrate)");
    node_fail_count_.store(0);
    std::string uid = h.current_uid ? h.current_uid() : std::string();
    if (!uid.empty()) {
      return heal(uid, SelfHealOrigin::NodeMigration, observed_active);
    }
    return Action::Idle;
  }

  // 4. 节点健康：连续 2 次判死或 600s 滑窗内 ≥2 次间歇黑洞 → 自愈（ConfirmedDead）
  bool node_ok = h.node_healthy ? h.node_healthy() : true;
  if (!node_ok) {
    // BUG-336 Checkpoint #3：耗时拨测返回后立即吸收拨测期间发生的手动切节点信号
    absorb_pending_manual_switch(now_fn());
    if (in_switch_grace()) {
      CWLOG_INFO(
          "CoreMonitor: 活跃节点检测异常，当前处于切换宽限期，不计入连续失败");
      node_fail_count_.store(0);
      {
        std::lock_guard<std::mutex> lock(brownout_m_);
        recent_brownout_episodes_ms_.clear();
      }
      return Action::Idle;
    }
    const int fails = node_fail_count_.fetch_add(1) + 1;
    const bool is_should_failover =
        h.has_healthy_standby_in_subset ? h.has_healthy_standby_in_subset()
                                        : true;
    const int64_t episode_now_ms = now_fn();
    size_t brownout_episodes = 0;
    {
      std::lock_guard<std::mutex> lock(brownout_m_);
      if (fails == 1 && is_should_failover) {
        brownout_episodes = record_brownout_episode(
            recent_brownout_episodes_ms_, episode_now_ms);
      } else {
        prune_brownout_episodes(recent_brownout_episodes_ms_, episode_now_ms);
        brownout_episodes = recent_brownout_episodes_ms_.size();
      }
    }
    CWLOG_INFO(std::format(
        "CoreMonitor: 活跃节点检测异常，连续失败次数: {}，窗口内间歇故障次数: {}",
        fails, brownout_episodes));
    if (decide_self_heal(
            {SelfHealTrigger::Kind::NodeConfirmedDead,
             static_cast<uint32_t>(fails), false, brownout_episodes},
            p.node_dead_threshold, p.api_fail_threshold) ==
        SelfHealDecision::Allow) {
      node_fail_count_.store(0);
      {
        std::lock_guard<std::mutex> lock(brownout_m_);
        recent_brownout_episodes_ms_.clear();
      }
      std::string dead_node = observed_active;
      if (dead_node.empty() && h.active_node_name) {
        dead_node = h.active_node_name();
      }
      if (!dead_node.empty()) {
        auto server_map = load_node_server_map();
        auto it = server_map.find(dead_node);
        if (it != server_map.end() && !it->second.empty()) {
          record_server_failure(it->second);
          CWLOG_INFO("CoreMonitor: 活跃节点连续判死，服务器记入冷却避让: " + it->second);
        }
      }
      std::string uid = h.current_uid ? h.current_uid() : std::string();
      return heal(uid, SelfHealOrigin::ConfirmedDead, dead_node);
    }
    return Action::Idle;
  }
  const int prev_fails = node_fail_count_.exchange(0);
  if (prev_fails > 0) {
    CWLOG_INFO(std::format(
        "CoreMonitor: 活跃节点复检恢复正常（此前连续失败 {} 次），解除重试状态",
        prev_fails));
  }
  auto_select_fail_count_.store(0);
  api_dead_heal_fail_count_.store(0);
  emit_network_health_notice("network_health::ok");
  return Action::Idle;
}

bool CoreMonitor::wait_for_clash_ready(const ReadyHooks& h) {
  auto poll = [&](std::function<bool()> ready, int budget_ms) {
    if (!ready) return true;
    int64_t start = h.now_ms ? h.now_ms() : 0;
    for (;;) {
      if (ready()) return true;
      int64_t now = h.now_ms ? h.now_ms() : 0;
      if (now - start >= budget_ms) return false;
      if (h.sleep_ms) h.sleep_ms(300);
      else Sleep(300);
    }
  };
  if (!poll(h.api_alive, 30000)) return false;
  if (!poll(h.group_ready, 20000)) return false;
  if (!poll(h.network_online, 15000)) return false;
  // 阶段 4：等待 DNS 解析链就绪（最多 15s；超时仅告警仍放行由下游兜底，← monitor.rs:1100-1138）
  if (h.dns_ready && !poll(h.dns_ready, 15000)) {
    CWLOG_WARN("CoreMonitor: 阶段 4 超时：DNS 解析链未就绪，放行测速（由下游兜底）");
  }
  return true;
}

}  // namespace clashwin::core
