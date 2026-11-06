// F-D11 测试：MUI SVG path 表 + 解析器 + fill_path 镂空像素验证。
// 源：@mui/icons-material（ClashVerge node_modules）d 属性；窗口控制器三键
//   （window-controller.tsx）与延迟徽章信号族（style-helpers.tsx getSignalIcon）。
#include <cmath>

#include "test_ui_helper.h"
#include "test_util.h"

#include "ui/icon_w.h"
#include "ui/title_bar.h"

using namespace clashwin::ui;
using namespace clashwin::ui::win;
using namespace ui_test;

UT_TEST(t27_mui_paths_table_hits) {
  // UI 实际使用的全部矢量名均命中 MUI 表（close=CloseRounded 抽屉关闭、
  // winclose=Close 标题栏、minimize、maximize/cropsquare、restore/filternone、
  // settings、pushpin、wifi+signalwifi0-4bar、wifioff、bolt、history、sort、
  // sortbyalpha、inbox）
  const char* names[] = {"close",   "winclose",     "minimize",
                         "maximize", "cropsquare",  "restore",
                         "filternone", "settings",  "pushpin",
                         "wifi",     "signalwifi0bar", "signalwifi1bar",
                         "signalwifi2bar", "signalwifi3bar", "signalwifi4bar",
                         "wifioff",  "bolt",         "history",
                         "sort",     "sortbyalpha",  "inbox"};
  for (const char* n : names) {
    std::vector<MuiLoopSet> out;
    UT_CHECK_MSG(IconW::mui_paths_for(n, out), n);
    for (const auto& ls : out) {
      UT_CHECK(ls.opacity > 0.0f && ls.opacity <= 1.0f);
      for (const auto& loop : ls.loops) {
        for (const auto& p : loop) {
          // 24×24 viewBox 内（微容差）
          UT_CHECK_MSG(p.x >= -0.5f && p.x <= 24.5f, n);
          UT_CHECK_MSG(p.y >= -0.5f && p.y <= 24.5f, n);
        }
      }
    }
  }
  // 未知名不命中
  std::vector<MuiLoopSet> none;
  UT_CHECK(!IconW::mui_paths_for("unknown_icon_xyz", none));
}

UT_TEST(t27_mui_parser_minimize_exact) {
  // Minimize d="M6 19h12v2H6z" → 单环 5 点（闭合补终点）：
  // (6,19)(18,19)(18,21)(6,21)(6,19)
  std::vector<MuiLoopSet> out;
  UT_CHECK(IconW::mui_paths_for("minimize", out));
  UT_CHECK_EQ(out.size(), 1u);
  UT_CHECK_EQ(out[0].loops.size(), 1u);
  const auto& lp = out[0].loops[0];
  UT_CHECK_EQ(lp.size(), 5u);
  if (lp.size() == 5) {
    UT_CHECK(near_f(lp[0].x, 6.0f) && near_f(lp[0].y, 19.0f));
    UT_CHECK(near_f(lp[1].x, 18.0f) && near_f(lp[1].y, 19.0f));
    UT_CHECK(near_f(lp[2].x, 18.0f) && near_f(lp[2].y, 21.0f));
    UT_CHECK(near_f(lp[3].x, 6.0f) && near_f(lp[3].y, 21.0f));
    UT_CHECK(near_f(lp[4].x, 6.0f) && near_f(lp[4].y, 19.0f));
  }
}

UT_TEST(t27_mui_parser_signal_family_structure) {
  // SignalWifi1-3Bar：两条 <path>（背景扇形 fillOpacity .3 + 前景弧 1.0）；
  // 4Bar：单条 1.0；0Bar：单条双环 donut（偶奇 → 弧带）
  std::vector<MuiLoopSet> out;
  UT_CHECK(IconW::mui_paths_for("signalwifi1bar", out));
  UT_CHECK_EQ(out.size(), 2u);
  if (out.size() == 2) {
    UT_CHECK(near_f(out[0].opacity, 0.3f, 0.01f));
    UT_CHECK(near_f(out[1].opacity, 1.0f, 0.01f));
  }
  for (const char* n : {"signalwifi2bar", "signalwifi3bar"}) {
    UT_CHECK(IconW::mui_paths_for(n, out));
    UT_CHECK_MSG(out.size() == 2, n);
    if (out.size() == 2) UT_CHECK(out[0].opacity < out[1].opacity);
  }  UT_CHECK(IconW::mui_paths_for("signalwifi4bar", out));
  UT_CHECK_EQ(out.size(), 1u);
  UT_CHECK(IconW::mui_paths_for("signalwifi0bar", out));
  UT_CHECK_EQ(out.size(), 1u);
  if (out.size() == 1) UT_CHECK_EQ(out[0].loops.size(), 2u);  // 内弧 + 外弧
  // WifiOff：单条 1.0、多子路径
  UT_CHECK(IconW::mui_paths_for("wifioff", out));
  UT_CHECK_EQ(out.size(), 1u);
  if (out.size() == 1) UT_CHECK(out[0].loops.size() >= 4u);
}

UT_TEST(t27_fill_path_donut_hole_pixels) {
  // CropSquare 16×16 黑色：偶奇填充 → 外框(viewBox 4..20)与内框(6..18)之间
  // 为环带（厚 2 单位 ≈1.33px @16px），中心镂空。16/24 缩放后环带：
  // 局部 2.67..4 与 12..13.33（全局 +8）。
  UT_CHECK(make_offscreen(32, 32, 96));
  IconW icon;
  icon.set_icon("cropsquare");
  icon.size_ = 16;
  icon.box_w = 16;
  icon.box_h = 16;
  icon.color_ = {0, 0, 0, 1};
  icon.layout_tree({8, 8, 16, 16});
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  icon.paint_all(rt());
  rt().end_frame();
  // 中心镂空（白）：内框 6..18（viewBox）→ 全局 12..20；(14,14)/(17,17) 在内
  Px c = pixel(14, 14);
  UT_CHECK_MSG(c.r > 200, "center must be hole");
  Px c2 = pixel(17, 17);
  UT_CHECK_MSG(c2.r > 200, "center2 must be hole");
  // 环带有色（黑）：顶边 y∈[10.67,12) 取 11；左边 x 同
  Px top = pixel(16, 11);
  UT_CHECK_MSG(top.r < 60, "top ring must be ink");
  Px left = pixel(11, 16);
  UT_CHECK_MSG(left.r < 60, "left ring must be ink");
  // 环带外（viewBox <4 → 全局 <10.67）与内框更内侧均镂空（白）
  Px outside = pixel(9, 16);
  UT_CHECK_MSG(outside.r > 200, "outside outer frame must be hole");
  Px innermost = pixel(16, 19);
  UT_CHECK_MSG(innermost.r > 200, "inner area must be hole");
}

UT_TEST(t27_signal_icon_switch_per_tier) {
  // set_icon 后 name() 归一化落表：延迟徽章按档位切换的名字均可达
  IconW w;
  w.set_icon("wifi");
  UT_CHECK_EQ(w.name(), std::string("wifi"));
  for (const char* sig :
       {"signalwifi0bar", "signalwifi1bar", "signalwifi2bar",
        "signalwifi3bar", "signalwifi4bar", "wifioff"}) {
    w.set_icon(sig);
    UT_CHECK_MSG(w.name() == std::string(sig), sig);
    std::vector<MuiLoopSet> out;
    UT_CHECK(IconW::mui_paths_for(w.name(), out));
  }
}

UT_TEST(t27_title_bar_buttons_use_mui_names) {
  // 三键挂 MUI path 名（window-controller.tsx：Minimize/CropSquare/Close；
  // large 态 FilterNone）
  TitleBar bar(nullptr);
  UT_CHECK(bar.btn_minimize()->icon_name_ == "minimize");
  UT_CHECK(bar.btn_large()->icon_name_ == "cropsquare");
  UT_CHECK(bar.btn_close()->icon_name_ == "winclose");
  bar.set_large_mode(true);
  UT_CHECK(bar.btn_large()->icon_name_ == "filternone");
  bar.set_large_mode(false);
  UT_CHECK(bar.btn_large()->icon_name_ == "cropsquare");
}
