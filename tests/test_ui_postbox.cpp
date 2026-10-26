// T10 UI 测试：UiPostbox 跨线程投递（后台 → UI 线程）与 EventBus 统一订阅。
#include "test_ui_helper.h"

#include <thread>

#include "events/event_bus.h"
#include "ui/ui_postbox.h"

using namespace clashwin::ui;
using namespace clashwin::events;

UT_TEST(ui_postbox_post_and_pump) {
  int ran = 0;
  UiPostbox::instance().post([&] { ++ran; });
  UiPostbox::instance().post([&] { ++ran; });
  UT_CHECK(ran == 0);  // 未 pump 不执行（保证 UI 线程序列化）
  UiPostbox::instance().pump();
  UT_CHECK_EQ(ran, 2);
}

UT_TEST(ui_postbox_from_background_thread) {
  int ran = 0;
  std::thread t([&] {
    UiPostbox::instance().post([&] { ++ran; });
  });
  t.join();
  UT_CHECK(ran == 0);
  UiPostbox::instance().pump();
  UT_CHECK_EQ(ran, 1);
}

UT_TEST(ui_postbox_subscribe_ui_eventbus) {
  int got = 0;
  nlohmann::json payload_seen = nullptr;
  uint64_t id = UiPostbox::instance().subscribe_ui(
      "test-ui-event", [&](const nlohmann::json& p) {
        ++got;
        payload_seen = p;
      });
  // 后台线程 publish（WS 语义）
  std::thread t([&] { EventBus::instance().publish("test-ui-event", 42); });
  t.join();
  UT_CHECK(got == 0);
  UiPostbox::instance().pump();
  UT_CHECK_EQ(got, 1);
  UT_CHECK(payload_seen.is_number_integer());

  // 退订后不再接收
  UiPostbox::instance().unsubscribe(id);
  EventBus::instance().publish("test-ui-event", 1);
  UiPostbox::instance().pump();
  UT_CHECK_EQ(got, 1);
}

UT_TEST(ui_postbox_handler_runs_on_pumping_thread) {
  // 断言回调在 pump 调用线程（= UI 线程语义）
  DWORD seen = 0;
  DWORD self = GetCurrentThreadId();
  UiPostbox::instance().post([&] { seen = GetCurrentThreadId(); });
  UiPostbox::instance().pump();
  UT_CHECK_EQ(seen, self);
}
