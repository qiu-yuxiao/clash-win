// §5 后台监控、健康探针与自愈体系 —— 三条铁律的 Win 侧落地验证
//
// 覆盖（对照源 = ClashVerge src-tauri/src/module/monitor.rs）：
//   ① §5.1 切换宽限期 60s
//      （monitor.rs:74 SWITCH_GRACE_PERIOD_SECS = 60；
//        :1358-1370 节点变化检测；:1383-1396 宽限期内不计连续失败）
//   ② §5.4 第 3 道铁锁：自愈决策层假节点拦截
//      （monitor.rs:663-666 evaluate_failover 的 is_dummy_node → ShouldFailover）
//   ③ §5.8 测速失真复核（主/备双地址）
//      （monitor.rs:103 FALLBACK_TEST_URL = https://www.gstatic.com/generate_204；
//        :188-223 probe_metrics_group 主备双拨测；
//        :685-703 主备均失败 → NoAction，不判节点死）
//
// 【测试铁律】全部走 hooks 注入 + tick_once 单步驱动，不触真线程/真内核。
#include "core/monitor.h"

#include <string>

#include "test_util.h"
#include "util/result.h"

using namespace clashwin::core;
using clashwin::Result;

namespace {

// 隔离夹具：装钩子 → reset_state → 析构时复位（避免污染其他用例）
struct MonitorFixture {
  CoreMonitor& m = CoreMonitor::instance();
  int64_t clock = 0;

  ~MonitorFixture() {
    m.set_hooks({});
    m.reset_state();
  }

  // 一套「一切正常」的基础钩子
  CoreMonitor::Hooks base() {
    CoreMonitor::Hooks h;
    h.core_alive = [] { return true; };
    h.network_online = [] { return true; };
    h.api_alive = [] { return true; };
    h.now_ms = [this] { return clock; };
    return h;
  }
};

}  // namespace

// ---------- ① §5.1 切换宽限期 60s ----------

// 1) 切节点后 60s 内失败判定不计入连续失败次数（防乒乓震荡）
UT_TEST(t27_switch_grace_blocks_fail_count_within_60s) {
  MonitorFixture f;
  auto& m = f.m;
  int heals = 0;
  CoreMonitor::Hooks h = f.base();
  h.current_uid = [] { return std::string("U1"); };
  h.node_healthy = [] { return false; };  // 活跃节点判定为死
  h.active_node_name = [] { return std::string("Node-A"); };
  h.auto_select_trigger = [&]() -> Result<bool> {
    ++heals;
    return Result<bool>::ok(true);
  };
  m.set_hooks(h);
  m.reset_state();

  // 第 1 次 tick：检测到活跃节点（首次观测）→ 记为切换 → 起算宽限期
  m.tick_once();
  UT_CHECK(m.in_switch_grace());
  UT_CHECK_EQ(m.node_fail_count(), 0);

  // 宽限期内连续 3 次失败判定 → 都不计入连续失败次数，更不触发自愈
  m.tick_once();
  m.tick_once();
  m.tick_once();
  UT_CHECK_EQ(m.node_fail_count(), 0);
  UT_CHECK_EQ(heals, 0);

  // 宽限期外（+60s）→ 第 1 次失败计数，第 2 次达阈值 → 自愈
  f.clock += 60000;
  UT_CHECK(!m.in_switch_grace());
  m.tick_once();
  UT_CHECK_EQ(m.node_fail_count(), 1);
  m.tick_once();
  UT_CHECK_EQ(heals, 1);
}

// 2) 宽限期时长常量 = 60s（← monitor.rs:74）
UT_TEST(t27_switch_grace_duration_is_60s) {
  MonitorFixture f;
  auto& m = f.m;
  CoreMonitor::Hooks h = f.base();
  h.active_node_name = [] { return std::string("Node-A"); };
  m.set_hooks(h);
  m.reset_state();
  UT_CHECK_EQ(m.params().switch_grace_ms, 60000);

  m.tick_once();
  UT_CHECK(m.in_switch_grace());
  f.clock += 59999;
  UT_CHECK(m.in_switch_grace());   // 59.999s → 仍在宽限期
  f.clock += 1;
  UT_CHECK(!m.in_switch_grace());  // 60.000s → 宽限期结束
}

// 3) 活跃节点发生变化 → 重新起算宽限期并清零连续失败计数
//    （← monitor.rs:1358-1370）
UT_TEST(t27_switch_grace_restarts_on_node_change) {
  MonitorFixture f;
  auto& m = f.m;
  std::string node = "Node-A";
  CoreMonitor::Hooks h = f.base();
  h.active_node_name = [&] { return node; };
  h.node_healthy = [] { return false; };
  m.set_hooks(h);
  m.reset_state();

  m.tick_once();
  UT_CHECK(m.in_switch_grace());
  f.clock += 59000;
  UT_CHECK(m.in_switch_grace());

  // 切到 Node-B → 宽限期重新起算（不是沿用旧的）
  node = "Node-B";
  m.tick_once();
  UT_CHECK_EQ(m.last_node_change_ms(), 59000);
  f.clock += 59000;  // 距新切换 59s
  UT_CHECK(m.in_switch_grace());
  f.clock += 1000;   // 距新切换 60s
  UT_CHECK(!m.in_switch_grace());
}

// 4) 节点未变化 → 宽限期不重新起算（否则宽限期会被无限延长）
UT_TEST(t27_switch_grace_not_restarted_without_node_change) {
  MonitorFixture f;
  auto& m = f.m;
  CoreMonitor::Hooks h = f.base();
  h.active_node_name = [] { return std::string("Node-A"); };
  m.set_hooks(h);
  m.reset_state();

  m.tick_once();
  const int64_t first = m.last_node_change_ms();
  f.clock += 10000;
  m.tick_once();
  UT_CHECK_EQ(m.last_node_change_ms(), first);  // 未变 → 不重新起算
}

// ---------- ② §5.4 第 3 道铁锁：假节点拦截 ----------

// 5) 活跃节点是伪节点 → 直接触发重选（不需等连续 2 次失败）
UT_TEST(t27_dummy_active_node_triggers_heal_immediately) {
  MonitorFixture f;
  auto& m = f.m;
  int heals = 0;
  CoreMonitor::Hooks h = f.base();
  h.current_uid = [] { return std::string("U1"); };
  h.node_healthy = [] { return true; };  // 「健康」但其实是伪节点
  h.is_dummy_active = [] { return true; };
  h.active_node_name = [] { return std::string("【剩余流量】80GB"); };
  h.auto_select_trigger = [&]() -> Result<bool> {
    ++heals;
    return Result<bool>::ok(true);
  };
  m.set_hooks(h);
  m.reset_state();

  m.tick_once();
  UT_CHECK_EQ(heals, 1);  // 首次 tick 即自愈（不需 2 次）
}

// 6) 非伪节点 → 不触发伪节点路径（走常规判定）
UT_TEST(t27_non_dummy_active_node_not_healed_by_dummy_path) {
  MonitorFixture f;
  auto& m = f.m;
  int heals = 0;
  CoreMonitor::Hooks h = f.base();
  h.current_uid = [] { return std::string("U1"); };
  h.node_healthy = [] { return true; };
  h.is_dummy_active = [] { return false; };
  h.auto_select_trigger = [&]() -> Result<bool> {
    ++heals;
    return Result<bool>::ok(true);
  };
  m.set_hooks(h);
  m.reset_state();

  m.tick_once();
  UT_CHECK_EQ(heals, 0);
}

// 7) 伪节点路径不受切换宽限期保护（伪节点属明确异常，须立即处置）
UT_TEST(t27_dummy_node_bypasses_switch_grace) {
  MonitorFixture f;
  auto& m = f.m;
  int heals = 0;
  CoreMonitor::Hooks h = f.base();
  h.current_uid = [] { return std::string("U1"); };
  h.node_healthy = [] { return false; };
  h.is_dummy_active = [] { return true; };
  h.active_node_name = [] { return std::string("官网"); };
  h.auto_select_trigger = [&]() -> Result<bool> {
    ++heals;
    return Result<bool>::ok(true);
  };
  m.set_hooks(h);
  m.reset_state();

  m.tick_once();
  UT_CHECK_EQ(heals, 1);  // 宽限期内也立即自愈
}

// ---------- ③ §5.8 测速失真复核 ----------

// 8) 测速链路不可用（主备均失败）→ 本轮不判节点死、不触发自愈
UT_TEST(t27_probe_link_unavailable_blocks_failover) {
  MonitorFixture f;
  auto& m = f.m;
  int heals = 0;
  CoreMonitor::Hooks h = f.base();
  h.current_uid = [] { return std::string("U1"); };
  h.node_healthy = [] { return false; };       // 看起来死了
  h.probe_metrics_with_fallback = [] { return false; };  // 但测速链路不可用
  h.auto_select_trigger = [&]() -> Result<bool> {
    ++heals;
    return Result<bool>::ok(true);
  };
  m.set_hooks(h);
  m.reset_state();

  m.tick_once();
  m.tick_once();
  m.tick_once();
  UT_CHECK_EQ(heals, 0);          // 不得触发自愈
  UT_CHECK_EQ(m.node_fail_count(), 0);  // 不得计入连续失败
}

// 9) 测速链路可用 → 正常走连续 2 次判死 → 自愈
UT_TEST(t27_probe_link_available_allows_failover) {
  MonitorFixture f;
  auto& m = f.m;
  int heals = 0;
  CoreMonitor::Hooks h = f.base();
  h.current_uid = [] { return std::string("U1"); };
  h.node_healthy = [] { return false; };
  h.probe_metrics_with_fallback = [] { return true; };
  h.auto_select_trigger = [&]() -> Result<bool> {
    ++heals;
    return Result<bool>::ok(true);
  };
  m.set_hooks(h);
  m.reset_state();

  m.tick_once();
  UT_CHECK_EQ(m.node_fail_count(), 1);
  UT_CHECK_EQ(heals, 0);
  m.tick_once();
  UT_CHECK_EQ(heals, 1);
}

// 10) 测速链路不可用 → 本轮清零失败计数；下一轮链路恢复后重新累计
UT_TEST(t27_probe_link_recovery_resets_fail_count) {
  MonitorFixture f;
  auto& m = f.m;
  bool link_ok = false;
  CoreMonitor::Hooks h = f.base();
  h.current_uid = [] { return std::string("U1"); };
  h.node_healthy = [] { return false; };
  h.probe_metrics_with_fallback = [&] { return link_ok; };
  h.auto_select_trigger = []() -> Result<bool> { return Result<bool>::ok(true); };
  m.set_hooks(h);
  m.reset_state();

  m.tick_once();  // 链路不可用 → 计数清零
  UT_CHECK_EQ(m.node_fail_count(), 0);

  link_ok = true;
  m.tick_once();  // 链路恢复 → 第 1 次失败
  UT_CHECK_EQ(m.node_fail_count(), 1);
  m.tick_once();  // 第 2 次 → 达阈值
  UT_CHECK(m.node_fail_count() == 0);  // 触发自愈后清零
}
