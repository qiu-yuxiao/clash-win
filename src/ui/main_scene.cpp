// 主场景骨架实现（T11）。
#include "main_scene.h"

#include <algorithm>
#include <memory>

#include "theme/theme.h"

namespace clashwin::ui::win {

MainScene::MainScene(Widget* parent) : Widget(parent) {
  // 上栏：quickbar 30px + list slot（flex 撑满）
  auto upper = std::make_unique<Column>();
  upper_ = upper.get();
  auto qb = std::make_unique<Box>();
  // src: _layout.tsx:1314-1322 仅有一层容器，padding 由该层承担；
  //   本 Box 只是 30px 承载槽，零内距与源码等价（内距见 quick_bar.cpp set_padding）
  qb->set_padding(0, 0, 0, 0);
  qb->set_z_order(9998);  // src: _layout.tsx:1320「zIndex: 9998」
  quickbar_ = qb.get();
  quickbar_->flex_weight = 0;
  upper_->add_child(std::move(qb));
  auto slot = std::make_unique<Stack>();
  list_slot_ = slot.get();
  list_slot_->flex_weight = 1;
  upper_->add_child(std::move(slot));
  add_child(std::move(upper));

  // 下栏：流量面板（T14）挂载槽（135/100，铺满语义由 on_arrange 负责）
  auto traffic = std::make_unique<Stack>();
  traffic_ = traffic.get();
  add_child(std::move(traffic));
}

MainScene::~MainScene() = default;

Widget* MainScene::list_slot() { return list_slot_; }
Widget* MainScene::traffic_slot() { return traffic_; }
Widget* MainScene::quickbar_slot() { return quickbar_; }

void MainScene::apply_state(WinState prev, WinState cur) {
  (void)prev;
  mini_ = (cur == WinState::Mini);
  if (list_slot_) {
    list_slot_->set_visible(!mini_);
  }
  invalidate();
}

void MainScene::set_narrow(bool narrow) {
  if (narrow_ == narrow) return;
  narrow_ = narrow;
  traffic_->flex_weight = 0;  // 高度由 on_arrange 按窄窗钳制
  invalidate();
}

void MainScene::unmount_upper() {
  // 真实卸载（remove_child 返回 unique_ptr 由本类保活，恢复时重新挂载）
  for (size_t i = 0; i < child_count(); ++i) {
    if (child(i) == upper_) {
      taken_upper_ = remove_child(upper_);  // 保活持有
      upper_mounted_ = false;
      invalidate();
      return;
    }
  }
}

void MainScene::mount_upper() {
  if (upper_mounted_ || !taken_upper_) return;
  insert_child(0, std::move(taken_upper_));  // 保序挂回（upper 在 traffic 前）
  upper_mounted_ = true;
  invalidate();
}

void MainScene::on_arrange(const RectF& area) {
  set_bounds(area);
  float w = area.w;
  if (mini_) {
    // 极小窗模式：上栏固定为 quickbar 高度 30px，下栏流量条占满剩余空间
    if (upper_) {
      upper_->layout_tree({0, 0, w, 30.0f});
    }
    if (traffic_) {
      traffic_->layout_tree({0, 30.0f, w, std::max(0.0f, area.h - 30.0f)});
    }
  } else {
    int traffic_h = narrow_ ? kTrafficPaneMinimal : kTrafficPaneWide;
    float upper_h = std::max(0.0f, area.h - static_cast<float>(traffic_h));
    if (upper_) {
      upper_->layout_tree({0, 0, w, upper_h});
    }
    if (traffic_) {
      traffic_->layout_tree({0, upper_h, w, static_cast<float>(traffic_h)});
    }
  }
}

void MainScene::on_paint(IRenderTarget& rt) {
  // 占位底色：tauri 背景色口径（← dt.background_color，T13 场景填充后替换）
  // 【T17-fix】on_paint 局部坐标约定：画 {0,0,w,h}（画 bounds() 会在
  //   scene 不从 y=0 起时双重偏移——root 改 Column 流式后 scene y=30 暴露）
  // src: _layout.tsx:1773 「background: 'inherit'」→ 继承 .layout 的 --theme-bg
  //   （assets/styles/layout.scss:8 background: var(--theme-bg) !important）
  rt.fill_rect({0, 0, bounds().w, bounds().h},
               Brush{theme::ThemeService::instance().vars().theme_bg});
}

}  // namespace clashwin::ui::win
