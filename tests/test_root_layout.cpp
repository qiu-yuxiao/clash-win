// T17-fix：root 布局根治测试——Stack{Column{titlebar(30), Stack{scene, drawer}}}
// 离屏像素断言：y0-29 标题栏（#ECECEC+divider），y30+ 场景（#F5F5F5），
// 窄窗/Mini 断点形态复测；抽屉只覆盖内容区（标题栏保持可见，Mini 同构）。
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
  Stack* content = nullptr;
  MainScene* scene = nullptr;
  SettingsDrawer* drawer = nullptr;

  RootTree() {
    auto f = std::make_unique<Column>();
    flow = f.get();
    root.add_child(std::move(f));
    auto t = std::make_unique<TitleBar>(nullptr);
    tb = t.get();
    tb->set_version(L"0.1.0");
    flow->add_child(std::move(t));
    auto c = std::make_unique<Stack>();
    content = c.get();
    content->flex_weight = 1;
    flow->add_child(std::move(c));
    auto sc = std::make_unique<MainScene>();
    scene = sc.get();
    content->add_child(std::move(sc));
    auto d = std::make_unique<SettingsDrawer>();
    drawer = d.get();
    content->add_child(std::move(d));
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
  // 结构：titlebar 固定 30 顶置；content Stack 从 y=30 撑满剩余，
  //   scene 与 drawer 都铺满内容区（Mini：抽屉挂在 .layout-content 内）。
  //   scene/drawer 是 Stack 子件 → bounds 为局部坐标，用 global_bounds 校验窗内位置。
  RectF tb = t.tb->bounds();
  UT_CHECK(near_f(tb.y, 0) && near_f(tb.h, 30) && near_f(tb.w, 271));
  RectF sc = t.scene->global_bounds();
  UT_CHECK(near_f(sc.y, 30) && near_f(sc.h, 650) && near_f(sc.w, 271));
  RectF dr = t.drawer->global_bounds();
  UT_CHECK(near_f(dr.y, 30) && near_f(dr.h, 650) && near_f(dr.w, 271));
  // 像素：y0-29 标题栏 #f5f5f5（src: layout.scss:452 var(--theme-bg) 覆写；
  //   = use-custom-theme.ts:421）；divider 一行；y30+ 场景内容底 #FFF
  t.paint();
  Px bar = pixel(135, 5);
  UT_CHECK(bar.r > 0xF0 && bar.r < 0xFB);  // #f5f5f5（Original 标题栏背景）
  Px div = pixel(135, 29);
  UT_CHECK(div.r < 0xFA);                  // divider（rgba(0,0,0,.06) 混色）
  Px scene_bg = pixel(135, 40);
  // 内容底 = --theme-bg（src: _layout.tsx:1773 background:'inherit' +
  //   use-custom-theme.ts:421 Original light '#f5f5f5'）
  UT_CHECK(scene_bg.r > 0xF0 && scene_bg.r < 0xFB && scene_bg.b > 0xF0 && scene_bg.b < 0xFB);
  Px mid = pixel(135, 300);
  UT_CHECK(mid.r > 0xF0 && mid.r < 0xFB && mid.b > 0xF0 && mid.b < 0xFB);  // 场景中部未被上栏覆盖异常
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
  RectF sc = t.scene->global_bounds();
  UT_CHECK(near_f(sc.y, 30) && near_f(sc.h, 650));
  t.paint();
  Px bar = pixel(120, 5);
  UT_CHECK(bar.r > 0xF0 && bar.r < 0xFB);
  Px scene_bg = pixel(120, 40);
  // 内容底 = --theme-bg（src: use-custom-theme.ts:421 Original light '#f5f5f5'）
  UT_CHECK(scene_bg.r > 0xF0 && scene_bg.r < 0xFB && scene_bg.b > 0xF0 && scene_bg.b < 0xFB);

  // Mini（scene 卸上栏）：titlebar/scene 结构不变
  make_offscreen(240, 680, 96);
  RootTree t2;
  t2.layout(240, 680);
  t2.scene->apply_state(WinState::Normal, WinState::Mini);
  t2.layout(240, 680);
  RectF tb2 = t2.tb->bounds();
  UT_CHECK(near_f(tb2.y, 0) && near_f(tb2.h, 30));
  RectF sc2 = t2.scene->global_bounds();
  UT_CHECK(near_f(sc2.y, 30) && near_f(sc2.h, 650));
  t2.paint();
  Px bar2 = pixel(120, 5);
  UT_CHECK(bar2.r > 0xF0 && bar2.r < 0xFB);
}

UT_TEST(tfix_drawer_covers_content_not_titlebar) {
  make_offscreen(271, 680, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Original, ThemeMode::Light);
  RootTree t;
  t.layout(271, 680);
  // 关闭态：标题栏区正常可见
  t.paint();
  Px bar_closed = pixel(135, 5);
  UT_CHECK(bar_closed.r > 0xF0 && bar_closed.r < 0xFB);
  // 打开态：抽屉只覆盖内容区（_layout.tsx:1254-1424，挂在 .layout-content），
  //   标题栏 y<30 保持原样、按钮可点；y>=30 为 light theme-panel 亮金渐变。
  t.drawer->set_open(true);
  t.paint();
  Px bar_under = pixel(135, 10);
  UT_CHECK(bar_under.r > 0xF0 && bar_under.r < 0xFB);  // 标题栏未被面板吃掉
  Px content_panel = pixel(135, 40);
  UT_CHECK(content_panel.r > 240 && content_panel.b < 240);  // 内容区亮金面板
  Px panel = pixel(135, 297);  // 左列卡片间隙（profile/combo 之间 5px gap）
  UT_CHECK(panel.r > 240);     // theme-panel 底（亮金）
  // 关闭恢复
  t.drawer->set_open(false);
  t.paint();
  Px bar_restored = pixel(135, 5);
  UT_CHECK(bar_restored.r > 0xF0 && bar_restored.r < 0xFB);
}

// ← _layout.tsx:1176-1186：按钮组容器 `marginLeft:'auto'`，三键永远贴右边缘
//   （而不是紧跟标题文字后面）。此前的实现三者按主轴顺序左对齐。
UT_TEST(t11_titlebar_controls_pinned_right) {
  make_offscreen(640, 200, 96);
  ThemeService::instance().init(SkinId::Original, ThemeMode::Light);
  RootTree t;
  for (float w : {271.0f, 400.0f, 640.0f}) {
    t.layout(w, 200);
    const RectF mi = t.tb->btn_minimize()->global_bounds();
    const RectF lg = t.tb->btn_large()->global_bounds();
    const RectF cl = t.tb->btn_close()->global_bounds();
    // 栏内右内距 8 → 关闭键右缘贴 w-8
    UT_CHECK(near_f(cl.right(), w - 8.0f));
    // 三键 28×28、间距 2、顺序 最小化→大尺寸→关闭
    UT_CHECK(near_f(mi.w, 28) && near_f(cl.w, 28));
    UT_CHECK(near_f(mi.y, 1) && near_f(cl.y, 1));  // 30 高栏内垂直居中
    UT_CHECK(near_f(lg.x, mi.right() + 2.0f));
    UT_CHECK(near_f(cl.x, lg.right() + 2.0f));
    // 标题文字不被按钮盖住：标题右缘不超过最小化键起点
    const RectF icon = t.tb->icon()->global_bounds();
    UT_CHECK(icon.x < mi.x);
  }
}
