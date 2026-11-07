// §2.7 每皮肤全局字体（← Mini layout.scss:319 --control-font-family）
//
// Mini 的实现（3 处，缺一不可）：
//   ① layout.scss:319  把 --control-font-family 作用到
//      .MuiSlider-root / .MuiInputBase-root / .MuiTypography-root /
//      .MuiListItem-root / .MuiMenuItem-root / .MuiListItemButton-root
//      —— 即「除图标字体外的全部文本」
//   ② layout.scss:324/328/352/371/418/442  各皮肤定义该变量的值
//   ③ button-styles.ts  Button(:76-460) / Card(:597-718) / Input(:784-893)
//      各自按皮肤硬编码 fontFamily（与 CSS 变量同值）
//
// Win 侧对应：
//   ① TextStyle::font_family 留空 = 跟随皮肤（D2D 层按需解析）
//   ② theme::skin_font_candidates(SkinId) 候选链（Mini 是 CSS 列表，
//      DirectWrite 只接受单一 family → 保留链，按本机存在性择一）
//   ③ Button/Card/Input 的文字走各自 TextStyle（默认留空 → 跟随皮肤）
#include <windows.h>

#include <string>

#include "test_ui_helper.h"
#include "test_util.h"

#include "ui/label.h"
#include "ui/theme/theme.h"

using namespace clashwin::ui;
using namespace clashwin::ui::theme;
using namespace ui_test;

namespace {

const SkinId kSkins[] = {SkinId::Retro3d,  SkinId::Original,
                          SkinId::ModernFlat, SkinId::FrostedGlass,
                          SkinId::Cyberpunk, SkinId::Monochrome};

}  // namespace

// 1) 每套皮肤必须有**非空**的字体候选链，且首项与 Mini 定义一致
UT_TEST(t29_skin_font_candidates_match_mini) {
  // Mini layout.scss 的各皮肤首字体
  const wchar_t* kExpected[6] = {
      L"Trebuchet MS",    // retro-3d      (:324)
      L"Segoe UI",        // original      (:442)
      L"Outfit",          // modern-flat   (:328)
      L"Segoe UI Light",  // frosted-glass (:352)
      L"Consolas",        // cyberpunk     (:371)
      L"Georgia",         // monochrome    (:418)
  };
  for (int i = 0; i < 6; ++i) {
    const auto& c = skin_font_candidates(kSkins[i]);
    UT_CHECK_MSG(!c.empty(), "候选链不得为空");
    if (c.empty()) continue;
    UT_CHECK_MSG(c.front() == kExpected[i], kExpected[i]);
    // 末位必须是 Windows 通用族（任何机器都至少有一个可用）
    UT_CHECK_MSG(!c.back().empty(), "回退项不得为空");
  }
}

// 2) 各皮肤候选链互不相同（不能全部指向同一个字体）
UT_TEST(t29_skin_font_candidates_are_distinct) {
  for (int i = 0; i < 6; ++i) {
    for (int j = i + 1; j < 6; ++j) {
      UT_CHECK(skin_font_candidates(kSkins[i]).front() !=
               skin_font_candidates(kSkins[j]).front());
    }
  }
}

// 3) 切皮肤 → 已解析的字体失效（下一帧按新皮肤重解析）
UT_TEST(t29_skin_change_invalidates_resolved_font) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  svc.set_ui_font(L"Trebuchet MS");
  UT_CHECK(svc.ui_font() == L"Trebuchet MS");
  svc.set_skin(SkinId::Cyberpunk);
  UT_CHECK_MSG(svc.ui_font().empty(), "切皮肤后必须清空已解析字体");
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 4) init() 也会清空已解析字体（init 可能换皮肤）
UT_TEST(t29_init_invalidates_resolved_font) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  svc.set_ui_font(L"Consolas");
  svc.init(SkinId::Monochrome, ThemeMode::Light);
  UT_CHECK(svc.ui_font().empty());
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 5) set_factors 不应清空字体（因子变动与字体无关，避免每次拖滑块都重解析）
UT_TEST(t29_factor_change_keeps_resolved_font) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  svc.set_ui_font(L"Trebuchet MS");
  svc.set_factors(0.8f, 2.0f);
  UT_CHECK(svc.ui_font() == L"Trebuchet MS");
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 6) ★ 端到端：渲染一个 Label（font_family 留空）→ 实际使用的字体随皮肤变化
//    （走 D2D 的 format() 解析路径）
UT_TEST(t29_label_font_follows_skin_end_to_end) {
  make_offscreen(200, 60, 96);
  auto& svc = ThemeService::instance();

  std::wstring retro_font, cyber_font;
  {
    svc.init(SkinId::Retro3d, ThemeMode::Light);
    Label lb(L"Depth");
    lb.style.size = 13;
    lb.style.font_family.clear();  // 跟随皮肤
    lb.layout_tree({0, 0, 200, 40});
    lb.on_paint(rt());
    retro_font = svc.ui_font();
  }
  {
    svc.init(SkinId::Cyberpunk, ThemeMode::Light);
    Label lb(L"Speed");
    lb.style.size = 13;
    lb.style.font_family.clear();
    lb.layout_tree({0, 0, 200, 40});
    lb.on_paint(rt());
    cyber_font = svc.ui_font();
  }
  // 解析必须成功（不能为空 → 说明没走解析路径）
  UT_CHECK_MSG(!retro_font.empty(), "retro-3d 字体未解析");
  UT_CHECK_MSG(!cyber_font.empty(), "cyberpunk 字体未解析");
  // 两者必须是不同字体（不同皮肤 → 不同字体）
  UT_CHECK_MSG(retro_font != cyber_font, "不同皮肤解析出同一字体");
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

// 7) 图标字体必须不受皮肤影响（显式指定 → 恒定）
UT_TEST(t29_explicit_icon_font_not_overridden_by_skin) {
  make_offscreen(200, 60, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  svc.set_ui_font(L"Trebuchet MS");
  Label lb(L"\uE700");
  lb.style.font_family = L"Segoe MDL2 Assets";  // 显式指定
  lb.layout_tree({0, 0, 200, 40});
  lb.on_paint(rt());
  UT_CHECK(svc.ui_font() == L"Trebuchet MS");
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}
