#pragma once
// T22 内核守护（CoreMonitor）：
// 对照源：ClashVerge src-tauri/src/module/monitor.rs
//
// 间隔/判死/自愈/升级参数：
//   - 体检间隔 15s（重试 3s / 物理网络离线 5s）
//   - 活跃节点连续 2 次判死 → 自愈选点（AutoSelect::trigger）
//   - 内核 API 连续 3 次不可达 → 自愈选点
//   - 自愈 60s 冷却；BUSY 冲突不计失败、不惩罚冷却
//   - 自愈连续失败 3 次：API 不可达 → restart_core；restart 失败 → 弹窗警报
//   - 自愈连续失败 5 次 → MessageBoxW 弹窗警报（fire_self_heal_alert）
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
#include <vector>

#include "util/result.h"

namespace clashwin::core {

class CoreMonitor {
 public:
  // 参数对照表（← census §70 / monitor.rs，全 hooks 注入可测）
  struct Params {
    int normal_interval_secs = 15;     // 正常体检 15s
    int retry_interval_secs = 3;       // 判死后重试 3s
    int offline_interval_secs = 5;     // 物理网络离线 5s
    int node_dead_threshold = 2;       // 节点连续 2 次判死
    int api_fail_threshold = 3;        // API 连续 3 次不可达
    int heal_fail_threshold = 3;       // 自愈连续失败 3 次
    int alert_fail_threshold = 5;      // 自愈连续失败 5 次
    int64_t heal_cooldown_ms = 60000;  // 自愈 60s 冷却
    int64_t failed_server_cooldown_ms = 600000;  // 失败服务器 600s
    // ★ §5.1 切换宽限期（← monitor.rs:74 SWITCH_GRACE_PERIOD_SECS = 60）
    //   切换后 60s 内的失败判定只记日志、不计入连续失败次数
    //   （防「切到尚未拨测的新节点 → 缓存陈旧误判 → 再次切走」的乒乓震荡）
    int64_t switch_grace_ms = 60000;
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

  // ---- wait_for_clash_ready（T16 留位转正） ----
  struct ReadyHooks {
    std::function<bool()> api_alive;
    std::function<bool()> group_ready;
    std::function<bool()> network_online;
    std::function<int64_t()> now_ms;
    std::function<void(int)> sleep_ms;  // 测试注入假 sleep
  };
  static bool wait_for_clash_ready(const ReadyHooks& h);

  // 观测（测试断言）
  int api_fail_count() const { return api_fail_count_.load(); }
  int node_fail_count() const { return node_fail_count_.load(); }
  int heal_fail_count() const { return heal_fail_count_.load(); }
  int64_t last_heal_ms() const { return last_heal_ms_.load(); }
  // ★ §5.1 切换宽限期：当前是否处于宽限期内（切节点后 60s）
  bool in_switch_grace() const;
  int64_t last_node_change_ms() const { return last_node_change_ms_.load(); }

  // 测试夹具：清空状态机计数/冷却/失败表（用例间隔离；生产勿调）
  void reset_state();

 private:
  CoreMonitor() = default;
  Action heal(const std::string& uid);  // 自愈记账核心（tick 内调用）

  std::mutex m_;  // 保护 hooks_/params_ 配置
  Hooks hooks_;
  Params params_;

  std::atomic<bool> running_{false};
  std::atomic<bool> kick_{false};  // 非阻塞唤醒标志（kick() 置位，worker 消费）
  std::atomic<int> api_fail_count_{0};
  std::atomic<int> node_fail_count_{0};
  std::atomic<int> heal_fail_count_{0};
  std::atomic<bool> was_offline_{false};
  std::atomic<int64_t> last_heal_ms_{0};
  std::atomic<int64_t> last_tick_ms_{0};
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

  // 失败服务器表（server → 失败时刻 ms；600s 冷却）
  std::mutex failed_m_;
  std::map<std::string, int64_t> failed_servers_;

  std::thread worker_;
  std::condition_variable cv_;
};

}  // namespace clashwin::core
