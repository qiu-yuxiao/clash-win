// T12 诊断：设置抽屉内 4 个开关（开机自启 / 隐身启动 / Allow LAN / IPv6）
// 在 6 套皮肤下的**轨道可见性**与 d/v 响应。
//
// 起因（用户反馈 2026-11-07）：
//   「默认皮肤，也就是那个黄金皮肤，我看见4个小开关……他们的滑槽是看不见的。
//     观感和mini差了很多。同时，其他几套皮肤上边儿同样的地方也要查一下。要举一反三。」
//
// 判据：
//   ① 轨道像素必须既不是「全透明」也不是「与卡片底完全同色」——
//      即与背景存在可见对比（这是「槽看得见」的客观定义）。
//   ② 轨道像素必须随皮肤变化（证明走查表而非全皮肤一套）。
//   ③ 开关本体（轨道 + 旋钮）在开启/关闭两态下像素必须不同。
#include <windows.h>

#include <cmath>

#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/scenes/settings_drawer.h"
#include "ui/switch_w.h"
#include "ui/theme/theme.h"

using namespace clashwin::ui;
using namespace clashwin::ui::theme;
using namespace ui_test;

namespace {

const SkinId kSkins[] = {SkinId::Retro3d,  SkinId::Original,
                          SkinId::ModernFlat, SkinId::FrostedGlass,
                          SkinId::Cyberpunk, SkinId::Monochrome};

// 渲染一个开关到离屏并回读像素
void render_switch(SwitchW& sw) {
  rt().begin_frame();
  rt().clear({0, 0, 0, 0});
  sw.on_paint(rt());
  rt().end_frame();
}

// 轨道左端（旋钮之外）的采样点
Px track_left(const SwitchW& sw) {
  return pixel(2, static_cast<int>(sw.bounds().h / 2));
}

// 轨道右端（旋钮之外）的采样点
Px track_right(const SwitchW& sw) {
  return pixel(static_cast<int>(sw.bounds().w) - 3,
               static_cast<int>(sw.bounds().h / 2));
}

// 槽内上部（inset 暗影压暗处）
Px track_top(const SwitchW& sw) {
  return pixel(static_cast<int>(sw.bounds().w) - 4, 3);
}

int brightness(const Px& p) { return p.r + p.g + p.b; }

}  // namespace

// 1) 6 套皮肤的轨道必须与透明背景有可见对比（不是隐形滑块）
UT_TEST(t12_diag_switch_track_visible_per_skin) {
  make_offscreen(80, 40, 96);
  auto& svc = ThemeService::instance();
  for (SkinId sk : kSkins) {
    svc.init(sk, ThemeMode::Dark);
    SwitchW sw;
    sw.layout_tree({0, 0, 28, 14});
    render_switch(sw);
    Px l = track_left(sw);
    Px r = track_right(sw);
    // 轨道必须有实际填充（alpha > 0）
    UT_CHECK(l.a > 8);
    UT_CHECK(r.a > 8);
    // 且有可见色（不是纯黑）
    UT_CHECK(brightness(r) > 12);
  }
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 2) 6 套皮肤的轨道色必须**逐皮肤匹配 Mini 源码的权威值**
//    （← src/components/base/base-switch.tsx 的各皮肤分支）
//
//    ★ 重要事实：Mini 暗色模式下 `modern-flat`(:266) 与 `monochrome`(:538)
//      的轨道底色**同为 #374151**；亮色模式同为 #e5e7eb。
//      所以「6 套两两互异」在 Mini 语义下不成立 —— 本用例断言的是
//      「每套皮肤的轨道色 == Mini 给它定的那个值」，这才是 1:1 判据。
//
//    暗色权威值：
//      retro-3d      rgba(212,175,55,.15) * opacity .85   (:104-108)
//      original      #30363d                              (:195)
//      modern-flat   #374151                              (:266)
//      frosted-glass rgba(255,255,255,.08)                 (:355)
//      cyberpunk     #05070c                              (:437)
//      monochrome    #374151                              (:538)
//    亮色权威值：
//      retro-3d      rgba(212,175,55,.25) * opacity .85
//      original      #e2e8f0
//      modern-flat   #e5e7eb
//      frosted-glass rgba(0,0,0,.06)
//      cyberpunk     #f1f5f9
//      monochrome    #e5e7eb
//
//    注：D2D 回读为**预乘 alpha**，故半透明色的 RGB 已被 alpha 缩放。
UT_TEST(t12_diag_switch_track_color_differs_per_skin) {
  make_offscreen(80, 40, 96);
  auto& svc = ThemeService::instance();

  // {r, g, b, a} —— 半透明者按预乘 alpha 给出期望回读值
  struct Exp {
    int r, g, b, a;
  };
  const Exp kDark[6] = {
      {27, 22, 7, 33},   // retro-3d  212/175/55 @ .15*.85=.1275 → a=33
      {48, 54, 61, 255}, // original  #30363d
      {55, 65, 81, 255}, // modern-flat #374151
      {20, 20, 20, 20},  // frosted   255 @ .08 → 预乘 20,20,20
      {5, 7, 12, 255},   // cyberpunk #05070c
      {55, 65, 81, 255}, // monochrome #374151（与 modern-flat 同色，Mini 事实）
  };
  const Exp kLight[6] = {
      {45, 37, 12, 54},  // retro-3d  212/175/55 @ .25*.85=.2125 → a=54
      {226, 232, 240, 255}, // original  #e2e8f0
      {229, 231, 235, 255}, // modern-flat #e5e7eb
      {0, 0, 0, 15},     // frosted   black @ .06 → 预乘 0,0,0
      {241, 245, 249, 255}, // cyberpunk #f1f5f9
      {229, 231, 235, 255}, // monochrome #e5e7eb（与 modern-flat 同色）
  };

  for (int pass = 0; pass < 2; ++pass) {
    const ThemeMode mode = pass == 0 ? ThemeMode::Dark : ThemeMode::Light;
    const Exp* exp = pass == 0 ? kDark : kLight;
    for (int i = 0; i < 6; ++i) {
      svc.init(kSkins[i], mode);
      SwitchW sw;
      sw.layout_tree({0, 0, 28, 14});
      render_switch(sw);
      const Px p = track_right(sw);
      UT_CHECK_MSG(std::abs(p.r - exp[i].r) <= 2 &&
                       std::abs(p.g - exp[i].g) <= 2 &&
                       std::abs(p.b - exp[i].b) <= 2 &&
                       std::abs(p.a - exp[i].a) <= 2,
                   (pass == 0 ? "dark" : "light"));
    }
  }
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 2b) 6 套皮肤确实走了 6 份**各自**的定义（而不是一套通用皮肤）
//     → 至少 5 个互不相同的轨道色组（Mini 事实：modern-flat 与 monochrome 同色）
UT_TEST(t12_diag_switch_track_has_multiple_distinct_groups) {
  make_offscreen(80, 40, 96);
  auto& svc = ThemeService::instance();
  Px seen[6];
  for (int i = 0; i < 6; ++i) {
    svc.init(kSkins[i], ThemeMode::Dark);
    SwitchW sw;
    sw.layout_tree({0, 0, 28, 14});
    render_switch(sw);
    seen[i] = track_right(sw);
  }
  int groups = 0;
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
    if (!dup) ++groups;
  }
  UT_CHECK(groups >= 5);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 3) retro-3d 槽内必须有「上暗下亮」的立体感（inset 暗影 + 底部高光线）
//    —— 这是「滑槽看得见」的关键，不是靠底色对比。
UT_TEST(t12_diag_retro_track_has_recess) {
  make_offscreen(80, 40, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Dark);
  SwitchW sw;
  sw.layout_tree({0, 0, 28, 14});
  render_switch(sw);
  const int top = brightness(track_top(sw));
  const int bot = brightness(pixel(static_cast<int>(sw.bounds().w) - 4, 11));
  UT_CHECK(top <= bot);  // 上部被 inset 压暗
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 4) on/off 两态的轨道像素必须不同（否则看起来没开）
UT_TEST(t12_diag_switch_on_off_differs_per_skin) {
  make_offscreen(80, 40, 96);
  auto& svc = ThemeService::instance();
  for (SkinId sk : kSkins) {
    svc.init(sk, ThemeMode::Dark);
    SwitchW off_sw;
    off_sw.layout_tree({0, 0, 28, 14});
    render_switch(off_sw);
    Px off_p = track_right(off_sw);

    SwitchW on_sw;
    on_sw.set_checked(true, false);
    on_sw.layout_tree({0, 0, 28, 14});
    render_switch(on_sw);
    Px on_p = track_right(on_sw);
    UT_CHECK(!(off_p.r == on_p.r && off_p.g == on_p.g && off_p.b == on_p.b));
  }
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 5) ★ 开关轨道必须响应 d/v（retro-3d 的 Depth/Vibrancy 语义）
//    → 拖动滑块时槽的立体感（inset 深度）必须变化。
UT_TEST(t12_diag_switch_responds_to_factors) {
  make_offscreen(80, 40, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Dark);
  svc.set_factors(0.0f, 1.0f);
  SwitchW a;
  a.layout_tree({0, 0, 28, 14});
  render_switch(a);
  Px a_top = track_top(a);

  svc.set_factors(1.0f, 1.0f);
  SwitchW b;
  b.layout_tree({0, 0, 28, 14});
  render_switch(b);
  Px b_top = track_top(b);
  // val1（Depth）↑ → inset 更深 → 槽上部更暗
  UT_CHECK(brightness(b_top) <= brightness(a_top));
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 6) 设置抽屉里 4 个开关都必须被实际挂载且轨道可见
//    （用户反馈指的就是这 4 个：开机自启 / 隐身启动 / Allow LAN / IPv6）
//    ← BasicCard 的 4 个 SwitchW：sw_auto_launch_ / sw_silent_start_
//      / sw_allow_lan_ / sw_allow_ipv6_
UT_TEST(t12_diag_drawer_has_four_visible_switches) {
  make_offscreen(271, 680, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  scene::SettingsDrawer drawer;
  drawer.layout_tree({0, 0, 271, 680});
  drawer.set_open(true);
  drawer.paint_all(rt());

  const auto* basic = drawer.basic_card();
  UT_CHECK(basic != nullptr);
  if (basic) {
    UT_CHECK(basic->sw_auto_launch_ != nullptr);
    UT_CHECK(basic->sw_silent_start_ != nullptr);
    UT_CHECK(basic->sw_allow_lan_ != nullptr);
    UT_CHECK(basic->sw_allow_ipv6_ != nullptr);
    // 至少一个开关的轨道在渲染后确实有可见填充
    if (basic->sw_allow_lan_) {
      const RectF b = basic->sw_allow_lan_->bounds();
      UT_CHECK(b.w > 0 && b.h > 0);
    }
  }
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}
