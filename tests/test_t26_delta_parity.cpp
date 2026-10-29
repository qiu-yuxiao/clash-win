// T26 测试：v3.2.1 增量对齐（BUG-304/305/308/310）
// 1. BUG-304：延迟判死阈值 2000→5000（前后端单一真源 + 五档色 + 自动选点）
// 2. BUG-305：TUN 切换触发 RESTART_CORE（含 Service 模式关闭 TUN 回退 Sidecar）
// 3. BUG-308：dns.nameserver 境外明文公共 DNS 上游改写为 DoH
// 4. BUG-310：未被最终 rules 引用的 rule-providers 裁剪（保留 gfwlist）
#include "test_util.h"

#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "cmd/verge_flags.h"
#include "core/auto_select.h"
#include "core/delay_manager.h"
#include "enhance/dns_defaults.h"
#include "enhance/enhance.h"
#include "enhance/mini_agreements.h"
#include "ui/scenes/node_data.h"

using namespace clashwin;

namespace {

std::vector<std::string> scalar_seq(const YAML::Node& seq) {
  std::vector<std::string> out;
  if (!seq || !seq.IsSequence()) return out;
  for (const auto& n : seq) {
    if (n.IsScalar()) out.push_back(n.Scalar());
  }
  return out;
}

}  // namespace

// ================= BUG-304 =================
UT_TEST(t26_delay_threshold_single_source) {
  // 前后端常量必须同为 5000（BUG-304 单一真源；经变量比较避免 C4127）
  uint32_t core_max = core::kNodeDelayMaxMs;
  int ui_max = ui::scene::kNodeDelayMaxMs;
  uint32_t core_min = core::kNodeDelayMinMs;
  int ui_min = ui::scene::kNodeDelayMinMs;
  UT_CHECK_EQ(core_max, 5000u);
  UT_CHECK_EQ(ui_max, 5000);
  UT_CHECK_EQ(core_min, 30u);
  UT_CHECK_EQ(ui_min, 30);

  // 五档色：<250 success / 250~399 primary / 400~4999 warning / 0 或 >=5000 error
  UT_CHECK(ui::scene::delay_tier(0) == ui::scene::DelayTier::Timeout);
  UT_CHECK(ui::scene::delay_tier(249) == ui::scene::DelayTier::Success);
  UT_CHECK(ui::scene::delay_tier(250) == ui::scene::DelayTier::Mid);
  UT_CHECK(ui::scene::delay_tier(399) == ui::scene::DelayTier::Mid);
  UT_CHECK(ui::scene::delay_tier(400) == ui::scene::DelayTier::Slow);
  UT_CHECK(ui::scene::delay_tier(2000) == ui::scene::DelayTier::Slow);
  UT_CHECK(ui::scene::delay_tier(4999) == ui::scene::DelayTier::Slow);
  UT_CHECK(ui::scene::delay_tier(5000) == ui::scene::DelayTier::Timeout);

  // 文本：4999 展示真实值；5000 才 Timeout；>1e5 = Error
  UT_CHECK(ui::scene::format_delay(4999) == L"4999");
  UT_CHECK(ui::scene::format_delay(5000) == L"Timeout");
  UT_CHECK(ui::scene::format_delay(100000) == L"Timeout");
  UT_CHECK(ui::scene::format_delay(100001) == L"Error");

  // 自动选点：candidates = [30, 5000)
  auto r = core::AutoSelect::pick_fastest({{"dead", 0}, {"slow", 2000}, {"edge", 5000}});
  UT_CHECK(r && *r == "slow");
  UT_CHECK(!core::AutoSelect::pick_fastest({{"a", 5000}, {"b", 10000}}).has_value());
  UT_CHECK(core::AutoSelect::pick_fastest({{"a", 4999}}).has_value());
}

// ================= BUG-305 =================
UT_TEST(t26_tun_toggle_restart_flags) {
  config::IVerge old_cfg;
  config::IVerge patch;

  // 关闭 TUN 且内核正运行在 Service 模式 → 必须 RESTART_CORE 退出服务模式
  old_cfg.enable_tun_mode = true;
  patch.enable_tun_mode = false;
  uint16_t f = cmd::determine_update_flags(old_cfg, patch, /*running_service=*/true);
  UT_CHECK((f & cmd::kFRestartCore) != 0);
  UT_CHECK((f & cmd::kFClashConfig) != 0);  // 标志仍置位，但 process 会因 RESTART_CORE 跳过 reload

  // 关闭 TUN 且已是 Sidecar 模式 → 仍因 current_tun=true 重启
  f = cmd::determine_update_flags(old_cfg, patch, /*running_service=*/false);
  UT_CHECK((f & cmd::kFRestartCore) != 0);

  // 开启 TUN（原关闭 / Sidecar）→ 重启并切 Service
  old_cfg.enable_tun_mode = false;
  patch.enable_tun_mode = true;
  f = cmd::determine_update_flags(old_cfg, patch, /*running_service=*/false);
  UT_CHECK((f & cmd::kFRestartCore) != 0);

  // 开启 TUN 且原本已开、内核已在 Service → 无需重启
  old_cfg.enable_tun_mode = true;
  patch.enable_tun_mode = true;
  f = cmd::determine_update_flags(old_cfg, patch, /*running_service=*/true);
  UT_CHECK((f & cmd::kFRestartCore) == 0);

  // 开启 TUN 且原本已开但内核仍在 Sidecar → 需重启切 Service
  f = cmd::determine_update_flags(old_cfg, patch, /*running_service=*/false);
  UT_CHECK((f & cmd::kFRestartCore) != 0);

  // 关闭 TUN 且原已关闭、Sidecar → 无需重启
  old_cfg.enable_tun_mode = false;
  patch.enable_tun_mode = false;
  f = cmd::determine_update_flags(old_cfg, patch, /*running_service=*/false);
  UT_CHECK((f & cmd::kFRestartCore) == 0);

  // 未触碰 TUN 的普通 patch 不应触发重启
  config::IVerge other;
  other.enable_auto_launch = true;
  f = cmd::determine_update_flags(old_cfg, other, /*running_service=*/true);
  UT_CHECK((f & cmd::kFRestartCore) == 0);
  UT_CHECK((f & cmd::kFLaunch) != 0);
}

// ================= BUG-308 =================
UT_TEST(t26_dns_foreign_plain_upstream_to_doh) {
  YAML::Node cfg = YAML::Load(R"(
dns:
  nameserver:
    - 8.8.8.8
    - 8.8.4.4:53
    - 1.1.1.1
    - 1.0.0.1
    - tls://8.8.8.8
    - 114.114.114.114
    - https://doh.pub/dns-query
  default-nameserver:
    - 8.8.8.8
    - 223.5.5.5
)");

  YAML::Node out = enhance::sanitize_foreign_plain_dns_upstreams(cfg);
  auto ns = scalar_seq(out["dns"]["nameserver"]);
  UT_CHECK_EQ(ns.size(), 7u);
  UT_CHECK_EQ(ns[0], "https://8.8.8.8/dns-query");
  UT_CHECK_EQ(ns[1], "https://8.8.4.4/dns-query");
  UT_CHECK_EQ(ns[2], "https://1.1.1.1/dns-query");
  UT_CHECK_EQ(ns[3], "https://1.0.0.1/dns-query");
  // 已加密上游与境内明文上游原样保留
  UT_CHECK_EQ(ns[4], "tls://8.8.8.8");
  UT_CHECK_EQ(ns[5], "114.114.114.114");
  UT_CHECK_EQ(ns[6], "https://doh.pub/dns-query");

  // default-nameserver 按协议必须保持明文，绝不改写
  auto dns_default = scalar_seq(out["dns"]["default-nameserver"]);
  UT_CHECK_EQ(dns_default.size(), 2u);
  UT_CHECK_EQ(dns_default[0], "8.8.8.8");

  // 无 dns / 无 nameserver 时安全直通
  YAML::Node plain = YAML::Load("mixed-port: 7890\n");
  YAML::Node pass = enhance::sanitize_foreign_plain_dns_upstreams(plain);
  UT_CHECK_EQ(pass["mixed-port"].as<int>(), 7890);
}

// ================= 流水线集成（调用顺序） =================
UT_TEST(t26_enhance_pipeline_applies_delta_parity) {
  enhance::EnhanceInput in;
  in.clash_config = YAML::Load(R"(
dns:
  nameserver:
    - 8.8.8.8
rule-providers:
  Unused:
    type: http
    behavior: domain
    url: https://example.com/unused.mrs
    path: ./ruleset/unused.mrs
)");
  in.verge.enable_builtin = false;  // 避免 QuickJS 分支
  in.profile.config = YAML::Node(YAML::NodeType::Map);

  enhance::EnhanceOutput out = enhance::enhance_core(in);
  auto ns = scalar_seq(out.config["dns"]["nameserver"]);
  UT_CHECK(!ns.empty());
  UT_CHECK_EQ(ns[0], "https://8.8.8.8/dns-query");

  const YAML::Node providers = out.config["rule-providers"];
  UT_CHECK(providers && providers.IsMap());
  UT_CHECK(!providers["Unused"].IsDefined());
  UT_CHECK(providers["gfwlist"].IsDefined());
}

// ================= BUG-310 =================
UT_TEST(t26_unreferenced_rule_providers_pruned) {
  YAML::Node cfg = YAML::Load(R"(
proxies: []
proxy-groups: []
rule-providers:
  China-Site:
    type: http
    behavior: domain
    url: https://example.com/china.mrs
    path: ./ruleset/china.mrs
  Custom:
    type: http
    behavior: domain
    url: https://example.com/custom.mrs
    path: ./ruleset/custom.mrs
  UsedByPrepend:
    type: http
    behavior: domain
    url: https://example.com/used.mrs
    path: ./ruleset/used.mrs
prepend-rules:
  - RULE-SET,UsedByPrepend,DIRECT
append-rules:
  - RULE-SET,Custom,PROXY
)");

  enhance::MiniAgreementOptions opt;
  opt.test_url = "https://cp.cloudflare.com/generate_204";
  opt.rule_fallback = "direct";
  YAML::Node out = enhance::enforce_mini_agreements(cfg, opt);

  const YAML::Node providers = out["rule-providers"];
  UT_CHECK(providers && providers.IsMap());
  // 被 prepend/append rules 引用的保留；未引用的 China-Site 裁剪；gfwlist 恒注入
  UT_CHECK(providers["UsedByPrepend"].IsDefined());
  UT_CHECK(providers["Custom"].IsDefined());
  UT_CHECK(providers["gfwlist"].IsDefined());
  UT_CHECK(!providers["China-Site"].IsDefined());

  // 规则链仍包含用户前置/后置规则与 gfwlist
  std::vector<std::string> rules;
  for (const auto& r : out["rules"]) {
    if (r.IsScalar()) rules.push_back(r.Scalar());
  }
  bool has_prepend = false, has_custom = false, has_gfw = false;
  for (const auto& r : rules) {
    if (r == "RULE-SET,UsedByPrepend,DIRECT") has_prepend = true;
    if (r == "RULE-SET,Custom,PROXY") has_custom = true;
    if (r == "RULE-SET,gfwlist,PROXY") has_gfw = true;
  }
  UT_CHECK(has_prepend);
  UT_CHECK(has_custom);
  UT_CHECK(has_gfw);
}
