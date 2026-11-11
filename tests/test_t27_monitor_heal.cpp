// §5 后台监控、健康探针与自愈体系 —— 三条铁律的 Win 侧落地验证（对齐 Clash Mini v3.2.4 / BUG-315）
//
// 覆盖（对照源 = ClashVerge src-tauri/src/module/monitor.rs）：
//   ① §5.1 切换宽限期 20s + 健康数据新鲜度 20s + 自愈冷却 25s（BUG-315）
//      （monitor.rs:63 HEALTH_DATA_FRESH_SECS = 20；
//        :74 SWITCH_GRACE_PERIOD_SECS = 20；
//        :1358-1370 节点变化检测；:1383-1396 宽限期内不计连续失败；
//        :1538-1542 自愈选点 25s 冷却保护）
//   ② §5.4 第 3 道铁锁：自愈决策层假节点拦截
//      （monitor.rs:663-666 evaluate_failover 的 is_dummy_node → ShouldFailover）
//   ③ §5.8 测速失真复核（主/备双地址）
//      （monitor.rs:103 FALLBACK_TEST_URL = https://www.gstatic.com/generate_204；
//        :188-223 probe_metrics_group 主备双拨测；
//        :685-703 主备均失败 → NoAction，不判节点死）
//   ④ §5.5 物理网络连通性双频探测（TCP:53 直连 223.5.5.5 / 119.29.29.29 / 1.1.1.1 + DNS 兜底）
//      （monitor.rs:271-308 / :1275-1293，v3.2.4 BUG-315）
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

// ---------- ① §5.1 切换宽限期 20s（v3.2.4 BUG-315） ----------

// 1) 切节点后 20s 内失败判定不计入连续失败次数（防乒乓震荡）
UT_TEST(t27_switch_grace_blocks_fail_count_within_20s) {
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

  // 宽限期外（+20s）→ 第 1 次失败计数，第 2 次达阈值 → 自愈
  f.clock += 20000;
  UT_CHECK(!m.in_switch_grace());
  m.tick_once();
  UT_CHECK_EQ(m.node_fail_count(), 1);
  m.tick_once();
  UT_CHECK_EQ(heals, 1);
}

// 2) v3.2.4 BUG-315 三大计时常量精确校验（新鲜度 20s / 切换宽限期 20s / 自愈冷却 25s）
UT_TEST(t27_v324_bug315_timing_constants_and_bounds) {
  MonitorFixture f;
  auto& m = f.m;
  UT_CHECK_EQ(CoreMonitor::kHealthDataFreshSecs, 20);
  UT_CHECK_EQ(CoreMonitor::kDeadConfirmMinIntervalSecs, 10);
  UT_CHECK_EQ(m.params().switch_grace_ms, 20000);
  UT_CHECK_EQ(m.params().heal_cooldown_ms, 25000);

  int heals = 0;
  CoreMonitor::Hooks h = f.base();
  h.active_node_name = [] { return std::string("Node-A"); };
  h.current_uid = [] { return std::string("U1"); };
  h.api_alive = [] { return false; };
  h.auto_select_trigger = [&]() -> Result<bool> {
    ++heals;
    return Result<bool>::ok(true);
  };
  m.set_hooks(h);
  m.reset_state();

  // 先验证 20s 切换宽限期边界：19.999s 仍在宽限期，20.000s 宽限期结束
  h.api_alive = [] { return true; };
  m.set_hooks(h);
  f.clock = 1000;
  m.tick_once();
  UT_CHECK(m.in_switch_grace());
  f.clock += 19999;
  UT_CHECK(m.in_switch_grace());   // 19.999s → 仍在宽限期
  f.clock += 1;
  UT_CHECK(!m.in_switch_grace());  // 20.000s → 宽限期结束

  // 再验证 25s 自愈冷却边界：24.999s 内跳过，25.000s 放行
  h.api_alive = [] { return false; };
  m.set_hooks(h);
  m.tick_once();
  m.tick_once();
  UT_CHECK(m.tick_once() == CoreMonitor::Action::Healed);
  UT_CHECK_EQ(heals, 1);

  f.clock += 24999;
  m.tick_once();
  m.tick_once();
  UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);  // 24.999s 仍在冷却
  UT_CHECK_EQ(heals, 1);

  f.clock += 1;  // 累计满 25000ms
  m.tick_once();
  m.tick_once();
  UT_CHECK(m.tick_once() == CoreMonitor::Action::Healed);  // 25.000s 放行自愈
  UT_CHECK_EQ(heals, 2);
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
  f.clock += 19000;
  UT_CHECK(m.in_switch_grace());

  // 切到 Node-B → 宽限期重新起算（不是沿用旧的）
  node = "Node-B";
  m.tick_once();
  UT_CHECK_EQ(m.last_node_change_ms(), 19000);
  f.clock += 19000;  // 距新切换 19s
  UT_CHECK(m.in_switch_grace());
  f.clock += 1000;   // 距新切换 20s
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

// ---------- ④ §5.5 物理网络连通性双频快速探测（v3.2.4 BUG-315） ----------

// 11) resolve_probe_target 覆盖 HTTPS/HTTP 默认端口、显式端口、IPv6 方括号剥除与异常回退
UT_TEST(t27_v324_resolve_probe_target_variants) {
  auto [h1, p1] =
      CoreMonitor::resolve_probe_target("https://cp.cloudflare.com/generate_204");
  UT_CHECK_EQ(h1, "cp.cloudflare.com");
  UT_CHECK_EQ(p1, 443);

  auto [h2, p2] =
      CoreMonitor::resolve_probe_target("http://www.gstatic.com/generate_204");
  UT_CHECK_EQ(h2, "www.gstatic.com");
  UT_CHECK_EQ(p2, 80);

  auto [h3, p3] =
      CoreMonitor::resolve_probe_target("https://1.2.3.4:8443/health?x=1");
  UT_CHECK_EQ(h3, "1.2.3.4");
  UT_CHECK_EQ(p3, 8443);

  auto [h4, p4] =
      CoreMonitor::resolve_probe_target("https://[2606:4700:4700::1111]:9443/p");
  UT_CHECK_EQ(h4, "2606:4700:4700::1111");
  UT_CHECK_EQ(p4, 9443);

  auto [h5, p5] = CoreMonitor::resolve_probe_target("not-a-valid-url");
  UT_CHECK_EQ(h5, "cp.cloudflare.com");
  UT_CHECK_EQ(p5, 443);
}

// 12) TCP:53 三路公网高可用 IP 顺序探测 + DNS 兜底 + 超时预算耗尽短路
UT_TEST(t27_v324_physical_network_tcp53_priority_and_dns_fallback) {
  // 场景 A：首跳 223.5.5.5:53 即通 → 短路返回 true，绝不触碰后续 IP 与 DNS
  {
    std::vector<std::string> attempted_ips;
    int dns_calls = 0;
    int64_t clock = 0;
    CoreMonitor::PhysicalNetProbeOps ops;
    ops.now_ms = [&] { return clock; };
    ops.tcp_connect = [&](const char* ip, uint16_t port, int) {
      UT_CHECK_EQ(port, 53);
      attempted_ips.emplace_back(ip);
      return std::string(ip) == "223.5.5.5";
    };
    ops.dns_lookup = [&](const std::string&, uint16_t, int) {
      ++dns_calls;
      return true;
    };
    UT_CHECK(CoreMonitor::probe_physical_network_with(
        2, "https://cp.cloudflare.com/generate_204", ops));
    UT_CHECK_EQ(attempted_ips.size(), 1);
    UT_CHECK_EQ(attempted_ips[0], "223.5.5.5");
    UT_CHECK_EQ(dns_calls, 0);
  }

  // 场景 B：前两跳不通、第 3 跳 1.1.1.1:53 通 → 返回 true，仍不触发 DNS（打破 TUN DNS 死锁）
  {
    std::vector<std::string> attempted_ips;
    int dns_calls = 0;
    int64_t clock = 0;
    CoreMonitor::PhysicalNetProbeOps ops;
    ops.now_ms = [&] { return clock; };
    ops.tcp_connect = [&](const char* ip, uint16_t, int) {
      attempted_ips.emplace_back(ip);
      clock += 20;
      return std::string(ip) == "1.1.1.1";
    };
    ops.dns_lookup = [&](const std::string&, uint16_t, int) {
      ++dns_calls;
      return false;
    };
    UT_CHECK(CoreMonitor::probe_physical_network_with(
        2, "https://cp.cloudflare.com/generate_204", ops));
    UT_CHECK_EQ(attempted_ips.size(), 3);
    UT_CHECK_EQ(attempted_ips[0], "223.5.5.5");
    UT_CHECK_EQ(attempted_ips[1], "119.29.29.29");
    UT_CHECK_EQ(attempted_ips[2], "1.1.1.1");
    UT_CHECK_EQ(dns_calls, 0);
  }

  // 场景 C：局域网封禁出站 53 端口（3 路 TCP:53 均失败）→ 回退 test_url DNS 兜底
  {
    std::string dns_host;
    uint16_t dns_port = 0;
    int64_t clock = 0;
    CoreMonitor::PhysicalNetProbeOps ops;
    ops.now_ms = [&] { return clock; };
    ops.tcp_connect = [&](const char*, uint16_t, int) {
      clock += 50;
      return false;
    };
    ops.dns_lookup = [&](const std::string& host, uint16_t port, int rem_ms) {
      dns_host = host;
      dns_port = port;
      UT_CHECK(rem_ms > 0);
      return true;
    };
    UT_CHECK(CoreMonitor::probe_physical_network_with(
        2, "https://www.gstatic.com:8443/generate_204", ops));
    UT_CHECK_EQ(dns_host, "www.gstatic.com");
    UT_CHECK_EQ(dns_port, 8443);
  }

  // 场景 D：总超时预算耗尽 → 不再发起后续探测，直接返回 false
  {
    int tcp_calls = 0;
    int dns_calls = 0;
    int64_t clock = 0;
    CoreMonitor::PhysicalNetProbeOps ops;
    ops.now_ms = [&] { return clock; };
    ops.tcp_connect = [&](const char*, uint16_t, int) {
      ++tcp_calls;
      clock += 2000;  // 首跳即耗尽 2s 预算
      return false;
    };
    ops.dns_lookup = [&](const std::string&, uint16_t, int) {
      ++dns_calls;
      return true;
    };
    UT_CHECK(!CoreMonitor::probe_physical_network_with(
        2, "https://cp.cloudflare.com/generate_204", ops));
    UT_CHECK_EQ(tcp_calls, 1);
    UT_CHECK_EQ(dns_calls, 0);
  }
}

// 13) 双频探测节流器：在线 60s 复用缓存 / 离线 5s 快速复探
UT_TEST(t27_v324_throttled_net_probe_dual_frequency) {
  CoreMonitor::ThrottledNetProbe throttle;
  int probes = 0;
  bool live_state = true;
  auto probe_fn = [&] {
    ++probes;
    return live_state;
  };

  // 首次调用必然真实探测
  UT_CHECK(throttle.check(1000, probe_fn));
  UT_CHECK_EQ(probes, 1);

  // 在线态：59.999s 内复用缓存，不重复发起探测
  live_state = false;
  UT_CHECK(throttle.check(60999, probe_fn));
  UT_CHECK_EQ(probes, 1);

  // 满 60.000s：重新探测，感知离线
  UT_CHECK(!throttle.check(61000, probe_fn));
  UT_CHECK_EQ(probes, 2);

  // 离线态：4.999s 内复用离线结果，满 5.000s 立即复探感知恢复在线
  live_state = true;
  UT_CHECK(!throttle.check(65999, probe_fn));
  UT_CHECK_EQ(probes, 2);
  UT_CHECK(throttle.check(66000, probe_fn));
  UT_CHECK_EQ(probes, 3);
}
