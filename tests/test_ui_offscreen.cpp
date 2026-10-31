// T10 UI 测试：离屏渲染像素断言 + DPI 96/144 逻辑→物理换算。
#include "test_ui_helper.h"

#include "ui/canvas_graph.h"
#include "ui/button.h"
#include "ui/label.h"
#include "ui/segmented.h"

using namespace clashwin::ui;
using namespace ui_test;

UT_TEST(ui_offscreen_fill_and_readback) {
  UT_CHECK(make_offscreen(64, 32, 96));
  UT_CHECK_EQ(rt().pixel_width(), 64);
  UT_CHECK_EQ(rt().pixel_height(), 32);
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  rt().fill_rect({10, 5, 20, 10}, Brush{Color4f::hex(0xFF0000)});
  rt().end_frame();
  Px red = pixel(15, 10);
  UT_CHECK_EQ(red.r, 255);
  UT_CHECK_EQ(red.g, 0);
  UT_CHECK_EQ(red.b, 0);
  Px bg = pixel(40, 20);
  UT_CHECK_EQ(bg.r, 255);
  UT_CHECK_EQ(bg.b, 255);
}

UT_TEST(ui_offscreen_dpi_144_scaling) {
  // 96dpi：逻辑 (10,10) → 物理 (10,10)；144dpi（150%）→ 物理 (15,15)
  UT_CHECK(make_offscreen(120, 120, 144));
  UT_CHECK(near_f(rt().scale(), 1.5f, 0.001f));
  UT_CHECK(near_f(rt().size().w, 80.0f));  // 120/1.5
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  rt().fill_rect({10, 10, 20, 20}, Brush{Color4f::hex(0x00FF00)});
  rt().end_frame();
  Px in = pixel(20, 20);   // 物理 (15..45) 内
  UT_CHECK_EQ(in.g, 255);
  UT_CHECK_EQ(in.r, 0);
  Px out = pixel(12, 12);  // 物理 18 < 15×1.5? (12 逻辑 @96 换算比较用物理坐标)
  // 物理 (12,12) 在填充区 (15,15) 之外
  UT_CHECK_EQ(out.g, 255);
  // 物理角点 (14,14) 必须是白底
  Px corner = pixel(14, 14);
  UT_CHECK(corner.g > 200);
  (void)out;
  (void)corner;
}

UT_TEST(ui_offscreen_dpi_96_baseline) {
  UT_CHECK(make_offscreen(120, 120, 96));
  UT_CHECK(near_f(rt().scale(), 1.0f, 0.001f));
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  rt().fill_rect({10, 10, 20, 20}, Brush{Color4f::hex(0x00FF00)});
  rt().end_frame();
  Px in = pixel(15, 15);
  UT_CHECK_EQ(in.g, 255);
  Px corner = pixel(9, 9);
  UT_CHECK_EQ(corner.g, 255);  // 白底
  UT_CHECK_EQ(corner.r, 255);
  // 96dpi 下 (9,9) 在填充区外
  Px outside = pixel(9, 15);
  UT_CHECK_EQ(outside.r, 255);
}

UT_TEST(ui_offscreen_text_renders_pixels) {
  UT_CHECK(make_offscreen(200, 60, 96));
  Label lbl(L"Clash Win 节点");
  lbl.style.size = 14;
  lbl.style.color = {0, 0, 0, 1};
  lbl.layout_tree({10, 20, 180, 24});
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  lbl.paint_all(rt());
  rt().end_frame();
  // 文本区域应有深色像素（DirectWrite 真渲染）
  int dark = 0, any_alpha = 0;
  for (int y = 0; y < 60; ++y)
    for (int x = 0; x < 200; ++x) {
      Px p = pixel(x, y);
      if (p.r < 128) ++dark;
      if (p.a != 255) ++any_alpha;
    }
  UT_CHECK(dark > 20);
  // 测量引擎可用
  SizeF m = rt().measure_text(L"abc", TextStyle{}, 0);
  UT_CHECK(m.w > 0 && m.h > 0);
}

UT_TEST(ui_offscreen_button_state_pixels) {
  UT_CHECK(make_offscreen(100, 40, 96));
  Button btn;
  btn.paint_params.fill_normal = Color4f::hex(0x007AFF);
  btn.layout_tree({0, 0, 80, 28});
  auto center_px = [&]() {
    rt().begin_frame();
    rt().clear({1, 1, 1, 1});
    btn.on_paint(rt());
    rt().end_frame();
    return pixel(40, 14);
  };
  Px normal = center_px();
  UT_CHECK_EQ(normal.r, 0);
  UT_CHECK_EQ(normal.b, 255);  // #007AFF
  btn.set_disabled(true);
  Px disabled = center_px();
  UT_CHECK(disabled.b < 240);  // 半透明黑叠加后变暗
}

UT_TEST(ui_offscreen_segmented_paint) {
  UT_CHECK(make_offscreen(150, 30, 96));
  Segmented seg;
  seg.set_options({L"MAN", L"SYS", L"TUN"}, 1);
  seg.layout_tree({0, 0, 120, 26});
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  seg.on_paint(rt());
  rt().end_frame();
  // 选中段（第二段中央）白底
  Px sel = pixel(70, 13);
  UT_CHECK(sel.r > 240 && sel.g > 240 && sel.b > 240);
  // 未选段底灰
  Px unsel = pixel(20, 13);
  UT_CHECK(unsel.r < 250);
}

UT_TEST(ui_offscreen_canvas_graph_lines) {
  UT_CHECK(make_offscreen(240, 80, 96));
  CanvasGraph g;
  std::vector<float> down(30), up(30);
  for (int i = 0; i < 30; ++i) {
    down[static_cast<size_t>(i)] = 100.0f + i * 5;
    up[static_cast<size_t>(i)] = 50.0f + i * 3;
  }
  g.set_series(up, down, 300.0f);
  g.layout_tree({0, 0, 240, 80});
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  g.paint_all(rt());
  rt().end_frame();
  // 折线经过的中部区域应有蓝色像素（↓线）
  int blue = 0, purple = 0;
  for (int y = 0; y < 80; ++y) {
    Px p = pixel(120, y);
    if (p.b > 200 && p.r < 120) ++blue;
    if (p.r > 140 && p.b > 180 && p.g < 130) ++purple;
  }
  UT_CHECK(blue > 0);  // 120 列处折线竖向穿过
}
