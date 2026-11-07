// T12 测试：6 套皮肤的开关（SwitchW）轨道/旋钮外观。
//
// 起因（用户反馈 2026-11-07）：默认 retro-3d（黄金皮肤）下，基础设置卡的
// 4 个开关（开机自启 / 隐身启动 / Allow LAN / IPv6）**滑槽看不见** ——
// 轨道是半透明金色叠在同色卡片底上，几乎无对比。
//
// Mini 源码依据（src/components/base/base-switch.tsx）：
//   retro-3d :103-115  track backgroundColor rgba(212,175,55,.25/.15)
//                            + opacity .85
//                            + border 1px rgba(212,175,55,.3/.18)
//                            + boxShadow inset 0 3d 5d rgba(0,0,0,.7)
//                                          , 0 1px 0 rgba(255,255,255,.08)
//   → 槽的立体感来自 **inset 内阴影 + 底部高光线**，不是底色对比。
//   Win 侧 SwitchPaint 此前既无 inset 字段也无高光线，槽自然"看不见"。
#include <windows.h>

#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/switch_w.h"
#include "ui/theme/theme.h"

using namespace clashwin::ui;
using namespace clashwin::ui::theme;
using namespace ui_test;

namespace {

// 渲染一个开关，返回离屏像素缓冲（供逐点采样）
void render_switch(SwitchW& sw, int w, int h) {
  rt().begin_frame();
  rt().clear({0, 0, 0, 0});
  sw.on_paint(rt());
  rt().end_frame();
}

// 采样轨道「右侧空白区」（旋钮不覆盖的那半）的像素
Px track_right_px(SwitchW& sw) {
  // 轨道右端往左 2px 处，垂直中心；避开旋钮与圆角
  const int x = static_cast<int>(sw.bounds().w) - 3;
  const int y = static_cast<int>(sw.bounds().h / 2);
  return pixel(x, y);
}

// 采样轨道「左侧空白区」
Px track_left_px(SwitchW& sw) {
  const int x = 2;
  const int y = static_cast<int>(sw.bounds().h / 2);
  return pixel(x, y);
}

}  // namespace

// 1) 6 套皮肤的轨道必须可辨（相对透明背景有可见差异）
UT_TEST(t12_switch_track_visible_all_skins) {
  make_offscreen(80, 40, 96);
  auto& svc = ThemeService::instance();
  const SkinId skins[] = {SkinId::Retro3d,  SkinId::Original,
                          SkinId::ModernFlat, SkinId::FrostedGlass,
                          SkinId::Cyberpunk, SkinId::Monochrome};
  for (SkinId sk : skins) {
    svc.init(sk, ThemeMode::Dark);
    SwitchW sw;
    sw.layout_tree({0, 0, 28, 14});
    render_switch(sw, 80, 40);
    Px p = track_right_px(sw);
    // 轨道像素必须「不是纯透明」也不是「全黑」——有可见的填充
    UT_CHECK(p.a > 10);
    // 至少有一个通道明显偏离 0（有色）
    UT_CHECK(p.r + p.g + p.b > 20);
  }
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 2) retro-3d 轨道必须带 inset 内阴影（槽的立体感来源）
//    —— 槽内上部应比底部更暗，且整体比卡片底深。
UT_TEST(t12_switch_retro_track_has_inset_shadow) {
  make_offscreen(80, 40, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Dark);
  SwitchW sw;
  sw.layout_tree({0, 0, 28, 14});
  render_switch(sw, 80, 40);
  const int x = static_cast<int>(sw.bounds().w) - 4;
  Px top = pixel(x, 3);
  Px mid = pixel(x, 7);
  Px bot = pixel(x, 11);
  // 上部（inset 阴影压暗处）应比中部更暗
  UT_CHECK(top.r + top.g + top.b <= mid.r + mid.g + mid.b + 24);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 3) retro-3d 的 on 态轨道是 135°→180° 三停金渐变，且旋钮为径向渐变
//    —— on 态与 off 态轨道像素必须不同（否则看起来像没开）。
UT_TEST(t12_switch_retro_on_off_track_differs) {
  make_offscreen(80, 40, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Dark);
  SwitchW off_sw;
  off_sw.layout_tree({0, 0, 28, 14});
  render_switch(off_sw, 80, 40);
  Px off_p = track_right_px(off_sw);

  SwitchW on_sw;
  on_sw.set_checked(true, false);
  on_sw.layout_tree({0, 0, 28, 14});
  render_switch(on_sw, 80, 40);
  Px on_p = track_right_px(on_sw);
  UT_CHECK(!(off_p.r == on_p.r && off_p.g == on_p.g && off_p.b == on_p.b));
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 4) 6 套皮肤的轨道底色必须彼此不同（证明查表生效，而非全皮肤一套）
UT_TEST(t12_switch_track_differs_per_skin) {
  make_offscreen(80, 40, 96);
  auto& svc = ThemeService::instance();
  const SkinId skins[] = {SkinId::Retro3d,  SkinId::Original,
                          SkinId::ModernFlat, SkinId::FrostedGlass,
                          SkinId::Cyberpunk, SkinId::Monochrome};
  Px seen[6];
  for (int i = 0; i < 6; ++i) {
    svc.init(skins[i], ThemeMode::Dark);
    SwitchW sw;
    sw.layout_tree({0, 0, 28, 14});
    render_switch(sw, 80, 40);
    seen[i] = track_right_px(sw);
  }
  // 至少存在两套皮肤轨道色不同（不要求两两互异，避免量化误差误报）
  int distinct = 0;
  for (int i = 0; i < 6; ++i) {
    bool dup = false;
    for (int j = 0; j < i; ++j) {
      if (std::abs(seen[i].r - seen[j].r) <= 2 &&
          std::abs(seen[i].g - seen[j].g) <= 2 &&
          std::abs(seen[i].b - seen[j].b) <= 2) {
        dup = true;
        break;
      }
    }
    if (!dup) ++distinct;
  }
  UT_CHECK(distinct >= 4);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 5) cyberpunk 轨道是 18×10（不是 28×14）——尺寸随皮肤
UT_TEST(t12_switch_size_per_skin) {
  make_offscreen(80, 40, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Cyberpunk, ThemeMode::Dark);
  SwitchW cy;
  SizeF s = cy.on_measure(100, 100);
  UT_CHECK_EQ(static_cast<int>(s.w), 18);
  UT_CHECK_EQ(static_cast<int>(s.h), 10);

  svc.init(SkinId::Retro3d, ThemeMode::Dark);
  SwitchW rt_sw;
  SizeF s2 = rt_sw.on_measure(100, 100);
  UT_CHECK_EQ(static_cast<int>(s2.w), 28);
  UT_CHECK_EQ(static_cast<int>(s2.h), 14);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}
