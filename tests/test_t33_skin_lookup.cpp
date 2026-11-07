// T33 测试：6 套皮肤「逐皮肤查表」回归（卡片 / 按钮 / 分段控件）。
//
// 本轮修掉的核心缺陷：Win 侧多处消费点**自己写死一份 retro-3d 的金色渐变**，
// 于是「切了皮肤但卡片/按钮还是金色」——6 套皮肤看起来几乎一样。
// 事实源：Clash Mini v3.2.2 `src/utils/button-styles.ts`
//   get3DCardStyle   :492-731  （4 卡型 × 6 皮肤）
//   get3DButtonStyle :17-491   （primary/default/error × 6 皮肤）
//   get3DSegmentedContainerStyle :917-990
//   get3DSegmentedActiveStyle    :992-1103
//   get3DSegmentedActiveTextColor:1105-1123
//
// Win 侧统一真源：
//   ThemeService::card_paint()     → card_draw::draw_card()
//   ThemeService::button_paint()   → Button::draw_button_bg / QuickBar
//   ThemeService::segmented_paint()→ Segmented / TakeoverSegmented
#include <windows.h>

#include <cmath>
#include <string>
#include <vector>

#include "test_ui_helper.h"
#include "test_util.h"

#include "ui/scenes/profile_import_card.h"
#include "ui/theme/theme.h"

using namespace clashwin;
using namespace clashwin::ui;
using namespace clashwin::ui::theme;
using namespace ui_test;

namespace {

const SkinId kSkins[6] = {SkinId::Retro3d,  SkinId::Original,
                           SkinId::ModernFlat, SkinId::FrostedGlass,
                           SkinId::Cyberpunk,  SkinId::Monochrome};

bool near_c(float a, float b, float eps = 1.5f) {
  return std::fabs(a - b) <= eps;
}

// 取第 8 位（0..255）色分量
int ch(const Color4f& c, int i) {
  const float v[4] = {c.r, c.g, c.b, c.a};
  return static_cast<int>(std::lround(v[i] * 255.0f));
}

}  // namespace

// 1) card_paint("default") 的底色必须逐皮肤等于 Mini 权威值
//    （src: button-styles.ts:508-728 get3DCardStyle 的 default 分支）
UT_TEST(t33_card_default_bg_matches_mini_per_skin) {
  auto& svc = ThemeService::instance();
  struct Row {
    SkinId skin;
    uint32_t light_bg;   // 期望底色（渐变皮肤取起始站）
    uint32_t dark_bg;
  };
  // retro-3d 是 135° 渐变（4 停），此处校验首站 + 末站。
  const Row kRows[] = {
      {SkinId::Original, 0xFFFFFF, 0x1E1F29},    // :585/:586
      {SkinId::ModernFlat, 0xFFFFFF, 0x1F2937},  // :625/:626
      {SkinId::Cyberpunk, 0xFFFFFF, 0x05070C},   // :676-682
      {SkinId::Monochrome, 0xFFFFFF, 0x000000},  // :723
  };
  for (const auto& r : kRows) {
    svc.init(r.skin, ThemeMode::Light);
    const CardPaint cp = svc.card_paint("default");
    UT_CHECK(!cp.grad_bg);
    UT_CHECK(ch(cp.bg, 0) == ((r.light_bg >> 16) & 0xFF));
    UT_CHECK(ch(cp.bg, 1) == ((r.light_bg >> 8) & 0xFF));
    UT_CHECK(ch(cp.bg, 2) == (r.light_bg & 0xFF));
    svc.init(r.skin, ThemeMode::Dark);
    const CardPaint cd = svc.card_paint("default");
    UT_CHECK(!cd.grad_bg);
    UT_CHECK(ch(cd.bg, 0) == ((r.dark_bg >> 16) & 0xFF));
    UT_CHECK(ch(cd.bg, 1) == ((r.dark_bg >> 8) & 0xFF));
    UT_CHECK(ch(cd.bg, 2) == (r.dark_bg & 0xFF));
  }
  // retro-3d：135° 4 停渐变（:537-540）
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  const CardPaint cr = svc.card_paint("default");
  UT_CHECK(cr.grad_bg);
  UT_CHECK(cr.grad_count == 4);
  UT_CHECK(near_c(cr.grad_angle, 135.0f));
  UT_CHECK(ch(cr.grad[0].color, 0) == 0xFF && ch(cr.grad[0].color, 1) == 0xFD &&
           ch(cr.grad[0].color, 2) == 0xE7);
  UT_CHECK(ch(cr.grad[3].color, 0) == 0xFF && ch(cr.grad[3].color, 1) == 0xE0 &&
           ch(cr.grad[3].color, 2) == 0x82);
  // frosted-glass：半透明白 0.06（:657）
  svc.init(SkinId::FrostedGlass, ThemeMode::Light);
  const CardPaint cf = svc.card_paint("default");
  UT_CHECK(!cf.grad_bg);
  UT_CHECK(ch(cf.bg, 0) == 0xFF && ch(cf.bg, 1) == 0xFF && ch(cf.bg, 2) == 0xFF);
  UT_CHECK(near_c(cf.bg.a, 0.06f, 0.005f));
}

// 2) 亮态 default 卡面：4 套皮肤（original/modern/cyberpunk/monochrome）
//    **同为纯白 #ffffff**（Mini 事实，:585/:625/:677/:723），
//    retro-3d 是浅金渐变、frosted-glass 是半透明白 —— 不能断言「两两互异」。
UT_TEST(t33_card_default_white_group_per_mini) {
  auto& svc = ThemeService::instance();
  const SkinId kWhite[4] = {SkinId::Original, SkinId::ModernFlat,
                             SkinId::Cyberpunk, SkinId::Monochrome};
  for (SkinId s : kWhite) {
    svc.init(s, ThemeMode::Light);
    const CardPaint cp = svc.card_paint("default");
    UT_CHECK(!cp.grad_bg);
    UT_CHECK(ch(cp.bg, 0) == 0xFF && ch(cp.bg, 1) == 0xFF && ch(cp.bg, 2) == 0xFF);
  }
  // retro-3d 亮态首站 #FFFDE7 ≠ 纯白
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  const CardPaint cr = svc.card_paint("default");
  UT_CHECK(cr.grad_bg);
  UT_CHECK(!(ch(cr.grad[0].color, 0) == 0xFF && ch(cr.grad[0].color, 1) == 0xFF &&
             ch(cr.grad[0].color, 2) == 0xFF));
  // frosted-glass 半透明白
  svc.init(SkinId::FrostedGlass, ThemeMode::Light);
  const CardPaint cf = svc.card_paint("default");
  UT_CHECK(ch(cf.bg, 0) == 0xFF && ch(cf.bg, 1) == 0xFF && ch(cf.bg, 2) == 0xFF);
  UT_CHECK(near_c(cf.bg.a, 0.06f, 0.005f));
}

// 3) 底缘 1px 实线（boxShadow 首层 '0 1px 0 0 bevelShadowDark'）**仅 retro-3d**
//    （src: button-styles.ts:556/:561；其余 5 套的 boxShadow 无此层）
UT_TEST(t33_bottom_bevel_only_retro3d) {
  auto& svc = ThemeService::instance();
  for (int i = 0; i < 6; ++i) {
    svc.init(kSkins[i], ThemeMode::Light);
    const CardPaint cp = svc.card_paint("default");
    if (kSkins[i] == SkinId::Retro3d) {
      UT_CHECK(cp.has_bottom_bevel);
      UT_CHECK(ch(cp.bottom_bevel_color, 0) == 0xB8 &&
               ch(cp.bottom_bevel_color, 1) == 0x86 &&
               ch(cp.bottom_bevel_color, 2) == 0x0B);
    } else {
      UT_CHECK(!cp.has_bottom_bevel);
    }
  }
  // retro-3d 暗态 = #755500
  svc.init(SkinId::Retro3d, ThemeMode::Dark);
  const CardPaint cd = svc.card_paint("default");
  UT_CHECK(cd.has_bottom_bevel);
  UT_CHECK(ch(cd.bottom_bevel_color, 0) == 0x75 &&
           ch(cd.bottom_bevel_color, 1) == 0x55 &&
           ch(cd.bottom_bevel_color, 2) == 0x00);
}

// 4) primary 按钮必须**逐皮肤匹配 Mini 权威值**
//    （src: get3DButtonStyle :17-491）
//    ★ original 与 modern-flat 同为 palette.primary.main（Mini 事实），
//      不能断言「两两互异」。
UT_TEST(t33_primary_button_matches_mini_per_skin) {
  auto& svc = ThemeService::instance();
  // retro-3d：135° 六停金渐变（:39）
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  {
    const ButtonPaint bp = svc.button_paint("primary");
    UT_CHECK(bp.use_grad);
    UT_CHECK(bp.grad_count == 6);
    UT_CHECK(ch(bp.grad[0].color, 0) == 0xFF && ch(bp.grad[0].color, 1) == 0xE0 &&
             ch(bp.grad[0].color, 2) == 0x82);
    UT_CHECK(ch(bp.text, 0) == 0x2C && ch(bp.text, 1) == 0x1F &&
             ch(bp.text, 2) == 0x03);  // :35
  }
  // original：var(--primary-main)（:152-...）
  svc.init(SkinId::Original, ThemeMode::Light);
  {
    const ButtonPaint bp = svc.button_paint("primary");
    UT_CHECK(!bp.use_grad);
    UT_CHECK(ch(bp.fill, 0) == ch(svc.primary(), 0));
    UT_CHECK(ch(bp.fill, 1) == ch(svc.primary(), 1));
    UT_CHECK(ch(bp.fill, 2) == ch(svc.primary(), 2));
    UT_CHECK(ch(bp.text, 0) == 0xFF && ch(bp.text, 1) == 0xFF &&
             ch(bp.text, 2) == 0xFF);  // :160 白字
  }
  // modern-flat：palette.primary.main（:220-...）—— 与 original 同色（Mini 事实）
  svc.init(SkinId::ModernFlat, ThemeMode::Light);
  {
    const ButtonPaint bp = svc.button_paint("primary");
    UT_CHECK(!bp.use_grad);
    UT_CHECK(ch(bp.fill, 0) == ch(svc.primary(), 0));
  }
  // frosted-glass：rgba(primary, 0.25)（:291-294）
  svc.init(SkinId::FrostedGlass, ThemeMode::Light);
  {
    const ButtonPaint bp = svc.button_paint("primary");
    UT_CHECK(!bp.use_grad);
    UT_CHECK(near_c(bp.fill.a, 0.25f, 0.005f));
    UT_CHECK(ch(bp.fill, 0) == ch(svc.primary(), 0));
  }
  // cyberpunk：亮 #ffffff / 暗 #0a0e17（:353-357）
  svc.init(SkinId::Cyberpunk, ThemeMode::Light);
  UT_CHECK(ch(svc.button_paint("primary").fill, 0) == 0xFF);
  svc.init(SkinId::Cyberpunk, ThemeMode::Dark);
  UT_CHECK(ch(svc.button_paint("primary").fill, 0) == 0x0A);
  // monochrome：亮 #000000 / 暗 #ffffff（:429-433）
  svc.init(SkinId::Monochrome, ThemeMode::Light);
  UT_CHECK(ch(svc.button_paint("primary").fill, 0) == 0x00);
  svc.init(SkinId::Monochrome, ThemeMode::Dark);
  UT_CHECK(ch(svc.button_paint("primary").fill, 0) == 0xFF);
}

// 5) 分段控件选中块必须逐皮肤匹配 Mini 权威值
//    （src: get3DSegmentedActiveStyle :992-1103 + ActiveTextColor :1105-1123）
UT_TEST(t33_segmented_active_matches_mini_per_skin) {
  auto& svc = ThemeService::instance();
  // retro-3d：六停金渐变 + 选中文字 #2C1F03
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  {
    const SegmentedPaint sp = svc.segmented_paint();
    UT_CHECK(sp.active_grad_count == 6);
    UT_CHECK(near_c(sp.active_grad_angle, 135.0f));
    UT_CHECK(ch(sp.active_grad[0].color, 0) == 0xFF &&
             ch(sp.active_grad[0].color, 1) == 0xE0 &&
             ch(sp.active_grad[0].color, 2) == 0x82);
    UT_CHECK(ch(sp.active_text, 0) == 0x2C && ch(sp.active_text, 1) == 0x1F &&
             ch(sp.active_text, 2) == 0x03);  // :1110
  }
  // original：var(--primary-main) + radius 3*val1（:1026-1040）
  svc.init(SkinId::Original, ThemeMode::Light);
  {
    const SegmentedPaint sp = svc.segmented_paint();
    UT_CHECK(sp.active_grad_count == 0);
    UT_CHECK(ch(sp.active_bg, 0) == ch(svc.primary(), 0));
    UT_CHECK(near_c(sp.active_radius, 3.0f * svc.val1()));
    UT_CHECK(ch(sp.active_text, 0) == 0xFF);  // :1117-1118
  }
  // modern-flat：palette.primary.main + radius 4*val1（:1041-1055）
  svc.init(SkinId::ModernFlat, ThemeMode::Light);
  {
    const SegmentedPaint sp = svc.segmented_paint();
    UT_CHECK(sp.active_grad_count == 0);
    UT_CHECK(ch(sp.active_bg, 0) == ch(svc.primary(), 0));
    UT_CHECK(near_c(sp.active_radius, 4.0f * svc.val1()));
  }
  // frosted-glass：rgba(primary, 0.3)（:1056-1069）
  svc.init(SkinId::FrostedGlass, ThemeMode::Light);
  {
    const SegmentedPaint sp = svc.segmented_paint();
    UT_CHECK(near_c(sp.active_bg.a, 0.3f, 0.005f));
    UT_CHECK(ch(sp.active_bg, 0) == ch(svc.primary(), 0));
  }
  // cyberpunk：亮 #00b0ff / 暗 #39ff14（:1070-1086）
  svc.init(SkinId::Cyberpunk, ThemeMode::Light);
  UT_CHECK(ch(svc.segmented_paint().active_bg, 0) == 0x00 &&
           ch(svc.segmented_paint().active_bg, 1) == 0xB0);
  svc.init(SkinId::Cyberpunk, ThemeMode::Dark);
  UT_CHECK(ch(svc.segmented_paint().active_bg, 0) == 0x39);
  // monochrome：亮 #000000 / 暗 #ffffff（:1087-1103）
  svc.init(SkinId::Monochrome, ThemeMode::Light);
  UT_CHECK(ch(svc.segmented_paint().active_bg, 0) == 0x00);
  svc.init(SkinId::Monochrome, ThemeMode::Dark);
  UT_CHECK(ch(svc.segmented_paint().active_bg, 0) == 0xFF);
}

// 6) 像素级：导入卡在 6 套皮肤下的卡面必须与 Mini 权威值一致
//    （旧实现在所有皮肤下都画 retro-3d 的浅金渐变）
//    ★ 亮态下 original/modern/cyberpunk/monochrome 四套**同为纯白**（Mini 事实）。
UT_TEST(t33_import_card_pixels_match_mini_per_skin) {
  make_offscreen(240, 110, 96);
  auto& svc = ThemeService::instance();
  auto card_pixel = [&svc](SkinId s) {
    svc.init(s, ThemeMode::Light);
    scene::ProfileImportCard card;
    card.layout_tree({0, 0, 240, 110});
    rt().begin_frame();
    rt().clear({1, 1, 1, 1});
    card.paint_all(rt());
    rt().end_frame();
    // y=21 落在标题行与 URL 输入框之间的卡面
    return pixel(120, 21);
  };
  // retro-3d：浅金渐变（#FFFDE7 起）
  const Px q = card_pixel(SkinId::Retro3d);
  UT_CHECK(q.r > 240 && q.g > 200 && q.b < 245);
  // 4 套纯白卡面
  const Px o = card_pixel(SkinId::Original);
  UT_CHECK(o.r > 245 && o.g > 245 && o.b > 245);
  const Px m = card_pixel(SkinId::ModernFlat);
  UT_CHECK(m.r > 245 && m.g > 245 && m.b > 245);
  const Px c = card_pixel(SkinId::Cyberpunk);
  UT_CHECK(c.r > 245 && c.g > 245 && c.b > 245);
  const Px mo = card_pixel(SkinId::Monochrome);
  UT_CHECK(mo.r > 245 && mo.g > 245 && mo.b > 245);
  // frosted-glass：卡面 rgba(255,255,255,.06) 叠白 → 近白；
  //   但其 boxShadow '0 8px 32px rgba(0,0,0,.06)'（:667）会在卡面附近留下
  //   微弱暗影，故允许略低于纯白。
  const Px f = card_pixel(SkinId::FrostedGlass);
  UT_CHECK(f.r > 230 && f.g > 230 && f.b > 230);
}

// 7) 卡片文字色必须逐皮肤取 card_paint("default").text_color
//    （src: get3DCardStyle 的 color 字段：cyberpunk 暗态 #00f0ff、
//      monochrome 亮 #000000 / 暗 #ffffff —— 都**不是** text.primary）
UT_TEST(t33_card_text_color_per_skin) {
  auto& svc = ThemeService::instance();
  // cyberpunk 亮态 #0f172a（:689）
  svc.init(SkinId::Cyberpunk, ThemeMode::Light);
  const CardPaint cc = svc.card_paint("default");
  UT_CHECK(ch(cc.text_color, 0) == 0x0F && ch(cc.text_color, 1) == 0x17 &&
           ch(cc.text_color, 2) == 0x2A);
  // cyberpunk 暗态 #00f0ff（:690）
  svc.init(SkinId::Cyberpunk, ThemeMode::Dark);
  const CardPaint ccd = svc.card_paint("default");
  UT_CHECK(ch(ccd.text_color, 0) == 0x00 && ch(ccd.text_color, 1) == 0xF0 &&
           ch(ccd.text_color, 2) == 0xFF);
  // monochrome 亮 #000000 / 暗 #ffffff（:724）
  svc.init(SkinId::Monochrome, ThemeMode::Light);
  UT_CHECK(ch(svc.card_paint("default").text_color, 0) == 0x00);
  svc.init(SkinId::Monochrome, ThemeMode::Dark);
  UT_CHECK(ch(svc.card_paint("default").text_color, 0) == 0xFF);
  // retro-3d 亮 #3C2F0F / 暗 #2C1F03（:535）
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  UT_CHECK(ch(svc.card_paint("default").text_color, 0) == 0x3C);
  svc.init(SkinId::Retro3d, ThemeMode::Dark);
  UT_CHECK(ch(svc.card_paint("default").text_color, 0) == 0x2C);
}
