// T12 测试：皮肤双滑块（val1 / val2）的**逐皮肤语义**。
//
// 起因（用户反馈 2026-11-07）：
//   「每套皮肤的那两个滑块调整数值是用来调整每套皮肤的特色参数等各对应不同的参数。
//     你要找到 mini 上的这一套对照理解它的含义，然后移植到 win 上。」
//   此前 Win 侧只把两个滑块当作全局的 depth/vibrancy，导致：
//     ① original / modern-flat / monochrome 的 val1（Radius/Roundness/Radius）
//        被当成「深度」用 → 圆角算成 8×0.3=2.4px 而非 8px；
//     ② 切皮肤后因子不重置 → 沿用 retro-3d 的 0.3 缺省；
//     ③ frosted / cyberpunk / monochrome 的 val1、val2 语义完全没落地。
//
// Mini 语义依据（权威表见 docs/evidence/2026-11-07-skin-slider-semantics.md）：
//   use-skin-controls.ts:70-93  四个 CSS 变量两两同值
//     --depth-factor ≡ --control-skin-val1 ≡ val1
//     --vibrancy-factor ≡ --control-skin-val2 ≡ val2
//   layout.scss:329 / :443       original 与 modern-flat 强制 --depth-factor: 0
//   use-skin-controls.ts:13-24   因子缺省逐皮肤不同（retro-3d 0.3，其余 1.0）
#include <windows.h>

#include <cmath>

#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/theme/theme.h"

using namespace clashwin::ui;
using namespace clashwin::ui::theme;
using namespace ui_test;

namespace {

// 记录当前皮肤下「因子三元组」的语义化取值
struct Factors {
  float val1, depth, vibrancy;
};

Factors probe() {
  auto& svc = ThemeService::instance();
  return Factors{svc.val1(), svc.depth(), svc.vibrancy()};
}

}  // namespace

// 1) 缺省因子逐皮肤不同（← use-skin-controls.ts:13-24 readFactor）
//    retro-3d 缺省 0.3/1.0；其余 5 套缺省 1.0/1.0。
UT_TEST(t12_skin_default_factors_per_skin) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  UT_CHECK(svc.val1() > 0.29f && svc.val1() < 0.31f);   // 0.3
  UT_CHECK(svc.vibrancy() > 0.99f && svc.vibrancy() < 1.01f);

  const SkinId others[] = {SkinId::Original, SkinId::ModernFlat,
                           SkinId::FrostedGlass, SkinId::Cyberpunk,
                           SkinId::Monochrome};
  for (SkinId sk : others) {
    svc.init(sk, ThemeMode::Light);
    UT_CHECK(svc.val1() > 0.99f && svc.val1() < 1.01f);   // ← 非 retro 是 1.0
    UT_CHECK(svc.vibrancy() > 0.99f && svc.vibrancy() < 1.01f);
  }
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 2) --depth-factor 的 !important 覆盖（← layout.scss:329 / :443）
//    original 与 modern-flat 的深度语义值恒为 0；
//    但 val1（几何语义）仍是滑块原值。
UT_TEST(t12_depth_factor_zeroed_for_original_modernflat) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Original, ThemeMode::Light);
  svc.set_factors(2.5f, 1.5f);
  UT_CHECK(svc.val1() > 2.49f && svc.val1() < 2.51f);   // 几何：仍为 2.5
  UT_CHECK(svc.depth() < 0.001f);                       // 深度：被强制 0
  UT_CHECK(svc.vibrancy() > 1.49f && svc.vibrancy() < 1.51f);

  svc.init(SkinId::ModernFlat, ThemeMode::Light);
  svc.set_factors(2.0f, 3.0f);
  UT_CHECK(svc.val1() > 1.99f && svc.val1() < 2.01f);
  UT_CHECK(svc.depth() < 0.001f);
  UT_CHECK(svc.vibrancy() > 2.99f && svc.vibrancy() < 3.01f);

  // 其余 4 套皮肤不覆盖，深度语义 = val1
  const SkinId others[] = {SkinId::Retro3d, SkinId::FrostedGlass,
                           SkinId::Cyberpunk, SkinId::Monochrome};
  for (SkinId sk : others) {
    svc.init(sk, ThemeMode::Light);
    svc.set_factors(1.75f, 1.0f);
    UT_CHECK(svc.depth() > 1.74f && svc.depth() < 1.76f);
  }
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 3) retro-3d：val1 = Depth（阴影/浮雕深度），val2 = Vibrancy（金辉光）
//    → 两者必须各自驱动不同参数。
UT_TEST(t12_retro_val1_is_depth_val2_is_vibrancy) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Dark);
  svc.set_factors(1.0f, 1.0f);
  auto base = svc.panel_paint();
  float base_shadow_blur = base.shadow_blur;
  float base_glow_blur = base.glow_blur;

  // val1 ↑ → 阴影模糊↑（深度），金辉光模糊不变
  svc.set_factors(0.6f, 1.0f);
  auto d_low = svc.panel_paint();
  UT_CHECK(d_low.shadow_blur < base_shadow_blur);
  UT_CHECK(d_low.glow_blur == base_glow_blur);

  // val2 ↑ → 金辉光模糊↑，阴影模糊不变
  svc.set_factors(1.0f, 3.0f);
  auto v_high = svc.panel_paint();
  UT_CHECK(v_high.glow_blur > base_glow_blur);
  UT_CHECK(v_high.shadow_blur == base_shadow_blur);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 4) original：val1 = Radius（圆角），val2 = Accent（主色色相）
//    → val1 必须改圆角，且**不能**改阴影深度（本皮肤无深度语义）。
UT_TEST(t12_original_val1_is_radius_val2_is_accent) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Original, ThemeMode::Light);
  svc.set_factors(1.0f, 1.0f);
  float r1 = svc.panel_paint().radius;
  Color4f c1 = svc.primary();

  svc.set_factors(3.0f, 1.0f);
  auto p2 = svc.panel_paint();
  UT_CHECK(p2.radius > r1);            // ← 圆角随 val1 增大
  UT_CHECK(p2.shadow_blur == 3.0f);    // ← 阴影深度固定（--depth-factor: 0）

  // val2 → 主色相变化（hsl 239*val2，26%，49%）
  //   hsl(239) ≈ #5C5E9D（蓝紫）→ hsl(478→118) ≈ #5F9D5C（绿），色相差显著。
  //   ★ Color4f 是 0–1 归一化，比较时换算回 0–255 空间。
  svc.set_factors(1.0f, 2.0f);
  Color4f c2 = svc.primary();
  const float diff255 =
      (std::abs(c2.r - c1.r) + std::abs(c2.g - c1.g) + std::abs(c2.b - c1.b)) *
      255.0f;
  UT_CHECK(diff255 > 20.0f);  // 主色必须随 Accent 滑块改变
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 5) modern-flat：val1 = Roundness（圆角），val2 = Shadow（阴影强度）
UT_TEST(t12_modernflat_val1_is_roundness_val2_is_shadow) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::ModernFlat, ThemeMode::Light);
  svc.set_factors(1.0f, 1.0f);
  float r1 = svc.panel_paint().radius;
  float s1 = svc.panel_paint().shadow_blur;

  svc.set_factors(3.0f, 1.0f);
  auto a = svc.panel_paint();
  UT_CHECK(a.radius > r1);            // ← val1 → 圆角
  UT_CHECK(a.shadow_blur == s1);      // ← val1 不改阴影

  svc.set_factors(1.0f, 4.0f);
  auto b = svc.panel_paint();
  UT_CHECK(b.shadow_blur > s1);       // ← val2 → 阴影模糊
  UT_CHECK(b.radius == r1);           // ← val2 不改圆角
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 6) frosted-glass：val1 = Opacity（玻璃不透明度）→ 必须改面板底色 alpha；
//    val2 = Blur（D2D 无 backdrop-filter 原语，差异登记，不做假映射）。
UT_TEST(t12_frosted_val1_is_opacity) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::FrostedGlass, ThemeMode::Light);
  svc.set_factors(1.0f, 1.0f);
  float a1 = svc.panel_paint().bg.a;

  svc.set_factors(4.0f, 1.0f);
  float a2 = svc.panel_paint().bg.a;
  UT_CHECK(a2 > a1);   // ← 0.02 + 0.1*val1：val1↑ → alpha↑
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 7) cyberpunk：val1 = Glow（霓虹辉光半径），val2 = Speed（脉冲周期）
UT_TEST(t12_cyberpunk_val1_is_glow_val2_is_speed) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Cyberpunk, ThemeMode::Dark);
  svc.set_factors(1.0f, 1.0f);
  float g1 = svc.card_paint("default").shadow_blur;
  float period1 = svc.panel_paint().pulse_period_s;

  svc.set_factors(4.0f, 1.0f);
  UT_CHECK(svc.card_paint("default").shadow_blur > g1);   // ← val1 → 辉光半径
  UT_CHECK(svc.panel_paint().pulse_period_s == period1);   // ← val1 不改速度

  svc.set_factors(1.0f, 5.0f);
  UT_CHECK(svc.panel_paint().pulse_period_s < period1);   // ← val2 → 周期变快
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 8) monochrome：val1 = Radius（圆角），val2 = Border（边框宽度）
UT_TEST(t12_monochrome_val1_is_radius_val2_is_border) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Monochrome, ThemeMode::Light);
  svc.set_factors(1.0f, 1.0f);
  float r1 = svc.panel_paint().radius;
  float bw1 = svc.button_paint("default").border_w;

  svc.set_factors(3.0f, 1.0f);
  UT_CHECK(svc.panel_paint().radius > r1);   // ← val1 → 圆角
  UT_CHECK(svc.button_paint("default").border_w == bw1);

  svc.set_factors(1.0f, 4.0f);
  UT_CHECK(svc.button_paint("default").border_w > bw1);  // ← val2 → 边框宽
  UT_CHECK(svc.panel_paint().radius == r1);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 9) 逐皮肤滑块规格（量程 + 标签）—— ← theme-settings-card.tsx:59-134
UT_TEST(t12_skin_slider_spec_per_skin) {
  struct Row {
    SkinId skin;
    float max1, max2;
    const wchar_t* l1;
    const wchar_t* l2;
  };
  const Row rows[] = {
      {SkinId::Retro3d, 1.0f, 5.0f, L"Depth", L"Vibrancy"},
      {SkinId::Original, 3.0f, 3.0f, L"Radius", L"Accent"},
      {SkinId::ModernFlat, 3.0f, 5.0f, L"Roundness", L"Shadow"},
      {SkinId::FrostedGlass, 5.0f, 5.0f, L"Opacity", L"Blur"},
      {SkinId::Cyberpunk, 5.0f, 5.0f, L"Glow", L"Speed"},
      {SkinId::Monochrome, 3.0f, 5.0f, L"Radius", L"Border"},
  };
  for (const Row& r : rows) {
    auto s = skin_slider_spec(r.skin);
    UT_CHECK(s.max1 == r.max1);
    UT_CHECK(s.max2 == r.max2);
    UT_CHECK(std::wstring(s.label1) == r.l1);
    UT_CHECK(std::wstring(s.label2) == r.l2);
  }
}

// 10) 滑块拖动必须产生**实际绘制差异**（不是只改数值）
//     ← 回归「滑块只是能滑动，但是完全不产生调整效果」。
UT_TEST(t12_slider_drag_changes_rendered_output) {
  make_offscreen(271, 680, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Dark);
  svc.set_factors(0.3f, 1.0f);
  auto before = svc.panel_paint();
  float blur_before = before.shadow_blur;

  svc.set_factors(1.0f, 1.0f);
  auto after = svc.panel_paint();
  UT_CHECK(after.shadow_blur > blur_before);
  UT_CHECK(std::abs(after.shadow_blur - blur_before) > 1.0f);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}
