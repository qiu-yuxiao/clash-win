// T10 UI 测试：命中测试（嵌套/遮挡/z 序/overlay 吞事件）。
#include "test_ui_helper.h"

#include "ui/button.h"
#include "ui/container.h"
#include "ui/context_menu.h"
#include "ui/label.h"
#include "ui/modal.h"

using namespace clashwin::ui;
using namespace ui_test;

UT_TEST(ui_hit_nested_deepest_wins) {
  Column col;
  auto row = std::make_unique<Row>();
  auto btn = std::make_unique<Button>();
  Button* pb = btn.get();
  row->flex_weight = 1;  // 主轴铺满 → btn 可命中 (30,30)
  btn->flex_weight = 1;  // btn 主轴也铺满
  row->add_child(std::move(btn));
  col.add_child(std::move(row));
  col.layout_tree({0, 0, 100, 100});

  Widget* hit = col.hit_test({30, 30});
  UT_CHECK_EQ(hit, static_cast<Widget*>(pb));  // 最深 widget

  UT_CHECK_EQ(col.hit_test({200, 30}), nullptr);  // 界外
  UT_CHECK_EQ(col.hit_test({-1, 5}), nullptr);
}

UT_TEST(ui_hit_occlusion_and_z_order) {
  Stack st;
  auto back = std::make_unique<Button>();
  auto front = std::make_unique<FixedBox>(60, 60);
  Button* pback = back.get();
  FixedBox* pfront = front.get();
  st.add_child(std::move(back));
  st.add_child(std::move(front));
  st.layout_tree({0, 0, 100, 100});

  // 后加入者（绘在上层）优先命中
  UT_CHECK_EQ(st.hit_test({30, 30}), static_cast<Widget*>(pfront));
  // z_order 提升翻转
  pback->set_z_order(5);
  UT_CHECK_EQ(st.hit_test({30, 30}), static_cast<Widget*>(pback));

  // 不重叠区域命中 back 区域内（front 60x60 外）
  pback->set_z_order(0);
  UT_CHECK_EQ(st.hit_test({90, 90}), static_cast<Widget*>(pfront));  // 都铺满
}

UT_TEST(ui_hit_invisible_and_disabled) {
  Column col;
  auto btn = std::make_unique<Button>();
  Button* pb = btn.get();
  pb->flex_weight = 1;
  col.add_child(std::move(btn));
  col.layout_tree({0, 0, 100, 100});

  UT_CHECK_EQ(col.hit_test({50, 50}), static_cast<Widget*>(pb));
  pb->set_visible(false);
  // 不可见子树不可命中；点落到父容器自身（父仍可交互）
  Widget* hit_hidden = col.hit_test({50, 50});
  UT_CHECK(hit_hidden != static_cast<Widget*>(pb));
  pb->set_visible(true);
  pb->set_disabled(true);  // disabled 子树不可命中（同样落父容器）
  Widget* hit_disabled = col.hit_test({50, 50});
  UT_CHECK(hit_disabled != static_cast<Widget*>(pb));
}

UT_TEST(ui_hit_hidden_click_no_effect) {
  Button btn(L"B");
  btn.layout_tree({0, 0, 60, 28});
  int clicks = 0;
  btn.on_click = [&] { ++clicks; };
  btn.set_visible(false);
  move_to(&btn, 30, 14);
  press(&btn, 30, 14);
  release(&btn, 30, 14);
  UT_CHECK_EQ(clicks, 0);
  UT_CHECK_EQ(hovered_widget(), nullptr);
  clear_input_state();
}

UT_TEST(ui_hit_modal_blocks_underlayer) {
  Stack root;
  auto under = std::make_unique<Button>();
  Button* punder = under.get();
  root.add_child(std::move(under));
  auto modal = std::make_unique<Modal>();
  Modal* pmodal = modal.get();
  root.add_child(std::move(modal));
  root.layout_tree({0, 0, 400, 300});

  int under_clicks = 0;
  punder->on_click = [&] { ++under_clicks; };

  auto content = std::make_unique<FixedBox>(200, 120);
  int mask_closes = 0;
  pmodal->on_close = [&] { ++mask_closes; };
  pmodal->open(std::move(content), {200, 120}, L"标题");
  UT_CHECK(pmodal->is_open());
  UT_CHECK(pmodal->dialog_bounds().w > 0);

  // 对话框下方（遮罩区）点击：下层收不到 + 遮罩关闭
  press(&root, 20, 20);  // 远离对话框（居中 100,90~300,210）
  release(&root, 20, 20);
  UT_CHECK_EQ(under_clicks, 0);
  UT_CHECK(!pmodal->is_open());
  UT_CHECK_EQ(mask_closes, 1);

  // 关闭后点击穿透
  click(&root, 30, 30);
  UT_CHECK_EQ(under_clicks, 1);
  clear_input_state();
}

UT_TEST(ui_hit_modal_button_row) {
  Modal modal;
  modal.layout_tree({0, 0, 400, 300});
  int got = -1;
  modal.on_button = [&](int i) { got = i; };
  modal.open(std::make_unique<FixedBox>(100, 60), {200, 120});
  UT_CHECK_EQ(modal.dialog_bounds().x, 100);  // (400-200)/2
  UT_CHECK_EQ(modal.dialog_bounds().y, 90);   // (300-120)/2

  // 对话框内（content 区）点击不关闭
  press(&modal, 200, 150);
  release(&modal, 200, 150);
  UT_CHECK(modal.is_open());
  clear_input_state();
}

UT_TEST(ui_hit_context_menu_select_and_dismiss) {
  Stack root;  // overlay 宿主：button 与 menu 都铺满（menu 全局=局部坐标）
  auto btn = std::make_unique<Button>();
  Button* pbtn = btn.get();
  root.add_child(std::move(btn));
  auto menu = std::make_unique<ContextMenu>();
  ContextMenu* pm = menu.get();
  root.add_child(std::move(menu));
  root.layout_tree({0, 0, 300, 200});

  int under_clicks = 0;
  pbtn->on_click = [&] { ++under_clicks; };

  int selected = -1;
  pm->on_select = [&](int i) { selected = i; };
  pm->open({{L"更新", false, false, L""},
            {L"编辑", false, false, L""},
            {L"", false, true, L""},   // separator
            {L"删除", true, false, L""}},  // disabled
           {20, 20});
  UT_CHECK(pm->is_open());
  UT_CHECK(pm->menu() != nullptr);
  // 菜单 4 项：3*28 + 9(sep) + 4 = 97 高；bounds 即全局（root 零点重合）
  RectF mr = pm->menu_rect();
  UT_CHECK(near_f(mr.w, 140));
  UT_CHECK(near_f(mr.h, 97));

  // 点第一项
  click(&root, mr.x + 20, mr.y + 14);
  UT_CHECK_EQ(selected, 0);
  UT_CHECK(!pm->is_open());

  // 重新打开，点菜单外：关闭且不触发下层
  pm->open({{L"A"}}, {20, 20});
  click(&root, 250, 180);
  UT_CHECK(!pm->is_open());
  UT_CHECK_EQ(selected, 0);  // 未变化

  // 点下层按钮：菜单已关 → 穿透
  click(&root, 150, 100);
  UT_CHECK_EQ(under_clicks, 1);
  clear_input_state();
}

UT_TEST(ui_hit_route_hover_enter_leave) {
  Button btn(L"B");
  btn.layout_tree({0, 0, 60, 28});
  UT_CHECK_EQ(hovered_widget(), nullptr);
  move_to(&btn, 30, 14);
  UT_CHECK_EQ(hovered_widget(), &btn);
  move_to(&btn, 100, 100);  // 移出
  UT_CHECK_EQ(hovered_widget(), nullptr);
  // 状态经 Enter/Leave 翻转
  UT_CHECK(btn.state() == Button::State::Normal);
  clear_input_state();
}
