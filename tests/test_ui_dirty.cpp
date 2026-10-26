// T10 UI 测试：脏区合并与 30fps 合帧（局部重绘面积 < 全屏断言）。
#include "test_ui_helper.h"

#include "ui/button.h"
#include "ui/container.h"
#include "ui/label.h"

using namespace clashwin::ui;
using namespace ui_test;

UT_TEST(ui_dirty_merge_union) {
  FrameClock fc;
  UT_CHECK(!fc.has_dirty());
  fc.invalidate({0, 0, 10, 10});
  fc.invalidate({20, 5, 15, 10});   // 不相交
  fc.invalidate({5, 8, 10, 10});    // 与第一个相交
  UT_CHECK(fc.has_dirty());
  RectF merged = fc.take_dirty();
  // 外接矩形：x=0..35, y=0..18
  UT_CHECK(near_f(merged.x, 0) && near_f(merged.y, 0));
  UT_CHECK(near_f(merged.right(), 35) && near_f(merged.bottom(), 18));
  UT_CHECK(!fc.has_dirty());  // take 后清空
  UT_CHECK(!fc.flush());      // 无脏区不重绘
}

UT_TEST(ui_dirty_flush_single_paint_per_frame) {
  FrameClock fc;
  int paints = 0;
  RectF last_dirty{};
  fc.bind([&](const RectF& d) {
    ++paints;
    last_dirty = d;
  });
  // 一帧内多次 invalidate（合帧语义：只绘一次）
  for (int i = 0; i < 5; ++i) fc.invalidate({static_cast<float>(i * 3), 0, 5, 5});
  UT_CHECK(fc.flush());
  UT_CHECK_EQ(paints, 1);
  UT_CHECK(near_f(last_dirty.right(), 3 * 4 + 5));
  UT_CHECK(!fc.flush());
  UT_CHECK_EQ(paints, 1);
  // 第二帧
  fc.invalidate({100, 100, 4, 4});
  UT_CHECK(fc.flush());
  UT_CHECK_EQ(paints, 2);
  UT_CHECK(near_f(last_dirty.x, 100));
  fc.unbind();
}

UT_TEST(ui_dirty_widget_invalidate_global_coords) {
  make_offscreen(200, 200);
  Column col;
  auto inner = std::make_unique<Column>();
  inner->set_padding(10, 10, 0, 0);
  auto btn = std::make_unique<Button>();
  Button* pb = btn.get();
  inner->add_child(std::move(btn));
  col.add_child(std::move(inner));
  col.layout_tree({0, 0, 200, 200});  // btn 全局 (10,10,190,24)

  FrameClock fc;
  set_frame_clock(&fc);
  pb->invalidate();  // 自身矩形
  RectF d = fc.take_dirty();
  UT_CHECK(near_f(d.x, 10) && near_f(d.y, 10));
  UT_CHECK(near_f(d.w, 190) && near_f(d.h, 24));

  pb->invalidate({5, 5, 20, 10});  // 局部 → 全局
  d = fc.take_dirty();
  UT_CHECK(near_f(d.x, 15) && near_f(d.y, 15));
  UT_CHECK(near_f(d.w, 20) && near_f(d.h, 10));
  set_frame_clock(nullptr);
}

UT_TEST(ui_dirty_partial_redraw_less_than_fullscreen) {
  // 硬指标：连续小控件 invalidate 合并后面积 < 全屏（无全屏重绘）
  make_offscreen(271, 680);  // 实际窗口尺寸
  float full = 271.0f * 680.0f;
  FrameClock fc;
  set_frame_clock(&fc);
  std::vector<Button> btns(10);  // 各自小矩形（一行工具条语义）
  for (int i = 0; i < 10; ++i)
    btns[static_cast<size_t>(i)].set_bounds(
        {static_cast<float>(i) * 27, 0, 25, 28});
  for (auto& b : btns) b.invalidate();
  set_frame_clock(nullptr);
  RectF merged = fc.take_dirty();
  UT_CHECK(merged.area() < full);  // 一行按钮外接远小于全屏
  UT_CHECK(merged.area() > 0);
}

UT_TEST(ui_dirty_invisible_widget_no_report) {
  FrameClock fc;
  set_frame_clock(&fc);
  Button btn(L"B");
  btn.set_visible(false);
  btn.layout_tree({0, 0, 50, 28});
  btn.invalidate();
  UT_CHECK(!fc.has_dirty());  // 不可见不上报
  btn.set_visible(true);      // set_visible(true) 触发 invalidate
  UT_CHECK(fc.has_dirty());
  set_frame_clock(nullptr);
}
