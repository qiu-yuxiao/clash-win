// T08 门禁测试：enhance_core 全流水线
//   合成轨：确定性输入 → 断言组/规则顺序/后缀格式与手工推导一致
//   实机 golden 轨（G3 最强证据）：只读实机 {config.yaml, verge.yaml,
//   profiles.yaml, profiles/*} → enhance_core → 与实机 clash-win.yaml
//   逐行 diff（按 golden 顶层键序重排消去实机 HashSet 随机序运行时差异）
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <sstream>
#include <string>
#include <time.h>
#include <vector>

#include "enhance/enhance.h"
#include "enhance/enhance_constants.h"
#include "enhance/field.h"
#include "enhance/merge.h"
#include "config/yaml_serde.h"
#include "test_enhance_util.h"
#include "test_util.h"
#include "util/yaml_utils.h"

using namespace clashwin;
using namespace clashwin::enhance;

namespace {

// 模拟 clash.yaml（IClashTemp 模板核心键）
YAML::Node synthetic_clash_config() {
  return YAML::Load(
      "config_version: 2\n"
      "mixed-port: 10801\n"
      "socks-port: 10802\n"
      "port: 10803\n"
      "log-level: warning\n"
      "allow-lan: false\n"
      "ipv6: false\n"
      "mode: rule\n"
      "tcp-concurrent: false\n"
      "dns:\n  enable: true\n  cache-size: 512\n"
      "profile:\n  store-selected: true\n  store-fake-ip: false\n"
      "external-controller: 127.0.0.1:9098\n"
      "external-controller-pipe: \\\\.\\pipe\\mini-mihomo-win\n"
      "tun:\n  enable: false\n  stack: gvisor\n  auto-route: true\n"
      "  strict-route: false\n  auto-detect-interface: true\n"
      "  dns-hijack:\n  - any:53\n  - tcp://any:53\n"
      "secret: adapted-by-qiu-yuxiao\n");
}

EnhanceVergeOptions synthetic_verge() {
  EnhanceVergeOptions v;
  v.clash_core = "mini-mihomo";
  v.enable_tun = true;
  v.enable_builtin = true;
  v.socks_enabled = false;
  v.http_enabled = false;
  v.enable_external_controller = false;
  v.rule_fallback = "direct";
  v.default_latency_test = "";
  return v;
}

}  // namespace

UT_TEST(t08_enhance_core_synthetic) {
  // 订阅（Rust 语义：current_mapping 与 get_merged_proxies 读同一 R 文件；
  // proxies 最终被 merged 版本整体替换——含 dummy 广告节点 + 重名 + 无效 server）
  auto dir = enhance_test::temp_dir("clashwin_t08_syn");
  CreateDirectoryW((dir + L"\\profiles").c_str(), nullptr);
  {
    std::ofstream f(dir + L"\\profiles\\Rtest1.yaml", std::ios::binary);
    f << "mixed-port: 7892\n"
         "mode: rule\n"
         "tcp-concurrent: true\n"
         "proxies:\n"
         "- name: 网址：https://ad.example\n  type: ss\n  server: ok.example\n"
         "- name: HK-1\n  type: ss\n  server: 1.2.3.4\n"
         "- name: US-1\n  type: vmess\n  server: 5.6.7.8\n"
         "- name: v0-网址:ad.example\n  type: vmess\n  server: '1'\n"
         "proxy-groups:\n- name: 原组\n  type: select\n  proxies:\n  - HK-1\n";
  }

  EnhanceInput input;
  input.clash_config = synthetic_clash_config();
  input.verge = synthetic_verge();
  input.profile.config =
      util::load_yaml_file(dir + L"\\profiles\\Rtest1.yaml").value();
  input.profile.profile_name = "测试订阅";
  input.profile.global_merge.kind = ChainItem::Kind::Merge;
  input.profile.global_merge.merge = YAML::Load(
      "prepend-rules:\n- DOMAIN-SUFFIX,google.com,PROXY\n");
  input.profile.global_script.kind = ChainItem::Kind::Script;
  input.profile.global_script.script = consts::kItemScript;
  input.profile.merge_item.kind = ChainItem::Kind::Merge;
  input.profile.script_item.kind = ChainItem::Kind::Script;
  input.profile.script_item.script = consts::kItemScript;
  input.profile.rules_item.kind = ChainItem::Kind::Rules;
  input.profile.proxies_item.kind = ChainItem::Kind::Proxies;
  input.merged_proxies_source.profiles_dir = dir + L"\\profiles";
  input.merged_proxies_source.items = {{"remote", "Rtest1.yaml", 1792947899}};

  EnhanceOutput out = enhance_core(input);
  YAML::Node& c = out.config;

  // ① 顶层：HANDLE 段顺序 + merge_default 覆盖
  std::vector<std::string> keys;
  for (const auto& kv : c) keys.push_back(kv.first.Scalar());
  UT_CHECK(!keys.empty() && keys[0] == "mode");
  UT_CHECK(c["mixed-port"].Scalar() == "10801");   // clash.yaml 覆盖订阅
  UT_CHECK(c["allow-lan"].Scalar() == "false");
  UT_CHECK(c["ipv6"].Scalar() == "true");          // use_tun 强制
  UT_CHECK(c["external-controller"].Scalar() == "");   // 开关关闭 → ''
  UT_CHECK(!c["socks-port"].IsDefined());          // socks 未启用删键
  UT_CHECK(!c["port"].IsDefined());                // http 未启用删键
  UT_CHECK(!c["config_version"].IsDefined());      // 剥离
  UT_CHECK(c["tcp-concurrent"].Scalar() == "true");  // 订阅优先仅补缺

  // ② tun：clash.yaml 深合并 + use_tun 注入
  UT_CHECK(c["tun"]["stack"].Scalar() == "gvisor");       // clash 值覆盖
  UT_CHECK(c["tun"]["enable"].Scalar() == "true");        // use_tun
  UT_CHECK(c["tun"]["inet6-address"][0].Scalar() == "fdfe:dcba:9876::1/126");

  // ③ dns：订阅优先 + 补缺 + fake-ip 注入（enable_tun + fake-ip 模式）
  UT_CHECK(c["dns"]["enable"].Scalar() == "true");
  UT_CHECK(c["dns"]["enhanced-mode"].Scalar() == "fake-ip");
  UT_CHECK(c["dns"]["fake-ip-range"].Scalar() == "198.18.0.1/16");
  UT_CHECK(c["dns"]["nameserver"].IsDefined());

  // ④ proxies：merged 整体替换（4 节点均带 HHMMSS 后缀）
  const YAML::Node& proxies = c["proxies"];
  UT_CHECK_EQ(proxies.size(), size_t(4));
  auto suffix = format_updated_suffix(1792947899);
  bool found_hk = false, found_ad = false, found_invalid = false;
  for (const auto& p : proxies) {
    const std::string& n = p["name"].Scalar();
    if (n == "HK-1 (" + suffix + ")") found_hk = true;
    if (n == "网址：https://ad.example (" + suffix + ")") found_ad = true;
    if (n == "v0-网址:ad.example (" + suffix + ")") found_invalid = true;
  }
  UT_CHECK(found_hk);
  UT_CHECK(found_ad);
  UT_CHECK(found_invalid);

  // ⑤ 组：PROXY（dummy 名与无效 server '1' 不入组 → 2 有效成员）
  // + PROXY__METRICS 同员
  UT_CHECK_EQ(c["proxy-groups"].size(), size_t(2));
  const YAML::Node& g0 = c["proxy-groups"][0];
  UT_CHECK(g0["name"].Scalar() == "PROXY");
  UT_CHECK_EQ(g0["proxies"].size(), size_t(2));  // HK-1 US-1
  const YAML::Node& g1 = c["proxy-groups"][1];
  UT_CHECK(g1["name"].Scalar() == "PROXY__METRICS");
  UT_CHECK_EQ(g1["proxies"].size(), size_t(2));

  // ⑥ 规则链顺序：prepend → RULE-SET → 私网 → MATCH,DIRECT
  const YAML::Node& rules = c["rules"];
  UT_CHECK(rules[0].Scalar() == "DOMAIN-SUFFIX,google.com,PROXY");
  UT_CHECK(rules[1].Scalar() == "RULE-SET,gfwlist,PROXY");
  bool saw_geoip = false;
  for (const auto& r : rules) {
    if (r.Scalar() == "GEOIP,CN,DIRECT") saw_geoip = true;
  }
  UT_CHECK(saw_geoip);
  UT_CHECK(rules[rules.size() - 1].Scalar() == "MATCH,DIRECT");

  // ⑦ gfwlist provider
  UT_CHECK(c["rule-providers"]["gfwlist"]["path"].Scalar() ==
           "./ruleset/gfwlist.yaml");

  // ⑧ use_sort：proxies/proxy-groups/rule-providers 尾段 + dns 最尾（MDS 移尾）
  UT_CHECK(keys[keys.size() - 1] == "dns");
}

UT_TEST(t08_enhance_core_empty_profile) {
  // 无 current 订阅 → 空订阅 + 空 proxies → PROXY 组回落 DIRECT
  EnhanceInput input;
  input.clash_config = synthetic_clash_config();
  input.verge = synthetic_verge();
  input.verge.enable_tun = false;
  input.profile.config = YAML::Node(YAML::NodeType::Map);
  input.profile.global_merge.kind = ChainItem::Kind::Merge;
  input.profile.global_script.kind = ChainItem::Kind::Script;
  input.profile.global_script.script = consts::kItemScript;
  input.profile.merge_item.kind = ChainItem::Kind::Merge;
  input.profile.script_item.kind = ChainItem::Kind::Script;
  input.profile.script_item.script = consts::kItemScript;
  input.profile.rules_item.kind = ChainItem::Kind::Rules;
  input.profile.proxies_item.kind = ChainItem::Kind::Proxies;

  EnhanceOutput out = enhance_core(input);
  const YAML::Node& c = out.config;
  UT_CHECK(c["proxies"].IsDefined());       // 总是注入（可空 seq）
  UT_CHECK_EQ(c["proxies"].size(), size_t(0));
  UT_CHECK_EQ(c["proxy-groups"].size(), size_t(2));
  UT_CHECK_EQ(c["proxy-groups"][0]["proxies"].size(), size_t(1));
  UT_CHECK(c["proxy-groups"][0]["proxies"][0].Scalar() == "DIRECT");
  UT_CHECK(c["proxy-groups"][1]["proxies"][0].Scalar() == "DIRECT");
  // ipv6=false（use_tun 关）
  UT_CHECK(c["ipv6"].Scalar() == "false");
}

// ---------- G3 golden 轨 ----------
//
// 【固化语义（09:49 实机漂移事故后）】
// 实机 %APPDATA% 产物会被 Clash Mini 的新构建/新工具重写（已实证：09:49 一次
// 生成把 serde_yaml_ng 风格换成 go-yaml v3 风格），活体对照不具备确定性。
// 因此 G3 防回退由两层承担：
//   1. t08_golden_fixture（阻塞）：输入 = tests/fixtures/golden/ 下实机配置的
//      只读快照（与 T08 验收时逐字节一致），期望 = expected_clash_mini.yaml
//      （T08 验收通过的实现所生成；验收时与实机 01:18 产物逐行 diff 为空）。
//      逐行 diff 非空 = 实现回退，FAIL。
//   2. t08_golden_live_probe（信息性）：读活体实机做对比，失败只打印归因
//      报告供人工评审，不计 FAIL——实机漂移不是我们的缺陷。
// 期望文件首次缺失时自动自种子生成（以当前实现为基准）；此后任何行为变化
// 都会被逐行 diff 捕获。若确需更新基线（有意的行为变更），人工删除
// expected_clash_mini.yaml 重跑并在 commit message 中说明理由。

namespace {

// 【测试环境注入】golden 夹具固化于 UTC+8 实机：expected_clash_win.yaml 里的
// (010459) 后缀出自 enhance.cpp:62-70 format_updated_suffix() 的 localtime_s。
// CI runner 默认 UTC，同一时间戳 1792947899 会产出 (170459)，逐行 diff 全红。
// 原用例把「运行机器时区 = 中国标准时间」当成隐含前提；这里在本用例作用域内
// 显式固定进程本地时区并在结束时 RAII 恢复，使基线比对不再依赖宿主机配置。
// 仅注入环境：diffs.empty() 等关键断言保持逐行严格比对，产品代码零改动。
class ScopedEast8Tz {
 public:
  ScopedEast8Tz() {
    if (const char* prev = std::getenv("TZ")) {
      had_prev_ = true;
      prev_ = prev;
    }
    // MSVC CRT 的 TZ 用「西正东负」偏移写法：UTC-8 表示 UTC+08:00。
    _putenv_s("TZ", "UTC-8");
    _tzset();
  }
  ~ScopedEast8Tz() {
    _putenv_s("TZ", had_prev_ ? prev_.c_str() : "");
    _tzset();
  }
  ScopedEast8Tz(const ScopedEast8Tz&) = delete;
  ScopedEast8Tz& operator=(const ScopedEast8Tz&) = delete;

 private:
  bool had_prev_ = false;
  std::string prev_;
};

std::wstring fixtures_dir() {
  std::string f = __FILE__;  // MSVC：绝对路径
  size_t pos = f.find_last_of("\\/");
  std::string dir = (pos == std::string::npos) ? "." : f.substr(0, pos);
  return clashwin::utf::utf8_to_utf16(dir) + L"\\fixtures\\golden";
}

// 从给定 home（含 config.yaml/verge.yaml/profiles.yaml/profiles/）构造输入
bool build_input_from_home(const std::wstring& home, EnhanceInput& input) {
  auto load_map = [&](const std::wstring& name) {
    auto r = util::load_yaml_file(home + L"\\" + name);
    return r.is_ok() ? r.value() : YAML::Node(YAML::NodeType::Map);
  };

  config::IVerge verge = config::IVerge::from_yaml(load_map(L"verge.yaml"));
  EnhanceVergeOptions v;
  v.clash_core = verge.get_valid_clash_core();
  v.enable_tun = verge.enable_tun_mode.value_or(false);
  v.enable_builtin = verge.enable_builtin_enhanced.value_or(true);
  v.socks_enabled = verge.verge_socks_enabled.value_or(false);
  v.http_enabled = verge.verge_http_enabled.value_or(false);
  v.enable_external_controller = verge.enable_external_controller.value_or(false);
  v.rule_fallback = verge.rule_fallback.value_or("direct");
  v.default_latency_test = verge.default_latency_test.value_or("");

  config::IProfiles profiles =
      config::IProfiles::from_yaml(load_map(L"profiles.yaml"));
  input.clash_config = load_map(L"config.yaml");
  input.verge = v;

  const config::PrfItem* cur =
      profiles.current ? profiles.get_item(*profiles.current) : nullptr;
  if (cur && cur->file) {
    auto cur_map =
        util::load_yaml_file(home + L"\\profiles\\" +
                             clashwin::utf::utf8_to_utf16(*cur->file));
    if (!cur_map.is_ok()) return false;
    input.profile.config = cur_map.value();
    input.profile.profile_name = cur->name.value_or("");
    auto load_chain = [&](const std::optional<std::string>& uid,
                          const char* fallback, ChainItem::Kind k) {
      std::string u = uid && !uid->empty() ? *uid : fallback;
      const config::PrfItem* it = profiles.get_item(u);
      if (it && it->itype && it->file) {
        return chain_item_load(home + L"\\profiles", u, *it->itype, *it->file);
      }
      ChainItem e;
      e.kind = k;
      e.uid = u;
      if (k == ChainItem::Kind::Script) e.script = consts::kItemScript;
      return e;
    };
    input.profile.merge_item =
        load_chain(cur->option ? cur->option->merge : std::nullopt, "Merge",
                   ChainItem::Kind::Merge);
    input.profile.script_item =
        load_chain(cur->option ? cur->option->script : std::nullopt, "Script",
                   ChainItem::Kind::Script);
    input.profile.rules_item =
        load_chain(cur->option ? cur->option->rules : std::nullopt, "Rules",
                   ChainItem::Kind::Rules);
    input.profile.proxies_item =
        load_chain(cur->option ? cur->option->proxies : std::nullopt, "Proxies",
                   ChainItem::Kind::Proxies);
  }
  input.profile.global_merge =
      chain_item_load(home + L"\\profiles", "Merge", "merge", "Merge.yaml");
  input.profile.global_script =
      chain_item_load(home + L"\\profiles", "Script", "script", "Script.js");

  input.merged_proxies_source.profiles_dir = home + L"\\profiles";
  if (profiles.items) {
    for (const auto& it : *profiles.items) {
      if (it.itype && (*it.itype == "remote" || *it.itype == "local") && it.file) {
        input.merged_proxies_source.items.push_back(
            {*it.itype, *it.file, it.updated.value_or(0)});
      }
    }
  }
  return true;
}

}  // namespace

UT_TEST(t08_golden_fixture) {
  // 固定为夹具生成时的 UTC+8；CI(UTC) 与本地(UTC+8) 走完全相同的比对路径。
  ScopedEast8Tz scoped_tz;
  std::wstring home = fixtures_dir();
  UT_CHECK(enhance_test::dir_exists(home));
  if (!enhance_test::dir_exists(home)) return;

  EnhanceInput input;
  UT_CHECK(build_input_from_home(home, input));

  EnhanceOutput out = enhance_core(input);
  std::string ours_text = config::serde::dump(out.config);

  std::wstring expected_path = home + L"\\expected_clash_win.yaml";
  std::string expected_text = enhance_test::read_file_str(expected_path);
  if (expected_text.empty()) {
    // 自种子：以验收通过的当前实现为基准固化期望（此后逐行 diff 守回退）
    std::ofstream f(expected_path, std::ios::binary);
    UT_CHECK(static_cast<bool>(f));
    f << ours_text;
    std::printf("    SEED: expected_clash_mini.yaml written (%zu lines); "
                "rerun to diff\n",
                ours_text.size());
    return;
  }

  auto diffs = enhance_test::diff_lines(ours_text, expected_text);
  for (const auto& d : diffs) {
    std::printf("    DIFF line %zu: %s\n", d.first, d.second.c_str());
  }
  UT_CHECK(diffs.empty());  // 回退判定：enhance 输出必须与 golden 夹具逐行一致

  // 关键结构逐条断言（与 T08 验收的实机产物形态一致）
  UT_CHECK_EQ(out.config["rules"].size(), size_t(26));
  UT_CHECK_EQ(out.config["proxy-groups"][0]["proxies"].size(), size_t(21));
  UT_CHECK(out.config["proxy-groups"][0]["name"].Scalar() == "PROXY");
  UT_CHECK(out.config["proxy-groups"][1]["name"].Scalar() == "PROXY__METRICS");
  UT_CHECK(out.config["rules"][1].Scalar() == "RULE-SET,gfwlist,PROXY");
  UT_CHECK(out.config["rules"][out.config["rules"].size() - 1].Scalar() ==
           "MATCH,DIRECT");
}

UT_TEST(t08_golden_live_probe) {
  // 【信息性用例——不阻塞套件】读活体实机 %APPDATA% 与夹具输出对比。
  // 实机产物会被 Clash Mini 新构建重写（如 2026-10-26 09:49 的一次生成把
  // serde_yaml_ng 风格换成 go-yaml v3 风格），差异属实机漂移，仅供人工
  // 评审归因；本用例无论结果如何都不得 FAIL。
  std::wstring home = enhance_test::real_home();
  std::wstring live_path = home + L"\\clash-win.yaml";
  std::string live_text = enhance_test::read_file_str(live_path);
  if (!enhance_test::dir_exists(home) || live_text.empty()) {
    std::printf("    INFO-SKIP: real machine clash-win.yaml not present\n");
    return;
  }

  EnhanceInput input;
  if (!build_input_from_home(home, input)) {
    std::printf("    INFO-SKIP: real machine inputs unreadable\n");
    return;
  }
  EnhanceOutput out = enhance_core(input);

  auto live = util::load_yaml_file(live_path);
  if (!live.is_ok()) {
    std::printf("    INFO: live clash-win.yaml unparseable\n");
    return;
  }
  YAML::Node g = live.value();

  // 语义对比（键序无关；格式风格差异不影响此判定）
  std::function<bool(const YAML::Node&, const YAML::Node&, const std::string&)>
      semantic_eq = [&](const YAML::Node& a, const YAML::Node& b,
                        const std::string& path) -> bool {
    if (a.IsScalar() || b.IsScalar()) {
      if (a.IsNull() && b.IsNull()) return true;
      std::string sa = a.IsScalar() ? a.Scalar() : "<nonscalar>";
      std::string sb = b.IsScalar() ? b.Scalar() : "<nonscalar>";
      if (sa != sb) {
        std::printf("    [live-drift] scalar at %s: ours=[%s] live=[%s]\n",
                    path.c_str(), sa.c_str(), sb.c_str());
      }
      return sa == sb;
    }
    if (a.IsSequence() != b.IsSequence()) {
      std::printf("    [live-drift] type at %s\n", path.c_str());
      return false;
    }
    if (a.IsSequence()) {
      if (a.size() != b.size()) {
        std::printf("    [live-drift] seq size at %s: ours=%zu live=%zu\n",
                    path.c_str(), a.size(), b.size());
        return false;
      }
      for (size_t i = 0; i < a.size(); ++i) {
        if (!semantic_eq(a[i], b[i], path + "[" + std::to_string(i) + "]"))
          return false;
      }
      return true;
    }
    if (a.IsMap() && b.IsMap()) {
      std::vector<std::string> ka, kb;
      for (const auto& kv : a) ka.push_back(kv.first.Scalar());
      for (const auto& kv : b) kb.push_back(kv.first.Scalar());
      std::sort(ka.begin(), ka.end());
      std::sort(kb.begin(), kb.end());
      if (ka != kb) {
        std::printf("    [live-drift] map keys at %s\n", path.c_str());
        return false;
      }
      for (const auto& kv : a) {
        if (!semantic_eq(kv.second,
                         static_cast<const YAML::Node&>(b)[kv.first.Scalar()],
                         path + "." + kv.first.Scalar())) {
          return false;
        }
      }
      return true;
    }
    return false;
  };
  bool semantic_ok = semantic_eq(out.config, g, "$");

  // 格式层逐行 diff（重排消去键序差异后的行级差异计数）
  YAML::Node ours_reordered = enhance_test::reorder_top_keys(out.config, g);
  std::string ours_text = config::serde::dump(ours_reordered);
  size_t diff_count = enhance_test::diff_lines(ours_text, live_text, 8).size();

  if (semantic_ok && diff_count == 0) {
    std::printf("    INFO: live machine matches fixture baseline exactly\n");
  } else if (semantic_ok) {
    std::printf(
        "    INFO: live machine SEMANTICALLY identical; formatting drifted "
        "(%zu+ line diffs) — writer is not the audited Rust enhance path "
        "(expected: go-yaml style rewrite at 2026-10-26 09:49); "
        "informational only\n",
        diff_count);
  } else {
    std::printf(
        "    INFO: live machine SEMANTIC DRIFT vs our enhance — see "
        "[live-drift] lines above; informational only (real machine may be "
        "running a newer build with changed enhance semantics)\n");
  }
}
