// T12 测试：SwitchW / Slider 的**交互态**（hover / 键盘）1:1 移植。
//
// 起因（反向审计 Mini 源码消费点时发现的第 5、6 个缺口）：
//   Mini 的每个皮肤都定义了 hover 态，Win 侧此前**完全没有**：
//     base-switch.tsx:114  retro-3d   '&:hover .MuiSwitch-thumb'
//                              transform scale(1.2) + filter brightness(1.15)
//                              + boxShadow 0 4d|5d 8d|10d rgba(0,0,0,.3|.6)
//                                  + inset 0 1d|1.2d 0 rgba(255,255,255,.8|.6)
//                                  + inset 0 -1d|-1.5d 0 rgba(0,0,0,.1|.4)
//                                  + 0 0 10v rgba(255,193,7,.35v)
//     base-switch.tsx:271  original    无 hover 规则（保持常态）
//     base-switch.tsx:361  modern-flat scale(1.1)
//     base-switch.tsx:442  frosted     无 hover 规则（保持常态）
//     base-switch.tsx:442  cyberpunk   boxShadow '0 0 10px currentColor'
//     base-switch.tsx:543  monochrome  scale(1.05)
//
//     style-helpers.tsx:292 retro-3d   slider thumb
//                              scale(1.2) + brightness(1.15)
//                              + 0 5d|6d 10d|12d rgba(0,0,0,.35|.7)
//                              + inset 0 1d 0 rgba(255,255,255,.9|.5)
//                              + 0 0 12v|14v rgba(255,193,7,.35v|.4v)
//     style-helpers.tsx:334 original    scale(1.15)
//     style-helpers.tsx:363 modern-flat scale(1.15)
//     style-helpers.tsx:395 frosted     scale(1.15) + bgcolor rgba(255,255,255,1.0)
//     style-helpers.tsx:427 cyberpunk   scale(1.15) + boxShadow 0 0 8px <主色>
//     style-helpers.tsx:450 monochrome  无 hover 规则
//
//   另有键盘交互缺口：Mini 的 MUI Slider 可聚焦并用方向键调节
//     （useSlider.js:326-370：方向键 ±step、Shift ±10、PageUp/Down ±10、
//       Home→min、End→max），Win 侧 Slider 此前无 on_key/wants_key。
#include <windows.h>

#include <cmath>

#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/slider.h"
#include "ui/switch_w.h"
#include "ui/theme/theme.h"

using namespace clashwin::ui;
using namespace clashwin::ui::theme;
using namespace ui_test;

namespace {

constexpr float kEps = 0.001f;

bool almost_eq(float a, float b) { return std::abs(a - b) < kEps; }

// 触发 SwitchW 的悬停（走真实 route：Enter 事件）
void hover_switch(SwitchW* sw, bool on) {
  MouseEvent e{on ? MouseEventType::Enter : MouseEventType::Leave, {0, 0}};
  sw->on_mouse(e);
}

// 触发 Slider 的鼠标移动（记录 hover 位置）
void move_slider(Slider* s, float x) {
  MouseEvent e{MouseEventType::Move, {x, 12}, 0};
  s->on_mouse(e);
}

}  // namespace

// 1) 开关 hover 缩放逐皮肤匹配 Mini
//    retro-3d 1.2 / modern-flat 1.1 / monochrome 1.05
//    original 与 frosted-glass 无 hover 规则 → thumb_hover == false
UT_TEST(t31_switch_hover_scale_per_skin) {
  auto& svc = ThemeService::instance();

  svc.init(SkinId::Retro3d, ThemeMode::Light);
  auto p = svc.switch_paint();
  UT_CHECK(p.thumb_hover);
  UT_CHECK(almost_eq(p.thumb_hover_scale, 1.20f));
  UT_CHECK(almost_eq(p.thumb_hover_brightness, 1.15f));
  UT_CHECK(p.on_thumb_hover_glow_color.a > 0.1f);
  UT_CHECK(almost_eq(p.on_thumb_hover_glow_color.r, 1.0f));

  svc.init(SkinId::ModernFlat, ThemeMode::Light);
  p = svc.switch_paint();
  UT_CHECK(p.thumb_hover);
  UT_CHECK(almost_eq(p.thumb_hover_scale, 1.10f));

  svc.init(SkinId::Monochrome, ThemeMode::Light);
  p = svc.switch_paint();
  UT_CHECK(p.thumb_hover);
  UT_CHECK(almost_eq(p.thumb_hover_scale, 1.05f));

  // original / frosted-glass 无 hover 规则
  svc.init(SkinId::Original, ThemeMode::Light);
  UT_CHECK(!svc.switch_paint().thumb_hover);
  svc.init(SkinId::FrostedGlass, ThemeMode::Light);
  UT_CHECK(!svc.switch_paint().thumb_hover);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 2) cyberpunk 开关 hover：辉光 4px → 10px，off/on 态颜色不同
//    ← base-switch.tsx:442-444（currentColor = #ff0055 / #00b0ff|#00f0ff）
UT_TEST(t31_switch_cyberpunk_hover_glow) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Cyberpunk, ThemeMode::Dark);
  auto p = svc.switch_paint();
  UT_CHECK(p.thumb_hover);
  UT_CHECK(p.thumb_hover_glow);
  UT_CHECK(almost_eq(p.thumb_hover_glow_blur, 10.0f));
  // off 态 currentColor = #ff0055
  UT_CHECK(almost_eq(p.thumb_hover_glow_color.r, 1.0f));
  UT_CHECK(almost_eq(p.thumb_hover_glow_color.g, 0.0f));
  // on 态 currentColor = #00f0ff（暗色）→ g=0xF0/255=0.941，b=1.0
  UT_CHECK(almost_eq(p.on_thumb_hover_glow_color.r, 0.0f));
  UT_CHECK(almost_eq(p.on_thumb_hover_glow_color.g, 0xF0 / 255.0f));
  UT_CHECK(almost_eq(p.on_thumb_hover_glow_color.b, 1.0f));
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 3) 开关 hover 事件真的改变状态（Enter/Leave）
UT_TEST(t31_switch_hover_event_tracks_state) {
  SwitchW sw;
  sw.layout_tree({0, 0, 36, 20});
  UT_CHECK(!sw.thumb_hovered());
  hover_switch(&sw, true);
  UT_CHECK(sw.thumb_hovered());
  hover_switch(&sw, false);
  UT_CHECK(!sw.thumb_hovered());
  clear_input_state();
}

// 4) retro-3d 开关 on 态旋钮也带内浮雕
//    ← base-switch.tsx:97-98：`.Mui-checked .MuiSwitch-thumb` 只覆盖
//      background 与 border，boxShadow 继承基础 thumb 的 inset 高光 + 底暗线。
UT_TEST(t31_switch_retro_on_thumb_has_inset_bevel) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Dark);
  auto p = svc.switch_paint();
  UT_CHECK(p.on_thumb_inset_hi);
  UT_CHECK(p.on_thumb_inset_dark);
  // 数值与 off 态一致（同一套 boxShadow 继承）
  UT_CHECK(almost_eq(p.on_thumb_inset_hi_color.a,
                     p.off_thumb_inset_hi_color.a));
  UT_CHECK(almost_eq(p.on_thumb_inset_dark_color.a,
                     p.off_thumb_inset_dark_color.a));
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 5) Slider thumb hover 缩放逐皮肤匹配 Mini
//    retro-3d 1.2 / original·modern-flat·frosted·cyberpunk 1.15 / monochrome 无
UT_TEST(t31_slider_hover_scale_per_skin) {
  auto& svc = ThemeService::instance();

  svc.init(SkinId::Retro3d, ThemeMode::Light);
  auto p = svc.slider_paint();
  UT_CHECK(p.thumb_hover);
  UT_CHECK(almost_eq(p.thumb_hover_scale, 1.20f));
  UT_CHECK(almost_eq(p.thumb_hover_brightness, 1.15f));

  const SkinId scaled[] = {SkinId::Original, SkinId::ModernFlat,
                            SkinId::FrostedGlass, SkinId::Cyberpunk};
  for (SkinId sk : scaled) {
    svc.init(sk, ThemeMode::Light);
    auto q = svc.slider_paint();
    UT_CHECK(q.thumb_hover);
    UT_CHECK(almost_eq(q.thumb_hover_scale, 1.15f));
  }
  // monochrome 无 hover 规则（style-helpers.tsx:450-459）
  svc.init(SkinId::Monochrome, ThemeMode::Light);
  UT_CHECK(!svc.slider_paint().thumb_hover);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 6) frosted-glass 滑块 hover 变实心白 + 阴影/辉光按明暗切换
//    ← style-helpers.tsx:395-404
UT_TEST(t31_slider_frosted_hover_fill_and_shadow) {
  auto& svc = ThemeService::instance();

  svc.init(SkinId::FrostedGlass, ThemeMode::Light);
  auto l = svc.slider_paint();
  UT_CHECK(l.thumb_hover_fill);
  UT_CHECK(almost_eq(l.thumb_hover_fill_color.a, 1.0f));
  UT_CHECK(l.thumb_hover_shadow);
  UT_CHECK(almost_eq(l.thumb_hover_shadow_dy, 2.0f));
  UT_CHECK(almost_eq(l.thumb_hover_shadow_blur, 5.0f));

  svc.init(SkinId::FrostedGlass, ThemeMode::Dark);
  auto d = svc.slider_paint();
  UT_CHECK(d.thumb_hover_glow);
  UT_CHECK(almost_eq(d.thumb_hover_glow_blur, 4.0f));
  UT_CHECK(almost_eq(d.thumb_hover_glow_color.a, 0.40f));
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 7) cyberpunk 滑块 hover 辉光 5px → 8px
UT_TEST(t31_slider_cyberpunk_hover_grow) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Cyberpunk, ThemeMode::Dark);
  auto p = svc.slider_paint();
  UT_CHECK(p.thumb_hover_glow);
  UT_CHECK(almost_eq(p.thumb_hover_glow_blur, 8.0f));
  UT_CHECK(almost_eq(p.thumb_glow_blur, 5.0f));  // 常态仍是 5px
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 8) Slider 悬停判定：鼠标落在 thumb 上才算 hover
UT_TEST(t31_slider_hover_tracks_thumb_position) {
  Slider sld;
  sld.min_ = 0;
  sld.max_ = 100;
  sld.step_ = 1;
  sld.layout_tree({0, 0, 114, 24});  // range = 100（thumb 14）
  sld.set_value(50, false);
  sld.themed_ = true;
  sld.apply_theme();

  // thumb 中心在 x = 7 + 0.5*100 = 57
  move_slider(&sld, 57);
  UT_CHECK(sld.thumb_hovered());
  // 远离 thumb → 不 hover
  move_slider(&sld, 10);
  UT_CHECK(!sld.thumb_hovered());
  clear_input_state();
}

// 9) Slider 键盘：方向键 ±step
//    ← useSlider.js:337-348（ArrowUp/Right +step，ArrowDown/Left -step）
UT_TEST(t31_slider_keyboard_arrows) {
  Slider sld;
  sld.min_ = 0;
  sld.max_ = 100;
  sld.step_ = 1;
  sld.layout_tree({0, 0, 114, 24});
  sld.set_value(50, false);
  sld.request_focus();

  key(&sld, VK_RIGHT);
  UT_CHECK(near_f(sld.value_, 51));
  key(&sld, VK_UP);
  UT_CHECK(near_f(sld.value_, 52));
  key(&sld, VK_LEFT);
  UT_CHECK(near_f(sld.value_, 51));
  key(&sld, VK_DOWN);
  UT_CHECK(near_f(sld.value_, 50));
  clear_input_state();
}

// 10) Slider 键盘：Home → min、End → max
UT_TEST(t31_slider_keyboard_home_end) {
  Slider sld;
  sld.min_ = 0;
  sld.max_ = 100;
  sld.step_ = 1;
  sld.layout_tree({0, 0, 114, 24});
  sld.set_value(50, false);
  sld.request_focus();

  key(&sld, VK_END);
  UT_CHECK(near_f(sld.value_, 100));
  key(&sld, VK_HOME);
  UT_CHECK(near_f(sld.value_, 0));
  clear_input_state();
}

// 11) Slider 键盘：PageUp/PageDown ±shiftStep(=10)、Shift+方向键 ±10
//     ← useSlider.js:335（stepSize = shiftKey ? shiftStep : step）
//     ← useSlider.js:182（shiftStep 缺省 10）
UT_TEST(t31_slider_keyboard_page_and_shift) {
  Slider sld;
  sld.min_ = 0;
  sld.max_ = 100;
  sld.step_ = 1;
  sld.layout_tree({0, 0, 114, 24});
  sld.set_value(50, false);
  sld.request_focus();

  key(&sld, VK_PRIOR);  // PageUp → +10
  UT_CHECK(near_f(sld.value_, 60));
  key(&sld, VK_NEXT);   // PageDown → -10
  UT_CHECK(near_f(sld.value_, 50));
  key(&sld, VK_RIGHT, false, true);  // Shift+Right → +10
  UT_CHECK(near_f(sld.value_, 60));
  key(&sld, VK_LEFT, false, true);   // Shift+Left → -10
  UT_CHECK(near_f(sld.value_, 50));
  clear_input_state();
}

// 12) Slider 键盘不得越界（getNewValue 的 min/max 钳制）
UT_TEST(t31_slider_keyboard_clamps_to_range) {
  Slider sld;
  sld.min_ = 0;
  sld.max_ = 5;   // retro-3d 的 val2 上限
  sld.step_ = 0.1f;
  sld.layout_tree({0, 0, 114, 24});
  sld.set_value(4.5f, false);
  sld.request_focus();

  key(&sld, VK_PRIOR);   // +10 → 钳到 max
  UT_CHECK(near_f(sld.value_, 5.0f, 0.01f));
  key(&sld, VK_NEXT);    // -10 → 钳到 min
  UT_CHECK(near_f(sld.value_, 0.0f, 0.01f));
  clear_input_state();
}
