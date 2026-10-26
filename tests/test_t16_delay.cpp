// T16 测试：节点操作+延迟管理（失败/超时语义/群测注入/择优矩阵/锁互斥/校准）。
#include <windows.h>

#include <atomic>
#include <thread>

#include "cmd/command_service.h"
#include "config/config.h"
#include "core/auto_select.h"
#include "core/delay_manager.h"
#include "ui/scenes/node_data.h"
#include "test_util.h"
#include "util/node.h"

using namespace clashwin;
using namespace clashwin::core;
using core::AutoSelect;
using core::DelayManager;
using core::DelayProbeHooks;
using DResult = core::DelayManager::GroupOutcome;

namespace {
// mock hooks 组装
struct ProbeState {
  // 测量组返回（nullopt = 测量组未就绪）
  std::optional<std::map<std::string, uint32_t>> probe_result;
  std::map<std::string, uint32_t> history;
  std::vector<std::pair<std::string, std::vector<std::pair<std::string, uint32_t>>>>
      emitted;  // 每次注入记录（断言 -2 中间态序列）
  int probe_calls = 0;
};

DelayProbeHooks make_hooks(ProbeState& st) {
  DelayProbeHooks h;
  h.group_probe = [&st](const std::string&) {
    ++st.probe_calls;
    return st.probe_result;
  };
  h.fresh_history = [&st] { return st.history; };
  h.emit_results = [&st](const std::string& group,
                         const std::vector<std::pair<std::string, uint32_t>>& r) {
    st.emitted.push_back({group, r});
  };
  return h;
}
}  // namespace

UT_TEST(t16_group_probe_and_testing_state) {
  ProbeState st;
  st.probe_result = std::map<std::string, uint32_t>{
      {"n1", 120}, {"n2", 450}, {"n3", 2500}};
  DelayManager::instance().set_hooks(make_hooks(st));

  auto out = DelayManager::instance().test_group(
      "PROXY", {"n1", "n2", "n3"}, false, std::nullopt);
  UT_CHECK(out.injected);
  UT_CHECK_EQ(out.display.size(), 3u);
  // 未测得=0 语义 + 实际值
  UT_CHECK(out.display[0] == std::make_pair(std::string("n1"), 120u));
  UT_CHECK(out.display[2] == std::make_pair(std::string("n3"), 2500u));
  // 注入序列：第一帧 -2 testing、第二帧实际值
  UT_CHECK_EQ(st.emitted.size(), 2u);
  bool first_all_testing = true;
  for (const auto& [n, d] : st.emitted[0].second)
    if (d != static_cast<uint32_t>(-2)) first_all_testing = false;
  UT_CHECK(first_all_testing);
  UT_CHECK(st.emitted[1].second[0].second == 120);
  // T13 tier 口径：-2 → Unknown / 2500 → Timeout
  UT_CHECK(clashwin::ui::scene::delay_tier(-2) == clashwin::ui::scene::DelayTier::Unknown);
  UT_CHECK(clashwin::ui::scene::delay_tier(2500) == clashwin::ui::scene::DelayTier::Timeout);
  UT_CHECK(clashwin::ui::scene::delay_tier(120) == clashwin::ui::scene::DelayTier::Success);
}

UT_TEST(t16_single_not_executed_no_fake_timeout) {
  // ← monitor.rs「单点测速语义：测量组未返回数据 → 未执行，严禁伪造 Timeout」
  ProbeState st;
  st.probe_result = std::nullopt;  // 测量组未就绪
  st.history = {{"n1", 300}};      // 有 history 但单点禁用兜底
  DelayManager::instance().set_hooks(make_hooks(st));
  auto out = DelayManager::instance().test_single("PROXY", "n1");
  UT_CHECK(!out.injected);       // 未执行
  UT_CHECK(out.display.empty());  // 不伪造
  // 群测路径：全空 → history 兜底生效
  ProbeState st2;
  st2.probe_result = std::nullopt;
  st2.history = {{"n1", 300}};
  DelayManager::instance().set_hooks(make_hooks(st2));
  auto out2 = DelayManager::instance().test_group(
      "PROXY", {"n1", "n2"}, false, std::nullopt);
  UT_CHECK(out2.injected);
  UT_CHECK(out2.display[0].second == 300);
  UT_CHECK(out2.display[1].second == 0);  // 兜底无 n2 → 0 Timeout
}

UT_TEST(t16_cache_reuse_skips_probe) {
  // ← 子集测速降频：新鲜缓存复用跳过拨测（len==1 不复用）
  ProbeState st;
  st.probe_result = std::map<std::string, uint32_t>{{"n1", 999}};
  DelayManager::instance().set_hooks(make_hooks(st));
  std::vector<std::pair<std::string, uint32_t>> cached = {{"n1", 120},
                                                          {"n2", 300}};
  auto out = DelayManager::instance().test_group(
      "PROXY", {"n1", "n2"}, /*allow_cache_reuse=*/true, cached);
  UT_CHECK_EQ(st.probe_calls, 0);  // 跳过拨测
  UT_CHECK(out.display[0].second == 120);
  // 单点不复用（强制实测）
  auto out2 = DelayManager::instance().test_single("PROXY", "n1");
  UT_CHECK_EQ(st.probe_calls, 1);
  UT_CHECK(out2.display[0].second == 999);
}

UT_TEST(t16_auto_select_pick_matrix) {
  using Pick = std::optional<std::string>;
  // 全正常 → 最小延迟
  auto r1 = AutoSelect::pick_fastest({{"a", 300}, {"b", 120}, {"c", 450}});
  UT_CHECK(r1 && *r1 == "b");
  // 全超时（0）+ 越界 → 无候选
  UT_CHECK(!AutoSelect::pick_fastest({{"a", 0}, {"b", 2000}, {"c", 5000}}).has_value());
  // 部分超时 → 只有 [30,2000) 内参与
  auto r3 = AutoSelect::pick_fastest({{"dead", 0}, {"slow", 1900}, {"ok", 500}});
  UT_CHECK(r3 && *r3 == "ok");
  // 失败服务器避让：b 在失败机上 → 选 c
  auto r4 = AutoSelect::pick_fastest(
      {{"a", 100}, {"b", 120}, {"c", 450}},
      [] { return std::vector<std::string>{"bad-server"}; },
      [] {
        return std::map<std::string, std::string>{
            {"a", "good"}, {"b", "bad-server"}, {"c", "good"}};
      });
  UT_CHECK(r4 && *r4 == "a");  // 避让剔除 bad-server 上的 b → a(100) 最快
  // 安全阀：全部候选在失败机上 → 放弃避让仍选最快
  auto r5 = AutoSelect::pick_fastest(
      {{"b", 120}},
      [] { return std::vector<std::string>{"bad-server"}; },
      [] { return std::map<std::string, std::string>{{"b", "bad-server"}}; });
  UT_CHECK(r5 && *r5 == "b");
}

UT_TEST(t16_auto_select_lock_mutex) {
  // 【BUG-121 差异】RAII Guard：并发 trigger 必返回 busy，且任何路径锁必释放
  auto& as = AutoSelect::instance();
  std::atomic<int> calls{0};
  AutoSelectHooks h;
  h.is_dummy = [](const std::string&) { return false; };
  h.current_uid = [] { return std::optional{std::string("u1")}; };
  h.select_node = [](const std::string&, const std::string&) { return true; };
  h.write_selected = [](const std::string&, const std::string&) { return true; };
  h.filter_text = [] { return std::string(); };
  as.set_hooks(std::move(h));
  // DelayManager 注入慢速探针（并发窗口）
  DelayProbeHooks dh;
  dh.group_probe = [&calls](const std::string&) {
    ++calls;
    Sleep(150);
    return std::optional{std::map<std::string, uint32_t>{{"n", 100}}};
  };
  dh.emit_results = [](const std::string&,
                       const std::vector<std::pair<std::string, uint32_t>>&) {};
  DelayManager::instance().set_hooks(std::move(dh));

  std::atomic<bool> second_busy{false};
  std::thread t1([&] { as.trigger("u1", std::vector<std::string>{"n"}, 1, false); });
  Sleep(30);  // 等 t1 进入探针
  std::thread t2([&] {
    auto r = as.trigger("u1", std::vector<std::string>{"n"}, 1, false);
    second_busy.store(r.busy);
  });
  t1.join();
  t2.join();
  UT_CHECK(second_busy.load());      // 第二调用 busy
  UT_CHECK_EQ(calls.load(), 1);      // 探针只执行一次（不重入）

  // 锁释放验证：结束后再 trigger 应正常（不卡死——BUG-121 差异点）
  auto r3 = as.trigger("u1", std::vector<std::string>{"n"}, 1, false);
  UT_CHECK(!r3.busy);
}

UT_TEST(t16_auto_select_select_and_writeback) {
  auto& as = AutoSelect::instance();
  AutoSelectHooks h;
  h.is_dummy = [](const std::string&) { return false; };
  h.current_uid = [] { return std::optional{std::string("u1")}; };
  h.filter_text = [] { return std::string(); };  // 无过滤 → 全子集
  std::string selected_node;
  h.select_node = [&](const std::string&, const std::string& node) {
    selected_node = node;
    return true;
  };
  int writes = 0;
  h.write_selected = [&](const std::string&, const std::string& node) {
    ++writes;
    selected_node = node;
    return true;
  };
  as.set_hooks(std::move(h));
  DelayProbeHooks dh;
  dh.group_probe = [](const std::string&) {
    return std::optional{std::map<std::string, uint32_t>{{"fast", 100},
                                                         {"slow", 800}}};
  };
  dh.emit_results = [](const std::string&,
                       const std::vector<std::pair<std::string, uint32_t>>&) {};
  DelayManager::instance().set_hooks(std::move(dh));

  auto out = as.trigger("u1", std::vector<std::string>{"fast", "slow"}, 1,
                        /*select=*/true);
  UT_CHECK(out.selected);
  UT_CHECK(selected_node == "fast");  // 择优 100ms
  UT_CHECK_EQ(writes, 1);             // 回写 selected

  // current_uid 变更 → 舍弃切换（双重校验）
  h.current_uid = [] { return std::optional{std::string("u2")}; };
  as.set_hooks(std::move(h));
  int writes_before = writes;
  auto out2 = as.trigger("u1", std::vector<std::string>{"fast", "slow"}, 1, true);
  UT_CHECK(!out2.selected);
  UT_CHECK_EQ(writes, writes_before);
}

UT_TEST(t16_calibrate_selected_paths) {
  int writes = 0;
  std::string written;
  auto read_sel = [](const std::string&) {
    return std::optional{std::string("stale-node")};
  };
  auto write_sel = [&](const std::string&, const std::string& node) {
    ++writes;
    written = node;
    return true;
  };
  // 1) selected 在子集内（filter 空=全子集）→ 保持 selected
  auto r1 = AutoSelect::calibrate_selected("u1", "", "kernel-now",
                                           read_sel, write_sel);
  UT_CHECK(r1 == "stale-node");
  UT_CHECK_EQ(writes, 0);
  // 2) selected 越界（filter "jp" 不匹配 "stale-node"）→ 回写内核实际
  auto r2 = AutoSelect::calibrate_selected("u1", "jp", "kernel-now",
                                           read_sel, write_sel);
  UT_CHECK(r2 == "kernel-now");
  UT_CHECK_EQ(writes, 1);
  UT_CHECK(written == "kernel-now");
  // 3) selected 缺失 → 回写
  auto read_none = [](const std::string&) { return std::optional<std::string>{}; };
  auto r3 = AutoSelect::calibrate_selected("u1", "", "kernel-now", read_none,
                                           write_sel);
  UT_CHECK(r3 == "kernel-now");
  UT_CHECK_EQ(writes, 2);
}

UT_TEST(t16_wakeup_cooldown) {
  AutoSelect& as = AutoSelect::instance();
  // 静默测速走 instance（含冷却态）——生产 hooks 已被 cmd 层绑定过？测试
  // 环境重设最小 hooks
  AutoSelectHooks h;
  h.is_dummy = [](const std::string&) { return false; };
  h.current_uid = [] { return std::optional{std::string("u")}; };
  as.set_hooks(std::move(h));
  DelayProbeHooks dh;
  dh.group_probe = [](const std::string&) {
    return std::optional{std::map<std::string, uint32_t>{{"n", 100}}};
  };
  dh.emit_results = [](const std::string&,
                       const std::vector<std::pair<std::string, uint32_t>>&) {};
  DelayManager::instance().set_hooks(std::move(dh));

  UT_CHECK(as.wakeup_cooldown_remaining_ms() == 0);  // 初始无冷却
  UT_CHECK(as.silent_test_on_wakeup("u"));           // 触发成功
  int64_t remain = as.wakeup_cooldown_remaining_ms();
  UT_CHECK(remain > 0 && remain <= 5 * 60 * 1000);   // 5min 冷却生效
  UT_CHECK(!as.silent_test_on_wakeup("u"));          // 冷却期内拒绝
}
