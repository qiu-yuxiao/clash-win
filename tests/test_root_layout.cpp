// T17-fix：root 布局根治测试——Stack{Column{titlebar(30), scene(flex)}, drawer}
// 离屏像素断言：y0-29 标题栏（#ECECEC+divider），y30+ 场景（#F5F5F5），
// 窄窗/Mini 断点形态复测，抽屉 overlay 盖标题栏。
#include <windows.h>

#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/theme/theme.h"
#include "ui/main_scene.h"
#include "ui/scenes/settings_drawer.h"
#include "ui/title_bar.h"

using namespace clashwin;
using namespace clashwin::ui;
using namespace clashwin::ui::win;
using namespace clashwin::ui::scene;
using namespace clashwin::ui::theme;
using namespace ui_test;

namespace {
// 复刻 MainWindow::build_widget_tree 的装配结构（无 HWND 离屏版）
struct RootTree {
  Stack root;
  Column* flow = nullptr;
  TitleBar* tb = nullptr;
  MainScene* scene = nullptr;
  SettingsDrawer* drawer = nullptr;

  RootTree() {
    auto f = std::make_unique<Column>();
    flow = f.get();
    root.add_child(std::move(f));
    auto t = std::make_unique<TitleBar>(nullptr);
    tb = t.get();
    tb->set_version(L"3.0.10");
    flow->add_child(std::move(t));
    auto sc = std::make_unique<MainScene>();
    scene = sc.get();
    scene->flex_weight = 1;
    flow->add_child(std::move(sc));
    auto d = std::make_unique<SettingsDrawer>();
    drawer = d.get();
    root.add_child(std::move(d));
  }
  void layout(float w, float h) { root.layout_tree({0, 0, w, h}); }
  void paint() {
    rt().begin_frame();
    rt().clear({1, 1, 1, 1});
    root.paint_all(rt());
    rt().end_frame();
  }
};
}  // namespace

UT_TEST(tfix_root_titlebar_30_scene_below) {
  make_offscreen(271, 680, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Original, ThemeMode::Light);
  RootTree t;
  t.layout(271, 680);
  // 结构：titlebar 固定 30 顶置；scene y=30 撑满剩余；drawer 全屏 overlay
  RectF tb = t.tb->bounds();
  UT_CHECK(near_f(tb.y, 0) && near_f(tb.h, 30) && near_f(tb.w, 271));
  RectF sc = t.scene->bounds();
  UT_CHECK(near_f(sc.y, 30) && near_f(sc.h, 650) && near_f(sc.w, 271));
  RectF dr = t.drawer->bounds();
  UT_CHECK(near_f(dr.y, 0) && near_f(dr.h, 680) && near_f(dr.w, 271));
  // 像素：y0-29 标题栏 #ECECEC；divider 一行；y30+ 场景 #F5F5F5
  t.paint();
  Px bar = pixel(135, 5);
  UT_CHECK(bar.r > 0xE4 && bar.r < 0xF2);  // ECECEC（标题栏背景）
  Px div = pixel(135, 29);
  UT_CHECK(div.r < 0xFA);                  // divider（rgba(0,0,0,.06) 混色）
  Px scene_bg = pixel(135, 40);
  UT_CHECK(scene_bg.r > 0xEF && scene_bg.r < 0xFB);  // F5F5F5（场景背景）
  Px mid = pixel(135, 300);
  UT_CHECK(mid.r > 0xEF);                  // 场景中部未被上栏覆盖异常
}

UT_TEST(tfix_root_narrow_and_mini_shapes) {
  make_offscreen(240, 680, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Original, ThemeMode::Light);
  RootTree t;
  // 窄窗（宽 240 < narrow 断点）：titlebar 仍 30 顶置、scene 撑满
  t.layout(240, 680);
  RectF tb = t.tb->bounds();
  UT_CHECK(near_f(tb.y, 0) && near_f(tb.h, 30) && near_f(tb.w, 240));
  RectF sc = t.scene->bounds();
  UT_CHECK(near_f(sc.y, 30) && near_f(sc.h, 650));
  t.paint();
  Px bar = pixel(120, 5);
  UT_CHECK(bar.r > 0xE4 && bar.r < 0xF2);
  Px scene_bg = pixel(120, 40);
  UT_CHECK(scene_bg.r > 0xEF && scene_bg.r < 0xFB);

  // Mini（scene 卸上栏）：titlebar/scene 结构不变
  make_offscreen(240, 680, 96);
  RootTree t2;
  t2.layout(240, 680);
  t2.scene->apply_state(WinState::Normal, WinState::Mini);
  t2.layout(240, 680);
  RectF tb2 = t2.tb->bounds();
  UT_CHECK(near_f(tb2.y, 0) && near_f(tb2.h, 30));
  RectF sc2 = t2.scene->bounds();
  UT_CHECK(near_f(sc2.y, 30) && near_f(sc2.h, 650));
  t2.paint();
  Px bar2 = pixel(120, 5);
  UT_CHECK(bar2.r > 0xE4 && bar2.r < 0xF2);
}

UT_TEST(tfix_drawer_overlay_covers_titlebar) {
  make_offscreen(271, 680, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Original, ThemeMode::Light);
  RootTree t;
  t.layout(271, 680);
  // 关闭态：标题栏区正常可见
  t.paint();
  Px bar_closed = pixel(135, 5);
  UT_CHECK(bar_closed.r > 0xE4 && bar_closed.r < 0xF2);
  // 打开态：遮罩盖住标题栏（z 最高）→ y=10 变混色灰；面板区白
  t.drawer->set_open(true);
  t.paint();
  Px masked = pixel(135, 10);
  UT_CHECK(masked.r > 100 && masked.r < 190);  // 白×α0.45 黑 ≈ 140
  Px panel = pixel(135, 300);
  UT_CHECK(panel.r > 240);                     // 面板底 theme_panel_bg
  // 关闭恢复
  t.drawer->set_open(false);
  t.paint();
  Px bar_restored = pixel(135, 5);
  UT_CHECK(bar_restored.r > 0xE4 && bar_restored.r < 0xF2);
}
