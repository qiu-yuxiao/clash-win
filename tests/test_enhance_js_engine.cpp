// T08 单测：js_engine（QuickJS 封装——执行/日志/异常降级/超时看门狗/限额）
#include <string>
#include <thread>

#include "enhance/js_engine.h"
#include "enhance/enhance_constants.h"
#include "test_util.h"

using namespace clashwin;
using namespace clashwin::enhance;

UT_TEST(t08_js_identity_script) {
  YAML::Node config = YAML::Load("rules:\n- 111\n- 222\ntun:\n  enable: false\n");
  auto res = use_script(consts::kItemScript, config, "");
  UT_CHECK(res.is_ok());
  UT_CHECK(!res.value().degraded);
  UT_CHECK(res.value().config["rules"][0].Scalar() == "111");
  UT_CHECK_EQ(res.value().logs.size(), size_t(0));
}

UT_TEST(t08_js_modify_config) {
  // ← script.rs::test_script：规则追加 + console.log + proxies 替换
  std::string script = R"JS(
    function main(config) {
      if (Array.isArray(config.rules)) {
        config.rules = [...config.rules, "add"];
      }
      console.log(config);
      config.proxies = ["111"];
      return config;
    }
  )JS";
  YAML::Node config = YAML::Load("rules:\n- 111\n- 222\ntun:\n  enable: false\n");
  auto res = use_script(script, config, "profile-x");
  UT_CHECK(res.is_ok());
  const auto& out = res.value();
  UT_CHECK(!out.degraded);
  UT_CHECK_EQ(out.config["rules"].size(), size_t(3));
  UT_CHECK(out.config["rules"][2].Scalar() == "add");
  UT_CHECK_EQ(out.config["proxies"].size(), size_t(1));
  UT_CHECK(out.config["proxies"][0].Scalar() == "111");
  // console.log(config) 捕获为 JSON.stringify(data, null, 2)
  UT_CHECK_EQ(out.logs.size(), size_t(1));
  UT_CHECK(out.logs[0].level == "log");
  UT_CHECK(out.logs[0].message.find("\"rules\"") != std::string::npos);
}

UT_TEST(t08_js_exception_degrades_to_original) {
  // main 抛异常 → "__error_flag__ ..." → JSON 解析失败 → 降级原 config +
  // logs 追加 (exception, "Script execution failed")
  std::string script = R"JS(
    function main(config) {
      throw new Error("boom");
      return config;
    }
  )JS";
  YAML::Node config = YAML::Load("rules:\n- keep\n");
  auto res = use_script(script, config, "");
  UT_CHECK(res.is_ok());  // 降级不是错误
  UT_CHECK(res.value().degraded);
  UT_CHECK(res.value().config["rules"][0].Scalar() == "keep");
  bool has_exception = false;
  for (const auto& l : res.value().logs) {
    if (l.level == "exception" && l.message == "Script execution failed")
      has_exception = true;
  }
  UT_CHECK(has_exception);
}

UT_TEST(t08_js_missing_main_is_error) {
  // 无 main → JS 运行时错误（ReferenceError）→ 外层 catch → "__error_flag__..."
  // → JSON 解析失败 → 降级原 config（script.rs：所有 JS 级错误都走降级路径）
  std::string script = "function not_main(config) { return config; }";
  YAML::Node config = YAML::Load("a: 1\n");
  auto res = use_script(script, config, "");
  UT_CHECK(res.is_ok());
  UT_CHECK(res.value().degraded);
  UT_CHECK(res.value().config["a"].Scalar() == "1");
  bool has_exception = false;
  for (const auto& l : res.value().logs) {
    if (l.level == "exception") has_exception = true;
  }
  UT_CHECK(has_exception);
}

UT_TEST(t08_js_timeout_watchdog) {
  // 死循环 → 3s 看门狗中断 → Error
  std::string script = "function main(config) { while(true) {} return config; }";
  YAML::Node config = YAML::Load("a: 1\n");
  auto res = use_script(script, config, "");
  UT_CHECK(!res.is_ok());
  UT_CHECK(res.error().message.find("timed out") != std::string::npos);
}

UT_TEST(t08_js_falsy_return_degrades) {
  // main 返回 falsy → '' → JSON 解析失败 → 降级原 config
  std::string script = "function main(config) { return null; }";
  YAML::Node config = YAML::Load("rules:\n- keep\n");
  auto res = use_script(script, config, "");
  UT_CHECK(res.is_ok());
  UT_CHECK(res.value().degraded);
  UT_CHECK(res.value().config["rules"][0].Scalar() == "keep");
}

UT_TEST(t08_js_output_limit) {
  // 2000 条 console.log → 第 1001 条触发 MAX_OUTPUTS → JS 异常 → catch →
  // "__error_flag__" → JSON 解析失败 → 降级原 config（script.rs
  // ::test_memory_limits：被限制但不 panic）
  std::string script = R"JS(
    function main(config) {
      for(let i = 0; i < 2000; i++) { console.log("test"); }
      return config;
    }
  )JS";
  YAML::Node config = YAML::Load("a: 1\n");
  auto res = use_script(script, config, "");
  if (!res.is_ok()) {
    std::printf("    js_output_limit error: %s\n", res.error().message.c_str());
  }
  UT_CHECK(res.is_ok());
  if (res.is_ok()) {
    UT_CHECK(res.value().degraded);
    UT_CHECK(res.value().config["a"].Scalar() == "1");
    bool has_exception = false;
    for (const auto& l : res.value().logs) {
      if (l.level == "exception") has_exception = true;
    }
    UT_CHECK(has_exception);
  }
}

UT_TEST(t08_js_strip_outer_quotes) {
  UT_CHECK(strip_outer_quotes("") == "");
  UT_CHECK(strip_outer_quotes("'") == "'");
  UT_CHECK(strip_outer_quotes("\"") == "\"");
  UT_CHECK(strip_outer_quotes("''") == "");
  UT_CHECK(strip_outer_quotes("\"\"") == "");
  UT_CHECK(strip_outer_quotes("'a'") == "a");
}

UT_TEST(t08_js_profile_name_default_global) {
  // name 为空 → __verge_name = "global"
  std::string script = R"JS(
    function main(config, name) {
      config.__name = name;
      return config;
    }
  )JS";
  YAML::Node config = YAML::Load("a: 1\n");
  auto res = use_script(script, config, "");
  UT_CHECK(res.is_ok());
  UT_CHECK(res.value().config["__name"].Scalar() == "global");
  // name 非空 → 透传
  auto res2 = use_script(script, config, "my-profile");
  UT_CHECK(res2.is_ok());
  UT_CHECK(res2.value().config["__name"].Scalar() == "my-profile");
}
