// T34 测试：命中链不得被「铺满型空容器」截断（P0：全应用鼠标点击失效）。
//
// 缺陷现象（2026-11-07 实测）：ClashWin 整个窗口的鼠标点击全部落空——
// 齿轮打不开设置抽屉、开关拨不动、滑块拖不动、标题栏三键点不动。
//
// 根因：Widget::hit_test 的兜底语义是「自身矩形命中即返回自身」。
//   SnackbarW（通知容器）铺满全窗且 visible=true，却没有覆写 hit_self，
//   于是命中测试在最上层就命中它 → 命中链在根节点被截断，
//   下层所有控件（gear / SwitchW / Slider / TitleBarButton）都收不到事件。
//   全项目其余整页浮层（ContextMenu / AddUrlDialog / EditProfileModal /
//   ClientUpdateModal / CoreUpdateModal / LogViewerModal / FirstRunGuideDialog /
//   SettingsDrawer / Modal 基类）都已有 `hit_self → open_ && …` 守卫，
//   唯独 SnackbarW 漏了（Mini 侧 notice-manager 容器是 pointer-events:none）。
//
// 本文件的用例把「铺满型浮层在未激活时不得拦截命中」固化为回归。
#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "test_ui_helper.h"
#include "test_util.h"

#include "ui/context_menu.h"
#include "ui/snackbar_w.h"
#include "ui/scenes/add_url_dialog.h"
#include "ui/scenes/quick_bar.h"

using namespace clashwin;
using namespace clashwin::ui;
using namespace ui_test;

namespace {

// 记录鼠标事件的探针控件（代替真实交互控件，验证命中链是否通到下层）
class Probe : public Widget {
 public:
  int down = 0;
  int up = 0;
  bool on_mouse(const MouseEvent& e) override {
    if (e.type == MouseEventType::Down) ++down;
    if (e.type == MouseEventType::Up) ++up;
    return true;
  }
};

constexpr float kW = 271.0f;
constexpr float kH = 680.0f;

}  // namespace

// 1) 空通知层（铺满全窗）不得拦截命中 —— 本缺陷的直接回归
UT_TEST(t34_snackbar_empty_does_not_block_hit) {
  Widget root;
  root.set_bounds({0, 0, kW, kH});

  // 通知层：铺满全窗、visible=true、无子件（与实机形态一致）
  auto sb = std::make_unique<SnackbarW>();
  sb->set_bounds({0, 0, kW, kH});
  root.add_child(std::move(sb));

  // 下层可交互控件
  auto pr = std::make_unique<Probe>();
  pr->set_bounds({10, 10, 60, 24});
  Probe* prp = pr.get();
  root.add_child(std::move(pr));

  // 点击探针中心
  click(&root, 40.0f, 22.0f);
  UT_CHECK(prp->down == 1);
  UT_CHECK(prp->up == 1);
}

// 2) 有通知时：条目矩形内可命中、条目外不得命中
//    （Mini：notice-manager 容器 pointer-events:none，只有 Snackbar 本体可交互）
UT_TEST(t34_snackbar_item_hit_only_inside_item) {
  SnackbarW sb;
  sb.set_bounds({0, 0, kW, kH});
  std::vector<events::NoticeItem> items;
  events::NoticeItem it;
  it.message = "hello";
  items.push_back(it);
  sb.set_items(items);
  sb.layout_items();

  const auto& rects = sb.item_rects();
  UT_CHECK(rects.size() == 1);
  if (rects.empty()) return;

  // 条目内 → 命中
  const RectF& r = rects[0];
  UT_CHECK(sb.hit_self({r.x + r.w / 2.0f, r.y + r.h / 2.0f}));
  // 条目外 → 不命中
  UT_CHECK(!sb.hit_self({2.0f, 2.0f}));
}

// 3) 其余整页浮层：未打开时不拦截命中
UT_TEST(t34_unopened_overlays_do_not_block_hit) {
  Widget root;
  root.set_bounds({0, 0, kW, kH});

  auto cm = std::make_unique<ContextMenu>();
  cm->set_bounds({0, 0, kW, kH});
  root.add_child(std::move(cm));

  auto au = std::make_unique<scene::AddUrlDialog>();
  au->set_bounds({0, 0, kW, kH});
  root.add_child(std::move(au));

  auto pr = std::make_unique<Probe>();
  pr->set_bounds({10, 10, 60, 24});
  Probe* prp = pr.get();
  root.add_child(std::move(pr));

  click(&root, 40.0f, 22.0f);
  UT_CHECK(prp->down == 1);
  UT_CHECK(prp->up == 1);
}

// 4) 端到端：点击快捷条齿轮 → 触发「打开设置抽屉」回调
//    （实机验证：修复前齿轮点击完全无效，抽屉打不开）
UT_TEST(t34_gear_click_triggers_open_settings) {
  scene::QuickBar qb;
  bool opened = false;
  qb.on_open_settings = [&opened] { opened = true; };
  qb.layout_tree({0, 0, 271.33f, 30.0f});

  // 齿轮命中盒 = 快捷条右端 28×28（quick_bar.cpp:318-321：right-28，btn_h=28）
  const float gx = 271.33f - 8.0f - 14.0f;
  const float gy = 14.0f;

  move_to(&qb, gx, gy);
  click(&qb, gx, gy);
  UT_CHECK(opened);
}
