#pragma once
// T18 代理守护（两件套）：
//
// ① SysproxyGuard（← sysproxy-rs GuardMonitor，sysopt.rs:49 Duration 30s）：
//    系统代理防篡改——开启代理守护后周期性重写注册表（手动改注册表
//    30s 内被复原，← T18 完成标准⑤）。GuardType 三态对照原版：
//    None / Sysproxy / Autoproxy（PAC）。
//
// ② CoreMonitor（← module/monitor.rs 后台监测线程语义子集）：
//    内核守护线程。间隔/判死/自愈参数对照 census §70：
//      - 体检间隔 15s（重试 3s / 物理网络离线 5s）
//      - 节点连续 2 次判死 → 自愈选点（接 T16 AutoSelect::trigger）
//      - 自愈 60s 冷却；自愈连续失败 3 次 → 内核 API 不可达则
//        restart_core()；restart 失败 → 弹 MessageBoxW 警报
//    【与原版差异声明】原版无进程句柄 wait（无 Job Object，census §122），
//    进程死靠「API 连续 3 次不可达」间接判死。本实现按 T18 任务卡补充
//    进程存活直检（sidecar_alive）——进程死 → 立即 restart_core（静默
//    崩溃日志），比原版更快恢复；节流复用 60s 自愈冷却，不加指数退避
//    （原版语义：API 3 次计数即天然退避）。
//
//    失败服务器表：record_server_failure / snapshot_failed_servers
//    （600s 冷却）——接 T16 auto_select.h:48 failed_servers 钩子（快照源
//    原 T16 注释归 T19，按 T18 任务卡改归本模块）。
//
//    wait_for_clash_ready：T16 auto_select.cpp:122 留位（skip_wait_ready）
//    转正——四阶段就绪等待（API→组列表→物理网络），budget 用尽返回当前态。
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
#include "system/sysproxy.h"

namespace clashwin::system {

// ---------- ① 系统代理防篡改 ----------
enum class GuardType { None, Sysproxy, Autoproxy };

class SysproxyGuard {
 public:
  static SysproxyGuard& instance();

  // ← sysopt.rs:111 set_interval（proxy_guard_duration.unwrap_or(30)）
  void set_interval(std::chrono::seconds s);
  uint64_t interval_secs() const;

  // ← sysopt.rs:181 set_guard_type（update_sysproxy 产出状态转达）
  void set_guard_type(GuardType t, const SysProxyState& sys = {},
                      const AutoProxyState& auto_proxy = {});
  GuardType guard_type() const;

  // ← sysopt.rs:113-117 start / :93,98 stop
  void start();
  void stop();
  bool running() const { return running_.load(); }

  // 重写回调注入（测试 mock；生产 = 经 Sysopt::access() 写注册表）
  void set_rewrite(std::function<void(GuardType, const SysProxyState&,
                                      const AutoProxyState&)>
                       fn);
  // 单步驱动（测试用；生产线程内部每 interval 调一次）
  void tick_once();

 private:
  SysproxyGuard() = default;
  void ensure_thread();
  void worker_loop();

  mutable std::mutex m_;
  std::condition_variable cv_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> interval_secs_{30};  // ← GuardMonitor::new(30s)
  GuardType type_ = GuardType::None;
  SysProxyState sys_;
  AutoProxyState auto_;
  std::function<void(GuardType, const SysProxyState&, const AutoProxyState&)>
      rewrite_;
};

// ---------- ② 内核守护线程 ----------
class CoreMonitor {
 public:
  // 参数对照表（← census §70 / monitor.rs，全 hooks 注入可测）
  struct Params {
    int normal_interval_secs = 15;   // 正常体检
    int retry_interval_secs = 3;     // 判死后重试
    int offline_interval_secs = 5;   // 物理网络离线
    int node_dead_threshold = 2;     // 节点连续 2 次判死
    int api_fail_threshold = 3;      // API 连续 3 次不可达
    int heal_fail_threshold = 3;     // 自愈连续失败 3 次
    int64_t heal_cooldown_ms = 60000;  // 自愈 60s 冷却
    int64_t failed_server_cooldown_ms = 600000;  // 失败服务器 600s
  };

  struct Hooks {
    // 内核 API 存活（生产 = get_base_config 3s 超时；mock）
    std::function<bool()> api_alive;
    // 内核进程存活（生产 = CoreManager::sidecar_alive）
    std::function<bool()> core_alive;
    // 内核重启（生产 = CoreManager restart_with_restore）
    std::function<Result<void>()> restart_core;
    // 物理网络在线（生产 = DNS/接口探测）
    std::function<bool()> network_online;
    // 节点健康（生产 = T16 延迟缓存/测量组轻量判定；true=健康）
    std::function<bool()> node_healthy;
    // 自愈选点触发（生产 = AutoSelect::instance().trigger(...)）
    // 返回值：true=已触发/成功；false=失败；"busy" 单列（见 HealResult）
    std::function<Result<bool>()> auto_select_trigger;
    // 警报（生产 = MessageBoxW；测试 = 注入记录）
    std::function<void(const std::wstring& title, const std::wstring& text)>
        alert;
    // 当前激活 profile uid（自愈选点入参；空 = 无法自愈）
    std::function<std::string()> current_uid;
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

  // 状态机单步（测试驱动；生产线程内部调用）
  // 返回本步动作（观测用）
  enum class Action {
    Idle,            // 离线跳过 / 一切正常
    Restarted,       // 进程死 → restart_core
    Healed,          // 自愈选点触发
    RestartedOnHealFail,  // 自愈连续失败 → API 不可达 → restart_core
    Alerted,         // restart 失败 → 警报
  };
  Action tick_once();

  // ---- 失败服务器表（← monitor.rs 节点失败冷却表；T16 避让接活） ----
  void record_server_failure(const std::string& server);
  // 快照：剔除冷却期内（600s）失败服务器
  std::vector<std::string> snapshot_failed_servers();
  void clear_server_failures();

  // ---- wait_for_clash_ready（T16 留位转正） ----
  // 四阶段简化：API 就绪（3s 探测轮询，budget 30s）→ 组列表就绪
  // （budget 20s）→ 物理网络（budget 15s）。budget 用尽返回 false。
  // 每阶段探测经 hooks；任一 hooks 缺失该阶段视为立即通过。
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

  // 测试夹具：清空状态机计数/冷却/失败表（用例间隔离；生产勿调）
  void reset_state();

 private:
  CoreMonitor() = default;
  Action heal(const std::string& uid);  // 自愈记账核心（tick 内调用）

  std::mutex m_;  // 保护 hooks_/params_ 配置
  Hooks hooks_;
  Params params_;

  std::atomic<bool> running_{false};
  std::atomic<int> api_fail_count_{0};
  std::atomic<int> node_fail_count_{0};
  std::atomic<int> heal_fail_count_{0};
  std::atomic<bool> was_offline_{false};
  std::atomic<int64_t> last_heal_ms_{0};
  std::atomic<int64_t> last_tick_ms_{0};

  // 失败服务器表（server → 失败时刻 ms；600s 冷却）
  std::mutex failed_m_;
  std::map<std::string, int64_t> failed_servers_;

  std::thread worker_;
  std::condition_variable cv_;
};

}  // namespace clashwin::system
