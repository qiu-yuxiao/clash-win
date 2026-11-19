#pragma once
// T22 内核守护（CoreMonitor）：
// 对照源：ClashVerge src-tauri/src/module/monitor.rs
//
// 间隔/判死/自愈/升级参数（对齐 Clash Mini v3.3.0 / Sync-1）：
//   - 体检间隔 15s（重试 3s / 物理网络离线 5s）
//   - 健康数据新鲜度 60s（HEALTH_DATA_FRESH_SECS = 60）；死节点复核最小间隔 10s
//   - 节点切换宽限期 20s（SWITCH_GRACE_PERIOD_SECS = 20）
//   - 活跃节点连续 2 次判死 → 自愈选点（AutoSelect::trigger）
//   - 内核 API 连续 3 次不可达 → 自愈选点
//   - 自愈 25s 冷却；BUSY 冲突不计失败、不惩罚冷却
//   - 物理网络连通性双频探测（在线 60s / 离线 5s）：TCP:53 直连 223.5.5.5 / 119.29.29.29 /
//     1.1.1.1 优先，test_url DNS 解析兜底（打破 TUN 断流下 DNS 死锁）
//   - 自愈连续失败 3 次 → 归零计数 → API 不可达 → restart_core → restart 失败 → 弹窗警报
//     （Mini 现行「3 次归零」语义，无独立 5 次弹窗；用户裁决 2026-11-09）
//   - 失败服务器表（record_server_failure / snapshot_failed_servers，600s 冷却）
//   - 四阶段就绪等待（wait_for_clash_ready）
//
// 【测试铁律】真线程/真注册表/真内核绝不在测试触发——tick_once 单步驱动 +
// hooks 注入；start/stop 仅生产装配调用。

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "util/result.h"

namespace clashwin::core {

class CoreMonitor {
 public:
  // ← monitor.rs v3.5.0 ~ v3.5.6 Continuity-First & Traffic Liveness 常量
  static constexpr uint64_t kSuspectWindowSecs = 30;
  static constexpr int64_t kPassiveTrafficMaxSkipSecs = 1800;  // BUG-399: 480s -> 1800s (30min)
  static constexpr int64_t kIdleHealthFreshSecs = 900;         // BUG-399: 300s -> 900s (15min)
  static constexpr int64_t kStallReprobeCooldownSecs = 60;
  static constexpr int64_t kMinAutoSwitchIntervalSecs = 120;
  static constexpr int64_t kAutoSwitchWindowSecs = 600;
  static constexpr size_t kMaxAutoSwitchesPerWindow = 3;
  static constexpr int64_t kHttpsVerifyIntervalSecs = 600;     // BUG-400: 180s -> 600s (10min)
  static constexpr size_t kPeriodicHttpsVerifySufficientStandbys = 3;       // BUG-400
  static constexpr size_t kSingleTargetSkipFallbackMinHealthyPeers = 3;     // BUG-401
  static constexpr int64_t kOnlineCheckFlowingSkipSecs = 60;                // BUG-403
  static constexpr int kPrimaryNetProbeTimeoutMs = 250;                     // BUG-403
  static constexpr int64_t kActiveVerifyCooldownSecs = 60;
  static constexpr int64_t kHttpsVerifyAllFailedBackoffSecs = 600;
  static constexpr int64_t kProbeCoalesceWindowMs = 1500;
  static constexpr int64_t kSelfHealSnapshotReuseMs = 10000;
  static constexpr bool kDeadConnCleanupEnabled = false;
  static constexpr const char* kVerifyHttpsUrlPrimary =
      "https://github.githubassets.com/favicons/favicon.svg";
  static constexpr const char* kVerifyHttpsUrlFallback =
      "https://www.wikipedia.org/static/favicon/wikipedia.ico";
  // ← monitor.rs:1277-1280 物理网络双频探测间隔（在线 60s / 离线 5s）
  static constexpr int64_t kOnlineProbeIntervalMs = 60000;
  static constexpr int64_t kOfflineProbeIntervalMs = 5000;
  // ← monitor.rs（BUG-317 / BUG-345 / BUG-403）：备用两路 TCP:53 并发竞速上限 600ms，总预算下限 2s
  static constexpr int kTcp53RaceTimeoutMs = 600;
  static constexpr uint64_t kMinProbeBudgetSecs = 2;
  static constexpr uint32_t kSupervisorRestartDelayMs = 2000;

  // 参数对照表（← census §70 / monitor.rs v3.2.4 + BUG-318，全 hooks 注入可测）
  struct Params {
    int normal_interval_secs = 15;     // 正常体检 15s
    int retry_interval_secs = 3;       // 判死后重试 3s
    int offline_interval_secs = 5;     // 物理网络离线 5s
    int node_dead_threshold = 2;       // 节点连续 2 次判死
    int api_fail_threshold = 3;        // API 连续 3 次不可达
    int heal_fail_threshold = 3;       // API 假死下连续 3 次自愈失败 → 重启内核（BUG-318）
    int alert_fail_threshold = 5;      // 自愈连续 5 次失败 → 系统网络警报（BUG-318）
    // ★ §5.3 自愈冷却保护（← monitor.rs:1538-1542 v3.2.4 BUG-315：60s → 25s）
    int64_t heal_cooldown_ms = 25000;
    int64_t failed_server_cooldown_ms = 600000;  // 失败服务器 600s
    // ★ §5.1 切换宽限期（← monitor.rs:74 SWITCH_GRACE_PERIOD_SECS = 20，v3.2.4 BUG-315：60s → 20s）
    //   切换后 20s 内的失败判定只记日志、不计入连续失败次数
    //   （兼顾新节点状态稳定与切到不可用节点时的二次自愈时效）
    int64_t switch_grace_ms = 20000;
  };

  struct Hooks {
    // 内核 API 存活（生产 = get_base_config 3s 超时；mock）
    std::function<bool()> api_alive;
    // 内核进程存活（生产 = CoreManager::core_alive，模式感知）
    std::function<bool()> core_alive;
    // 内核重启（生产 = CoreManager restart_with_restore）
    std::function<Result<void>()> restart_core;
    // 物理网络在线（生产 = DNS/接口探测）
    std::function<bool()> network_online;
    // 节点健康（生产 = T16 延迟缓存/测量组轻量判定；true=健康）
    std::function<bool()> node_healthy;
    // ★ 当前活跃节点名（生产 = PROXY 组 .now；用于检测「节点切换」以起算宽限期）
    //   ← Mini monitor.rs:1358-1370 的 active_node_name 读取
    std::function<std::string()> active_node_name;
    // ★ §5.8 测速失真复核（← Mini monitor.rs:188-223 probe_metrics_group）
    //   生产 = 对 PROXY__METRICS 整组拨测：主 URL 整体失败 → 备用 URL 复核；
    //   主备双双整体失败 → 返回 false = 「测速链路不可用」，**不可据此判节点已死**。
    //   返回 true = 测速链路可用（本次测得结果可能是空的，但链路正常）。
    std::function<bool()> probe_metrics_with_fallback;
    // ★ §5.4 第 3 道铁锁：当前活跃节点是否为伪/广告节点
    //   （← Mini monitor.rs:663-666 evaluate_failover 的 is_dummy_node 分支）
    std::function<bool()> is_dummy_active;
    // 自愈选点触发（生产 = AutoSelect::instance().trigger(...)）
    // 返回值：true=已触发/成功；false=失败；"busy" 单列（见 HealResult）
    std::function<Result<bool>()> auto_select_trigger;
    // 警报（生产 = MessageBoxW；测试 = 注入记录）
    std::function<void(const std::wstring& title, const std::wstring& text)> alert;
    // 当前激活 profile uid（自愈选点入参；空 = 无法自愈）
    std::function<std::string()> current_uid;
    // 托盘/无窗口纯后台启动判定（无窗口可见时由后台执行首次初始化选点）
    std::function<bool()> is_headless;
    // ← v3.5.0 Continuity-First：当前持久出口是否结构有效（有效时冷启动只测不切）
    std::function<bool()> persisted_exit_usable;
    // ← v3.5.0 可选指定 select 参数的初始化选点（缺失时回退 auto_select_trigger）
    std::function<Result<bool>(bool select)> auto_select_with_mode;
    // ← v3.5.0 S4 ConfirmedDead 切点前同一权威来源复核（返回 false = 放弃本次切点）
    std::function<bool(const std::string& dead_node)> confirm_dead_gate;
    // ← v3.5.0 BUG-388 API 恢复保活门检（返回 true = 当前活跃真节点已健康，保活跳过切点）
    std::function<bool()> api_recovery_keep_alive;
    // ← v3.5.0 后台定期真 HTTPS 验真（每 180s 周期调用，可选）
    std::function<void()> periodic_https_verify;
    // ← v3.5.5 无头/冷启动恢复 profile.selected 到 PROXY 组（在 persisted_exit_usable 前）
    std::function<void(const std::string& uid)> restore_selected_nodes;
    // ← v3.5.5 Alive 但 HTTPS 验真失败 → ShouldMigrate（在 probe_metrics_with_fallback 之后且受 20s 宽限期保护）
    std::function<bool()> should_migrate_untrusted;
    // 时钟（Unix 毫秒；测试注入虚拟时钟）
    std::function<int64_t()> now_ms;
  };

  static CoreMonitor& instance();
  void set_hooks(Hooks h);
  void set_params(Params p);
  const Params& params() const { return params_; }

  // 生产：启动/停止守护线程（内部按状态机间隔 tick）
  void start();
  void stop();
  bool running() const { return running_.load(); }
  // 非阻塞唤醒：仅通知守护线程尽快 tick，不在调用线程（UI 线程）做网络探测。
  // ← 修复 WM_CLOSE→轻量模式路径同步 tick 造成的消息循环阻塞（AppHangB1）。
  void kick();
  bool has_pending_kick_for_test() const { return kick_.load(); }
  void clear_kick_for_test() { kick_.store(false); }

  // 状态机单步（测试驱动；生产线程内部调用）
  enum class Action {
    Idle,                 // 离线跳过 / 一切正常
    Restarted,            // 进程死 → restart_core
    Healed,               // 自愈选点触发成功
    RestartedOnHealFail,  // 自愈连续失败 → API 不可达 → restart_core
    Alerted,              // 连续 5 次失败 / restart 失败 → 警报
  };
  Action tick_once();

  // 当前状态对应的下一次 tick 间隔（秒）
  int current_interval_secs() const;

  // ---- 失败服务器表（← monitor.rs 节点失败冷却表；T16 避让接活） ----
  void record_server_failure(const std::string& server);
  // 快照：剔除冷却期内（600s）失败服务器
  std::vector<std::string> snapshot_failed_servers();
  void clear_server_failures();

  // 读取运行时配置构建「节点名 → 服务器地址」映射（mihomo /proxies API 不暴露 server）
  std::map<std::string, std::string> load_node_server_map();

  // ---- 物理网络连通性探测（← monitor.rs:271-308 / :1275-1293，v3.2.4 BUG-315） ----
  // 将测速 URL 解析为 (host, port)，IPv6 自动剥除方括号供 getaddrinfo 使用；
  // 缺省/解析失败回退 ("cp.cloudflare.com", 443)。
  static std::pair<std::string, uint16_t> resolve_probe_target(
      const std::string& test_url);

  // 可注入探测原语（单测零外网依赖）
  struct PhysicalNetProbeOps {
    // BUG-317 / BUG-345：3 路并发非阻塞 TCP:53 竞速（生产默认注入 tcp_race_connect_ipv4）
    std::function<bool(const std::vector<const char*>& ips, uint16_t port,
                       int timeout_ms)>
        tcp_race_connect;
    std::function<bool(const char* ip, uint16_t port, int timeout_ms)> tcp_connect;
    std::function<bool(const std::string& host, uint16_t port, int timeout_ms)>
        dns_lookup;
    std::function<int64_t()> now_ms;
  };
  static bool tcp_race_connect_ipv4(const std::vector<const char*>& ips,
                                    uint16_t port, int timeout_ms);
  static bool probe_physical_network_with(uint64_t timeout_secs,
                                          const std::string& test_url,
                                          const PhysicalNetProbeOps& ops);
  // 生产入口（BUG-403）：首选单路 223.5.5.5:53 (250ms)，失败再并发竞速 119.29.29.29 / 1.1.1.1（600ms），
  // 全部不通时回退 test_url 域名 DNS 查询兜底。
  static bool is_physical_network_online(uint64_t timeout_secs,
                                         const std::string& test_url);

  // BUG-403：近期真实下行流量（Flowing < 60s）证明物理网卡在线，跳过定期 TCP:53 探针
  static bool has_recent_flowing_traffic(std::optional<int64_t> elapsed_secs);
  static void record_flowing_traffic_at(int64_t now_ms);
  static bool is_recent_flowing_traffic_active(int64_t now_ms);
  static void reset_flowing_traffic_state();

  // ---- BUG-318：自愈双计数器纯函数状态机 ----
  enum class SelfHealAction {
    None,
    RestartCore,
    FireAlert,
  };
  static SelfHealAction evaluate_self_heal_counters(
      int& auto_select_fail_count, int& api_dead_heal_fail_count,
      bool api_alive, int restart_threshold = 3, int alert_threshold = 5);
  static SelfHealAction complete_restart_core_attempt(
      bool restart_ok, int& auto_select_fail_count,
      int& api_dead_heal_fail_count);

  // ---- BUG-336：手动切节点即时刷新宽限期 ----
  void notify_manual_node_switched();
  // ---- BUG-388：手动切点代际号（AutoSelect 期间若被手动切点抢占则放弃写回） ----
  uint64_t manual_switch_epoch() const {
    return manual_switch_epoch_.load(std::memory_order_acquire);
  }
  static bool is_manual_switch_preempted(uint64_t start_epoch,
                                         uint64_t current_epoch);
  static bool is_benign_auto_select_abort(const std::string& err_msg);

  // ---- v3.5.0 Continuity-First 纯判定与状态机辅助（← monitor.rs） ----
  enum class NodeStatusKind { Unknown, Suspect, Dead, Alive };
  struct NodeStatus {
    NodeStatusKind kind = NodeStatusKind::Unknown;
    uint32_t delay_ms = 0;
  };
  static NodeStatus classify_node_status(
      bool alive, std::optional<uint32_t> last_delay,
      std::optional<uint64_t> age_secs);
  static bool should_proceed_with_self_heal(const NodeStatus& status);

  enum class ActiveTrafficState { Flowing, Stalled, Idle };
  struct ActiveTrafficTracker {
    std::optional<std::string> active_node;
    std::map<std::string, std::pair<uint64_t, uint64_t>> conn_bytes;
    int64_t last_stall_probe_ms = -1;
  };
  static std::pair<ActiveTrafficState,
                   std::map<std::string, std::pair<uint64_t, uint64_t>>>
  classify_active_traffic_delta(
      const std::optional<std::string>& prev_node,
      const std::string& active_node,
      const std::map<std::string, std::pair<uint64_t, uint64_t>>& prev_conns,
      const std::vector<std::tuple<std::string, uint64_t, uint64_t>>&
          current_conns);
  static std::pair<ActiveTrafficState, bool> update_active_traffic_state(
      std::optional<ActiveTrafficTracker>& tracker_slot,
      const std::string& active_node,
      const std::vector<std::tuple<std::string, uint64_t, uint64_t>>&
          current_conns,
      int64_t now_ms);
  static bool should_probe_active_node(const NodeStatus& status,
                                       std::optional<uint64_t> age_secs,
                                       ActiveTrafficState traffic,
                                       bool stall_cooldown_active);
  static bool should_allow_https_migration(ActiveTrafficState traffic);

  // BUG-400：后台定期真 HTTPS 验真资格与“够用即止”（>=3 已验真 Alive 备用）门控
  static bool should_run_periodic_https_verify_with(
      const std::vector<std::string>& members,
      const std::optional<std::string>& active_node,
      const std::string& filter_text,
      const std::function<bool(const std::string&)>& is_fake_node,
      const std::function<bool(const std::string&)>& is_alive,
      const std::function<bool(const std::string&)>& is_verified);

  struct MetricsProbeOutcome {
    std::map<std::string, uint32_t> delays;
    bool primary_url_distorted = false;
  };
  static bool should_recheck_with_fallback(
      const std::map<std::string, uint32_t>& primary_delays,
      const std::vector<std::string>& expected_nodes);
  // BUG-401：单目标主 URL 已有 >=3 个真实节点响应该 URL 时跳过整组备用 URL 复核
  static bool should_recheck_primary_with_fallback_with(
      const std::map<std::string, uint32_t>& primary_delays,
      const std::vector<std::string>& expected_nodes,
      bool allow_single_target_skip,
      const std::function<bool(const std::string&)>& is_fake_node);
  static bool should_recheck_primary_with_fallback(
      const std::map<std::string, uint32_t>& primary_delays,
      const std::vector<std::string>& expected_nodes,
      bool allow_single_target_skip);
  static std::map<std::string, uint32_t> merge_probe_delays(
      std::map<std::string, uint32_t> primary,
      const std::map<std::string, uint32_t>& fallback);
  static std::vector<std::pair<std::string, uint32_t>>
  build_probe_ui_display_with(
      const std::vector<std::string>& all_nodes,
      const std::map<std::string, uint32_t>& probed_delays,
      bool primary_url_distorted,
      const std::function<bool(const std::string&)>& is_fake_node);
  static std::vector<std::pair<std::string, uint32_t>> build_probe_ui_display(
      const std::vector<std::string>& all_nodes,
      const std::map<std::string, uint32_t>& probed_delays,
      bool primary_url_distorted);

  struct UiActiveSyncState {
    std::string node_name;
    std::optional<std::string> history_time;
    bool reported_alive = false;
  };
  static std::optional<UiActiveSyncState> compute_next_ui_active_sync(
      const std::string& active_node,
      const std::vector<std::pair<std::string, uint32_t>>& display,
      const std::optional<std::string>& active_hist_time,
      const UiActiveSyncState* prev);
  static bool should_sync_alive_ui(bool health_not_ok,
                                   const UiActiveSyncState* last_sync,
                                   const std::string& active_node,
                                   const std::string* active_hist_time);

  enum class SelfHealOrigin { ConfirmedDead, NodeMigration, ApiChannelDown };
  struct SelfHealTrigger {
    enum class Kind { NodeConfirmedDead, NetworkRecovered, ApiUnreachable };
    Kind kind = Kind::NodeConfirmedDead;
    uint32_t count = 0;
    bool api_alive_on_recheck = false;
  };
  enum class SelfHealDecision { Allow, Wait };
  static SelfHealDecision decide_self_heal(const SelfHealTrigger& trigger,
                                           int node_dead_threshold = 2,
                                           int api_fail_threshold = 3);
  static bool self_heal_requires_dead_gate(SelfHealOrigin origin);
  static bool self_heal_subject_to_switch_rate_limit(SelfHealOrigin origin);
  static bool auto_switch_permitted(std::optional<int64_t> last_switch_age_secs,
                                    size_t recent_switch_count);
  static bool should_keep_alive_on_api_recovery(bool is_valid_real_node,
                                                const NodeStatus& status);
  static bool persisted_exit_is_usable(
      const std::optional<std::string>& now,
      const std::vector<std::string>* all, const std::string& filter_text,
      bool confirmed_fake);
  static bool is_https_verify_backoff_active(std::optional<int64_t> elapsed_secs);

  // ---- BUG-359：网络健康状态去重广播（network_health::ok / offline / all_dead） ----
  void emit_network_health_notice(const std::string& health_msg);
  std::string last_notified_health_for_test() const;

  // ---- BUG-353：外层受监管守护循环 ----
  static void run_supervised_daemon(
      const char* task_name, const std::function<bool()>& is_stopping,
      const std::function<void(uint32_t)>& backoff_sleep_ms,
      const std::function<void()>& on_reset,
      const std::function<void()>& daemon_body);

  // 双频探测节流器（← monitor.rs:1275-1293：在线 60s / 离线 5s，首次必探）
  class ThrottledNetProbe {
   public:
    bool check(int64_t now_ms, const std::function<bool()>& probe_fn);
    bool was_online() const;
    void reset();

   private:
    std::mutex mu_;
    bool has_checked_ = false;
    bool was_online_ = true;
    int64_t last_check_ms_ = 0;
  };

  // ---- wait_for_clash_ready（T16 留位转正） ----
  struct ReadyHooks {
    std::function<bool()> api_alive;
    std::function<bool()> group_ready;
    std::function<bool()> network_online;
    std::function<bool()> dns_ready;  // 阶段 4：DNS 解析链（15s 上限，超时放行）
    std::function<int64_t()> now_ms;
    std::function<void(int)> sleep_ms;  // 测试注入假 sleep
  };
  static bool wait_for_clash_ready(const ReadyHooks& h);

  // ---- 全局共享 PROXY__METRICS 拨测锁与快照复用（← monitor.rs:110/657） ----
  static std::mutex& metrics_probe_mutex();
  static std::optional<std::map<std::string, uint32_t>> try_reuse_metrics_probe(
      int64_t now_ms, int64_t max_age_ms,
      const std::vector<std::string>& expected_nodes, bool check_fallback);
  static void record_metrics_probe(int64_t now_ms,
                                   std::map<std::string, uint32_t> delays,
                                   bool reported_live_to_ui = false);
  static void invalidate_metrics_probe();
  static bool mark_metrics_probe_reported_if_unreported();

  // 观测（测试断言）
  int api_fail_count() const { return api_fail_count_.load(); }
  int node_fail_count() const { return node_fail_count_.load(); }
  int auto_select_fail_count() const { return auto_select_fail_count_.load(); }
  int api_dead_heal_fail_count() const {
    return api_dead_heal_fail_count_.load();
  }
  int heal_fail_count() const { return auto_select_fail_count_.load(); }
  int64_t last_heal_ms() const { return last_heal_ms_.load(); }
  // ★ §5.1 切换宽限期：当前是否处于宽限期内（切节点后 20s）
  bool in_switch_grace() const;
  int64_t last_node_change_ms() const { return last_node_change_ms_.load(); }

  // 测试夹具：清空状态机计数/冷却/失败表（用例间隔离；生产勿调）
  void reset_state();

 private:
  CoreMonitor() = default;
  Action heal(const std::string& uid,
              SelfHealOrigin origin = SelfHealOrigin::ConfirmedDead,
              const std::string& dead_node = {});  // 自愈记账核心（tick 内调用）
  void absorb_pending_manual_switch(int64_t fallback_now_ms);
  void reset_runtime_loop_state();

  std::mutex m_;  // 保护 hooks_/params_ 配置
  Hooks hooks_;
  Params params_;

  std::atomic<bool> running_{false};
  std::atomic<bool> kick_{false};  // 非阻塞唤醒标志（kick() 置位，worker 消费）
  std::atomic<int> api_fail_count_{0};
  std::atomic<int> node_fail_count_{0};
  std::atomic<int> auto_select_fail_count_{0};
  std::atomic<int> api_dead_heal_fail_count_{0};
  std::atomic<bool> was_offline_{false};
  std::atomic<int64_t> last_heal_ms_{0};
  std::atomic<int64_t> last_tick_ms_{0};
  std::atomic<int64_t> last_https_verify_ms_{-1};
  std::atomic<uint64_t> manual_switch_epoch_{0};
  // ★ §5.1 切换宽限期：最近一次「活跃节点发生变化」的时刻（ms）
  //   ← Mini monitor.rs:1174 last_node_change_time
  //   ★ 用独立标志位而非「值 != 0」判别：测试虚拟时钟可能从 0 起算，
  //     0 既是合法时刻又是「从未切换」的哨兵 → 必须分开表达。
  std::atomic<int64_t> last_node_change_ms_{0};
  std::atomic<bool> has_node_change_{false};
  // ★ 上一次观测到的活跃节点名（用于判定「是否发生切换」）
  //   ← Mini monitor.rs:1173 last_active_node
  std::mutex last_node_m_;
  std::string last_active_node_;

  // BUG-336：前端手动切节点暂存真实点击时刻 + 非阻塞唤醒标志
  std::mutex manual_m_;
  int64_t pending_manual_switch_ms_{-1};
  std::atomic<bool> manual_switch_wake_{false};
  std::atomic<bool> manual_grace_unmatched_{false};

  // v3.5.0 S4：自愈切换速率窗口（120s 最小间隔，600s 内上限 3 次；仅记录实际切换）
  std::mutex switch_rate_m_;
  std::vector<int64_t> recent_auto_switches_ms_;

  // BUG-359：去重记录最近广播的网络健康状态
  mutable std::mutex health_m_;
  std::string last_notified_health_{"network_health::ok"};

  // 失败服务器表（server → 失败时刻 ms；600s 冷却）
  std::mutex failed_m_;
  std::map<std::string, int64_t> failed_servers_;

  std::thread worker_;
  std::condition_variable cv_;
};

}  // namespace clashwin::core
