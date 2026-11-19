// Clash Win v1.1.1 — Clash Mini v3.5.7 (BUG-404 & BUG-405) 1:1 对齐专项单测：
// 1. BUG-405：effective_filter_text / pick_subset_fallback_node / promote_preferred_proxy_first
// 2. BUG-405：enforce_mini_agreements & apply_proxy_group_head_and_store_selected
// 3. BUG-405：should_close_excluded_proxy_connection / should_migrate_filtered_out_active /
//    persisted_exit_is_usable / CoreMonitor NodeMigration 钩子触发
// 4. BUG-405：CoreManager::resolve_desired_proxy_selection & ensure_proxy_group_selection
// 5. BUG-404：get_runtime_config 与 uninstall_service 死命令彻底移除

#include <yaml-cpp/yaml.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cmd/cmd_install.h"
#include "cmd/command_service.h"
#include "core/connection_stale.h"
#include "core/core_manager.h"
#include "core/monitor.h"
#include "enhance/enhance.h"
#include "enhance/mini_agreements.h"
#include "test_util.h"
#include "util/node.h"

using namespace clashwin;
using namespace clashwin::core;
using clashwin::mihomo::BaseConfig;
using clashwin::mihomo::ProxyItem;

namespace {

struct Bug405MockApi : ICoreApiClient {
  std::vector<std::string> members;
  std::optional<std::string> current_now;
  std::vector<std::string> selected_history;
  std::vector<std::string> closed_filter_calls;
  bool api_ok = true;
  bool select_ok = true;

  Result<BaseConfig> get_base_config() override {
    if (!api_ok) return Result<BaseConfig>::err(Error{1, "down"});
    return Result<BaseConfig>::ok(BaseConfig{});
  }
  Result<ProxyItem> get_group_by_name(const std::string& name) override {
    ProxyItem g;
    g.name = name;
    g.now = current_now;
    g.all = members;
    return Result<ProxyItem>::ok(g);
  }
  Result<void> select_node_for_group(const std::string&,
                                     const std::string& node) override {
    selected_history.push_back(node);
    if (!select_ok) return Result<void>::err(Error{1100, "select err"});
    current_now = node;
    return Result<void>::ok();
  }
  Result<void> reload_config(bool, const std::string&) override {
    return Result<void>::ok();
  }
  int close_excluded_connections(const std::string& filter_text) override {
    closed_filter_calls.push_back(filter_text);
    return 1;
  }
};

RestoreTimings fast_timings() {
  RestoreTimings t;
  t.api_ready_budget_ms = 50;
  t.api_ready_poll_ms = 1;
  t.group_fill_budget_ms = 50;
  t.group_fill_poll_ms = 1;
  t.select_budget_ms = 50;
  t.select_retry_ms = 1;
  return t;
}

}  // namespace

UT_TEST(v111_bug405_node_filter_and_head_promotion) {
  UT_CHECK_EQ(util::effective_filter_text("-香港 -台湾"), std::string("-香港 -台湾"));
  UT_CHECK_EQ(util::effective_filter_text("  日本  "), std::string("日本"));
  UT_CHECK_EQ(util::effective_filter_text(""), std::string(""));
  UT_CHECK_EQ(util::effective_filter_text("   "), std::string(""));
  UT_CHECK_EQ(util::effective_filter_text("-"), std::string(""));
  UT_CHECK_EQ(util::effective_filter_text("日本 -"), std::string(""));
  UT_CHECK_EQ(util::effective_filter_text("hk | "), std::string(""));

  const std::vector<std::string> all = {
      "剩余流量：100 GB",
      "香港-01",
      "日本-01",
      "日本-02",
  };
  auto is_valid = [](const std::string& n) {
    return n.find("剩余流量") == std::string::npos;
  };
  UT_CHECK_EQ(
      util::pick_subset_fallback_node(
          all, "日本", is_valid, [](const std::string& n) { return n == "日本-02"; })
          .value_or(""),
      std::string("日本-02"));
  UT_CHECK_EQ(
      util::pick_subset_fallback_node(
          all, "日本", is_valid, [](const std::string&) { return false; })
          .value_or(""),
      std::string("日本-01"));
  UT_CHECK(!util::pick_subset_fallback_node(
                all, "新加坡", is_valid,
                [](const std::string& n) { return n == "香港-01"; })
                .has_value());
  UT_CHECK_EQ(
      util::pick_subset_fallback_node(
          all, "   ", is_valid, [](const std::string& n) { return n == "日本-01"; })
          .value_or(""),
      std::string("日本-01"));
  UT_CHECK_EQ(
      util::pick_subset_fallback_node(
          all, "日本 -", is_valid, [](const std::string&) { return false; })
          .value_or(""),
      std::string("香港-01"));

  // promote_preferred_proxy_first：命中过滤词的持久节点提前至 proxies[0]，保留相对顺序
  std::vector<std::string> names = {"香港1", "台湾1", "新加坡2", "新加坡3"};
  auto promoted = util::promote_preferred_proxy_first(
      names, std::string("新加坡3"), "-香港 -台湾",
      [](const std::string&) { return true; },
      [](const std::string&) { return false; });
  UT_CHECK(promoted.has_value() && *promoted == "新加坡3");
  UT_CHECK((names == std::vector<std::string>{"新加坡3", "香港1", "台湾1", "新加坡2"}));

  // 被过滤词排除的持久节点不可提前，改选合规已验真候选提前
  std::vector<std::string> names2 = {"香港1", "台湾1", "新加坡2", "新加坡3"};
  auto fallback_promoted = util::promote_preferred_proxy_first(
      names2, std::string("香港1"), "-香港 -台湾",
      [](const std::string&) { return true; },
      [](const std::string& n) { return n == "新加坡3"; });
  UT_CHECK(fallback_promoted.has_value() && *fallback_promoted == "新加坡3");
  UT_CHECK((names2 == std::vector<std::string>{"新加坡3", "香港1", "台湾1", "新加坡2"}));

  // 子集全灭时不改动原序并返回 nullopt
  std::vector<std::string> only_blocked = {"香港1", "台湾1"};
  auto none = util::promote_preferred_proxy_first(
      only_blocked, std::string("香港1"), "-香港 -台湾",
      [](const std::string&) { return true; },
      [](const std::string&) { return true; });
  UT_CHECK(!none.has_value());
  UT_CHECK((only_blocked == std::vector<std::string>{"香港1", "台湾1"}));
}

UT_TEST(v111_bug405_enhance_store_selected_and_head_sync) {
  util::reset_verify_state_for_test(true);

  const YAML::Node input = YAML::Load(
      "profile:\n"
      "  store-selected: true\n"
      "  store-fake-ip: false\n"
      "proxies:\n"
      "  - name: 香港1 (184629)\n"
      "    type: ss\n"
      "    server: 198.51.100.1\n"
      "    port: 443\n"
      "    cipher: aes-128-gcm\n"
      "    password: p1\n"
      "  - name: 台湾1 (184629)\n"
      "    type: ss\n"
      "    server: 198.51.100.2\n"
      "    port: 443\n"
      "    cipher: aes-128-gcm\n"
      "    password: p2\n"
      "  - name: 新加坡3 (184629)\n"
      "    type: ss\n"
      "    server: 198.51.100.3\n"
      "    port: 443\n"
      "    cipher: aes-128-gcm\n"
      "    password: p3\n");

  enhance::MiniAgreementOptions opt;
  opt.preferred_proxy = "新加坡3 (184629)";
  opt.filter_text = "-香港 -台湾";
  YAML::Node enforced = enhance::enforce_mini_agreements(input, opt);

  UT_CHECK(enforced["profile"].IsMap());
  UT_CHECK(enforced["profile"]["store-selected"].IsDefined());
  UT_CHECK_EQ(enforced["profile"]["store-selected"].Scalar(), std::string("false"));
  UT_CHECK_EQ(enforced["profile"]["store-fake-ip"].Scalar(), std::string("false"));

  const YAML::Node proxies = enforced["proxy-groups"][0]["proxies"];
  UT_CHECK_EQ(proxies.size(), size_t(3));
  UT_CHECK_EQ(proxies[0].Scalar(), std::string("新加坡3 (184629)"));
  UT_CHECK_EQ(proxies[1].Scalar(), std::string("香港1 (184629)"));
  UT_CHECK_EQ(proxies[2].Scalar(), std::string("台湾1 (184629)"));

  // 直接调用纯函数 apply_proxy_group_head_and_store_selected 也可覆盖已有顺序并关闭 store-selected
  YAML::Node direct = YAML::Clone(enforced);
  direct["profile"]["store-selected"] = true;
  enhance::apply_proxy_group_head_and_store_selected(
      direct, std::string("台湾1 (184629)"), "");
  UT_CHECK_EQ(direct["profile"]["store-selected"].Scalar(), std::string("false"));
  UT_CHECK_EQ(direct["proxy-groups"][0]["proxies"][0].Scalar(),
              std::string("台湾1 (184629)"));
}

UT_TEST(v111_bug405_excluded_conn_and_monitor_migration) {
  const std::vector<std::string> chains_hk = {"香港1 (184629)", "PROXY"};
  const std::vector<std::string> chains_sg = {"新加坡3 (184629)", "PROXY"};
  const std::vector<std::string> chains_direct = {"DIRECT"};

  UT_CHECK(core::should_close_excluded_proxy_connection(chains_hk, "PROXY", "-香港 -台湾"));
  UT_CHECK(!core::should_close_excluded_proxy_connection(chains_sg, "PROXY", "-香港 -台湾"));
  UT_CHECK(!core::should_close_excluded_proxy_connection(chains_direct, "PROXY", "-香港 -台湾"));
  // 半输入态过滤词不可误杀
  UT_CHECK(!core::should_close_excluded_proxy_connection(chains_hk, "PROXY", "日本 -"));

  const std::vector<std::string> members = {
      "香港1 (184629)", "台湾1 (184629)", "新加坡3 (184629)"};
  auto not_fake = [](const std::string&) { return false; };
  UT_CHECK(CoreMonitor::should_migrate_filtered_out_active(
      "香港1 (184629)", members, "-香港 -台湾", not_fake));
  UT_CHECK(!CoreMonitor::should_migrate_filtered_out_active(
      "新加坡3 (184629)", members, "-香港 -台湾", not_fake));
  UT_CHECK(!CoreMonitor::should_migrate_filtered_out_active(
      "香港1 (184629)", members, "日本 -", not_fake));

  const std::vector<std::string> only_blocked = {"香港1 (184629)", "台湾1 (184629)"};
  UT_CHECK(!CoreMonitor::should_migrate_filtered_out_active(
      "香港1 (184629)", only_blocked, "-香港 -台湾", not_fake));
  const std::vector<std::string> only_blocked_with_direct = {"DIRECT", "香港1 (184629)"};
  UT_CHECK(!CoreMonitor::should_migrate_filtered_out_active(
      "香港1 (184629)", only_blocked_with_direct, "-香港 -台湾", not_fake));

  // persisted_exit_is_usable 对半输入态过滤词保持豁免
  UT_CHECK(CoreMonitor::persisted_exit_is_usable(
      std::string("香港1 (184629)"), &members, "日本 -", false));
  UT_CHECK(!CoreMonitor::persisted_exit_is_usable(
      std::string("香港1 (184629)"), &members, "-香港 -台湾", false));

  // CoreMonitor::tick_once 通过 should_migrate_filtered_out 立即走 NodeMigration 并调用清理回调
  auto& mon = CoreMonitor::instance();
  mon.reset_state();
  bool migration_cleaned = false;
  bool probed = false;
  int64_t mock_now = 100000;
  CoreMonitor::Hooks hooks;
  hooks.api_alive = [] { return true; };
  hooks.core_alive = [] { return true; };
  hooks.network_online = [] { return true; };
  hooks.current_uid = [] { return std::string("p1"); };
  hooks.active_node_name = [] { return std::string("香港1 (184629)"); };
  hooks.should_migrate_filtered_out = [] { return true; };
  hooks.probe_metrics_with_fallback = [&] {
    probed = true;
    return true;
  };
  hooks.auto_select_trigger = [] { return Result<bool>::ok(true); };
  hooks.on_node_migration_healed = [&] { migration_cleaned = true; };
  hooks.now_ms = [&] { return mock_now; };
  mon.set_hooks(hooks);

  // 首轮从空节点观测到活跃节点会起算 20s 切换宽限期（对齐 monitor.rs:2827）
  UT_CHECK(mon.tick_once() == CoreMonitor::Action::Idle);
  UT_CHECK(!migration_cleaned);

  // 跨过 20s 切换宽限期后，绕过测速拨测直接触发 NodeMigration 并清理越界连接
  mock_now += 25000;
  auto act = mon.tick_once();
  UT_CHECK(act == CoreMonitor::Action::Healed);
  UT_CHECK(!probed);
  UT_CHECK(migration_cleaned);

  mon.set_hooks({});
  mon.reset_state();
}

UT_TEST(v111_bug405_core_manager_resolve_and_ensure) {
  util::reset_verify_state_for_test(true);
  CoreManager mgr;
  const RestoreTimings t = fast_timings();

  // Case A：实时 now 为被过滤词排除的 "香港1"，resolve_desired_proxy_selection 放弃它并回退合规持久选点 "新加坡3"
  {
    Bug405MockApi api;
    api.members = {"香港1", "台湾1", "新加坡3"};
    api.current_now = "香港1";
    const std::pair<std::optional<std::string>, std::string> state{
        std::string("新加坡3"), "-香港 -台湾"};

    auto desired = mgr.resolve_desired_proxy_selection(api, state);
    UT_CHECK(desired.has_value() && *desired == "新加坡3");

    UT_CHECK(mgr.ensure_proxy_group_selection(api, desired, t, state));
    UT_CHECK_EQ(api.selected_history.size(), size_t(1));
    UT_CHECK_EQ(api.selected_history[0], std::string("新加坡3"));
    UT_CHECK_EQ(api.closed_filter_calls.size(), size_t(1));
    UT_CHECK_EQ(api.closed_filter_calls[0], std::string("-香港 -台湾"));
  }

  // Case B：冷启动 preferred=nullopt 且当前已落位在合规节点 "新加坡3"，零重复 select 但仍清理越界连接
  {
    Bug405MockApi api;
    api.members = {"香港1", "台湾1", "新加坡3"};
    api.current_now = "新加坡3";
    const std::pair<std::optional<std::string>, std::string> state{
        std::string("新加坡3"), "-香港 -台湾"};

    UT_CHECK(mgr.ensure_proxy_group_selection(api, std::nullopt, t, state));
    UT_CHECK(api.selected_history.empty());
    UT_CHECK_EQ(api.closed_filter_calls.size(), size_t(1));
  }

  // Case C：preferred 为被排除节点 "香港1" 且无持久节点，自动落入合规子集兜底 "新加坡3"
  {
    Bug405MockApi api;
    api.members = {"香港1", "台湾1", "新加坡3"};
    api.current_now = "香港1";
    const std::pair<std::optional<std::string>, std::string> state{
        std::nullopt, "-香港 -台湾"};

    UT_CHECK(mgr.ensure_proxy_group_selection(api, std::string("香港1"), t, state));
    UT_CHECK_EQ(api.selected_history.size(), size_t(1));
    UT_CHECK_EQ(api.selected_history[0], std::string("新加坡3"));
    UT_CHECK_EQ(api.closed_filter_calls.size(), size_t(1));
  }
}

UT_TEST(v111_bug404_dead_ipc_removed) {
  auto& svc = cmd::CommandService::instance();
  svc.install_all();
  UT_CHECK(!svc.exists("get_runtime_config"));
  UT_CHECK(!svc.exists("uninstall_service"));
}
