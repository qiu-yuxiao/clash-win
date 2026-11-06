// T12 测试：皮肤参数表核心（色值出处逐项断言，值级）。
// 出处行号对应 ClashVerge 只读参考（见 palettes.h/theme.h 头注）。
#include "test_util.h"

#include "ui/theme/palettes.h"
#include "ui/theme/theme.h"

using namespace clashwin::ui::theme;
using clashwin::ui::Color4f;
namespace tw = clashwin::ui::theme;

// 色值近似断言（alpha 混合/浮点容差）
static void expect_rgb(const Color4f& c, uint32_t rgb, const char* what,
                       float eps = 2.0f / 255.0f) {
  auto ch = [&](float v, unsigned shift) {
    float want = static_cast<float>((rgb >> shift) & 0xFF) / 255.0f;
    return v >= want - eps && v <= want + eps;
  };
  UT_CHECK_MSG(ch(c.r, 16) && ch(c.g, 8) && ch(c.b, 0), what);
}

UT_TEST(t12_palette_light_values) {
  // ← _theme.tsx defaultTheme
  const auto& p = tw::base_palette(ThemeMode::Light);
  // primary 取 Mini 字面量回退值：_theme.tsx:3 primary_color '#007AFF'，
  // 运行期 AccentColor 解析失败时回退此值（use-custom-theme.ts:172-175）；与 palettes.cpp 同步。
  expect_rgb(p.primary_color, 0x007AFF, "light primary #007AFF");
  expect_rgb(p.secondary_color, 0xFC9B76, "light secondary #FC9B76");
  expect_rgb(p.primary_text, 0x000000, "light primary_text");
  expect_rgb(p.secondary_text, 0x3C3C43, "light secondary_text #3C3C4399");
  UT_CHECK(p.secondary_text.a > 0.55f && p.secondary_text.a < 0.65f);  // 99/255
  expect_rgb(p.error_color, 0xFF3B30, "light error");
  expect_rgb(p.warning_color, 0xFF9500, "light warning");
  expect_rgb(p.success_color, 0x06943D, "light success");
  expect_rgb(p.background_color, 0xF5F5F5, "light bg #F5F5F5");
}

UT_TEST(t12_palette_dark_values) {
  // ← _theme.tsx defaultDarkTheme
  const auto& p = tw::base_palette(ThemeMode::Dark);
  expect_rgb(p.primary_color, 0x0A84FF, "dark primary #0A84FF");  // src: _theme.tsx:18
  expect_rgb(p.secondary_color, 0xFF9F0A, "dark secondary #FF9F0A");
  expect_rgb(p.primary_text, 0xFFFFFF, "dark primary_text");
  expect_rgb(p.error_color, 0xFF453A, "dark error #FF453A");
  expect_rgb(p.success_color, 0x30D158, "dark success #30D158");
  expect_rgb(p.background_color, 0x2E303D, "dark bg #2E303D");
}

UT_TEST(t12_css_vars_original_vs_others) {
  // ← use-custom-theme.ts:367-495
  auto o = tw::css_vars(ThemeMode::Light, SkinId::Original);
  expect_rgb(o.background_color, 0xECECEC, "--background-color light #ECECEC");
  expect_rgb(o.selection_color, 0xf5f5f5, "--selection-color light");
  expect_rgb(o.divider_color, 0x000000, "divider rgb");
  UT_CHECK(o.divider_color.a > 0.04f && o.divider_color.a < 0.08f);
  expect_rgb(o.window_border_color, 0xcccccc, "--window-border-color light");
  expect_rgb(o.scrollbar_bg, 0xf1f1f1, "--scrollbar-bg light");
  expect_rgb(o.scrollbar_thumb, 0xc1c1c1, "--scrollbar-thumb light");
  expect_rgb(o.theme_bg, 0xf5f5f5, "original --theme-bg light");
  expect_rgb(o.theme_panel_bg, 0xffffff, "original --theme-panel-bg light");
  expect_rgb(o.theme_border, 0xe2e8f0, "original --theme-border light");
  expect_rgb(o.theme_input_border, 0xcbd5e1, "original --theme-input-border light");

  auto d = tw::css_vars(ThemeMode::Dark, SkinId::Original);
  expect_rgb(d.background_color, 0x2E303D, "--background-color dark #2E303D");
  expect_rgb(d.selection_color, 0x3E3E3E, "--selection-color dark");
  expect_rgb(d.theme_bg, 0x121212, "original --theme-bg dark");
  expect_rgb(d.theme_input_bg, 0x1a1a1a, "original --theme-input-bg dark");
  expect_rgb(d.theme_input_border, 0x555555, "original --theme-input-border dark");

  // 非 original 分支（其余 5 皮肤共用，← :436-495）
  auto mo = tw::css_vars(ThemeMode::Light, SkinId::ModernFlat);
  expect_rgb(mo.theme_bg, 0xf0f5ff, "non-original --theme-bg light #f0f5ff");
  expect_rgb(mo.theme_panel_bg, 0xffffff, "non-original --theme-panel-bg light");
  expect_rgb(mo.theme_border, 0xd0d7de, "non-original --theme-border light");
  auto md = tw::css_vars(ThemeMode::Dark, SkinId::Cyberpunk);
  expect_rgb(md.theme_bg, 0x0f1423, "non-original --theme-bg dark #0f1423");
  expect_rgb(md.theme_panel_bg, 0x1e2438, "non-original --theme-panel-bg dark");
  expect_rgb(md.theme_input_bg, 0x121824, "non-original --theme-input-bg dark");
}

UT_TEST(t12_original_forced_primary_hsl) {
  // ← use-custom-theme.ts:180 hsl(239,26%,49%) = rgb(91,92,157)
  //   （恰为 --primary-color-rgb 默认 fallback "91, 92, 157"）
  auto c = tw::original_primary(1.0f);
  UT_CHECK(c.r > 90.0f / 255 - 0.01 && c.r < 92.0f / 255 + 0.01);
  UT_CHECK(c.g > 91.0f / 255 - 0.01 && c.g < 93.0f / 255 + 0.01);
  UT_CHECK(c.b > 156.0f / 255 - 0.01 && c.b < 158.0f / 255 + 0.01);
  // val2 缩放色调：val2=0 → hsl(0,26%,49%) ≠ hsl(239,...)
  auto c0 = tw::original_primary(0.0f);
  UT_CHECK(!(c0.r == c.r && c0.g == c.g && c0.b == c.b));
}

UT_TEST(t12_skin_names_roundtrip) {
  UT_CHECK_EQ(std::string(tw::name_of(SkinId::Retro3d)), "retro-3d");
  UT_CHECK_EQ(std::string(tw::name_of(SkinId::Original)), "original");
  UT_CHECK_EQ(std::string(tw::name_of(SkinId::ModernFlat)), "modern-flat");
  UT_CHECK_EQ(std::string(tw::name_of(SkinId::FrostedGlass)), "frosted-glass");
  UT_CHECK_EQ(std::string(tw::name_of(SkinId::Cyberpunk)), "cyberpunk");
  UT_CHECK_EQ(std::string(tw::name_of(SkinId::Monochrome)), "monochrome");
  UT_CHECK(tw::skin_from_name("retro-3d") == SkinId::Retro3d);
  UT_CHECK(tw::skin_from_name("monochrome") == SkinId::Monochrome);
  UT_CHECK(!tw::skin_from_name("nope").has_value());  // ← getActiveSkin fallback 语义
}

UT_TEST(t12_button_paint_table_all_skins) {
  // ← get3DButtonStyle colorType=default（contained）逐皮肤关键色
  ThemeService::instance().init(SkinId::Original, ThemeMode::Light);
  auto o = ThemeService::instance().button_paint("default");
  expect_rgb(o.fill, 0xffffff, "original default bg #ffffff");
  expect_rgb(o.fill_hover, 0xf5f5f5, "original default hover #f5f5f5");
  UT_CHECK_EQ(o.radius, 4.0f);
  auto op = ThemeService::instance().button_paint("primary");
  expect_rgb(op.fill, 0x5B5C9D, "original primary = forced hsl");
  expect_rgb(op.text, 0xFFFFFF, "original primary text white");

  ThemeService::instance().init(SkinId::ModernFlat, ThemeMode::Light);
  auto m = ThemeService::instance().button_paint("default");
  expect_rgb(m.fill, 0xf3f4f6, "modern-flat default bg #f3f4f6");
  expect_rgb(m.fill_hover, 0xe5e7eb, "modern-flat hover #e5e7eb");
  expect_rgb(m.border, 0xd1d5db, "modern-flat border #d1d5db");
  UT_CHECK_EQ(m.radius, 6.0f);
  auto mp = ThemeService::instance().button_paint("primary");
  expect_rgb(mp.fill, 0x007AFF, "modern-flat primary = palette.primary light #007AFF");

  ThemeService::instance().init(SkinId::FrostedGlass, ThemeMode::Light);
  auto f = ThemeService::instance().button_paint("default");
  expect_rgb(f.fill, 0x000000, "frosted default bg rgb");
  UT_CHECK(f.fill.a > 0.04f && f.fill.a < 0.08f);  // rgba(0,0,0,0.06)
  UT_CHECK_EQ(f.radius, 8.0f);

  ThemeService::instance().init(SkinId::Cyberpunk, ThemeMode::Light);
  auto c = ThemeService::instance().button_paint("default");
  expect_rgb(c.fill, 0xffffff, "cyberpunk light bg #ffffff");
  expect_rgb(c.border, 0x4caf50, "cyberpunk light border #4caf50");
  UT_CHECK_EQ(c.radius, 0.0f);
  ThemeService::instance().init(SkinId::Cyberpunk, ThemeMode::Dark);
  auto cd = ThemeService::instance().button_paint("default");
  expect_rgb(cd.fill, 0x0d1117, "cyberpunk dark bg #0d1117");
  expect_rgb(cd.text, 0x39ff14, "cyberpunk dark text neon #39ff14");

  ThemeService::instance().init(SkinId::Monochrome, ThemeMode::Light);
  auto mo = ThemeService::instance().button_paint("default");
  expect_rgb(mo.fill, 0xffffff, "monochrome light bg #ffffff");
  expect_rgb(mo.text, 0x000000, "monochrome light text #000000");
  auto mop = ThemeService::instance().button_paint("primary");
  expect_rgb(mop.fill, 0x000000, "monochrome primary bg #000000");
  expect_rgb(mop.text, 0xffffff, "monochrome primary text #ffffff");
  UT_CHECK_EQ(mo.radius, 0.0f);

  ThemeService::instance().init(SkinId::Retro3d, ThemeMode::Light);
  auto r = ThemeService::instance().button_paint("default");
  expect_rgb(r.fill, 0xFFF59D, "retro-3d default fill（渐变 70% 站近似）");
  expect_rgb(r.border, 0xD4AF37, "retro-3d border #D4AF37");
  UT_CHECK_EQ(r.radius, 6.0f);
  UT_CHECK_EQ(r.weight, 700);
}

UT_TEST(t12_segmented_paint_table) {
  // ← get3DSegmented* 三函数
  ThemeService::instance().init(SkinId::Original, ThemeMode::Light);
  auto so = ThemeService::instance().segmented_paint();
  expect_rgb(so.container_bg, 0xf1f5f9, "original segmented container #f1f5f9");
  expect_rgb(so.container_border, 0xcbd5e1, "original container border");
  expect_rgb(so.active_bg, 0x5B5C9D, "original active = forced primary");
  expect_rgb(so.active_text, 0xFFFFFF, "original active text #fff");

  ThemeService::instance().init(SkinId::Retro3d, ThemeMode::Light);
  auto sr = ThemeService::instance().segmented_paint();
  expect_rgb(sr.active_text, 0x2C1F03, "retro-3d active text #2C1F03");
  expect_rgb(sr.active_bg, 0xFFC107, "retro-3d active bg（渐变站）");

  ThemeService::instance().init(SkinId::Cyberpunk, ThemeMode::Dark);
  auto sc = ThemeService::instance().segmented_paint();
  expect_rgb(sc.container_border, 0x39ff14, "cyberpunk dark container border");
  expect_rgb(sc.active_bg, 0x39ff14, "cyberpunk dark active");
  expect_rgb(sc.active_text, 0x000000, "cyberpunk dark active text #000");

  ThemeService::instance().init(SkinId::Monochrome, ThemeMode::Light);
  auto sm = ThemeService::instance().segmented_paint();
  expect_rgb(sm.active_bg, 0x000000, "monochrome light active #000");
  expect_rgb(sm.active_text, 0xffffff, "monochrome light active text #fff");
}
