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
//      - 自愈 25s 冷却（v3.2.4 BUG-315）；自愈连续失败 3 次 → 内核 API 不可达则
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
#include "core/monitor.h"

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

// ---------- ② 内核守护线程（← T22 已正式归入 core/monitor.h） ----------
using CoreMonitor = clashwin::core::CoreMonitor;

}  // namespace clashwin::system
