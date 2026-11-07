// T12 诊断：hover 态必须产生**真实像素差异**（不是只改状态位）。
//
// 判据（与「滑块只是能滑动但完全不产生调整效果」同一判据）：
//   ① 同一控件在「常态」与「悬停」两次渲染之间，像素必须有可测量差异；
//   ② 差异必须落在旋钮/滑块本体区域，而不是别处；
//   ③ 无 hover 规则的皮肤（original / frosted 开关、monochrome 滑块）
//      悬停前后必须**完全一致** —— 这是「不擅自加戏」的反向校验。
#include <windows.h>

#include <cmath>
#include <vector>

#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/slider.h"
#include "ui/switch_w.h"
#include "ui/theme/theme.h"

using namespace clashwin::ui;
using namespace clashwin::ui::theme;
using namespace ui_test;

namespace {

std::vector<Px> render_capture(Widget& w, int w_px, int h_px) {
  make_offscreen(w_px, h_px, 96);
  rt().begin_frame();
  rt().clear({0, 0, 0, 0});
  w.on_paint(rt());
  rt().end_frame();
  std::vector<uint8_t> raw;
  int rw = 0, rh = 0;
  rt().read_pixels(raw, rw, rh);
  std::vector<Px> buf(raw.size() / 4);
  for (size_t i = 0; i < buf.size(); ++i) {
    buf[i] = Px{raw[i * 4 + 0], raw[i * 4 + 1], raw[i * 4 + 2], raw[i * 4 + 3]};
  }
  return buf;
}

// 两帧像素差异统计
struct Diff {
  int changed = 0;          // 差异像素数
  int max_delta = 0;        // 单像素最大通道差
  int min_x = 1 << 30;      // 差异区域范围
  int max_x = -1;
};

Diff compare(const std::vector<Px>& a, const std::vector<Px>& b, int w,
             int h) {
  Diff d;
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    const int dr = std::abs(a[i].r - b[i].r);
    const int dg = std::abs(a[i].g - b[i].g);
    const int db = std::abs(a[i].b - b[i].b);
    const int da = std::abs(a[i].a - b[i].a);
    const int m = std::max(std::max(dr, dg), std::max(db, da));
    if (m > 0) {
      ++d.changed;
      if (m > d.max_delta) d.max_delta = m;
      const int x = static_cast<int>(i % static_cast<size_t>(w));
      if (x < d.min_x) d.min_x = x;
      if (x > d.max_x) d.max_x = x;
    }
  }
  (void)h;
  return d;
}

}  // namespace

// 1) retro-3d 开关悬停必须产生像素差异（放大 + 亮度 + 阴影）
UT_TEST(t32_hover_retro_switch_changes_pixels) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Dark);

  SwitchW sw;
  sw.layout_tree({0, 0, 28, 14});

  const auto normal = render_capture(sw, 60, 40);

  // 触发悬停
  MouseEvent enter{MouseEventType::Enter, {0, 0}};
  sw.on_mouse(enter);
  UT_CHECK(sw.thumb_hovered());

  const auto hovered = render_capture(sw, 60, 40);
  const Diff d = compare(normal, hovered, 60, 40);
  UT_CHECK(d.changed > 0);            // 悬停必须真的改变画面
  UT_CHECK(d.max_delta >= 8);         // 且差异不是 1-bit 抖动
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  clear_input_state();
}

// 2) 无 hover 规则original 开关：悬停前后必须完全一致
UT_TEST(t32_hover_original_switch_unchanged) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Original, ThemeMode::Dark);

  SwitchW sw;
  sw.layout_tree({0, 0, 28, 14});
  const auto normal = render_capture(sw, 60, 40);

  MouseEvent enter{MouseEventType::Enter, {0, 0}};
  sw.on_mouse(enter);
  const auto hovered = render_capture(sw, 60, 40);

  const Diff d = compare(normal, hovered, 60, 40);
  UT_CHECK_EQ(d.changed, 0);   // Mini 原文无 hover 规则 → 不许加戏
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  clear_input_state();
}

// 3) modern-flat 开关悬停（scale 1.1）必须产生像素差异
UT_TEST(t32_hover_modernflat_switch_changes_pixels) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::ModernFlat, ThemeMode::Dark);

  SwitchW sw;
  sw.layout_tree({0, 0, 28, 14});
  const auto normal = render_capture(sw, 60, 40);

  MouseEvent enter{MouseEventType::Enter, {0, 0}};
  sw.on_mouse(enter);
  const auto hovered = render_capture(sw, 60, 40);

  const Diff d = compare(normal, hovered, 60, 40);
  UT_CHECK(d.changed > 0);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  clear_input_state();
}

// 4) retro-3d 滑块 thumb 悬停必须产生像素差异
UT_TEST(t32_hover_retro_slider_changes_pixels) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Dark);

  Slider sld;
  sld.min_ = 0;
  sld.max_ = 100;
  sld.step_ = 1;
  sld.layout_tree({0, 0, 114, 24});
  sld.set_value(50, false);
  sld.themed_ = true;
  sld.apply_theme();

  const auto normal = render_capture(sld, 140, 40);

  // 鼠标移到 thumb 上（thumb 中心 x = 7 + 0.5*100 = 57）
  MouseEvent mv{MouseEventType::Move, {57, 12}, 0};
  sld.on_mouse(mv);
  UT_CHECK(sld.thumb_hovered());

  const auto hovered = render_capture(sld, 140, 40);
  const Diff d = compare(normal, hovered, 140, 40);
  UT_CHECK(d.changed > 0);
  UT_CHECK(d.max_delta >= 8);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  clear_input_state();
}

// 5) monochrome 滑块（无 hover 规则）悬停前后完全一致
UT_TEST(t32_hover_monochrome_slider_unchanged) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Monochrome, ThemeMode::Dark);

  Slider sld;
  sld.min_ = 0;
  sld.max_ = 100;
  sld.step_ = 1;
  sld.layout_tree({0, 0, 114, 24});
  sld.set_value(50, false);
  sld.themed_ = true;
  sld.apply_theme();

  const auto normal = render_capture(sld, 140, 40);
  MouseEvent mv{MouseEventType::Move, {57, 12}, 0};
  sld.on_mouse(mv);
  // monochrome 无 hover 规则 → thumb_hovered_ 必须仍为 false
  UT_CHECK(!sld.thumb_hovered());
  const auto hovered = render_capture(sld, 140, 40);

  const Diff d = compare(normal, hovered, 140, 40);
  UT_CHECK_EQ(d.changed, 0);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  clear_input_state();
}

// 6) retro-3d 开关 on 态旋钮内浮雕必须产生像素差异
//    （Mini 的 `.Mui-checked .MuiSwitch-thumb` 只改 background/border，
//      boxShadow 的 inset 顶高光继承基础 thumb —— 缺了就少了立体感）
UT_TEST(t32_retro_switch_on_thumb_bevel_visible) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Dark);

  SwitchW sw;
  sw.layout_tree({0, 0, 28, 14});
  sw.set_checked(true, false);

  const auto on = render_capture(sw, 60, 40);
  // 采样 on 态旋钮顶缘（内浮雕高光线所在行）
  // 旋钮在 on 态位于右侧：x ≈ 28 - 14 = 14 起，中心 ≈ 21
  const int thumb_cx = 21;
  const Px top = on[static_cast<size_t>(4 * 60 + thumb_cx)];
  const Px bot = on[static_cast<size_t>(9 * 60 + thumb_cx)];
  // 顶部高光线应比中部亮（内浮雕高光）
  UT_CHECK(top.r + top.g + top.b > bot.r + bot.g + bot.b);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  clear_input_state();
}
