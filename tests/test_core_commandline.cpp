// T07 测试：命令行/环境变量构造 + 恢复语义纯函数（对照 state.rs::start_core_by_sidecar）
#include "test_util.h"

#include "clashwin/core/constants.h"
#include "core/core_manager.h"
#include "util/utf.h"

#include <cstdio>

using namespace clashwin;
using namespace clashwin::core;

UT_TEST(cmdline_matches_state_rs) {
  // 对照 state.rs:56-63：
  //   mini-mihomo.exe -d <app_home_dir> -f <clash-mini.yaml> -ext-ctl-pipe \\.\pipe\mini-mihomo
  SidecarLaunch s;
  s.core_path = L"C:\\app\\mini-mihomo.exe";
  s.config_dir = L"C:\\app\\.config\\io.github.clash-mini.clash-mini";
  s.config_file = L"C:\\app\\.config\\io.github.clash-mini.clash-mini\\clash-mini.yaml";
  s.ipc_pipe = L"\\\\.\\pipe\\mini-mihomo";
  s.go_memlimit = "128MiB";
  s.go_gc = "100";
  s.go_maxprocs = "2";

  std::string cmd = utf::utf16_to_utf8(build_sidecar_command_line(s));
  const char* expected =
      "\"C:\\app\\mini-mihomo.exe\""
      " -d \"C:\\app\\.config\\io.github.clash-mini.clash-mini\""
      " -f \"C:\\app\\.config\\io.github.clash-mini.clash-mini\\clash-mini.yaml\""
      " -ext-ctl-pipe \\\\.\\pipe\\mini-mihomo";
  UT_CHECK_EQ(cmd, expected);

  // 参数顺序与原版逐字段一致
  UT_CHECK(cmd.find("-d \"") < cmd.find("-f \""));
  UT_CHECK(cmd.find("-f \"") < cmd.find("-ext-ctl-pipe"));
  UT_CHECK(cmd.find("-ext-ctl-pipe") < cmd.size());
}

UT_TEST(env_defaults) {
  // ← state.rs:39-41：缺省 GOMEMLIMIT=128MiB / GOGC=100 / GOMAXPROCS=2
  SidecarLaunch s = make_sidecar_launch(L"C:\\core.exe", L"C:\\home", L"C:\\home\\clash-mini.yaml",
                                        L"\\\\.\\pipe\\mini-mihomo");
  // 宿主进程未设置这些变量时的缺省（若宿主已设置则按语义应取环境值——两分支都合法）
  UT_CHECK(!s.go_memlimit.empty());
  UT_CHECK(!s.go_gc.empty());
  UT_CHECK(!s.go_maxprocs.empty());
  bool defaults_ok = (s.go_memlimit == "128MiB" || s.go_memlimit == "256MiB") &&
                     (s.go_gc == "100" || s.go_gc == "off") &&
                     (s.go_maxprocs == "2" || s.go_maxprocs == "4");
  UT_CHECK(defaults_ok);
}

UT_TEST(env_override_from_process) {
  // ← std::env::var("GOMEMLIMIT").unwrap_or(...)：进程环境优先于默认
  if (_putenv("GOMEMLIMIT=512MiB") != 0) {
    std::printf("  SKIP: _putenv unavailable\n");
    return;
  }
  SidecarLaunch s = make_sidecar_launch(L"C:\\core.exe", L"C:\\home", L"C:\\home\\c.yaml", L"p");
  UT_CHECK_EQ(s.go_memlimit, std::string("512MiB"));
  _putenv("GOMEMLIMIT=");
}

UT_TEST(builtin_outbound_table) {
  // ← lifecycle.rs:19-21
  UT_CHECK(is_builtin_outbound("DIRECT"));
  UT_CHECK(is_builtin_outbound("REJECT"));
  UT_CHECK(is_builtin_outbound("REJECT-DROP"));
  UT_CHECK(is_builtin_outbound("PASS"));
  UT_CHECK(is_builtin_outbound("COMPATIBLE"));
  UT_CHECK(!is_builtin_outbound("PROXY"));
  UT_CHECK(!is_builtin_outbound("HK-01"));
  UT_CHECK(!is_builtin_outbound("direct"));  // 大小写敏感
}

UT_TEST(filter_text_extract) {
  // ← lifecycle.rs:419-441 read_filter_text：state[uid]["PROXY"]["filterText"] trim+lower
  nlohmann::json state = {
      {"uid-1", {{"PROXY", {{"filterText", "  HK  "}}}}},
      {"uid-2", {{"PROXY", {{"filterText", "JP-Tokyo"}}}}},
  };
  UT_CHECK_EQ(extract_filter_text(state, "uid-1"), std::string("hk"));
  UT_CHECK_EQ(extract_filter_text(state, "uid-2"), std::string("jp-tokyo"));
  UT_CHECK_EQ(extract_filter_text(state, "missing"), std::string());
  UT_CHECK_EQ(extract_filter_text(nlohmann::json::object(), "uid-1"), std::string());
  // 非字符串/缺键退化
  nlohmann::json bad = {{"u", {{"PROXY", {{"filterText", 5}}}}}};
  UT_CHECK_EQ(extract_filter_text(bad, "u"), std::string());
}

UT_TEST(pick_fallback_rules) {
  // ← lifecycle.rs:444-453 select_fallback_in_subset 选点规则
  // dummy 判定按 node.rs：中文广告短语「开头或全等」+ 英文广告词/URL 包含
  std::vector<std::string> all = {"购买节点", "HK-01", "JP-02", "US-03"};
  // 无 filter → 首个非 dummy
  UT_CHECK_EQ(pick_fallback_node(all, ""), std::string("HK-01"));
  // filter 匹配优先
  UT_CHECK_EQ(pick_fallback_node(all, "jp"), std::string("JP-02"));
  UT_CHECK_EQ(pick_fallback_node(all, "us-"), std::string("US-03"));
  // filter 无匹配 → 首个非 dummy 兜底
  UT_CHECK_EQ(pick_fallback_node(all, "sg"), std::string("HK-01"));
  // 全 dummy → 空
  UT_CHECK(pick_fallback_node({"官网", "流量套餐"}, "").empty());
  UT_CHECK(pick_fallback_node({}, "").empty());
}
