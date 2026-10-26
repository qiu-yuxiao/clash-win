// T10 UI 测试：虚拟滚动（@tanstack/react-virtual 语义：可视区+overscan）。
#include "test_ui_helper.h"

#include "ui/container.h"
#include "ui/label.h"
#include "ui/scroll_list.h"

using namespace clashwin::ui;
using namespace ui_test;

namespace {
struct ListFixture {
  ScrollList list;
  int created = 0;
  ListFixture() {
    list.set_content(1000, 39.0f,
                     [this](int index) {
                       ++created;
                       return std::make_unique<Label>(L"node-" + std::to_wstring(index));
                     },
                     5);
    list.layout_tree({0, 0, 271, 390});  // 可视 10 行
  }
};
}  // namespace

UT_TEST(ui_scroll_materialize_visible_plus_overscan) {
  ListFixture fx;
  // 可视 10 行（390/39）+ 上下 overscan 5 → ≤20 行物化（1000 项不全建）
  UT_CHECK(fx.created <= 20);
  UT_CHECK(fx.created >= 10);
  auto idx = fx.list.materialized_indices();
  UT_CHECK_EQ(idx.size(), static_cast<size_t>(fx.created));
  // 物化窗口：[0-5, 10+5] → 0..15
  UT_CHECK_EQ(idx.front(), 0);
  UT_CHECK_EQ(idx.back(), 15);
  // 行位置正确
  Widget* row5 = fx.list.materialized_row(5);
  UT_CHECK(row5 != nullptr);
  UT_CHECK(near_f(row5->bounds().y, 5 * 39.0f));
  Widget* row900 = fx.list.materialized_row(900);
  UT_CHECK(row900 == nullptr);  // 未物化
}

UT_TEST(ui_scroll_offset_clamp) {
  ListFixture fx;
  fx.list.set_scroll_offset(-50);
  UT_CHECK(near_f(fx.list.scroll_offset(), 0));
  float max_s = 1000 * 39.0f - 390.0f;
  fx.list.set_scroll_offset(1e9);
  UT_CHECK(near_f(fx.list.scroll_offset(), max_s));
  UT_CHECK(near_f(fx.list.max_scroll(), max_s));
}

UT_TEST(ui_scroll_wheel_and_window_slide) {
  ListFixture fx;
  int scrolls = 0;
  fx.list.on_scroll = [&] { ++scrolls; };
  // 滚轮一格 = 3 行 = 117px
  wheel_at(&fx.list, 100, 200, 120);
  UT_CHECK(near_f(fx.list.scroll_offset(), 117));
  UT_CHECK_EQ(scrolls, 1);
  // 物化窗口滑动：0..15 → 卸载顶部、物化底部
  auto idx = fx.list.materialized_indices();
  UT_CHECK_EQ(idx.front(), 0);   // first=-2 → clamp 0
  UT_CHECK_EQ(idx.back(), 18);   // (117+390)/39=13 → +5
  // 连滚到深处：物化数恒有界（不随 1000 项增长）
  for (int i = 0; i < 30; ++i) wheel_at(&fx.list, 100, 200, 120);
  idx = fx.list.materialized_indices();
  UT_CHECK(idx.size() <= 25);
  // 滚出可视区的行已卸载
  UT_CHECK(fx.list.materialized_row(0) == nullptr);
  UT_CHECK(fx.list.materialized_row(1) == nullptr);
}

UT_TEST(ui_scroll_scrollbar_logical) {
  make_offscreen(271, 680);
  ListFixture fx;
  fx.list.paint_all(rt());  // 不崩溃 + 滚动条区域绘制（m>0 时）
  UT_CHECK(fx.list.max_scroll() > 0);
}

UT_TEST(ui_scroll_wheel_bubbles_to_container) {
  // wheel 命中行子项（Label 不处理）→ 沿父链冒泡到 ScrollList
  ListFixture fx;  // list 自身即容器；再包一层 Column 验证跨层冒泡
  Column root;
  root.add_child(std::unique_ptr<ScrollList>(&fx.list));  // 所有权转 root
  root.layout_tree({0, 0, 271, 390});
  wheel_at(&root, 100, 100, 120);
  UT_CHECK(near_f(fx.list.scroll_offset(), 117));
  root.remove_child(&fx.list).release();  // 归还所有权避免双销毁（fixture 接管）
}
