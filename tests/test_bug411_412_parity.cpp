// Clash Win — Clash Mini v3.5.10 (BUG-411 & BUG-412) 1:1 对齐专项单测：
// 1. BUG-411：is_pipe_missing_os_error / is_named_pipe_server_missing / is_local_socket_missing
// 2. BUG-411：CoreManager::should_expect_core_pipe_alive & ScopedConfigUpdate
// 3. BUG-411：CoreMonitor 缺管 500ms 二次确认秒级快重启、25s 冷却、稳态归零与 ApiChannelPrecheck 免等
// 4. BUG-412：600s 间歇黑洞滑窗逃生、单目标 >=3 同伴全阶段免备用 URL 复核、dead_node 死链精准清理
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/connection_stale.h"
#include "core/core_manager.h"
#include "core/monitor.h"
#include "mihomo/mihomo_client.h"
#include "mihomo/pipe_http.h"
#include "mock_pipe_server.h"
#include "test_util.h"

using namespace clashwin;
using namespace clashwin::core;
using namespace clashwin::mihomo;

namespace {

struct MonitorGuard {
  MonitorGuard() { CoreMonitor::instance().reset_state(); }
  ~MonitorGuard() {
    auto& m = CoreMonitor::instance();
    m.stop();
    m.set_hooks({});
    m.set_params({});
    m.reset_state();
  }
};

std::wstring unique_pipe_411(const wchar_t* tag) {
  static std::atomic<int> seq{0};
  wchar_t buf[128];
  swprintf_s(buf, L"\\\\.\\pipe\\clashwin-bug411-%s-%lu-%d", tag,
             ::GetCurrentProcessId(), seq.fetch_add(1));
  return buf;
}

}  // namespace

UT_TEST(bug411_named_pipe_missing_distinguishes_missing_ready_and_busy) {
  UT_CHECK(is_pipe_missing_os_error(ERROR_FILE_NOT_FOUND));
  UT_CHECK(is_pipe_missing_os_error(ERROR_PATH_NOT_FOUND));
  UT_CHECK(is_pipe_missing_os_error(ERROR_BAD_PATHNAME));
  UT_CHECK(!is_pipe_missing_os_error(ERROR_SEM_TIMEOUT));
  UT_CHECK(!is_pipe_missing_os_error(ERROR_PIPE_BUSY));
  UT_CHECK(!is_pipe_missing_os_error(std::nullopt));
  UT_CHECK(!is_named_pipe_server_missing(L""));
  UT_CHECK(!is_named_pipe_server_missing(L"   "));
  UT_CHECK(is_named_pipe_server_missing(L"clashwin-bug411-bare-missing"));

  const std::wstring pipe_path = unique_pipe_411(L"probe");
  MihomoClient client(pipe_path);

  // 1. 服务端创建前：管道不存在 -> true
  UT_CHECK(is_named_pipe_server_missing(pipe_path));
  UT_CHECK(client.is_local_socket_missing());

  // 2. 创建单实例服务端：空闲就绪 -> false
  HANDLE server = ::CreateNamedPipeW(
      pipe_path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0,
      nullptr);
  UT_CHECK(server != INVALID_HANDLE_VALUE);
  UT_CHECK(!is_named_pipe_server_missing(pipe_path));
  UT_CHECK(!client.is_local_socket_missing());

  // 3. 唯一实例已被客户端占用（处于繁忙态 ERROR_SEM_TIMEOUT）-> 仍判存在 (false)
  HANDLE occupier = ::CreateFileW(pipe_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                  0, nullptr, OPEN_EXISTING, 0, nullptr);
  UT_CHECK(occupier != INVALID_HANDLE_VALUE);
  UT_CHECK(!is_named_pipe_server_missing(pipe_path));
  UT_CHECK(!client.is_local_socket_missing());

  ::CloseHandle(occupier);
  ::CloseHandle(server);

  // 4. 服务端与客户端均销毁后 -> 恢复判定为不存在 (true)
  UT_CHECK(is_named_pipe_server_missing(pipe_path));
  UT_CHECK(client.is_local_socket_missing());
}

UT_TEST(bug411_core_manager_should_expect_core_pipe_alive_gates) {
  CoreManager mgr;
  // NotRunning 态 -> false
  UT_CHECK(!mgr.should_expect_core_pipe_alive());

  bool exiting = false;
  mgr.set_is_exiting_check([&] { return exiting; });

  bool saw_expect_false_inside_lifecycle = false;
  CoreManager::StartAutoTestHooks hooks;
  hooks.is_admin_fn = [] { return false; };
  hooks.start_sidecar_fn = [&](const std::wstring&, const std::wstring&) {
    saw_expect_false_inside_lifecycle = !mgr.should_expect_core_pipe_alive();
    return Result<void>::ok();
  };
  mgr.set_start_auto_test_hooks(hooks);

  UT_CHECK(mgr.start_auto(L"dummy.yaml").is_ok());
  UT_CHECK(mgr.running_mode() == RunningMode::Sidecar);
  UT_CHECK(saw_expect_false_inside_lifecycle);
  UT_CHECK(mgr.should_expect_core_pipe_alive());

  // 配置更新进行中 -> false，退出守卫后恢复 true
  {
    CoreManager::ScopedConfigUpdate cfg_guard(mgr);
    UT_CHECK(mgr.is_config_update_in_progress());
    UT_CHECK(!mgr.should_expect_core_pipe_alive());
  }
  UT_CHECK(!mgr.is_config_update_in_progress());
  UT_CHECK(mgr.should_expect_core_pipe_alive());

  // exiting 态 -> false
  exiting = true;
  UT_CHECK(!mgr.should_expect_core_pipe_alive());
  exiting = false;
  UT_CHECK(mgr.should_expect_core_pipe_alive());

  // 生命周期锁被占用（含同线程及跨线程）-> false，释放后恢复 true
  {
    CoreManager::ScopedLifecycleLockForTest lock_guard(mgr);
    UT_CHECK(!mgr.should_expect_core_pipe_alive());
    bool cross_thread_expect = true;
    std::thread probe_t([&] {
      cross_thread_expect = mgr.should_expect_core_pipe_alive();
    });
    probe_t.join();
    UT_CHECK(!cross_thread_expect);
  }
  UT_CHECK(mgr.should_expect_core_pipe_alive());

  // apply_config 期间自动置位 ScopedConfigUpdate
  struct ProbeConfigApi : ICoreApiClient {
    CoreManager& m;
    bool saw_config_updating = false;
    bool saw_expect_false = false;
    explicit ProbeConfigApi(CoreManager& mgr_ref) : m(mgr_ref) {}
    Result<BaseConfig> get_base_config() override {
      return Result<BaseConfig>::ok(BaseConfig{});
    }
    Result<ProxyItem> get_group_by_name(const std::string& name) override {
      ProxyItem g;
      g.name = name;
      g.now = std::string("JP-1");
      g.all = std::vector<std::string>{"JP-1"};
      return Result<ProxyItem>::ok(g);
    }
    Result<void> select_node_for_group(const std::string&,
                                       const std::string&) override {
      return Result<void>::ok();
    }
    Result<void> reload_config(bool, const std::string&) override {
      saw_config_updating = m.is_config_update_in_progress();
      saw_expect_false = !m.should_expect_core_pipe_alive();
      return Result<void>::ok();
    }
  } api(mgr);
  CoreOps noop_ops;
  noop_ops.start = [] { return Result<void>::ok(); };
  noop_ops.stop = [] { return Result<void>::ok(); };
  RestoreTimings fast;
  fast.api_ready_budget_ms = 20;
  fast.api_ready_poll_ms = 1;
  fast.group_fill_budget_ms = 20;
  fast.group_fill_poll_ms = 1;
  fast.select_budget_ms = 20;
  fast.select_retry_ms = 1;
  UT_CHECK(mgr.apply_config(api, "dummy.yaml", noop_ops, fast).is_ok());
  UT_CHECK(api.saw_config_updating);
  UT_CHECK(api.saw_expect_false);
  UT_CHECK(!mgr.is_config_update_in_progress());
  UT_CHECK(mgr.should_expect_core_pipe_alive());

  mgr.stop();
  UT_CHECK(!mgr.should_expect_core_pipe_alive());
}

UT_TEST(bug411_monitor_fast_restart_api_precheck_and_steady_reset) {
  // 1. 纯门控判定 can_fast_restart_missing_pipe
  UT_CHECK(!CoreMonitor::can_fast_restart_missing_pipe(false, true, std::nullopt));
  UT_CHECK(!CoreMonitor::can_fast_restart_missing_pipe(true, false, std::nullopt));
  UT_CHECK(CoreMonitor::can_fast_restart_missing_pipe(true, true, std::nullopt));
  UT_CHECK(!CoreMonitor::can_fast_restart_missing_pipe(
      true, true, CoreMonitor::kSelfHealCooldownSecs - 1));
  UT_CHECK(CoreMonitor::can_fast_restart_missing_pipe(
      true, true, CoreMonitor::kSelfHealCooldownSecs));

  // 2. 缺管 500ms 二次确认：期间管道恢复则取消重启；确失则第 1 拍即重启 + 25s 冷却 + 5 次升级告警
  {
    MonitorGuard mg;
    auto& m = CoreMonitor::instance();
    int64_t clock = 10000;
    bool pipe_missing = true;
    bool expect_alive = true;
    bool restore_pipe_during_delay = false;
    bool restart_ok = true;
    int restarts = 0;
    int alerts = 0;
    std::vector<uint32_t> sleep_calls;

    CoreMonitor::Hooks h;
    h.core_alive = [] { return true; };
    h.network_online = [] { return true; };
    h.api_alive = [] { return false; };
    h.pipe_missing = [&] { return pipe_missing; };
    h.expect_core_pipe_alive = [&] { return expect_alive; };
    h.sleep_ms = [&](uint32_t ms) {
      sleep_calls.push_back(ms);
      clock += static_cast<int64_t>(ms);
      if (restore_pipe_during_delay) pipe_missing = false;
    };
    h.restart_core = [&] {
      ++restarts;
      return restart_ok ? Result<void>::ok()
                        : Result<void>::err(Error{-1, "restart failed"});
    };
    h.alert = [&](const std::wstring&, const std::wstring&) { ++alerts; };
    h.now_ms = [&] { return clock; };
    m.set_hooks(h);

    // 二次确认期间管道恢复（热重启完成）-> 取消重启
    restore_pipe_during_delay = true;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    UT_CHECK_EQ(sleep_calls.size(), size_t(1));
    UT_CHECK_EQ(sleep_calls[0], CoreMonitor::kPipeMissingConfirmDelayMs);
    UT_CHECK_EQ(restarts, 0);
    UT_CHECK_EQ(m.api_fail_count(), 1);  // 未走快重启则正常计入一次 API 异常

    // 管道持续缺失 -> 第 1 拍直接秒级重启（无需等 3 轮）
    restore_pipe_during_delay = false;
    pipe_missing = true;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Restarted);
    UT_CHECK_EQ(restarts, 1);
    UT_CHECK_EQ(m.api_fail_count(), 0);
    UT_CHECK_EQ(m.auto_select_fail_count(), 1);

    // 25s 冷却期内（24.999s）-> 不重复触发快重启
    clock += 24999;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    UT_CHECK_EQ(restarts, 1);

    // 满 25s -> 允许第 2 次快重启（第 1 拍跳过 3 轮）
    clock += 1;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Restarted);
    UT_CHECK_EQ(restarts, 2);
    UT_CHECK_EQ(m.auto_select_fail_count(), 2);

    // 第 3, 4 次跨冷却快重启后第 5 次升级为系统网络警报并清零计数
    for (int round = 3; round <= 4; ++round) {
      clock += 25000;
      UT_CHECK(m.tick_once() == CoreMonitor::Action::Restarted);
      UT_CHECK_EQ(m.auto_select_fail_count(), round);
    }
    clock += 25000;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Alerted);
    UT_CHECK_EQ(alerts, 1);
    UT_CHECK_EQ(restarts, 4);
    UT_CHECK_EQ(m.auto_select_fail_count(), 0);
    UT_CHECK_EQ(m.api_dead_heal_fail_count(), 0);

    // 主动停核/配置更新窗口（expect_alive = false）-> 不得触发快重启
    clock += 30000;
    expect_alive = false;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    UT_CHECK_EQ(restarts, 4);

    // 快重启期间 restart_core 失败 -> 立即升级为终态告警并复位计数
    expect_alive = true;
    restart_ok = false;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Alerted);
    UT_CHECK_EQ(restarts, 5);
    UT_CHECK_EQ(alerts, 2);
    UT_CHECK_EQ(m.auto_select_fail_count(), 0);
    UT_CHECK_EQ(m.api_dead_heal_fail_count(), 0);
  }

  // 3. ApiChannelPrecheck 三态 + 稳态 NoAction 清零双失败计数
  {
    MonitorGuard mg;
    auto& m = CoreMonitor::instance();
    int64_t clock = 10000;
    bool api_ok = false;
    bool node_ok = true;
    int select_calls = 0;
    int restarts = 0;
    bool last_skip_wait_ready = false;
    auto precheck = CoreMonitor::ApiChannelPrecheck::ConfirmedDown;

    CoreMonitor::Hooks h;
    h.core_alive = [] { return true; };
    h.network_online = [] { return true; };
    h.api_alive = [&] { return api_ok; };
    h.node_healthy = [&] { return node_ok; };
    h.current_uid = [] { return std::string("u1"); };
    h.api_channel_precheck = [&] { return precheck; };
    h.auto_select_for_heal = [&](bool skip_wait_ready) {
      ++select_calls;
      last_skip_wait_ready = skip_wait_ready;
      return Result<CoreMonitor::HealSelectOutcome>::ok(
          CoreMonitor::HealSelectOutcome{true, "JP-2"});
    };
    h.restart_core = [&] {
      ++restarts;
      return Result<void>::ok();
    };
    h.now_ms = [&] { return clock; };
    m.set_hooks(h);

    // ConfirmedDown：直接跳过 auto_select，3 轮后触发 RestartedOnHealFail
    for (int round = 1; round <= 3; ++round) {
      if (round > 1) clock += 30000;
      m.tick_once();
      m.tick_once();
      const auto act = m.tick_once();
      if (round < 3) {
        UT_CHECK(act == CoreMonitor::Action::Idle);
        UT_CHECK_EQ(m.auto_select_fail_count(), round);
      } else {
        UT_CHECK(act == CoreMonitor::Action::RestartedOnHealFail);
        UT_CHECK_EQ(restarts, 1);
        UT_CHECK_EQ(m.auto_select_fail_count(), 3);
        UT_CHECK_EQ(m.api_dead_heal_fail_count(), 0);
      }
    }
    UT_CHECK_EQ(select_calls, 0);

    // API 与节点恢复稳态 (NoAction) -> 同步清零 auto_select_fail_count 与 api_dead_heal_fail_count
    api_ok = true;
    node_ok = true;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    UT_CHECK_EQ(m.auto_select_fail_count(), 0);
    UT_CHECK_EQ(m.api_dead_heal_fail_count(), 0);

    // KeepAlive：清零计数器且不占用 25s 冷却
    clock += 30000;
    api_ok = false;
    precheck = CoreMonitor::ApiChannelPrecheck::KeepAlive;
    m.tick_once();
    m.tick_once();
    const int64_t before_keep_alive_heal_ms = m.last_heal_ms();
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    UT_CHECK_EQ(m.last_heal_ms(), before_keep_alive_heal_ms);
    UT_CHECK_EQ(select_calls, 0);

    // 紧接着无需等 25s 即走 ProceedAutoSelect，且 skip_wait_ready 必须为 true
    clock += 1000;
    precheck = CoreMonitor::ApiChannelPrecheck::ProceedAutoSelect;
    m.tick_once();
    m.tick_once();
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Healed);
    UT_CHECK_EQ(select_calls, 1);
    UT_CHECK(last_skip_wait_ready);
  }
}

UT_TEST(bug412_brownout_window_single_target_skip_and_dead_conn_cleanup) {
  // 1. 600s 间歇性黑洞滑窗纯函数与 decide_self_heal
  {
    std::vector<int64_t> episodes_ms;
    const int64_t t0 = 100000;
    UT_CHECK_EQ(CoreMonitor::record_brownout_episode(episodes_ms, t0), size_t(1));
    UT_CHECK(CoreMonitor::decide_self_heal(
                 {CoreMonitor::SelfHealTrigger::Kind::NodeConfirmedDead, 1,
                  false, episodes_ms.size()}) ==
             CoreMonitor::SelfHealDecision::Wait);

    // 600s 窗口内第二次独立 ShouldFailover -> 即使仅连续失败 1 次也立即 Allow
    const int64_t t1 =
        t0 + (CoreMonitor::kBrownoutWindowSecs - 1) * 1000;
    UT_CHECK_EQ(CoreMonitor::record_brownout_episode(episodes_ms, t1), size_t(2));
    UT_CHECK(CoreMonitor::decide_self_heal(
                 {CoreMonitor::SelfHealTrigger::Kind::NodeConfirmedDead, 1,
                  false, episodes_ms.size()}) ==
             CoreMonitor::SelfHealDecision::Allow);

    // 超过 600s 窗口后旧记录淘汰，仅剩当前 1 次 -> 重新 Wait
    const int64_t t2 = t1 + CoreMonitor::kBrownoutWindowSecs * 1000;
    UT_CHECK_EQ(CoreMonitor::record_brownout_episode(episodes_ms, t2), size_t(1));
    UT_CHECK(CoreMonitor::decide_self_heal(
                 {CoreMonitor::SelfHealTrigger::Kind::NodeConfirmedDead, 1,
                  false, episodes_ms.size()}) ==
             CoreMonitor::SelfHealDecision::Wait);
  }

  // 2. CoreMonitor::tick_once 间歇黑洞全链路 + dead_node 精准清理回调
  {
    MonitorGuard mg;
    auto& m = CoreMonitor::instance();
    int64_t clock = 10000;
    std::string active = "日本5 (031306)";
    bool online = true;
    bool node_ok = true;
    bool has_standby = true;
    std::vector<std::string> cleaned_dead_nodes;

    CoreMonitor::Hooks h;
    h.core_alive = [] { return true; };
    h.network_online = [&] { return online; };
    h.api_alive = [] { return true; };
    h.current_uid = [] { return std::string("u1"); };
    h.active_node_name = [&] { return active; };
    h.node_healthy = [&] { return node_ok; };
    h.has_healthy_standby_in_subset = [&] { return has_standby; };
    h.auto_select_for_heal = [&](bool) {
      active = "日本1 (031306)";
      return Result<CoreMonitor::HealSelectOutcome>::ok(
          CoreMonitor::HealSelectOutcome{true, "日本1 (031306)"});
    };
    h.on_dead_node_healed = [&](const std::string& dead_node) {
      cleaned_dead_nodes.push_back(dead_node);
    };
    h.now_ms = [&] { return clock; };
    m.set_hooks(h);

    // 初始化观测活跃节点并跨过 20s 宽限期
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    clock += 25000;

    // 第 1 次黑洞故障 -> Idle，计入 1 次滑窗故障
    node_ok = false;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    UT_CHECK_EQ(m.node_fail_count(), 1);
    UT_CHECK_EQ(m.brownout_episode_count_for_test(), size_t(1));

    // 3 秒后压线侥幸恢复 -> NoAction 解除重试状态，但保留 600s 滑窗记录
    clock += 3000;
    node_ok = true;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    UT_CHECK_EQ(m.node_fail_count(), 0);
    UT_CHECK_EQ(m.brownout_episode_count_for_test(), size_t(1));

    // 120 秒后再次独立失败（仍是第 1 次失败）-> 滑窗满 2 次立即切离并精准清理旧节点死链
    clock += 120000;
    node_ok = false;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Healed);
    UT_CHECK_EQ(active, std::string("日本1 (031306)"));
    UT_CHECK_EQ(m.brownout_episode_count_for_test(), size_t(0));
    UT_CHECK_EQ(cleaned_dead_nodes.size(), size_t(1));
    UT_CHECK_EQ(cleaned_dead_nodes[0], std::string("日本5 (031306)"));

    // AllDead（无健康备选）不累计间歇黑洞次数；手动切点清空滑窗
    clock += 30000;  // 跨过新节点 20s 宽限期
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    clock += 25000;  // 新观察到日本1会刷新切点宽限期，再推进跨过 20s 宽限期
    has_standby = false;
    node_ok = false;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    UT_CHECK_EQ(m.brownout_episode_count_for_test(), size_t(0));

    // 物理离线转在线也清空滑窗
    node_ok = true;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    has_standby = true;
    node_ok = false;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    UT_CHECK_EQ(m.brownout_episode_count_for_test(), size_t(1));
    online = false;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    online = true;
    node_ok = true;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    UT_CHECK_EQ(m.brownout_episode_count_for_test(), size_t(0));

    // 手动切点通知也清空滑窗
    node_ok = false;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    UT_CHECK_EQ(m.brownout_episode_count_for_test(), size_t(1));
    m.notify_manual_node_switched();
    node_ok = true;
    UT_CHECK(m.tick_once() == CoreMonitor::Action::Idle);
    UT_CHECK_EQ(m.brownout_episode_count_for_test(), size_t(0));
  }

  // 3. 单目标（活跃节点）在 >= 3 个健康真实节点通过主 URL 时跳过备用 URL 复核；< 3 时保留复核
  {
    const std::map<std::string, uint32_t> sufficient_peers = {
        {"日本1", 65}, {"日本2", 80}, {"日本3", 95}, {"DIRECT", 10}};
    UT_CHECK(!CoreMonitor::should_recheck_primary_with_fallback(
        sufficient_peers, {"日本5 (031306)"}, true));

    const std::map<std::string, uint32_t> insufficient_peers = {
        {"日本1", 65}, {"日本2", 80}, {"DIRECT", 10}, {"剩余流量：10GB", 50}};
    UT_CHECK(CoreMonitor::should_recheck_primary_with_fallback(
        insufficient_peers, {"日本5 (031306)"}, true));
  }

  // 4. should_close_dead_node_proxy_connection 纯规则与管道实测精准清理
  {
    const std::vector<std::string> dead_chain = {"日本5 (031306)", "PROXY"};
    const std::vector<std::string> healthy_chain = {"日本1 (031306)", "PROXY"};
    const std::vector<std::string> direct_chain = {"DIRECT"};
    const std::vector<std::string> reject_chain = {"REJECT"};

    UT_CHECK(should_close_dead_node_proxy_connection(
        dead_chain, "PROXY", "日本5 (031306)"));
    UT_CHECK(!should_close_dead_node_proxy_connection(
        healthy_chain, "PROXY", "日本5 (031306)"));
    UT_CHECK(!should_close_dead_node_proxy_connection(
        direct_chain, "PROXY", "日本5 (031306)"));
    UT_CHECK(!should_close_dead_node_proxy_connection(
        reject_chain, "PROXY", "日本5 (031306)"));
    UT_CHECK(!should_close_dead_node_proxy_connection(
        dead_chain, "PROXY", ""));
    UT_CHECK(!should_close_dead_node_proxy_connection(
        dead_chain, "PROXY", "DIRECT"));
    UT_CHECK(!should_close_dead_node_proxy_connection(
        dead_chain, "PROXY", "GLOBAL"));

    const std::wstring pipe_name = unique_pipe_411(L"deadconn");
    std::vector<std::string> deleted_paths;
    std::mutex del_mu;
    mockpipe::Server srv(pipe_name, 4);
    srv.start([&](const mockpipe::Request& req) {
      mockpipe::Response resp;
      resp.headers["Content-Type"] = "application/json";
      if (req.method == "GET" && req.path == "/connections") {
        resp.body =
            R"json({"downloadTotal":0,"uploadTotal":0,"connections":[)json"
            R"json({"id":"c-dead","metadata":{},"upload":100,"download":0,"start":"","chains":["日本5 (031306)","PROXY"],"rule":"MATCH","rulePayload":""},)json"
            R"json({"id":"c-healthy","metadata":{},"upload":100,"download":500,"start":"","chains":["日本1 (031306)","PROXY"],"rule":"MATCH","rulePayload":""},)json"
            R"json({"id":"c-direct","metadata":{},"upload":10,"download":20,"start":"","chains":["DIRECT"],"rule":"GEOIP","rulePayload":"CN"},)json"
            R"json({"id":"c-reject","metadata":{},"upload":0,"download":0,"start":"","chains":["REJECT"],"rule":"RULE-SET","rulePayload":"ads"}]})json";
        return resp;
      }
      if (req.method == "DELETE") {
        std::lock_guard<std::mutex> lk(del_mu);
        deleted_paths.push_back(req.path);
        resp.status = 204;
        return resp;
      }
      resp.status = 404;
      return resp;
    });

    MihomoClient client(pipe_name);
    const int closed =
        close_dead_node_proxy_connections(client, "日本5 (031306)");
    UT_CHECK_EQ(closed, 1);
    UT_CHECK_EQ(deleted_paths.size(), size_t(1));
    UT_CHECK_EQ(deleted_paths[0], std::string("/connections/c-dead"));
    srv.stop();
  }
}
