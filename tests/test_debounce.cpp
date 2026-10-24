// debounce/节流门测试（虚拟时钟，单线程语义验证）
#include "test_util.h"

#include "clashwin/util/debounce.h"

using clashwin::util::DebounceGate;
using clashwin::util::ThrottleGate;

UT_TEST(debounce_fires_once_after_quiet_period) {
  // 250ms 防抖（← refresh-clash/verge-config 常量）
  DebounceGate g(250);
  g.touch(0);
  UT_CHECK(!g.poll(100));
  UT_CHECK(!g.poll(200));
  g.touch(250);   // 中途再次 touch：重新计时
  UT_CHECK(!g.poll(300));
  UT_CHECK(!g.poll(499));
  UT_CHECK(g.poll(500));    // 静默 250ms 后放行一次
  UT_CHECK(!g.poll(501));   // 消费后不再触发
  UT_CHECK(!g.pending());
}

UT_TEST(debounce_cancel) {
  DebounceGate g(100);
  g.touch(0);
  g.cancel();
  UT_CHECK(!g.poll(1000));
}

UT_TEST(throttle_leading_and_trailing) {
  // 200ms 节流（← refresh-proxy-config 常量）
  ThrottleGate g(200);
  UT_CHECK(g.allow(0));      // 前沿立即放行
  UT_CHECK(!g.allow(100));   // 窗口内拒绝，记尾随
  UT_CHECK(!g.allow(150));
  UT_CHECK(!g.take_trailing(199));
  UT_CHECK(g.take_trailing(200));    // 窗口结束补发一次
  UT_CHECK(!g.take_trailing(201));   // 只补一次
  UT_CHECK(g.allow(400));            // 新窗口前沿放行（清尾随）
  UT_CHECK(!g.allow(450));           // 窗口内拒绝，记尾随
  UT_CHECK(g.take_trailing(700));    // 450 的被拒调用在窗口后补发一次
  UT_CHECK(!g.take_trailing(701));
}

UT_TEST(throttle_no_trailing_when_window_unused) {
  ThrottleGate g(800);  // ← profile-changed 常量
  UT_CHECK(g.allow(0));
  UT_CHECK(!g.take_trailing(800));  // 窗口内无被拒调用 → 不补发
  UT_CHECK(g.allow(800));
}
