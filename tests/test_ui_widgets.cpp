// T10 UI 测试：控件状态机（button/switch/slider/textfield/segmented/checkbox/icon）。
#include "test_ui_helper.h"

#include "ui/button.h"
#include "ui/checkbox_w.h"
#include "ui/icon_w.h"
#include "ui/overlay_fade.h"
#include "ui/segmented.h"
#include "ui/slider.h"
#include "ui/switch_w.h"
#include "ui/textfield.h"

using namespace clashwin::ui;
using namespace ui_test;

UT_TEST(ui_widget_button_state_machine) {
  Button btn(L"OK");
  btn.layout_tree({0, 0, 80, 28});
  int clicks = 0;
  btn.on_click = [&] { ++clicks; };

  UT_CHECK(btn.state() == Button::State::Normal);
  move_to(&btn, 40, 14);
  UT_CHECK(btn.state() == Button::State::Hover);
  press(&btn, 40, 14);
  UT_CHECK(btn.state() == Button::State::Pressed);
  release(&btn, 40, 14);
  UT_CHECK(btn.state() == Button::State::Hover);
  UT_CHECK_EQ(clicks, 1);

  // 移出按钮后松开：不触发点击（capture 送达但位置在外）
  press(&btn, 40, 14);
  release(&btn, 40, 200);  // widget 外（bounds h=28）
  UT_CHECK_EQ(clicks, 1);
  move_to(&btn, 400, 400);  // 离开 → Normal
  UT_CHECK(btn.state() == Button::State::Normal);

  // disabled：吞事件不响应
  btn.set_disabled(true);
  UT_CHECK(btn.state() == Button::State::Disabled);
  move_to(&btn, 40, 14);
  press(&btn, 40, 14);
  release(&btn, 40, 14);
  UT_CHECK(btn.state() == Button::State::Disabled);
  UT_CHECK_EQ(clicks, 1);
  clear_input_state();
}

UT_TEST(ui_widget_button_paint_varies_by_state) {
  make_offscreen(120, 60);
  Button btn(L"B");
  btn.layout_tree({0, 0, 60, 28});
  auto fill_at = [&](Button::State s) {
    std::vector<uint8_t> px;
    int w, h;
    rt().begin_frame();
    rt().clear({1, 1, 1, 1});
    btn.on_paint(rt());
    rt().end_frame();
    rt().read_pixels(px, w, h);
    return px[(14 * w + 5) * 4];  // (5,14) 按钮中心偏左
  };
  float normal = fill_at(Button::State::Normal);
  btn.set_disabled(true);
  float disabled = fill_at(Button::State::Disabled);
  UT_CHECK(disabled < normal);  // disabled 半透明黑明显更暗（蓝通道）
}

UT_TEST(ui_widget_switch_click_and_drag) {
  SwitchW sw;
  sw.layout_tree({0, 0, 36, 20});
  int changes = 0;
  sw.on_change = [&](bool v) { (void)v; ++changes; };

  click(&sw, 18, 10);  // 单击（无位移）= toggle
  UT_CHECK(sw.checked());
  UT_CHECK_EQ(changes, 1);
  UT_CHECK(sw.anim() > 0.99f);
  click(&sw, 18, 10);
  UT_CHECK(!sw.checked());
  UT_CHECK_EQ(changes, 2);

  // 拖动回弹：从中间按住往左拖（位移 >3px 才算拖动）。
  // 先拉起（不经回调），保证回弹 false 会触发一次回调
  sw.set_checked(true, false);
  press(&sw, 10, 10);
  UT_CHECK(!sw.dragging());
  move_to(&sw, 4, 10, kMouseLeft);
  UT_CHECK(sw.dragging());
  UT_CHECK(sw.anim() < 0.1f);
  release(&sw, 4, 10);
  UT_CHECK(!sw.checked());   // <0.5 回弹
  UT_CHECK(!sw.dragging());
  UT_CHECK_EQ(changes, 3);   // true→false 触发

  // 拖动过半 → 开
  press(&sw, 10, 10);
  move_to(&sw, 30, 10, kMouseLeft);
  UT_CHECK(sw.anim() > 0.5f);
  release(&sw, 30, 10);
  UT_CHECK(sw.checked());
  UT_CHECK_EQ(changes, 4);

  // 拖动中进度跟随
  press(&sw, 10, 10);
  move_to(&sw, 20, 10, kMouseLeft);
  UT_CHECK(sw.anim() > 0.3f);
  release(&sw, 20, 10);
  UT_CHECK(!sw.dragging());
  clear_input_state();
}

UT_TEST(ui_widget_slider_value_quantize) {
  Slider sld;
  sld.min_ = 0;
  sld.max_ = 100;
  sld.step_ = 10;
  sld.layout_tree({0, 0, 114, 24});  // range = 100（thumb 14）
  int changes = 0;
  sld.on_change = [&](float) { ++changes; };

  click(&sld, 14 + 55, 12);  // x=69 → t=0.55 → 55 → 量化 60
  UT_CHECK(near_f(sld.value_, 60));
  UT_CHECK_EQ(changes, 1);

  // 拖动连续更新
  press(&sld, 14, 12);
  move_to(&sld, 24, 12);  // (24-7)/100 → 17 → 量化 20
  release(&sld, 24, 12);
  UT_CHECK(near_f(sld.value_, 20));
  UT_CHECK(!sld.dragging());

  // set_value clamp + 不触发
  sld.set_value(150, false);
  UT_CHECK(near_f(sld.value_, 100));
  UT_CHECK_EQ(changes, 3);
  clear_input_state();
}

UT_TEST(ui_widget_textfield_editing) {
  TextField::set_clipboard_hooks([] { return std::wstring(L"粘贴内容"); },
                                 [](const std::wstring& v) { (void)v; });
  TextField tf;
  tf.layout_tree({0, 0, 160, 28});
  tf.request_focus();
  UT_CHECK(tf.focused());

  chr(&tf, L'a');
  chr(&tf, L'b');
  chr(&tf, L'中');
  UT_CHECK(tf.text() == L"ab中");
  UT_CHECK_EQ(tf.cursor(), 3);

  key(&tf, VK_LEFT);
  key(&tf, VK_LEFT);
  UT_CHECK_EQ(tf.cursor(), 1);
  // shift 扩选
  key(&tf, VK_RIGHT, false, true);
  UT_CHECK(tf.selection() == L"b");
  chr(&tf, L'X');  // 覆盖选区
  UT_CHECK(tf.text() == L"aX中");

  // home/end
  key(&tf, VK_HOME);
  UT_CHECK_EQ(tf.cursor(), 0);
  key(&tf, VK_END);
  UT_CHECK_EQ(tf.cursor(), 3);

  // backspace / delete
  key(&tf, VK_BACK);
  UT_CHECK(tf.text() == L"aX");
  key(&tf, VK_HOME);
  key(&tf, VK_DELETE);
  UT_CHECK(tf.text() == L"X");

  // Ctrl+A 全选 + Ctrl+V 粘贴替换
  key(&tf, 'A', true);
  UT_CHECK(tf.selection() == L"X");
  chr(&tf, L'v', true);  // Ctrl+V 经 Char 或 Down 均可——实现挂 Down
  key(&tf, 'V', true);
  UT_CHECK(tf.text() == L"粘贴内容");

  // placeholder
  tf.set_text(L"");
  tf.set_placeholder(L"过滤节点...");
  UT_CHECK(tf.text().empty());
  UT_CHECK_EQ(tf.cursor(), 0);
  clear_input_state();
}

UT_TEST(ui_widget_textfield_clear_button) {
  TextField tf;
  tf.layout_tree({0, 0, 160, 28});
  tf.set_clear_button(true);
  UT_CHECK(tf.clear_button());
  UT_CHECK(!tf.can_clear());
  tf.set_text(L"abc");
  UT_CHECK(tf.can_clear());

  // ✕ 热区：右边距 4 + 24 框（src: base-search-box.tsx:126-136）
  click(&tf, 145, 14);
  UT_CHECK(tf.text().empty());
  UT_CHECK(!tf.can_clear());

  // opt-in：默认关
  TextField plain;
  plain.layout_tree({0, 0, 160, 28});
  UT_CHECK(!plain.clear_button());
  clear_input_state();
}

UT_TEST(ui_widget_textfield_edit_commands) {
  std::wstring clip;
  TextField::set_clipboard_hooks([&clip] { return clip; },
                                 [&clip](const std::wstring& v) { clip = v; });
  TextField tf;
  tf.layout_tree({0, 0, 160, 28});
  tf.set_text(L"abcd");
  tf.request_focus();

  UT_CHECK(!tf.can_undo());  // 无撤销栈 → EM_CANUNDO=false
  UT_CHECK(!tf.can_cut());
  UT_CHECK(!tf.can_copy());
  tf.select_all();
  UT_CHECK(tf.selection() == L"abcd");
  UT_CHECK(tf.can_cut());
  UT_CHECK(tf.can_copy());

  tf.copy();
  UT_CHECK(clip == L"abcd");
  tf.cut();
  UT_CHECK(tf.text().empty());
  UT_CHECK(clip == L"abcd");

  UT_CHECK(tf.can_paste());  // 编辑控件恒可粘贴
  tf.paste();
  UT_CHECK(tf.text() == L"abcd");

  tf.select_all();
  tf.clear_text();
  UT_CHECK(tf.text().empty());
  UT_CHECK(!tf.can_select_all());

  // 右键回调锚点（窗口坐标）+ 焦点
  PointF got{-1, -1};
  tf.on_context_menu = [&](PointF p) { got = p; };
  MouseEvent e{MouseEventType::Down, {20, 14}, kMouseRight};
  route_mouse_event(&tf, e);
  UT_CHECK(got.x == 20 && got.y == 14);
  UT_CHECK(tf.focused());
  clear_input_state();
}

UT_TEST(ui_widget_segmented_switch) {
  Segmented seg;
  seg.set_options({L"手动", L"系统代理", L"TUN"}, 0);
  int changed = -1;
  seg.on_change = [&](int i) { changed = i; };
  seg.layout_tree({0, 0, 120, 28});

  UT_CHECK_EQ(seg.selected(), 0);
  click(&seg, 80, 14);  // 第二段（seg_w=40 → idx 2）
  UT_CHECK_EQ(seg.selected(), 2);
  UT_CHECK_EQ(changed, 2);
  click(&seg, 10, 14);
  UT_CHECK_EQ(seg.selected(), 0);
  UT_CHECK_EQ(seg.option_count(), 3u);
}

UT_TEST(ui_widget_checkbox_toggle) {
  CheckboxW cb;
  cb.set_label(L"允许局域网");
  cb.layout_tree({0, 0, 120, 20});
  bool got = false;
  cb.on_change = [&](bool v) { got = v; };
  UT_CHECK(!cb.checked());
  click(&cb, 8, 10);
  UT_CHECK(cb.checked());
  UT_CHECK(got);
  click(&cb, 8, 10);
  UT_CHECK(!cb.checked());
  UT_CHECK(!got);
}

UT_TEST(ui_widget_icon_mapping) {
  // 原版实际引用清单的映射（@mui 名为 ASCII 窄串）
  UT_CHECK_EQ(IconW::glyph_for("close"), 0xE711);
  UT_CHECK_EQ(IconW::glyph_for("CloseRounded"), 0xE711);
  UT_CHECK_EQ(IconW::glyph_for("minimize"), 0xE921);
  UT_CHECK_EQ(IconW::glyph_for("CropSquare"), 0xE922);
  UT_CHECK_EQ(IconW::glyph_for("FilterNone"), 0xE923);
  UT_CHECK_EQ(IconW::glyph_for("push_pin"), 0xE718);
  UT_CHECK_EQ(IconW::glyph_for("KeyboardArrowUp"), 0xE70E);
  UT_CHECK_EQ(IconW::glyph_for("RefreshRounded"), 0xE72C);
  UT_CHECK_EQ(IconW::glyph_for("unknown_icon_xyz"), 0);

  IconW icon;
  icon.set_icon("settings");
  icon.layout_tree({0, 0, 20, 20});
  UT_CHECK(icon.glyph_for("settings") != 0);
}

// ← MUI theme.transitions.duration：Dialog 淡入 225ms（enteringScreen）、
//   淡出 195ms（leavingScreen）。这是 Mini 所有 <Dialog> 的缺省过渡。
UT_TEST(ui_overlay_fade_enter_leave_timing) {
  using clashwin::ui::OverlayFade;
  OverlayFade f;
  UT_CHECK(f.hidden());
  UT_CHECK(!f.animating());

  // 进入：0 → 1，225ms
  f.show_at(1000);
  UT_CHECK(f.animating());
  UT_CHECK(near_f(f.value(), 0.0f));
  f.advance_at(1000 + 112);  // 112/225 ≈ 0.498
  UT_CHECK(f.value() > 0.45f && f.value() < 0.55f);
  f.advance_at(1000 + 225);
  UT_CHECK(!f.animating());
  UT_CHECK(near_f(f.value(), 1.0f));

  // 退出：1 → 0，195ms（比进入快，MUI 缺省不对称）
  f.hide_at(2000);
  UT_CHECK(f.animating());
  f.advance_at(2000 + 97);  // 97/195 ≈ 0.497 → 1-0.497 ≈ 0.503
  UT_CHECK(f.value() > 0.4f && f.value() < 0.6f);
  f.advance_at(2000 + 195);
  UT_CHECK(f.hidden());
  UT_CHECK(!f.animating());

  // jump_to：跳过过渡（首帧直出）
  f.jump_to(1.0f);
  UT_CHECK(near_f(f.value(), 1.0f) && !f.animating());
  f.jump_to(0.0f);
  UT_CHECK(f.hidden());

  // Widget::set_opacity 钳位与「<1 才合成」的判定口径
  Button b(L"x");
  b.set_opacity(0.5f);
  UT_CHECK(near_f(b.opacity(), 0.5f));
  b.set_opacity(-1.0f);
  UT_CHECK(near_f(b.opacity(), 0.0f));
  b.set_opacity(2.0f);
  UT_CHECK(near_f(b.opacity(), 1.0f));
}

// ← Mini：<IconButton sx={{width:'28px',height:'28px'}}><SettingsRoundedIcon
//   sx={{fontSize:'20px',width:'20px',height:'20px'}} /></IconButton>
//   （_layout.tsx:1371-1387）。按钮 28 是命中盒，字形只有 20。
//   早期实现拿命中盒当缩放基准 → 20px 的齿轮被画成 28px，比 Mini 大一圈。
UT_TEST(ui_icon_glyph_size_ignores_hitbox) {
  make_offscreen(40, 40, 96);
  IconW ic;
  ic.set_icon("settings");
  ic.size_ = 20;                 // fontSize
  ic.box_w = ic.box_h = 28;      // IconButton 命中盒
  ic.color_ = Color4f::hex(0x000000);
  ic.layout_tree({6, 6, 28, 28});

  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  ic.paint_all(rt());
  rt().end_frame();

  int minx = 999, maxx = -1, miny = 999, maxy = -1;
  for (int y = 0; y < 40; ++y) {
    for (int x = 0; x < 40; ++x) {
      Px p = pixel(x, y);
      if (p.r > 240 && p.g > 240 && p.b > 240) continue;  // 背景白
      if (x < minx) minx = x;
      if (x > maxx) maxx = x;
      if (y < miny) miny = y;
      if (y > maxy) maxy = y;
    }
  }
  UT_CHECK(maxx >= 0);
  const int gw = maxx - minx + 1, gh = maxy - miny + 1;
  // MUI 24 viewBox 的 SettingsRounded 实际占约 21/24 → 20px 盒里约 17.5px。
  // 若按 28 的命中盒缩放，包围盒会到 24px 左右，这里直接卡住。
  UT_CHECK(gw <= 21 && gh <= 21);
  // 居中：盒 x∈[6,34)，20px 字形两侧各留 4px
  UT_CHECK(minx >= 6 + 3 && maxx <= 6 + 25);
  UT_CHECK(miny >= 6 + 3 && maxy <= 6 + 25);
}
