// T07 测试：状态机与 restart_with_restore / apply_config 时序（mock ICoreApiClient）
//
// 对照 lifecycle.rs::restart_core 逐段：
//   snapshot → stop → start → 等 API 就绪(30s) → 等组员填充(20s) →
//   select(90s 预算) / 兜底 select_fallback_in_subset
// 所有等待参数注入小值（毫秒级）跑完全部分支。
#include "test_util.h"

#include "clashwin/core/constants.h"
#include "core/core_manager.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace clashwin;
using namespace clashwin::core;
using clashwin::mihomo::BaseConfig;
using clashwin::mihomo::ProxyItem;

namespace {

// 脚本化 mock：按调用序列弹出的响应 + 调用计数
struct MockApi : ICoreApiClient {
  // 队列式脚本（空时用默认值）
  std::vector<Result<BaseConfig>> base_script;
  std::vector<Result<ProxyItem>> group_script;  // 依次对应每次 get_group_by_name
  std::vector<Result<void>> select_script;      // 依次对应每次 select（成功即不弹后续）
  std::vector<Result<void>> reload_script;
  std::string last_select_node;
  std::string last_reload_path;
  bool last_reload_force = false;
  int base_calls = 0, group_calls = 0, select_calls = 0, reload_calls = 0;

  Result<BaseConfig> get_base_config() override {
    ++base_calls;
    if (!base_script.empty()) {
      auto r = base_script.front();
      base_script.erase(base_script.begin());
      return r;
    }
    return Result<BaseConfig>::ok(BaseConfig{});
  }
  Result<ProxyItem> get_group_by_name(const std::string&) override {
    ++group_calls;
    if (!group_script.empty()) {
      auto r = group_script.front();
      group_script.erase(group_script.begin());
      return r;
    }
    ProxyItem g;
    g.name = "PROXY";
    g.now = std::string();
    g.all = std::vector<std::string>{};
    return Result<ProxyItem>::ok(g);
  }
  Result<void> select_node_for_group(const std::string&, const std::string& node) override {
    ++select_calls;
    last_select_node = node;
    if (!select_script.empty()) {
      auto r = select_script.front();
      select_script.erase(select_script.begin());
      return r;
    }
    return Result<void>::ok();
  }
  Result<void> reload_config(bool force, const std::string& path) override {
    ++reload_calls;
    last_reload_force = force;
    last_reload_path = path;
    if (!reload_script.empty()) {
      auto r = reload_script.front();
      reload_script.erase(reload_script.begin());
      return r;
    }
    return Result<void>::ok();
  }
};

ProxyItem group_with(const std::optional<std::string>& now,
                     std::optional<std::vector<std::string>> all = std::nullopt) {
  ProxyItem g;
  g.name = "PROXY";
  g.now = now;
  if (all.has_value()) {
    g.all = *all;
  } else {
    g.all = std::vector<std::string>{};
  }
  return g;
}

ProxyItem err_group() {
  ProxyItem g;
  g.name = "PROXY";
  return g;
}

Result<ProxyItem> ok_group(ProxyItem g) { return Result<ProxyItem>::ok(std::move(g)); }

// ops 录制桩：记录 start/stop 顺序
// 用 std::string 而非 const char*：vector<const char*> 的 == 比较指针地址，
// 不同翻译单元的字符串字面量地址不同，会导致内容相同却判不等
struct OpsRecorder {
  std::vector<std::string> order;
  bool start_fails = false;
  bool stop_fails = false;
  CoreOps ops() {
    auto start_func = [this]() -> Result<void> {
      this->order.push_back("start");
      if (this->start_fails) return Result<void>::err(Error{1, "start fail"});
      return Result<void>::ok();
    };
    auto stop_func = [this]() -> Result<void> {
      this->order.push_back("stop");
      if (this->stop_fails) return Result<void>::err(Error{2, "stop fail"});
      return Result<void>::ok();
    };
    CoreOps o;
    o.start = start_func;
    o.stop = stop_func;
    return o;
  }
  const std::vector<std::string>& get_order() const { return order; }
  void clear() { order.clear(); }
};

RestoreTimings fast() {
  RestoreTimings t;
  t.api_ready_budget_ms = 200;
  t.api_ready_poll_ms = 5;
  t.group_fill_budget_ms = 100;
  t.group_fill_poll_ms = 5;
  t.select_budget_ms = 200;
  t.select_retry_ms = 10;
  return t;
}

}  // namespace

// ---------- 快照：成功 / now 空 / 读失败 ----------
UT_TEST(restore_snapshot_paths) {
  {
    MockApi api;
    api.group_script = {ok_group(group_with(std::string("HK-01"))), ok_group(group_with(std::string("HK-01"))),
                        ok_group(group_with(std::string("HK-01"))),
                        ok_group(group_with(std::string("HK-01"), std::vector<std::string>{"HK-01"}))};
    api.select_script = {Result<void>::ok()};
    CoreManager m;
    OpsRecorder rec;
    UT_CHECK(m.restart_with_restore(api, rec.ops(), fast()).is_ok());
    // select 一次命中快照节点
    UT_CHECK_EQ(api.select_calls, 1);
    UT_CHECK_EQ(api.last_select_node, std::string("HK-01"));
    UT_CHECK(rec.order == std::vector<std::string>({"stop", "start"}));
  }
  {
    // now 为空 → 无需恢复（不调 select）
    MockApi api;
    api.group_script = {ok_group(group_with(std::string())), ok_group(group_with(std::string()))};
    CoreManager m;
    OpsRecorder rec;
    UT_CHECK(m.restart_with_restore(api, rec.ops(), fast()).is_ok());
    UT_CHECK_EQ(api.select_calls, 0);
  }
  {
    // group 读取失败 → 无需恢复
    MockApi api;
    api.group_script = {Result<ProxyItem>::err(Error{1103, "http"})};
    CoreManager m;
    OpsRecorder rec;
    UT_CHECK(m.restart_with_restore(api, rec.ops(), fast()).is_ok());
    UT_CHECK_EQ(api.select_calls, 0);
  }
}

// ---------- API 就绪等待：30s 预算内转好 / 超时跳过恢复 ----------
UT_TEST(restore_api_ready_wait) {
  {
    // 前 3 次 base 失败，之后成功 → 恢复流程继续
    MockApi api;
    api.base_script = {Result<BaseConfig>::err(Error{1, "pipe"}),
                       Result<BaseConfig>::err(Error{1, "pipe"}),
                       Result<BaseConfig>::err(Error{1, "pipe"})};
    api.group_script = {ok_group(group_with(std::string("HK-01"))),
                        ok_group(group_with(std::string("HK-01"), std::vector<std::string>{"HK-01"}))};
    CoreManager m;
    OpsRecorder rec;
    UT_CHECK(m.restart_with_restore(api, rec.ops(), fast()).is_ok());
    UT_CHECK(api.base_calls >= 4);
    UT_CHECK_EQ(api.select_calls, 1);
  }
  {
    // API 一直不就绪 → 跳过恢复（仍不判失败）
    MockApi api;
    api.base_script = {Result<BaseConfig>::err(Error{1, "down"})};
    // base_script 弹完默认成功……需要脚本耗尽后仍失败 → 用恒失败子类
    struct AlwaysFailBase : MockApi {
      Result<BaseConfig> get_base_config() override {
        ++base_calls;
        return Result<BaseConfig>::err(Error{1, "down"});
      }
    } api2;
    CoreManager m;
    OpsRecorder rec;
    UT_CHECK(m.restart_with_restore(api2, rec.ops(), fast()).is_ok());
    UT_CHECK_EQ(api2.select_calls, 0);
    (void)api;
  }
}

// ---------- 内置出站快照 → 直接子集兜底（lifecycle.rs:354-364） ----------
UT_TEST(restore_builtin_outbound_fallback) {
  MockApi api;
  api.group_script = {
      ok_group(group_with(std::string("DIRECT"))),  // 快照 DIRECT（空组兜底窗口）
      ok_group(group_with(std::string("DIRECT"), std::vector<std::string>{"续费节点", "HK-01"})),
      ok_group(group_with(std::string("HK-01"), std::vector<std::string>{"续费节点", "HK-01"})),  // 兜底读组
  };
  api.select_script = {Result<void>::ok()};
  CoreManager m;
  OpsRecorder rec;
  UT_CHECK(m.restart_with_restore(api, rec.ops(), fast()).is_ok());
  // 不对 DIRECT 做 select；兜底选中首个非 dummy = HK-01
  UT_CHECK_EQ(api.select_calls, 1);
  UT_CHECK_EQ(api.last_select_node, std::string("HK-01"));
}

// ---------- 快照节点不在组内 → 子集兜底（BUG-301，lifecycle.rs:367-377） ----------
UT_TEST(restore_node_missing_from_group) {
  MockApi api;
  api.group_script = {
      ok_group(group_with(std::string("GONE-NODE"))),
      // 组填充后不含 GONE-NODE
      ok_group(group_with(std::string(), std::vector<std::string>{"HK-01", "JP-02"})),
      ok_group(group_with(std::string(), std::vector<std::string>{"HK-01", "JP-02"})),
  };
  api.select_script = {Result<void>::ok()};
  CoreManager m;
  OpsRecorder rec;
  UT_CHECK(m.restart_with_restore(api, rec.ops(), fast()).is_ok());
  UT_CHECK_EQ(api.select_calls, 1);
  UT_CHECK_EQ(api.last_select_node, std::string("HK-01"));
}

// ---------- select 连续失败 → 90s 预算耗尽 → 子集兜底（lifecycle.rs:384-415） ----------
UT_TEST(restore_select_retry_then_fallback) {
  struct AlwaysFailSelect : MockApi {
    Result<void> select_node_for_group(const std::string&, const std::string& node) override {
      ++select_calls;
      last_select_node = node;
      return Result<void>::err(Error{1103, "ipc timeout"});
    }
  } api;
  api.group_script = {
      ok_group(group_with(std::string("HK-01"))),
      ok_group(group_with(std::string("HK-01"), std::vector<std::string>{"HK-01", "JP-02"})),
      ok_group(group_with(std::string("HK-01"), std::vector<std::string>{"HK-01", "JP-02"})),  // 兜底读组
      ok_group(group_with(std::string("HK-01"), std::vector<std::string>{"HK-01", "JP-02"})),  // 兜底可能再读
  };
  CoreManager m;
  OpsRecorder rec;
  UT_CHECK(m.restart_with_restore(api, rec.ops(), fast()).is_ok());
  // 重试多次（预算 100ms / 重试 10ms → 至少 5 次），最终兜底 select 到 HK-01
  UT_CHECK(api.select_calls >= 5);
  UT_CHECK_EQ(api.last_select_node, std::string("HK-01"));
}

// ---------- 组永不填充 → 子集兜底 ----------
UT_TEST(restore_group_never_fills) {
  struct EmptyGroup : MockApi {
    Result<ProxyItem> get_group_by_name(const std::string&) override {
      ++group_calls;
      if (group_calls == 1) return ok_group(group_with(std::string("HK-01")));  // 快照
      return ok_group(group_with(std::string("HK-01"), std::vector<std::string>{}));  // 恒空
    }
  } api;
  CoreManager m;
  OpsRecorder rec;
  UT_CHECK(m.restart_with_restore(api, rec.ops(), fast()).is_ok());
  // 组员恒空 → 等满 20s（此处 100ms）预算后仍空 → 不 select（组不存在无从选点）
  UT_CHECK_EQ(api.select_calls, 0);
}

// ---------- stop/start 失败传播 ----------
UT_TEST(restart_ops_failure_propagation) {
  MockApi api;
  api.group_script = {ok_group(group_with(std::string("HK-01"))),
                      ok_group(group_with(std::string("HK-01"))),
                      ok_group(group_with(std::string("HK-01"))),
                      ok_group(group_with(std::string("HK-01"), std::vector<std::string>{"HK-01"}))};
  CoreManager m;
  {
    OpsRecorder rec;
    rec.stop_fails = true;
    auto r = m.restart_with_restore(api, rec.ops(), fast());
    UT_CHECK(r.is_err());
    UT_CHECK(rec.order == std::vector<std::string>({"stop"}));  // start 未执行
  }
  {
    OpsRecorder rec;
    rec.start_fails = true;
    auto r = m.restart_with_restore(api, rec.ops(), fast());
    UT_CHECK(r.is_err());
    UT_CHECK(rec.order == std::vector<std::string>({"stop", "start"}));
    UT_CHECK_EQ(api.select_calls, 0);  // 启动失败不恢复
  }
}

// ---------- apply_config：reload 成功 → restore；失败 → 整核重启兜底 ----------
UT_TEST(apply_config_paths) {
  {
    // reload 成功路径：snapshot → reload(force=true, path) → restore
    MockApi api;
    api.group_script = {
        ok_group(group_with(std::string("JP-02"))),                                            // 快照
        ok_group(group_with(std::string("JP-02"), std::vector<std::string>{"HK-01", "JP-02"})),  // 等组员
        ok_group(group_with(std::string("JP-02"), std::vector<std::string>{"HK-01", "JP-02"})),
    };
    api.select_script = {Result<void>::ok()};
    CoreManager m;
    OpsRecorder rec;
    UT_CHECK(m.apply_config(api, "C:\\home\\clash-win.yaml", rec.ops(), fast()).is_ok());
    UT_CHECK_EQ(api.reload_calls, 1);
    UT_CHECK_EQ(api.last_reload_force, true);
    UT_CHECK_EQ(api.last_reload_path, std::string("C:\\home\\clash-win.yaml"));
    UT_CHECK_EQ(api.last_select_node, std::string("JP-02"));
    UT_CHECK(rec.order.empty());  // 未整核重启
  }
  {
    // reload 失败 → restart_with_restore 兜底
    MockApi api;
    api.group_script = {
        ok_group(group_with(std::string("JP-02"))),  // 快照
        ok_group(group_with(std::string("JP-02"))),  // 重启后快照（restart 内部再 snapshot）
        ok_group(group_with(std::string("JP-02"), std::vector<std::string>{"HK-01", "JP-02"})),
        ok_group(group_with(std::string("JP-02"), std::vector<std::string>{"HK-01", "JP-02"})),
    };
    CoreManager m;
    OpsRecorder rec;
    api.reload_script = {Result<void>::err(Error{1103, "reload rejected"})};
    UT_CHECK(m.apply_config(api, "C:\\home\\clash-win.yaml", rec.ops(), fast()).is_ok());
    UT_CHECK(rec.order == std::vector<std::string>({"stop", "start"}));
    // reload 失败后 restart 内部快照 → select 恢复
    UT_CHECK_EQ(api.select_calls, 1);
    UT_CHECK_EQ(api.last_select_node, std::string("JP-02"));
  }
  {
    // reload 失败 + restart 也失败 → err 传播
    MockApi api;
    api.group_script = {ok_group(group_with(std::string("JP-02"))),
                        ok_group(group_with(std::string("JP-02")))};
    CoreManager m;
    OpsRecorder rec;
    rec.stop_fails = true;
    api.reload_script = {Result<void>::err(Error{1103, "reload rejected"})};
    auto r = m.apply_config(api, "C:\\home\\clash-win.yaml", rec.ops(), fast());
    UT_CHECK(r.is_err());
    UT_CHECK(r.error().message.find("Failed to apply config") != std::string::npos);
  }
}

// ---------- CoreManager 状态机初值 ----------
UT_TEST(state_machine_initial) {
  CoreManager m;
  UT_CHECK(m.running_mode() == RunningMode::NotRunning);
  UT_CHECK(!m.sidecar_alive());
  // stop 对 NotRunning 幂等
  UT_CHECK(m.stop().is_ok());
  UT_CHECK(m.running_mode() == RunningMode::NotRunning);
  // to_string 对照 manager/mod.rs Display
  UT_CHECK_EQ(std::string(to_string(RunningMode::Service)), std::string("Service"));
  UT_CHECK_EQ(std::string(to_string(RunningMode::Sidecar)), std::string("Sidecar"));
  UT_CHECK_EQ(std::string(to_string(RunningMode::NotRunning)), std::string("NotRunning"));
}
