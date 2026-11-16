// Clash Mini v3.5.0 后端全量对标测试：
// - util/node: has_region_hint / 结构形态横幅 / match_node_filter / 拓扑连坐 / 24h HTTPS 验真状态机
// - enhance: PROXY__METRICS lazy: false / 拓扑去重 / 持久化黑名单跳过与内存连坐重置
// - cmd: get_confirmed_fake_proxy_names / update_profile 手动清除未验真失败
// - monitor & auto_select: Suspect 迟滞 / /connections 流量保鲜 / primary_url_distorted /
//   UiActiveSyncState / decide_self_heal / 120s 切换限速(ConfirmedDead豁免) /
//   API恢复保活 / MANUAL_SWITCH_EPOCH 抢占 / persisted_exit_is_usable / pick_subset_fallback_node
// - quick_routing: serialize_custom_routing_backup / parse_custom_routing_backup
#include "test_util.h"

#include <atomic>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "cmd/command_service.h"
#include "core/auto_select.h"
#include "core/core_manager.h"
#include "core/delay_manager.h"
#include "core/monitor.h"
#include "core/quick_routing.h"
#include "enhance/mini_agreements.h"
#include "util/node.h"
#include "yaml-cpp/yaml.h"

namespace {

struct VerifyTestGuard {
  VerifyTestGuard() { clashwin::util::reset_verify_state_for_test(true); }
  ~VerifyTestGuard() { clashwin::util::reset_verify_state_for_test(true); }
};

struct MonitorTestGuard {
  MonitorTestGuard() { clashwin::core::CoreMonitor::instance().reset_state(); }
  ~MonitorTestGuard() {
    auto& m = clashwin::core::CoreMonitor::instance();
    m.set_hooks({});
    m.set_params({});
    m.reset_state();
  }
};

}  // namespace

using namespace clashwin;

UT_TEST(v350_region_hint_and_structural_banners) {
  using util::has_region_hint;
  using util::is_dummy_node;

  // 地区暗示（国旗 Emoji / CJK / 边界 ASCII）
  UT_CHECK(has_region_hint("🇭🇰 HK-01"));
  UT_CHECK(has_region_hint("香港1 (005024)"));
  UT_CHECK(has_region_hint("Tokyo 专线"));
  UT_CHECK(has_region_hint("CN2-GIA-01"));
  UT_CHECK(has_region_hint("US-01"));
  UT_CHECK(has_region_hint("node-us-west"));
  UT_CHECK(!has_region_hint("永久官网:666.example.hk"));  // `.hk` 域名后缀不误作地区
  UT_CHECK(!has_region_hint("user-01"));

  // 结构形态横幅拦截 vs 原始主机域名放行
  UT_CHECK(is_dummy_node("永久官网:666.boostqz.com"));
  UT_CHECK(is_dummy_node("可用额度:167.26 GB"));
  UT_CHECK(is_dummy_node("套餐截至：2026-12-31"));
  UT_CHECK(is_dummy_node("剩余时长：12天"));
  UT_CHECK(!is_dummy_node("vps1.myserver.com"));  // 纯 ASCII 无冒号真实主机名放行
  UT_CHECK(!is_dummy_node("香港-167.26 GB专线"));  // 含真实地区名不误杀
}

UT_TEST(v350_match_node_filter_syntax) {
  using util::match_node_filter;

  UT_CHECK(match_node_filter("香港1 (005024)", ""));
  UT_CHECK(match_node_filter("香港1 (005024)", "   "));
  UT_CHECK(match_node_filter("日本 01 [IPLC]", "日本 -"));
  UT_CHECK(match_node_filter("HK-BGP-01", "hk"));
  UT_CHECK(match_node_filter("日本 01", "日本 | 新加坡"));
  UT_CHECK(match_node_filter("新加坡-专线", "日本 ｜ 新加坡"));
  UT_CHECK(!match_node_filter("香港1 (005024)", "日本 | 新加坡"));
  UT_CHECK(!match_node_filter("香港1 (005024)", "-香港"));
  UT_CHECK(match_node_filter("日本 01", "-香港"));
  UT_CHECK(!match_node_filter("HK-01", "!hk"));
  UT_CHECK(!match_node_filter("高倍率 3x", "！倍率"));
  UT_CHECK(match_node_filter("日本 专线 01", "日本 | 新加坡 -倍率 -香港"));
  UT_CHECK(!match_node_filter("日本 2x高倍率", "日本 | 新加坡 -倍率 -香港"));
  UT_CHECK(!match_node_filter("香港-日本中转", "日本 | 新加坡 -倍率 -香港"));
  UT_CHECK(match_node_filter("新加坡-01", "-香港|-英国"));
  UT_CHECK(!match_node_filter("英国-01", "-香港|-英国"));
}

UT_TEST(v350_fingerprint_and_clone_topology_taint) {
  YAML::Node m1 = YAML::Load("name: node-a\ntype: ss\nserver: 1.2.3.4\nport: 443\ncipher: aes-128-gcm\n");
  YAML::Node m2 = YAML::Load("cipher: aes-128-gcm\nport: 443\nserver: 1.2.3.4\ntype: ss\nname: 公告:test\n");
  auto fp1 = util::proxy_endpoint_fingerprint(m1);
  auto fp2 = util::proxy_endpoint_fingerprint(m2);
  UT_CHECK(fp1.has_value());
  UT_CHECK(fp2.has_value());
  UT_CHECK_EQ(*fp1, *fp2);  // 忽略 name 与字段顺序

  // Case A: 纯横幅指纹连坐清除（含 dummy 且无地区真节点）
  {
    std::vector<util::RawProxyEntry> entries = {
        {"剩余流量：100GB", fp1, true},
        {"新客服联系:abc", fp1, false},
        {"日本-01", std::string("fp-jp"), false},
    };
    auto out = util::resolve_proxy_clone_topology(entries);
    UT_CHECK_EQ(out.names.size(), size_t(1));
    UT_CHECK_EQ(out.names[0], "日本-01");
    UT_CHECK_EQ(out.tainted_banner_removed, size_t(1));
    UT_CHECK(out.tainted_fps.count(*fp1) == 1);
  }

  // Case B: 寄生横幅与真地区节点同指纹 → 吞掉横幅仅保留真地区名
  {
    std::vector<util::RawProxyEntry> entries = {
        {"官网:x.com", fp1, true},
        {"随机标签", fp1, false},
        {"香港1", fp1, false},
    };
    auto out = util::resolve_proxy_clone_topology(entries);
    UT_CHECK_EQ(out.names.size(), size_t(1));
    UT_CHECK_EQ(out.names[0], "香港1");
    UT_CHECK(out.tainted_fps.empty());
  }

  // Case C: 无地区词全员冒号多条同指纹 → 连坐剔除
  {
    std::vector<util::RawProxyEntry> entries = {
        {"线路状态:正常", std::string("fp-colon"), false},
        {"联系邮箱:a@b", std::string("fp-colon"), false},
    };
    auto out = util::resolve_proxy_clone_topology(entries);
    UT_CHECK(out.names.empty());
    UT_CHECK_EQ(out.tainted_banner_removed, size_t(2));
  }
}

UT_TEST(v350_https_verify_state_machine_and_24h_ttl) {
  VerifyTestGuard g;
  std::map<std::string, util::NodeVerifyRecord> records;
  const std::string good = "fp-good";
  const std::string dead = "fp-dead";

  // 离线保护：passed 为空时不得累加失败
  auto o0 = util::apply_verify_batch_to_records(records, {}, {dead}, 100);
  UT_CHECK(!o0.dirty);
  UT_CHECK_EQ(records.size(), size_t(0));

  // 前 5 次在有参照通过时失败：未达 >5 阈值，仅 fail_count 递增
  for (int i = 1; i <= 5; ++i) {
    auto o = util::apply_verify_batch_to_records(records, {good}, {dead}, 100 + i);
    UT_CHECK(o.dirty);
    UT_CHECK(!records[dead].is_confirmed_fake_at(100 + i));
  }
  UT_CHECK(records[good].verified_true);

  // 第 6 次失败（fail_count == 6 > 5）触发 24h 软隔离
  auto o6 = util::apply_verify_batch_to_records(records, {good}, {dead}, 200);
  UT_CHECK_EQ(o6.newly_confirmed_fake, size_t(1));
  UT_CHECK(records[dead].is_confirmed_fake_at(200));
  UT_CHECK(records[dead].is_confirmed_fake_at(200 + util::kHttpsVerifyBanTtlSecs - 1));
  UT_CHECK(!records[dead].is_confirmed_fake_at(200 + util::kHttpsVerifyBanTtlSecs));

  // 期满后再次失败先归零再记 1，而非直接再封
  const int64_t after_ttl = 200 + util::kHttpsVerifyBanTtlSecs + 10;
  auto o7 = util::apply_verify_batch_to_records(records, {good}, {dead}, after_ttl);
  UT_CHECK_EQ(records[dead].fail_count, uint8_t(1));
  UT_CHECK(!records[dead].is_confirmed_fake_at(after_ttl));
  UT_CHECK_EQ(o7.newly_confirmed_fake, size_t(0));

  // verified_true 单向粘滞：即便后续未通过也不会被降级或累计失败
  auto o8 = util::apply_verify_batch_to_records(records, {dead}, {good}, after_ttl + 5);
  UT_CHECK(records[good].verified_true);
  UT_CHECK_EQ(records[good].fail_count, uint8_t(0));
  UT_CHECK(records[dead].verified_true);
  UT_CHECK_EQ(records[dead].fail_count, uint8_t(0));

  // 活跃节点即时迁移门槛（>= 2 次失败且未过 24h TTL）
  UT_CHECK(!util::record_has_verify_failure(false, 1, 100, 110));
  UT_CHECK(util::record_has_verify_failure(false, 2, 100, 110));
  UT_CHECK(!util::record_has_verify_failure(true, 3, 100, 110));
  UT_CHECK(!util::record_has_verify_failure(false, 3, 100, 100 + util::kHttpsVerifyBanTtlSecs));
}

UT_TEST(v350_enhance_quarantine_and_clear_failures) {
  VerifyTestGuard g;
  YAML::Node cfg = YAML::Load(
      "proxies:\n"
      "- name: HK-Real\n  type: ss\n  server: 10.0.0.1\n  port: 443\n"
      "- name: Banner-Leak\n  type: ss\n  server: 10.0.0.2\n  port: 443\n"
      "- name: 公告：到期提醒\n  type: ss\n  server: 10.0.0.2\n  port: 443\n"
      "- name: Fake-Node\n  type: ss\n  server: 10.0.0.3\n  port: 443\n");
  enhance::MiniAgreementOptions opt;
  YAML::Node out1 = enhance::enforce_mini_agreements(cfg, opt);
  UT_CHECK_EQ(out1["proxy-groups"][1]["lazy"].Scalar(), "false");
  // Banner-Leak 被同指纹横幅连坐清除
  UT_CHECK_EQ(out1["proxy-groups"][0]["proxies"].size(), size_t(2));
  auto fakes = util::confirmed_fake_node_names();
  UT_CHECK_EQ(fakes.size(), size_t(2));  // Banner-Leak + 公告：到期提醒

  // 将 Fake-Node 推入 HTTPS 软隔离，并确认 clear_unverified_https_failures 清除失败保留已验真
  for (int i = 0; i < 6; ++i) {
    util::record_https_verify_results({"HK-Real"}, {"Fake-Node"});
  }
  UT_CHECK(util::is_confirmed_fake_node("Fake-Node"));
  UT_CHECK(util::is_verified_true_node("HK-Real"));

  YAML::Node out2 = enhance::enforce_mini_agreements(cfg, opt);
  UT_CHECK_EQ(out2["proxy-groups"][0]["proxies"].size(), size_t(1));
  UT_CHECK_EQ(out2["proxy-groups"][0]["proxies"][0].Scalar(), "HK-Real");

  const size_t cleared = util::clear_unverified_https_failures();
  UT_CHECK(cleared >= 1);
  UT_CHECK(!util::is_confirmed_fake_node("Fake-Node"));
  UT_CHECK(util::is_verified_true_node("HK-Real"));
}

UT_TEST(v350_cmd_confirmed_fake_proxy_names) {
  VerifyTestGuard g;
  YAML::Node cfg = YAML::Load(
      "proxies:\n"
      "- name: HK-1\n  type: ss\n  server: 1.1.1.1\n  port: 443\n"
      "- name: Unlabeled-1\n  type: ss\n  server: 2.2.2.2\n  port: 443\n"
      "- name: 剩余：10GB\n  type: ss\n  server: 2.2.2.2\n  port: 443\n");
  enhance::enforce_mini_agreements(cfg, {});
  auto& svc = cmd::CommandService::instance();
  svc.install_all();
  auto res = svc.invoke("get_confirmed_fake_proxy_names", {});
  UT_CHECK(res.is_ok());
  UT_CHECK(res.value().is_array());
  UT_CHECK_EQ(res.value().size(), size_t(2));
}

UT_TEST(v350_continuity_first_monitor_pure_logic) {
  using CM = core::CoreMonitor;

  // 1. NodeStatus 三态迟滞（Suspect < 30s vs Dead >= 30s）
  UT_CHECK(CM::classify_node_status(false, std::nullopt, 5).kind ==
           CM::NodeStatusKind::Unknown);
  auto alive = CM::classify_node_status(true, 120, 5);
  UT_CHECK(alive.kind == CM::NodeStatusKind::Alive && alive.delay_ms == 120);
  auto suspect = CM::classify_node_status(false, 120, 29);
  UT_CHECK(suspect.kind == CM::NodeStatusKind::Suspect);
  UT_CHECK(!CM::should_proceed_with_self_heal(suspect));
  auto dead = CM::classify_node_status(false, 120, 30);
  UT_CHECK(dead.kind == CM::NodeStatusKind::Dead);
  UT_CHECK(CM::should_proceed_with_self_heal(dead));

  // 2. 被动连接流量分流（Flowing 480s / Stalled 60s / Idle 300s）
  std::optional<CM::ActiveTrafficTracker> tracker;
  CM::update_active_traffic_state(
      tracker, "NodeA", {{"c1", 100, 100}}, 1000);
  auto [s_flow, _cd1] = CM::update_active_traffic_state(
      tracker, "NodeA", {{"c1", 150, 300}}, 15000);
  UT_CHECK(s_flow == CM::ActiveTrafficState::Flowing);
  UT_CHECK(!CM::should_allow_https_migration(s_flow));
  UT_CHECK(!CM::should_probe_active_node(alive, 479, s_flow, false));
  UT_CHECK(CM::should_probe_active_node(alive, 480, s_flow, false));

  auto [s_stall, _cd2] = CM::update_active_traffic_state(
      tracker, "NodeA", {{"c1", 300, 300}}, 30000);
  UT_CHECK(s_stall == CM::ActiveTrafficState::Stalled);
  UT_CHECK(CM::should_allow_https_migration(s_stall));
  UT_CHECK(CM::should_probe_active_node(alive, 10, s_stall, false));
  UT_CHECK(!CM::should_probe_active_node(alive, 10, s_stall, true));

  UT_CHECK(!CM::should_probe_active_node(alive, 299, CM::ActiveTrafficState::Idle, false));
  UT_CHECK(CM::should_probe_active_node(alive, 300, CM::ActiveTrafficState::Idle, false));

  // 3. primary_url_distorted UI 显示与 UiActiveSyncState
  const std::vector<std::string> all_nodes = {"HK-1", "HK-2"};
  const std::map<std::string, uint32_t> partial = {{"HK-1", 150}};
  auto d_normal = CM::build_probe_ui_display_with(
      all_nodes, partial, false, [](const std::string&) { return false; });
  UT_CHECK_EQ(d_normal.size(), size_t(2));
  UT_CHECK_EQ(d_normal[1].second, uint32_t(0));
  auto d_distorted = CM::build_probe_ui_display_with(
      all_nodes, partial, true, [](const std::string&) { return false; });
  UT_CHECK_EQ(d_distorted.size(), size_t(1));

  CM::UiActiveSyncState alive_prev{"HK-1", "t1", true};
  auto next_timeout = CM::compute_next_ui_active_sync(
      "HK-1", {{"HK-1", 0}}, std::string("t1"), &alive_prev);
  UT_CHECK(next_timeout.has_value());
  UT_CHECK(!next_timeout->reported_alive);
  UT_CHECK(!next_timeout->history_time.has_value());
  const std::string same_t = "t1";
  UT_CHECK(CM::should_sync_alive_ui(false, &*next_timeout, "HK-1", &same_t));

  // 4. decide_self_heal / 切换限速 / API 恢复保活 / persisted_exit_is_usable
  UT_CHECK(CM::decide_self_heal({CM::SelfHealTrigger::Kind::NetworkRecovered, 0, false}) ==
           CM::SelfHealDecision::Wait);
  UT_CHECK(CM::decide_self_heal({CM::SelfHealTrigger::Kind::ApiUnreachable, 3, true}) ==
           CM::SelfHealDecision::Wait);
  UT_CHECK(CM::decide_self_heal({CM::SelfHealTrigger::Kind::ApiUnreachable, 3, false}) ==
           CM::SelfHealDecision::Allow);
  UT_CHECK(!CM::self_heal_subject_to_switch_rate_limit(CM::SelfHealOrigin::ConfirmedDead));
  UT_CHECK(CM::self_heal_subject_to_switch_rate_limit(CM::SelfHealOrigin::NodeMigration));
  UT_CHECK(CM::self_heal_subject_to_switch_rate_limit(CM::SelfHealOrigin::ApiChannelDown));
  UT_CHECK(!CM::auto_switch_permitted(119, 0));
  UT_CHECK(CM::auto_switch_permitted(120, 2));
  UT_CHECK(!CM::auto_switch_permitted(120, 3));
  UT_CHECK(CM::should_keep_alive_on_api_recovery(true, alive));
  UT_CHECK(!CM::should_keep_alive_on_api_recovery(true, dead));
  UT_CHECK(CM::persisted_exit_is_usable(
      std::string("HK-1"), &all_nodes, "hk|jp", false));
  UT_CHECK(!CM::persisted_exit_is_usable(
      std::string("HK-1"), &all_nodes, "sg", false));
  UT_CHECK(!CM::persisted_exit_is_usable(
      std::string("DIRECT"), &all_nodes, "", false));
}

UT_TEST(v350_rate_limit_confirmed_dead_exempt_and_manual_preemption) {
  MonitorTestGuard mg;
  auto& m = core::CoreMonitor::instance();

  // A. NodeMigration (is_dummy_active) 受 120s 限速，ConfirmedDead 豁免（只受 25s 冷却）
  int64_t clock = 10000;
  int heals = 0;
  std::string active = "Node-1";
  bool dummy_active = true;
  bool node_ok = true;
  core::CoreMonitor::Hooks h;
  h.core_alive = [] { return true; };
  h.network_online = [] { return true; };
  h.api_alive = [] { return true; };
  h.current_uid = [] { return std::string("u1"); };
  h.active_node_name = [&] { return active; };
  h.is_dummy_active = [&] { return dummy_active; };
  h.node_healthy = [&] { return node_ok; };
  h.auto_select_trigger = [&]() -> Result<bool> {
    ++heals;
    active = "Node-" + std::to_string(heals + 1);
    return Result<bool>::ok(true);
  };
  h.now_ms = [&] { return clock; };
  m.set_hooks(h);

  // 先建立活跃节点，再跨越 20s 切换宽限期以测后续 ConfirmedDead
  UT_CHECK(m.tick_once() == core::CoreMonitor::Action::Healed);  // 第 1 次 NodeMigration 成功（Node-1 -> Node-2）
  UT_CHECK_EQ(heals, 1);

  // 30s 后（> 25s 冷却但 < 120s 限速）再次 NodeMigration → 被限速拦截
  clock += 30000;
  UT_CHECK(m.tick_once() == core::CoreMonitor::Action::Idle);
  UT_CHECK_EQ(heals, 1);

  // 跨过 Node-2 的 20s 切换宽限期但仍在 120s 限速期内（累计仅 55s），ConfirmedDead 豁免限速立即放行
  clock += 25000;
  dummy_active = false;
  node_ok = false;
  m.tick_once();
  UT_CHECK(m.tick_once() == core::CoreMonitor::Action::Healed);
  UT_CHECK_EQ(heals, 2);

  // B. MANUAL_SWITCH_EPOCH 抢占测速中的 AutoSelect
  auto& as = core::AutoSelect::instance();
  core::AutoSelectHooks ah;
  ah.is_dummy = [](const std::string&) { return false; };
  ah.current_uid = [] { return std::optional<std::string>("u1"); };
  ah.filter_text = [] { return std::string(); };
  int selects = 0;
  ah.select_node = [&](const std::string&, const std::string&) {
    ++selects;
    return true;
  };
  as.set_hooks(std::move(ah));

  core::DelayProbeHooks dh;
  dh.group_probe = [&](const std::string&) {
    // 模拟测速过程中用户手动切节点
    m.notify_manual_node_switched();
    return std::optional<std::map<std::string, uint32_t>>(
        std::map<std::string, uint32_t>{{"Node-1", 80}});
  };
  dh.emit_results = [](const std::string&,
                       const std::vector<std::pair<std::string, uint32_t>>&,
                       bool) {};
  core::DelayManager::instance().set_hooks(std::move(dh));

  auto out = as.trigger("u1", std::vector<std::string>{"Node-1"}, 1, true);
  UT_CHECK(out.preempted);
  UT_CHECK(!out.selected);
  UT_CHECK_EQ(selects, 0);

  // C. pick_subset_fallback_node 过滤边界锁定与已验真优先
  VerifyTestGuard vg;
  std::vector<std::string> all = {"购买", "HK-1", "JP-1", "JP-Verified"};
  auto is_valid = [](const std::string& n) { return !util::is_dummy_node(n); };
  auto is_verified = [](const std::string& n) { return n == "JP-Verified"; };
  UT_CHECK_EQ(core::pick_subset_fallback_node(all, "jp", is_valid, is_verified),
              "JP-Verified");
  UT_CHECK(core::pick_subset_fallback_node(all, "sg", is_valid, is_verified)
               .empty());  // 绝不跨过滤边界选中 HK-1
  UT_CHECK_EQ(core::pick_subset_fallback_node(all, "", is_valid, is_verified),
              "JP-Verified");
}

UT_TEST(v350_custom_routing_backup_roundtrip) {
  using namespace core::quick_routing;

  const std::vector<std::string> rules = {
      "DOMAIN,example.com,DIRECT",
      "DOMAIN,example.com,PROXY",   // 同身份键去重（保留首次出现）
      "IP-CIDR,1.2.3.4/32,PROXY",
      "PROCESS-NAME,curl.exe,REJECT",
  };
  const std::string yaml_str =
      serialize_custom_routing_backup(rules, "2026-11-17T00:00:00.000Z");
  UT_CHECK(yaml_str.find("# Clash Mini Custom Routing Rules Backup") == 0);
  UT_CHECK(yaml_str.find("version: 1") != std::string::npos);
  UT_CHECK(yaml_str.find("exportedAt: 2026-11-17T00:00:00.000Z") !=
               std::string::npos ||
           yaml_str.find("exportedAt: '2026-11-17T00:00:00.000Z'") !=
               std::string::npos);

  auto parsed = parse_custom_routing_backup(yaml_str);
  UT_CHECK_EQ(parsed.size(), size_t(3));
  UT_CHECK_EQ(parsed[0], "DOMAIN,example.com,DIRECT");
  UT_CHECK_EQ(parsed[1], "IP-CIDR,1.2.3.4/32,PROXY");
  UT_CHECK_EQ(parsed[2], "PROCESS-NAME,curl.exe,REJECT");

  // 支持 `rules:` 键及顶层数组，遇到非法内容安全返回空
  auto from_seq = parse_custom_routing_backup(
      "- DOMAIN,a.com,PROXY\n- INVALID,foo,BAR\n- IP-CIDR6,::1/128,DIRECT\n");
  UT_CHECK_EQ(from_seq.size(), size_t(2));
  UT_CHECK(parse_custom_routing_backup(":::invalid[[[").empty());
}
