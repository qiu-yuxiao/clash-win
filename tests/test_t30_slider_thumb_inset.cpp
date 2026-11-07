// T12 测试：Slider thumb 的两项**内凹/内高光**特征（← Mini CSS box-shadow inset）。
//
// 起因（反向审计 Mini 源码消费点时发现）：
//   Mini `src/pages/_layout/utils/style-helpers.tsx:274-310` 的 retro-3d thumb 声明：
//     boxShadow: `0 3d 6d rgba(0,0,0,.25),
//                 inset 0 1d 0 rgba(255,255,255,.8),
//                 0 0 8v rgba(255,193,7,.25v)`
//     '&.Mui-active': `inset 1d 1d 2d rgba(0,0,0,.15)`   （暗色 1.5d/1.5d/2.5d/.5）
//   其中 **inset 顶高光** 与 **:active 内凹** 两项在 Win 侧此前完全缺失：
//     SwitchPaint 有 off/on_thumb_inset_hi（开关有），SliderPaint 没有（滑块没有）。
//   缺了顶高光 → retro-3d 金色滑块旋钮显得扁平（无实体感）；
//   缺了 active 内凹 → 按下旋钮时没有「被压下去」的反馈。
//
// 事实源：ClashVerge/src/pages/_layout/utils/style-helpers.tsx:284-307
#include <windows.h>

#include <cmath>

#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/theme/theme.h"

using namespace clashwin::ui;
using namespace clashwin::ui::theme;
using namespace ui_test;

namespace {

constexpr float kEps = 0.001f;

bool almost_eq(float a, float b) { return std::abs(a - b) < kEps; }

}  // namespace

// 1) retro-3d thumb 顶内高光必须置位，亮色 alpha 0.80 / 暗色 0.40
//    ← style-helpers.tsx:285（亮）/ :288（暗）
UT_TEST(t30_thumb_inset_hi_enabled_on_retro3d) {
  auto& svc = ThemeService::instance();

  svc.init(SkinId::Retro3d, ThemeMode::Light);
  svc.set_factors(1.0f, 1.0f);
  auto light = svc.slider_paint();
  UT_CHECK(light.thumb_inset_hi);
  UT_CHECK(almost_eq(light.thumb_inset_hi_color.a, 0.80f));
  // 颜色是白（r=g=b=1）
  UT_CHECK(almost_eq(light.thumb_inset_hi_color.r, 1.0f));
  UT_CHECK(almost_eq(light.thumb_inset_hi_color.g, 1.0f));
  UT_CHECK(almost_eq(light.thumb_inset_hi_color.b, 1.0f));

  svc.init(SkinId::Retro3d, ThemeMode::Dark);
  svc.set_factors(1.0f, 1.0f);
  auto dark = svc.slider_paint();
  UT_CHECK(dark.thumb_inset_hi);
  UT_CHECK(almost_eq(dark.thumb_inset_hi_color.a, 0.40f));
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 2) 顶高光的 dy 必须随深度因子 d 变化：inset 0 calc(1px * d) 0
//    ← style-helpers.tsx:285/288 的 `calc(1px * var(--depth-factor, 1.0))`
UT_TEST(t30_thumb_inset_hi_dy_scales_with_depth) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Light);

  svc.set_factors(0.5f, 1.0f);
  const float dy_low = svc.slider_paint().thumb_inset_hi_dy;
  svc.set_factors(1.0f, 1.0f);
  const float dy_high = svc.slider_paint().thumb_inset_hi_dy;

  UT_CHECK(almost_eq(dy_low, 0.5f));
  UT_CHECK(almost_eq(dy_high, 1.0f));
  UT_CHECK(dy_high > dy_low);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 3) :active 内凹必须置位，且亮/暗两套数值不同
//    ← style-helpers.tsx:306-307
//      light: inset calc(1px*d)  calc(1px*d)  calc(2px*d)  rgba(0,0,0,0.15)
//      dark : inset calc(1.5px*d) calc(1.5px*d) calc(2.5px*d) rgba(0,0,0,0.5)
UT_TEST(t30_thumb_active_inset_enabled_on_retro3d) {
  auto& svc = ThemeService::instance();

  svc.init(SkinId::Retro3d, ThemeMode::Light);
  svc.set_factors(1.0f, 1.0f);
  auto light = svc.slider_paint();
  UT_CHECK(light.thumb_active_inset);
  UT_CHECK(almost_eq(light.thumb_active_inset_color.a, 0.15f));
  UT_CHECK(almost_eq(light.thumb_active_inset_dx, 1.0f));
  UT_CHECK(almost_eq(light.thumb_active_inset_dy, 1.0f));
  UT_CHECK(almost_eq(light.thumb_active_inset_blur, 2.0f));

  svc.init(SkinId::Retro3d, ThemeMode::Dark);
  svc.set_factors(1.0f, 1.0f);
  auto dark = svc.slider_paint();
  UT_CHECK(dark.thumb_active_inset);
  UT_CHECK(almost_eq(dark.thumb_active_inset_color.a, 0.50f));
  UT_CHECK(almost_eq(dark.thumb_active_inset_dx, 1.5f));
  UT_CHECK(almost_eq(dark.thumb_active_inset_dy, 1.5f));
  UT_CHECK(almost_eq(dark.thumb_active_inset_blur, 2.5f));
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 4) :active 内凹的三元组必须同步随 d 缩放
UT_TEST(t30_thumb_active_inset_scales_with_depth) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Light);

  svc.set_factors(0.5f, 1.0f);
  auto low = svc.slider_paint();
  svc.set_factors(2.0f, 1.0f);
  auto high = svc.slider_paint();

  UT_CHECK(almost_eq(low.thumb_active_inset_dx, 0.5f));
  UT_CHECK(almost_eq(low.thumb_active_inset_blur, 1.0f));
  UT_CHECK(almost_eq(high.thumb_active_inset_dx, 2.0f));
  UT_CHECK(almost_eq(high.thumb_active_inset_blur, 4.0f));
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 5) 其余 5 套皮肤**没有**这两项 inset 特征
//    ← Mini 只有 retro-3d 的 thumb 声明了 inset 高光 / :active 内凹；
//      original/modern-flat/frosted-glass/cyberpunk/monochrome 的 thumb boxShadow
//      只有外投影（MUI 缺省），无 inset 顶高光，也无 :active 覆盖。
UT_TEST(t30_thumb_inset_features_absent_on_other_skins) {
  auto& svc = ThemeService::instance();
  const SkinId others[] = {SkinId::Original, SkinId::ModernFlat,
                           SkinId::FrostedGlass, SkinId::Cyberpunk,
                           SkinId::Monochrome};
  for (SkinId sk : others) {
    svc.init(sk, ThemeMode::Light);
    svc.set_factors(1.0f, 1.0f);
    auto p = svc.slider_paint();
    UT_CHECK(!p.thumb_inset_hi);
    UT_CHECK(!p.thumb_active_inset);
  }
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 6) SliderPaint 与 SwitchPaint 的 inset 字段是**两套独立字段**
//    （回归：防止把开关的 inset 实现错挂到滑块上，或反之）
UT_TEST(t30_slider_and_switch_inset_fields_are_independent) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  svc.set_factors(1.0f, 1.0f);
  auto sp = svc.slider_paint();
  auto sw = svc.switch_paint();
  // 滑块侧字段有值
  UT_CHECK(sp.thumb_inset_hi);
  UT_CHECK(sp.thumb_active_inset);
  // 开关侧字段是另一套命名（off_thumb_inset_hi / on_thumb_inset_hi）
  UT_CHECK(sw.off_thumb_inset_hi || sw.on_thumb_inset_hi);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}
