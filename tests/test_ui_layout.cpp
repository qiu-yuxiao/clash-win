// T10 UI 测试：Column/Row/Stack 两阶段布局（权重/间距/对齐数值断言）。
#include "test_ui_helper.h"

#include "ui/container.h"
#include "ui/label.h"

using namespace clashwin::ui;
using namespace ui_test;

UT_TEST(ui_layout_column_fixed_and_padding) {
  Column col;
  col.set_padding(10, 5, 10, 5);
  col.spacing = 4;
  auto a = std::make_unique<FixedBox>(50, 30);
  auto b = std::make_unique<FixedBox>(60, 40);
  auto c = std::make_unique<FixedBox>(20, 10);
  FixedBox* pa = a.get();
  FixedBox* pb = b.get();
  FixedBox* pc = c.get();
  col.add_child(std::move(a));
  col.add_child(std::move(b));
  col.add_child(std::move(c));

  SizeF m = col.measure(200, 400);
  // 高 = 5+30+4+40+4+10+5 = 98；宽 = 10+60+10 = 80
  UT_CHECK_EQ(m.w, 80);
  UT_CHECK_EQ(m.h, 98);

  col.layout_tree({0, 0, 200, 400});
  // cross stretch：宽 = 200-20 = 180
  UT_CHECK(near_f(pa->bounds().x, 10) && near_f(pa->bounds().y, 5));
  UT_CHECK(near_f(pa->bounds().w, 180) && near_f(pa->bounds().h, 30));
  UT_CHECK(near_f(pb->bounds().y, 5 + 30 + 4));
  UT_CHECK(near_f(pc->bounds().y, 5 + 30 + 4 + 40 + 4));
}

UT_TEST(ui_layout_column_flex_weights) {
  Column col;
  col.layout_tree({0, 0, 100, 300});  // 先落位（measure 需要可用空间）
  auto f1 = std::make_unique<FixedBox>(10, 50);
  auto f2 = std::make_unique<FixedBox>(10, 50);
  auto fix = std::make_unique<FixedBox>(10, 60);
  FixedBox* pf1 = f1.get();
  FixedBox* pf2 = f2.get();
  FixedBox* pfix = fix.get();
  pf1->flex_weight = 1;
  pf2->flex_weight = 2;
  col.add_child(std::move(f1));
  col.add_child(std::move(f2));
  col.add_child(std::move(fix));
  col.layout_tree({0, 0, 100, 300});
  // 剩余 = 300-60 = 240 → 80 / 160
  UT_CHECK(near_f(pf1->bounds().h, 80));
  UT_CHECK(near_f(pf2->bounds().h, 160));
  UT_CHECK(near_f(pfix->bounds().h, 60));
  UT_CHECK(near_f(pfix->bounds().y, 240));
}

UT_TEST(ui_layout_column_cross_align) {
  auto mk = [] {
    auto col = std::make_unique<Column>();
    auto box = std::make_unique<FixedBox>(40, 20);
    FixedBox* p = box.get();
    col->add_child(std::move(box));
    return std::pair{std::move(col), p};
  };
  {  // start
    auto [col, p] = mk();
    col->cross_align = CrossAlign::Start;
    col->layout_tree({0, 0, 100, 100});
    UT_CHECK(near_f(p->bounds().x, 0) && near_f(p->bounds().w, 40));
  }
  {  // center
    auto [col, p] = mk();
    col->cross_align = CrossAlign::Center;
    col->layout_tree({0, 0, 100, 100});
    UT_CHECK(near_f(p->bounds().x, 30));
  }
  {  // end
    auto [col, p] = mk();
    col->cross_align = CrossAlign::End;
    col->layout_tree({0, 0, 100, 100});
    UT_CHECK(near_f(p->bounds().x, 60));
  }
  {  // stretch
    auto [col, p] = mk();
    col->cross_align = CrossAlign::Stretch;
    col->layout_tree({0, 0, 100, 100});
    UT_CHECK(near_f(p->bounds().x, 0) && near_f(p->bounds().w, 100));
  }
}

UT_TEST(ui_layout_row_main_align) {
  auto mk = [](MainAlign ma) {
    auto row = std::make_unique<Row>();
    row->main_align = ma;
    auto a = std::make_unique<FixedBox>(30, 10);
    auto b = std::make_unique<FixedBox>(30, 10);
    FixedBox* pa = a.get();
    FixedBox* pb = b.get();
    row->add_child(std::move(a));
    row->add_child(std::move(b));
    row->layout_tree({0, 0, 100, 50});
    return std::pair{pa->bounds().x, pb->bounds().x};
  };
  auto [xs, xe] = mk(MainAlign::Start);
  UT_CHECK(near_f(xs, 0) && near_f(xe, 30));
  auto [cs, ce] = mk(MainAlign::Center);
  UT_CHECK(near_f(cs, 20) && near_f(ce, 50));
  auto [es, ee] = mk(MainAlign::End);
  UT_CHECK(near_f(es, 40) && near_f(ee, 70));
  auto [bs, be] = mk(MainAlign::SpaceBetween);
  UT_CHECK(near_f(bs, 0) && near_f(be, 70));  // 间隙 40 居中
}

UT_TEST(ui_layout_nested_and_relayout) {
  // Column > Row > FixedBox：嵌套 arrange 传递
  Column col;
  auto row = std::make_unique<Row>();
  auto box = std::make_unique<FixedBox>(20, 20);
  FixedBox* pb = box.get();
  row->add_child(std::move(box));
  Row* pr = row.get();
  col.add_child(std::move(row));
  col.layout_tree({0, 0, 100, 100});
  // Row 无 flex：主轴尺寸 = 其 measure 高（20）；cross stretch 宽 = 100
  UT_CHECK(near_f(pr->bounds().y, 0) && near_f(pr->bounds().h, 20));
  UT_CHECK(near_f(pr->bounds().w, 100));
  UT_CHECK(near_f(pb->bounds().w, 20) && near_f(pb->bounds().h, 20));

  // relayout：根尺寸变化后子随动
  col.layout_tree({0, 0, 200, 100});
  UT_CHECK(near_f(pr->bounds().w, 200));
}

UT_TEST(ui_layout_stack_fill_and_paint_order) {
  static std::vector<int> g_paint_order;  // FixedBox id 记录绘制顺序
  struct Tagged : FixedBox {
    using FixedBox::FixedBox;
    void on_paint(IRenderTarget&) override {
      ++paint_count;
      g_paint_order.push_back(tag);
    }
    int tag = 0;
  };
  g_paint_order.clear();
  Stack st;
  auto a = std::make_unique<Tagged>(100, 100);
  auto b = std::make_unique<Tagged>(100, 100);
  a->tag = 1;
  b->tag = 2;
  Tagged* pa = a.get();
  Tagged* pb = b.get();
  st.add_child(std::move(a));
  st.add_child(std::move(b));
  UT_CHECK(pa->bounds().is_empty());
  st.layout_tree({0, 0, 100, 80});
  UT_CHECK(near_f(pa->bounds().w, 100) && near_f(pa->bounds().h, 80));  // 铺满
  UT_CHECK(near_f(pb->bounds().w, 100) && near_f(pb->bounds().h, 80));

  // Stack measure = 最大子期望
  SizeF m = st.measure(0, 0);
  UT_CHECK(near_f(m.w, 100) && near_f(m.h, 100));

  // 绘制顺序：加入顺序（后者在上）；z_order 提升改变顺序
  make_offscreen(100, 100);
  rt().begin_frame();
  st.paint_all(rt());
  rt().end_frame();
  UT_CHECK_EQ(g_paint_order.size(), 2u);
  UT_CHECK_EQ(g_paint_order[0], 1);
  UT_CHECK_EQ(g_paint_order[1], 2);
  g_paint_order.clear();
  pb->set_z_order(-1);  // b 沉底
  rt().begin_frame();
  st.paint_all(rt());
  rt().end_frame();
  UT_CHECK_EQ(g_paint_order[0], 2);
  UT_CHECK_EQ(g_paint_order[1], 1);
}
