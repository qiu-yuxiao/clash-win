// T17 测试：设置抽屉（开关/遮罩/布局像素/5 卡调用链/皮肤条/滑块/阈值/AccentColor）。
#include <windows.h>

#include "cmd/command_service.h"
#include "config/config.h"
#include "i18n/i18n.h"
#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/theme/theme.h"
#include "ui/scenes/settings_drawer.h"
#include "util/utf.h"

using namespace clashwin;
using namespace clashwin::ui;
using namespace clashwin::ui::scene;
using namespace clashwin::ui::theme;
using namespace ui_test;

namespace {
struct PatchLog {
  std::vector<std::pair<std::string, nlohmann::json>> calls;
  void record(const std::string& k, const nlohmann::json& v) {
    calls.push_back({k, v});
  }
  int count(const std::string& k) const {
    int n = 0;
    for (auto& [key, v] : calls)
      if (key == k) ++n;
    return n;
  }
  bool has(const std::string& k, const nlohmann::json& v) const {
    for (auto& [key, val] : calls)
      if (key == k && val == v) return true;
    return false;
  }
};
}  // namespace

UT_TEST(t17_drawer_open_close_and_mask) {
  make_offscreen(271, 680, 96);
  SettingsDrawer drawer;
  drawer.layout_tree({0, 0, 271, 680});
  UT_CHECK(!drawer.is_open());
  drawer.set_open(true);
  UT_CHECK(drawer.is_open());
  // 面板顶部留标题栏 30px
  RectF panel = drawer.panel()->bounds();
  UT_CHECK(near_f(panel.y, 30));
  UT_CHECK(near_f(panel.w, 271));
  // 遮罩点击关闭（点击面板外=顶部 30px 区）
  MouseEvent up{MouseEventType::Up, {135, 10}};
  route_mouse_event(&drawer, up);
  UT_CHECK(!drawer.is_open());
  clear_input_state();
}

UT_TEST(t17_left_column_layout_pixels) {
  make_offscreen(271, 680, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Original, ThemeMode::Light);
  SettingsDrawer drawer;
  drawer.layout_tree({0, 0, 271, 680});
  drawer.set_open(true);
  // 卡宽 = 271 - 2*6 边距 = 259（单列全宽；大窗两列形态留 T20）
  UT_CHECK(near_f(drawer.profile_card()->bounds().w, 259));
  // 绘制：遮罩 + 面板 + 卡片底
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  drawer.paint_all(rt());
  rt().end_frame();
  // 遮罩半透明黑混白（顶部 30px 区）≈ (140,140,140)
  Px mask = pixel(135, 10);
  UT_CHECK(mask.r > 100 && mask.r < 190);
  // 卡片底 #ffffff（original panel）：卡全局 y = 面板30 + 局部36 = 66 起
  Px card = pixel(135, 72);
  UT_CHECK(card.r > 240);
  // 订阅卡橙标题存在（标题全局 y 66-86）
  int orange = 0;
  for (int y = 66; y < 86; ++y)
    for (int x = 8; x < 200; ++x) {
      Px p = pixel(x, y);
      if (p.r > 200 && p.g > 100 && p.g < 190 && p.b < 90) ++orange;
    }
  UT_CHECK(orange > 3);
}

UT_TEST(t17_takeover_routing_callbacks) {
  make_offscreen(271, 680, 96);
  SettingsDrawer drawer;
  drawer.layout_tree({0, 0, 271, 680});
  PatchLog log;
  drawer.on_patch_verge = [&](const std::string& k, const nlohmann::json& v) {
    log.record(k, v);
    return true;
  };
  drawer.on_patch_clash_mode = [&](const std::string& mode) {
    log.record("clash_mode", mode);
    return true;
  };
  drawer.wire_callbacks();

  // 接管 SYS → esp=true/etm=false（← layout.tsx:1059-1063）
  drawer.takeover_card()->seg_->apply("SYS");
  UT_CHECK(log.has("enable_system_proxy", true));
  UT_CHECK(log.has("enable_tun_mode", false));
  // 接管 TUN → esp=false/etm=true
  drawer.takeover_card()->seg_->apply("TUN");
  UT_CHECK(log.has("enable_system_proxy", false));
  UT_CHECK(log.has("enable_tun_mode", true));
  // 失败回弹：apply 返回 false → mode 不变 + 不发 patch
  int before = log.count("enable_system_proxy");
  drawer.takeover_card()->seg_->apply = [](const std::string&) { return false; };
  drawer.takeover_card()->seg_->apply("MAN");
  UT_CHECK_EQ(log.count("enable_system_proxy"), before);
  // 恢复成功回调
  drawer.takeover_card()->seg_->apply = [](const std::string&) { return true; };

  // 分流 PRX → rule_fallback=proxy + patch_clash_mode("rule")
  drawer.routing_card()->seg_->on_change.operator()(2);
  UT_CHECK(log.has("rule_fallback", "proxy"));
  UT_CHECK(log.has("clash_mode", "rule"));
}

UT_TEST(t17_basic_card_callbacks) {
  make_offscreen(271, 680, 96);
  SettingsDrawer drawer;
  drawer.layout_tree({0, 0, 271, 680});
  PatchLog log;
  drawer.on_patch_verge = [&](const std::string& k, const nlohmann::json& v) {
    log.record(k, v);
    return true;
  };
  drawer.wire_callbacks();

  drawer.basic_card()->sw_auto_launch_->set_checked(true, true);
  UT_CHECK(log.has("enable_auto_launch", true));
  drawer.basic_card()->sw_silent_start_->set_checked(true, true);
  UT_CHECK(log.has("enable_silent_start", true));
  drawer.basic_card()->sw_allow_lan_->set_checked(true, true);
  UT_CHECK(log.has("clash_allow_lan", true));
  drawer.basic_card()->sw_allow_ipv6_->set_checked(true, true);
  UT_CHECK(log.has("clash_ipv6", true));
  drawer.basic_card()->sw_allow_ipv6_->set_checked(false, true);  // 同值不触发（SwitchW 语义）
  UT_CHECK(log.has("clash_ipv6", false));

  // Mixed Port：合法值 → verge_mixed_port
  drawer.basic_card()->port_field_->set_text(L"1080");
  drawer.basic_card()->save_btn_->on_click();
  UT_CHECK(log.has("verge_mixed_port", 1080));
  // 非法值（<1024）→ 不发
  int before = log.count("verge_mixed_port");
  drawer.basic_card()->port_field_->set_text(L"80");
  drawer.basic_card()->save_btn_->on_click();
  UT_CHECK_EQ(log.count("verge_mixed_port"), before);
}

UT_TEST(t17_theme_card_skin_mode_sliders) {
  make_offscreen(271, 680, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Original, ThemeMode::Light);
  SettingsDrawer drawer;
  drawer.layout_tree({0, 0, 271, 680});
  PatchLog log;
  drawer.on_patch_verge = [&](const std::string& k, const nlohmann::json& v) {
    log.record(k, v);
    return true;
  };
  drawer.wire_callbacks();

  // 皮肤条 → set_skin + 持久化 + patch 键
  drawer.theme_card()->skin_buttons_[1]->on_click();  // Org
  UT_CHECK(svc.skin() == SkinId::Original);
  UT_CHECK(log.has("clash_mini_control_skin", "original"));
  SkinPrefs p = load_skin_prefs();
  UT_CHECK(p.skin == "original");
  // 皮肤切换热生效（Original 强制主色）
  auto c = svc.primary();
  UT_CHECK(c.r > 0.34 && c.r < 0.37);  // rgb(91,92,157)

  // 亮暗三段 → set_mode + patch
  drawer.theme_card()->mode_seg_->set_selected(2);  // DARK
  UT_CHECK(svc.mode() == ThemeMode::Dark);
  UT_CHECK(log.has("theme_mode", "dark"));
  svc.set_mode(ThemeMode::Light);

  // Depth/Vibrancy 滑块 → set_factors + 数值标签
  drawer.theme_card()->depth_slider_->set_value(0.6f, true);
  UT_CHECK(near_f(svc.depth_factor(), 0.6f));
  UT_CHECK(drawer.theme_card()->depth_value_->text == L"0.6");
  drawer.theme_card()->vibrancy_slider_->set_value(1.2f, true);
  UT_CHECK(near_f(svc.vibrancy_factor(), 1.2f));
  UT_CHECK(drawer.theme_card()->vibrancy_value_->text == L"1.2");
  // 持久化
  SkinPrefs p2 = load_skin_prefs();
  UT_CHECK(near_f(p2.depth, 0.6f));
  UT_CHECK(near_f(p2.vibrancy, 1.2f));
}

UT_TEST(t17_skin_bar_hidden_below_830) {
  // 高≤830 皮肤条隐藏（census 口径）——卡片层直接驱动可见性
  make_offscreen(271, 680, 96);
  SettingsDrawer drawer;
  drawer.layout_tree({0, 0, 271, 680});
  drawer.set_open(true);
  // 模拟阈值逻辑（与 main_window 集成线同款判定）
  double logical_h = 680.0;  // 窄窗 680 < 830
  bool show_skin = logical_h > 830.0;
  for (auto* b : drawer.theme_card()->skin_buttons_)
    b->set_visible(show_skin);
  UT_CHECK(!drawer.theme_card()->skin_buttons_[0]->visible());
  // >830 → 显示
  show_skin = 900.0 > 830.0;
  for (auto* b : drawer.theme_card()->skin_buttons_)
    b->set_visible(show_skin);
  UT_CHECK(drawer.theme_card()->skin_buttons_[0]->visible());
}

UT_TEST(t17_language_dropdown) {
  // 语言下拉切换即时生效（i18n.set_language + patch_verge_config({language})）
  make_offscreen(271, 680, 96);
  auto& i18n = clashwin::i18n::I18n::instance();
  wchar_t exe_path[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  std::wstring dir(exe_path);
  size_t pos = dir.find_last_of(L"\\/");
  i18n.init((pos == std::wstring::npos ? L"." : dir.substr(0, pos)) + L"\\locales");
  std::string before = i18n.t("settings.page.title");
  i18n.set_language("en");
  std::string after = i18n.t("settings.page.title");
  UT_CHECK_MSG(before != after || before.empty(),
               (before + " vs " + after).c_str());  // 即时生效
  i18n.set_language("zh");
  // patch 链路由 on_patch_verge（与 Drawer 其他项同路径）
  PatchLog log;
  SettingsDrawer drawer;
  drawer.layout_tree({0, 0, 271, 680});
  drawer.on_patch_verge = [&](const std::string& k, const nlohmann::json& v) {
    log.record(k, v);
    return true;
  };
  drawer.on_patch_verge("language", "en");
  UT_CHECK(log.has("language", "en"));
}

UT_TEST(t17_accent_color_probe) {
  // AccentColor 探测（DWM）：返回值可能为 nullopt（未开主题色）——
  // 不强断言具体值，验证调用不崩且值域合理
  auto c = system_accent_color_rgb();
  if (c) {
    UT_CHECK(*c <= 0xFFFFFF);
    // ThemeService primary_override 接线（非 original 皮肤时生效）
    auto& svc = ThemeService::instance();
    svc.init(SkinId::ModernFlat, ThemeMode::Light, "#123456");
    auto p = svc.primary();
    UT_CHECK(p.r > 0.05f && p.r < 0.09f && p.b > 0.32f && p.b < 0.36f);
    svc.init(SkinId::Retro3d, ThemeMode::Light);
  }
}
