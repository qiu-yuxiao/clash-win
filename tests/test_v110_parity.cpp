// Clash Win v1.1.0 全维度回归与 Clash Mini v3.5.6 增量对齐测试：
// - 命名/常量契约：kRuntimeConfig / kDefaultMixed / clash_mini_path / ipc_path
// - BUG-399：proxy-providers.*.health-check.lazy=true、PROXY__METRICS interval=1800/lazy=true、
//   Flowing 1800s / Idle 900s / 前端 HISTORY_FALLBACK_MAX_AGE_MS=45min
// - BUG-400：should_run_periodic_https_verify_with 五场景与够用即止(>=3)门控
// - BUG-401：should_recheck_primary_with_fallback_with 与 DelayManager single-target 透传
// - BUG-402：entry_lightweight_mode 不唤醒 CoreMonitor::kick()、has_fresh_active_exit_latency、
//   AutoSelect 过滤子集触发与 silent_test_on_wakeup 接口
// - BUG-403：has_recent_flowing_traffic / update_active_traffic_state 证活在线 / ThrottledNetProbe::was_online
// - UI/弹窗/通知：resolve_verge_patch_success_key、LogViewerModal::on_pause_changed、FirstRunGuideDialog::dismiss_via_close

#include <windows.h>
#include <yaml-cpp/yaml.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "app/lifecycle.h"
#include "clashwin/config/constants.h"
#include "clashwin/core/constants.h"
#include "core/auto_select.h"
#include "core/delay_manager.h"
#include "core/monitor.h"
#include "enhance/mini_agreements.h"
#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/notice_handler.h"
#include "ui/scenes/first_run_guide_dialog.h"
#include "ui/scenes/node_data.h"
#include "ui/scenes/utility_modals.h"
#include "util/dirs.h"
#include "util/node.h"

using namespace clashwin;
using namespace clashwin::core;
using namespace clashwin::ui;
using namespace clashwin::ui::scene;

namespace {

struct TempHomeGuard {
  std::filesystem::path dir;
  TempHomeGuard() {
    auto base = std::filesystem::temp_directory_path();
    dir = base / ("clashwin_v110_" + std::to_string(GetTickCount64()) + "_" +
                  std::to_string(rand()));
    std::filesystem::create_directories(dir);
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", dir.wstring().c_str());
    _wputenv_s(L"CLASHWIN_PORTABLE_OVERRIDE", L"0");
    util::dirs::reset_portable_cache_for_testing();
  }
  ~TempHomeGuard() {
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
    _wputenv_s(L"CLASHWIN_PORTABLE_OVERRIDE", L"");
    util::dirs::reset_portable_cache_for_testing();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
};

struct SingletonResetGuard {
  ~SingletonResetGuard() {
    DelayManager::instance().set_hooks(DelayProbeHooks{});
    AutoSelect::instance().set_hooks(AutoSelectHooks{});
    CoreMonitor::instance().reset_state();
    CoreMonitor::instance().clear_kick_for_test();
    app::Lifecycle::instance().reset();
    app::Lifecycle::instance().reset_hooks();
  }
};

int64_t make_utc_epoch_ms(int year, int mon, int mday, int hour, int min, int sec) {
  std::tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = mon - 1;
  tm.tm_mday = mday;
  tm.tm_hour = hour;
  tm.tm_min = min;
  tm.tm_sec = sec;
  return static_cast<int64_t>(_mkgmtime(&tm)) * 1000LL;
}

}  // namespace

UT_TEST(v110_naming_and_constants_parity) {
  UT_CHECK_EQ(std::string(config::consts::kRuntimeConfig), "clash-win.yaml");
  UT_CHECK_EQ(std::string(config::consts::kCheckConfig), "clash-win-check.yaml");
  static_assert(config::consts::kDefaultMixed == uint16_t(10801));
  UT_CHECK_EQ(std::wstring(core::consts::kCoreProcessToken), L"win-mihomo");

  TempHomeGuard home;
  const std::wstring cfg_path = util::dirs::clash_mini_path();
  UT_CHECK(cfg_path.size() >= 14 &&
           cfg_path.compare(cfg_path.size() - 14, 14, L"clash-win.yaml") == 0);
  UT_CHECK_EQ(util::dirs::ipc_path(), L"\\\\.\\pipe\\mini-mihomo-win");
}

UT_TEST(v110_bug399_passive_freshness_and_provider_lazy) {
  static_assert(CoreMonitor::kPassiveTrafficMaxSkipSecs == int64_t(1800));
  static_assert(CoreMonitor::kIdleHealthFreshSecs == int64_t(900));
  static_assert(kHistoryFallbackMaxAgeMs == int64_t(45LL * 60 * 1000));

  YAML::Node cfg = YAML::Load(
      "proxies:\n"
      "- name: jp-1\n  type: ss\n  server: 1.2.3.4\n  port: 443\n"
      "proxy-providers:\n"
      "  subA:\n"
      "    type: http\n"
      "    url: https://example.com/sub.yaml\n"
      "    health-check:\n"
      "      enable: true\n"
      "      lazy: false\n"
      "      interval: 300\n"
      "proxy-groups:\n"
      "- name: Group-A\n  type: select\n  proxies:\n  - jp-1\n");
  YAML::Node out = enhance::enforce_mini_agreements(cfg, enhance::MiniAgreementOptions{});

  UT_CHECK_EQ(out["proxy-providers"]["subA"]["health-check"]["lazy"].Scalar(), "true");
  const YAML::Node& metrics = out["proxy-groups"][1];
  UT_CHECK_EQ(metrics["name"].Scalar(), "PROXY__METRICS");
  UT_CHECK_EQ(metrics["interval"].Scalar(), "1800");
  UT_CHECK_EQ(metrics["lazy"].Scalar(), "true");

  const int64_t rec_ms = make_utc_epoch_ms(2026, 11, 19, 12, 0, 0);
  std::vector<mihomo::DelayHistory> hist{
      mihomo::DelayHistory{"2026-11-19T12:00:00Z", uint16_t(120)}};
  UT_CHECK(is_proxy_history_fresh_and_valid(true, hist, rec_ms + 44LL * 60 * 1000));
  UT_CHECK(!is_proxy_history_fresh_and_valid(true, hist, rec_ms + 46LL * 60 * 1000));
  UT_CHECK(!is_proxy_history_fresh_and_valid(false, hist, rec_ms + 1000));

  std::vector<mihomo::DelayHistory> hist_oob{
      mihomo::DelayHistory{"2026-11-19T12:00:00Z", uint16_t(5000)}};
  UT_CHECK(!is_proxy_history_fresh_and_valid(true, hist_oob, rec_ms + 1000));
  std::vector<mihomo::DelayHistory> hist_low{
      mihomo::DelayHistory{"2026-11-19T12:00:00Z", uint16_t(29)}};
  UT_CHECK(!is_proxy_history_fresh_and_valid(true, hist_low, rec_ms + 1000));
}

UT_TEST(v110_bug400_periodic_https_verify_sufficient_standbys) {
  static_assert(CoreMonitor::kHttpsVerifyIntervalSecs == int64_t(600));
  static_assert(CoreMonitor::kPeriodicHttpsVerifySufficientStandbys == size_t(3));

  const std::vector<std::string> members = {
      "日本1", "日本2", "日本3", "日本4", "香港-离线",
  };
  auto never_fake = [](const std::string&) { return false; };

  // 1. 全员 Alive 均已验真，仅离线死节点未验真 -> 不触发
  UT_CHECK(!CoreMonitor::should_run_periodic_https_verify_with(
      members, "日本1", "", never_fake,
      [](const std::string& n) { return n != "香港-离线"; },
      [](const std::string& n) { return n != "香港-离线"; }));

  // 2. 活跃节点自身 Alive 但尚未验真 -> 触发
  UT_CHECK(CoreMonitor::should_run_periodic_https_verify_with(
      members, "日本1", "", never_fake,
      [](const std::string& n) { return n != "香港-离线"; },
      [](const std::string& n) { return n != "日本1" && n != "香港-离线"; }));

  // 3. 活跃已验真，但已验真 Alive 备用仅 2 个(<3)且仍有 Alive 未验真 -> 触发
  UT_CHECK(CoreMonitor::should_run_periodic_https_verify_with(
      members, "日本1", "", never_fake,
      [](const std::string& n) { return n != "香港-离线"; },
      [](const std::string& n) {
        return n == "日本1" || n == "日本2" || n == "日本3";
      }));

  // 4. 够用即止：活跃已验真且匹配子集内已验真 Alive 备用 >= 3（日本2/3/4）-> 跳过
  UT_CHECK(!CoreMonitor::should_run_periodic_https_verify_with(
      members, "日本1", "", never_fake,
      [](const std::string&) { return true; },
      [](const std::string& n) {
        return n == "日本1" || n == "日本2" || n == "日本3" || n == "日本4";
      }));

  // 5. filter_text 排除范围外的未验真 Alive 节点不得触发当前子集探测
  UT_CHECK(!CoreMonitor::should_run_periodic_https_verify_with(
      members, "日本1", "-香港", never_fake,
      [](const std::string&) { return true; },
      [](const std::string& n) { return n.find("香港") == std::string::npos; }));
}

UT_TEST(v110_bug401_single_target_skip_fallback_and_delay_manager) {
  static_assert(CoreMonitor::kSingleTargetSkipFallbackMinHealthyPeers == size_t(3));
  auto never_fake = [](const std::string&) { return false; };

  // 1. 单目标超时，同组已有 >=3 个真实节点在主 URL 响应 -> 免备用 URL
  const std::map<std::string, uint32_t> healthy_peers = {
      {"目标-坏节点", 0u}, {"日本1", 80u}, {"日本2", 95u}, {"日本3", 110u},
  };
  const std::vector<std::string> single_expected = {"目标-坏节点"};
  UT_CHECK(!CoreMonitor::should_recheck_primary_with_fallback_with(
      healthy_peers, single_expected, true, never_fake));

  // 2. 排除 DIRECT / 假节点后仅 2 个真实节点 -> 仍走备用 URL 复核
  const std::map<std::string, uint32_t> few_peers = {
      {"目标-坏节点", 0u}, {"DIRECT", 50u}, {"剩余流量：10GB", 60u},
      {"日本1", 80u},      {"日本2", 95u},
  };
  UT_CHECK(CoreMonitor::should_recheck_primary_with_fallback_with(
      few_peers, single_expected, true,
      [](const std::string& n) { return n == "剩余流量：10GB"; }));

  // 3. allow_single_target_skip=false（如二次确认判死前复核）-> 强制复核
  UT_CHECK(CoreMonitor::should_recheck_primary_with_fallback_with(
      healthy_peers, single_expected, false, never_fake));

  // 4. 多目标期望全灭 -> 仍走备用 URL 复核
  const std::vector<std::string> multi_expected = {"目标A", "目标B"};
  UT_CHECK(CoreMonitor::should_recheck_primary_with_fallback_with(
      healthy_peers, multi_expected, true, never_fake));

  // 5. DelayManager::test_group 前端单节点透传 allow_coalesce=false / allow_single_target_skip=true
  SingletonResetGuard guard;
  bool seen_coalesce = true;
  bool seen_single_skip = false;
  std::vector<std::string> seen_expected;
  DelayProbeHooks dhooks;
  dhooks.group_probe_ex = [&](const std::string& /*mg*/,
                              const std::vector<std::string>& expected,
                              bool allow_coalesce,
                              bool allow_single_target_skip)
      -> std::optional<std::map<std::string, uint32_t>> {
    seen_expected = expected;
    seen_coalesce = allow_coalesce;
    seen_single_skip = allow_single_target_skip;
    return std::map<std::string, uint32_t>{{"日本1", 72u}};
  };
  DelayManager::instance().set_hooks(dhooks);

  auto out_single = DelayManager::instance().test_group(
      "PROXY", {"日本1"}, /*allow_cache_reuse=*/false, std::nullopt,
      /*is_frontend_subset=*/true);
  UT_CHECK(out_single.injected);
  UT_CHECK(!seen_coalesce);
  UT_CHECK(seen_single_skip);
  UT_CHECK_EQ(seen_expected.size(), size_t(1));

  auto out_multi = DelayManager::instance().test_group(
      "PROXY", {"日本1", "日本2"}, /*allow_cache_reuse=*/false, std::nullopt,
      /*is_frontend_subset=*/true);
  UT_CHECK(out_multi.injected);
  UT_CHECK(seen_coalesce);
  UT_CHECK(!seen_single_skip);
}

UT_TEST(v110_bug402_lightweight_no_kick_and_fresh_active_exit_skip) {
  TempHomeGuard home;
  SingletonResetGuard guard;

  // 1. entry_lightweight_mode 不得调用 CoreMonitor::kick()
  CoreMonitor::instance().clear_kick_for_test();
  app::Lifecycle::instance().reset();
  UT_CHECK(app::Lifecycle::instance().entry_lightweight_mode(/*force=*/true));
  UT_CHECK(!CoreMonitor::instance().has_pending_kick_for_test());
  app::Lifecycle::instance().reset();

  // 2. NodeData::has_fresh_active_exit_latency（过滤子集包含 + 鲜活延迟）
  int64_t clock_ms = 50000;
  DataHooks nh;
  nh.now_ms = [&] { return clock_ms; };
  nh.fetch_group = [](std::vector<std::string>* all, std::string* now) {
    *all = {"hk-1", "hk-2"};
    *now = "hk-1";
    return true;
  };
  nh.fetch_proxies = [](std::map<std::string, mihomo::ProxyItem>* items) {
    mihomo::ProxyItem p1;
    p1.name = "hk-1";
    p1.type = "ss";
    p1.alive = true;
    mihomo::ProxyItem p2;
    p2.name = "hk-2";
    p2.type = "ss";
    p2.alive = true;
    (*items)["hk-1"] = p1;
    (*items)["hk-2"] = p2;
    return true;
  };
  NodeData nd(nh);
  UT_CHECK(nd.load());
  UT_CHECK(!nd.has_fresh_active_exit_latency({"hk-1", "hk-2"}));
  nd.set_delay("hk-1", 88);
  UT_CHECK(nd.has_fresh_active_exit_latency({"hk-1", "hk-2"}));
  UT_CHECK(!nd.has_fresh_active_exit_latency({"hk-2"}));

  // 3. AutoSelect 接收过滤子集触发与 silent_test_on_wakeup(uid, subset) 签名
  std::vector<std::string> probed_nodes;
  DelayProbeHooks dhooks;
  dhooks.group_probe_ex = [&](const std::string&,
                              const std::vector<std::string>& expected,
                              bool, bool) {
    probed_nodes = expected;
    return std::map<std::string, uint32_t>{{"hk-1", 80u}, {"hk-2", 90u}};
  };
  DelayManager::instance().set_hooks(dhooks);

  AutoSelectHooks ah;
  ah.current_uid = []() -> std::optional<std::string> { return "u-v110"; };
  ah.is_dummy = [](const std::string& n) { return util::is_dummy_node(n); };
  ah.is_confirmed_fake = [](const std::string&) { return false; };
  ah.is_verified_true = [](const std::string&) { return true; };
  ah.group_nodes = [](const std::string&) {
    return std::vector<std::string>{"hk-1", "hk-2", "us-9"};
  };
  AutoSelect::instance().set_hooks(ah);

  auto tr = AutoSelect::instance().trigger(
      "u-v110", std::vector<std::string>{"hk-1", "hk-2"}, 0, false, true);
  UT_CHECK_EQ(tr.display.size(), size_t(2));
  UT_CHECK_EQ(probed_nodes.size(), size_t(2));

  // 若尚无唤醒冷却，验证 silent_test_on_wakeup 接受可选过滤子集重载
  if (AutoSelect::instance().wakeup_cooldown_remaining_ms() == 0) {
    probed_nodes.clear();
    UT_CHECK(AutoSelect::instance().silent_test_on_wakeup(
        "u-v110", std::vector<std::string>{"hk-2"}));
    UT_CHECK_EQ(probed_nodes.size(), size_t(1));
    UT_CHECK_EQ(probed_nodes[0], std::string("hk-2"));
  }
}

UT_TEST(v110_bug403_flowing_traffic_online_and_throttled_probe) {
  static_assert(CoreMonitor::kOnlineCheckFlowingSkipSecs == int64_t(60));
  static_assert(CoreMonitor::kPrimaryNetProbeTimeoutMs == 250);

  UT_CHECK(!CoreMonitor::has_recent_flowing_traffic(std::nullopt));
  UT_CHECK(CoreMonitor::has_recent_flowing_traffic(0));
  UT_CHECK(CoreMonitor::has_recent_flowing_traffic(59));
  UT_CHECK(!CoreMonitor::has_recent_flowing_traffic(60));

  CoreMonitor::reset_flowing_traffic_state();
  UT_CHECK(!CoreMonitor::is_recent_flowing_traffic_active(10000));

  std::optional<CoreMonitor::ActiveTrafficTracker> tracker;
  CoreMonitor::update_active_traffic_state(tracker, "NodeA", {{"c1", 10, 10}}, 1000);
  auto [st, _cd] = CoreMonitor::update_active_traffic_state(
      tracker, "NodeA", {{"c1", 20, 512}}, 5000);
  UT_CHECK(st == CoreMonitor::ActiveTrafficState::Flowing);
  UT_CHECK(CoreMonitor::is_recent_flowing_traffic_active(64999));
  UT_CHECK(!CoreMonitor::is_recent_flowing_traffic_active(65000));

  CoreMonitor::reset_flowing_traffic_state();
  UT_CHECK(!CoreMonitor::is_recent_flowing_traffic_active(5000));

  CoreMonitor::ThrottledNetProbe net_probe;
  UT_CHECK(net_probe.was_online());
  UT_CHECK(!net_probe.check(1000, [] { return false; }));
  UT_CHECK(!net_probe.was_online());
  net_probe.reset();
  UT_CHECK(net_probe.was_online());
}

UT_TEST(v110_ui_notices_modals_and_first_run_parity) {
  TempHomeGuard home;
  ui_test::make_offscreen(360, 480, 96.0f);

  // 1. notice_handler 分流/基础开关提示键对齐 Mini v3.5.6
  UT_CHECK_EQ(resolve_verge_patch_success_key("rule_fallback", "proxy"),
              "layout.notices.switchedToGlobalProxy");
  UT_CHECK_EQ(resolve_verge_patch_success_key("rule_fallback", "direct"),
              "layout.notices.switchedToGfwlist");
  UT_CHECK_EQ(resolve_verge_patch_success_key("rule_fallback", "addurl"),
              "layout.notices.switchedToAddUrl");
  UT_CHECK_EQ(resolve_verge_patch_success_key("enable_silent_start", true),
              "settings.mini.settingEnabled");
  UT_CHECK_EQ(resolve_verge_patch_success_key("clash_allow_lan", false),
              "settings.mini.settingDisabled");
  UT_CHECK_EQ(resolve_verge_patch_success_key("clash_ipv6", true),
              "settings.mini.settingEnabled");
  UT_CHECK_EQ(resolve_verge_patch_success_key("enable_tun_sniffer", false),
              "settings.mini.tunSnifferUpdated");

  // 2. LogViewerModal::set_paused 触发 on_pause_changed 供宿主同步 WS 订阅
  LogViewerModal lv;
  int calls = 0;
  bool last_paused = false;
  lv.on_pause_changed = [&](bool p) {
    ++calls;
    last_paused = p;
  };
  lv.set_paused(true);
  UT_CHECK_EQ(calls, 1);
  UT_CHECK(last_paused);
  lv.set_paused(false);
  UT_CHECK_EQ(calls, 2);
  UT_CHECK(!last_paused);

  // 3. FirstRunGuideDialog::dismiss_via_close 关闭向导窗口
  FirstRunGuideDialog::set_first_run_completed(false);
  FirstRunGuideDialog dlg;
  dlg.layout_tree({0, 0, 360, 480});
  dlg.open();
  UT_CHECK(dlg.is_open());
  dlg.dismiss_via_close();
  UT_CHECK(!dlg.is_open());
}
