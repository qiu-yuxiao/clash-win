// T09 测试：事件总线门控（防抖/节流/轻量过滤）+ notice 50 条上限/自动消失
#include "test_util.h"

#include "events/event_bus.h"
#include "events/event_names.h"
#include "events/notice.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace clashwin::events;

UT_TEST(eventbus_debounce_gate) {
  EventBus bus(false);  // 无 worker：手动 flush 驱动，确定性
  bus.set_gate("e.debounce", GateType::Debounce, 60);
  std::atomic<int> hits{0};
  bus.subscribe("e.debounce", [&](const Payload&) { hits.fetch_add(1); });

  bus.publish("e.debounce", 1);
  bus.publish("e.debounce", 2);
  bus.flush_pending();  // 未静默到期 → 不派发
  UT_CHECK_EQ(hits.load(), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  bus.publish("e.debounce", 3);   // touch 重置
  bus.flush_pending();
  UT_CHECK_EQ(hits.load(), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(70));
  bus.flush_pending();            // 静默到期 → 尾随派发一次，取最后一次 payload
  UT_CHECK_EQ(hits.load(), 1);
}

UT_TEST(eventbus_throttle_gate) {
  EventBus bus(false);
  bus.set_gate("e.throttle", GateType::Throttle, 60);
  std::vector<int> seen;
  bus.subscribe("e.throttle", [&](const Payload& p) { seen.push_back(p.get<int>()); });

  bus.publish("e.throttle", 1);   // 首沿立即派发
  bus.flush_pending();
  UT_CHECK(seen.size() == 1 && seen[0] == 1);
  bus.publish("e.throttle", 2);   // 窗口内 → 拒，记尾随
  bus.publish("e.throttle", 3);
  bus.flush_pending();
  UT_CHECK_EQ(seen.size(), 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(70));
  bus.flush_pending();            // 窗口结束 → 尾随补发窗口内最后一次（3）
  UT_CHECK_EQ(seen.size(), 2);
  UT_CHECK_EQ(seen.back(), 3);
  bus.publish("e.throttle", 4);   // 新窗口首沿
  bus.flush_pending();
  UT_CHECK_EQ(seen.size(), 3);
  UT_CHECK_EQ(seen.back(), 4);
}

UT_TEST(eventbus_immediate_and_unsub) {
  EventBus bus(false);
  std::atomic<int> hits{0};
  uint64_t id = bus.subscribe("e.imm", [&](const Payload&) { hits.fetch_add(1); });
  bus.publish("e.imm");  // Immediate 同步派发
  UT_CHECK_EQ(hits.load(), 1);
  bus.unsubscribe(id);
  bus.publish("e.imm");
  UT_CHECK_EQ(hits.load(), 1);
  // 轻量过滤器
  bus.set_lightweight_filter(true);
  bus.publish("e.imm2");
  bus.set_lightweight_filter(false);
}

UT_TEST(eventbus_default_gates_from_names) {
  EventBus& g = EventBus::instance();
  UT_CHECK(g.gate_of(names::kRefreshClash) == GateType::Debounce);
  UT_CHECK(g.gate_of(names::kRefreshVerge) == GateType::Debounce);
  UT_CHECK(g.gate_of(names::kRefreshProxies) == GateType::Debounce);
  UT_CHECK(g.gate_of(names::kProfileChanged) == GateType::Throttle);
  UT_CHECK(g.gate_of(names::kNoticeMessage) == GateType::Immediate);
  UT_CHECK(g.gate_of(names::kBackendDelayResults) == GateType::Immediate);
  UT_CHECK(g.gate_of(names::kCoreUpgradeProgress) == GateType::Immediate);
}

UT_TEST(notice_ring_and_durations) {
  NoticeService& n = NoticeService::instance();
  n.subscribe([] {});
  // 缺省时长（← notice-service.ts DEFAULT_DURATIONS）
  uint64_t a = n.push("success", "s");
  uint64_t b = n.push("info", "i");
  uint64_t c = n.push("error", "e");
  auto snap = n.snapshot();
  UT_CHECK_EQ(snap.size(), 3u);
  UT_CHECK_EQ(snap[0].duration_ms, 3000);
  UT_CHECK_EQ(snap[1].duration_ms, 5000);
  UT_CHECK_EQ(snap[2].duration_ms, 8000);
  (void)a; (void)b; (void)c;
}

UT_TEST(notice_auto_dismiss) {
  NoticeService& n = NoticeService::instance();
  for (auto& item : n.snapshot()) n.hide(item.id);  // 清场（单例跨用例状态）
  n.push("success", "short");   // 3000ms
  n.push("error", "long");      // 8000ms
  n.advance_for_test(3500);
  auto s1 = n.snapshot();
  UT_CHECK_EQ(s1.size(), 1u);
  UT_CHECK_EQ(s1[0].message, std::string("long"));
  n.advance_for_test(5000);
  UT_CHECK_EQ(n.snapshot().size(), 0u);
}

UT_TEST(notice_max_fifty_ring) {
  NoticeService& n = NoticeService::instance();
  // 先清场
  for (auto& item : n.snapshot()) n.hide(item.id);
  for (int i = 0; i < 55; ++i) n.push("info", "m" + std::to_string(i), 0);  // 0=常驻
  auto snap = n.snapshot();
  UT_CHECK_EQ(snap.size(), consts::kNoticeMaxCount);
  // 裁剪的是最旧：m0..m4 应不在，m54 在
  bool has_m0 = false, has_m54 = false;
  for (auto& item : snap) {
    if (item.message == "m0") has_m0 = true;
    if (item.message == "m54") has_m54 = true;
  }
  UT_CHECK(!has_m0);
  UT_CHECK(has_m54);
  for (auto& item : n.snapshot()) n.hide(item.id);
}

UT_TEST(notice_hide_and_override) {
  NoticeService& n = NoticeService::instance();
  for (auto& item : n.snapshot()) n.hide(item.id);
  uint64_t id = n.push("info", "x", 0);          // 覆盖为常驻
  auto snap = n.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].duration_ms, 0);
  n.hide(id);
  UT_CHECK_EQ(n.snapshot().size(), 0u);
}
