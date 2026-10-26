// T12 测试：控件主题接入（离屏像素断言）——热切换 / close hover 红 / 双语标签。
#include <windows.h>

#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/button.h"
#include "ui/label.h"
#include "ui/segmented.h"
#include "ui/title_bar.h"
#include "i18n/i18n.h"
#include "ui/theme/theme.h"

using namespace clashwin::ui;
using namespace clashwin::ui::theme;
using namespace ui_test;

// utf8 → wstring（测试内小工具）
inline std::wstring utf_hint(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  if (!w.empty()) w.resize(w.size() - 1);
  return w;
}

namespace {
// 中心像素取色（对 button/segmented 而言已避开圆角/边框）
Px center_px(Widget& w, float ox = -1, float oy = -1) {
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  w.on_paint(rt());
  rt().end_frame();
  return pixel(static_cast<int>(ox >= 0 ? ox : w.bounds().w / 2),
               static_cast<int>(oy >= 0 ? oy : w.bounds().h / 2));
}
}  // namespace

UT_TEST(t12_widget_button_theme_pixels) {
  make_offscreen(120, 60, 96);
  auto& svc = ThemeService::instance();
  // modern-flat light：default bg #f3f4f6
  svc.init(SkinId::ModernFlat, ThemeMode::Light);
  Button btn;
  btn.themed_ = true;
  btn.text = L"B";
  btn.layout_tree({0, 0, 90, 28});
  Px p = center_px(btn, 10, 6);
  UT_CHECK(p.r > 0xE8 && p.g > 0xE8 && p.b > 0xE8);  // #f3f4f6 近白

  // monochrome light primary：黑底白字
  svc.init(SkinId::Monochrome, ThemeMode::Light);
  Button mono;
  mono.themed_ = true;
  mono.theme_color_type_ = "primary";
  mono.text = L"M";
  mono.layout_tree({0, 0, 90, 28});
  Px mp = center_px(mono, 10, 6);
  UT_CHECK(mp.r < 16 && mp.g < 16 && mp.b < 16);  // #000000

  // cyberpunk dark default：#0d1117
  svc.init(SkinId::Cyberpunk, ThemeMode::Dark);
  Button cy;
  cy.themed_ = true;
  cy.text = L"C";
  cy.layout_tree({0, 0, 90, 28});
  Px cp = center_px(cy, 10, 6);
  UT_CHECK(cp.r < 0x20 && cp.g < 0x20 && cp.b < 0x28);  // #0d1117
  svc.init(SkinId::Retro3d, ThemeMode::Light);  // 复位缺省
}

UT_TEST(t12_widget_skin_hot_switch_changes_pixels) {
  make_offscreen(120, 60, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Original, ThemeMode::Light);
  Button btn;
  btn.themed_ = true;
  btn.text = L"B";
  btn.layout_tree({0, 0, 90, 28});
  Px before = center_px(btn, 10, 6);  // original default = #ffffff

  svc.set_skin(SkinId::ModernFlat);  // 热切换
  Px after = center_px(btn, 10, 6);  // modern-flat = #f3f4f6
  UT_CHECK(!(before.r == after.r && before.g == after.g &&
             before.b == after.b));

  svc.set_mode(ThemeMode::Dark);     // 暗色热切换：#1f2937
  Px dark = center_px(btn, 10, 6);
  UT_CHECK(dark.r < 0x30 && dark.g < 0x38 && dark.b < 0x44);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

UT_TEST(t12_theme_service_change_notification) {
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Original, ThemeMode::Light);
  int fired = 0;
  uint64_t id = svc.subscribe([&] { ++fired; });
  svc.set_skin(SkinId::ModernFlat);
  svc.set_mode(ThemeMode::Dark);
  svc.set_mode(ThemeMode::Dark);   // 无变化不通知
  UT_CHECK_EQ(fired, 2);
  svc.unsubscribe(id);
  svc.set_skin(SkinId::Retro3d);
  UT_CHECK_EQ(fired, 2);
}

UT_TEST(t12_widget_segmented_theme_pixels) {
  make_offscreen(160, 60, 96);
  auto& svc = ThemeService::instance();
  // original light：container #f1f5f9、active = 强制主色 rgb(91,92,157)
  svc.init(SkinId::Original, ThemeMode::Light);
  Segmented seg;
  seg.themed_ = true;
  seg.set_options({L"A", L"B", L"C"}, 1);
  seg.layout_tree({0, 0, 120, 28});
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  seg.on_paint(rt());
  rt().end_frame();
  Px active = pixel(50, 24);   // 第二段 active 块下部（避开文字笔画）
  UT_CHECK(active.r > 80 && active.r < 105 && active.b > 140);  // 紫 #5B5C9D
  Px inactive = pixel(10, 24); // 第一段 container 区（避开文字）
  UT_CHECK(inactive.r > 0xE0 && inactive.b > 0xE8);

  // monochrome light：active 黑底白字
  svc.init(SkinId::Monochrome, ThemeMode::Light);
  Segmented seg2;
  seg2.themed_ = true;
  seg2.set_options({L"A", L"B"}, 0);
  seg2.layout_tree({0, 0, 100, 28});
  Px ap = center_px(seg2, 10, 6);
  UT_CHECK(ap.r < 16 && ap.g < 16 && ap.b < 16);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

UT_TEST(t12_titlebar_uses_theme_vars) {
  make_offscreen(300, 60, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Original, ThemeMode::Light);
  win::TitleBar tb(nullptr);
  tb.layout_tree({0, 0, 271, 30});
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  tb.on_paint(rt());
  rt().end_frame();
  // 标题栏背景 = --background-color light #ECECEC（非窗口底 #F5F5F5！）
  Px bg = pixel(135, 5);
  UT_CHECK(bg.r > 0xE4 && bg.r < 0xF2 && bg.b > 0xE4 && bg.b < 0xF2);
  // 底边 divider（rgba(0,0,0,0.06) 混白 ≈ #F2）
  Px div = pixel(135, 28);
  UT_CHECK(div.r < 0xFA && div.b < 0xFA);

  // 暗色：#2E303D
  svc.set_mode(ThemeMode::Dark);
  rt().begin_frame();
  rt().clear({0, 0, 0, 1});
  tb.on_paint(rt());
  rt().end_frame();
  Px dark_bg = pixel(135, 5);
  UT_CHECK(dark_bg.r > 0x24 && dark_bg.r < 0x38 && dark_bg.b > 0x30 &&
           dark_bg.b < 0x46);
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

UT_TEST(t12_close_hover_red_both_modes) {
  make_offscreen(300, 60, 96);
  auto& svc = ThemeService::instance();
  // close hover 红为原版硬编码（window-controller.tsx ':hover': red），
  // 亮暗两态同色
  for (ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark}) {
    svc.init(SkinId::Original, mode);
    win::TitleBar tb(nullptr);
    tb.layout_tree({0, 0, 271, 30});
    win::TitleBarButton* close = tb.btn_close();
    close->hover_ = true;
    RectF b = close->bounds();
    rt().begin_frame();
    rt().clear({1, 1, 1, 1});
    close->on_paint(rt());
    rt().end_frame();
    Px hp = pixel(static_cast<int>(b.x + 4), static_cast<int>(b.y + 4));
    UT_CHECK_MSG(hp.r > 200 && hp.g < 90 && hp.b < 90,
                 "close hover red in mode");
  }
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

UT_TEST(t12_bilingual_label_in_theme) {
  // zh/en 双语标签在主题文本色下渲染（i18n + Label + theme text color）
  auto& i18n = clashwin::i18n::I18n::instance();
  wchar_t exe_path[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  std::wstring dir(exe_path);
  size_t pos = dir.find_last_of(L"\\/");
  i18n.init((pos == std::wstring::npos ? L"." : dir.substr(0, pos)) + L"\\locales");

  auto& svc = ThemeService::instance();
  svc.init(SkinId::Original, ThemeMode::Light);
  make_offscreen(240, 80, 96);

  for (const char* lang : {"zh", "en"}) {
    i18n.set_language(lang);
    std::string title = i18n.t("settings.page.title");  // 设置 / Settings
    Label lbl(utf_hint(title));
    lbl.style.color = svc.text_primary();
    lbl.layout_tree({10, 20, 200, 24});
    rt().begin_frame();
    rt().clear({1, 1, 1, 1});
    lbl.paint_all(rt());
    rt().end_frame();
    int dark = 0;
    for (int y = 18; y < 46; ++y)
      for (int x = 8; x < 180; ++x)
        if (pixel(x, y).r < 128) ++dark;
    UT_CHECK_MSG(dark > 5, (std::string("label pixels ") + lang).c_str());
  }
  i18n.set_language("zh");
}
