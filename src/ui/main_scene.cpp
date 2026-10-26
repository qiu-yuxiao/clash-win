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
  qb->set_padding(0, 0, 0, 0);
  quickbar_ = qb.get();
  quickbar_->flex_weight = 0;
  upper_->add_child(std::move(qb));
  auto slot = std::make_unique<Stack>();
  list_slot_ = slot.get();
  list_slot_->flex_weight = 1;
  upper_->add_child(std::move(slot));
  add_child(std::move(upper));

  // 下栏：流量占位（T14）
  auto traffic = std::make_unique<Box>();
  traffic_ = traffic.get();
  add_child(std::move(traffic));
}

MainScene::~MainScene() = default;

Widget* MainScene::list_slot() { return upper_mounted_ ? list_slot_ : nullptr; }
Widget* MainScene::traffic_slot() { return traffic_; }
Widget* MainScene::quickbar_slot() {
  return upper_mounted_ ? quickbar_ : nullptr;
}

void MainScene::apply_state(WinState prev, WinState cur) {
  // ← WindowProvider applyGeometry：离开极小窗 → 恢复标题栏/挂载；
  //   进入极小窗 → 卸载上栏（数据精简 + DOM 卸载）
  if (cur == WinState::Mini && upper_mounted_) {
    unmount_upper();
  } else if (prev == WinState::Mini && cur != WinState::Mini &&
             !upper_mounted_) {
    mount_upper();
  }
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
  // content 布局：上栏撑满剩余（flex 1），下栏固定 135/100（← TRAFFIC_PANE_*）
  // 【坐标约定】子 layout_tree 传本 widget 局部坐标（x 从 0 起）
  set_bounds(area);
  int traffic_h = narrow_ ? kTrafficPaneMinimal : kTrafficPaneWide;
  float w = area.w;
  for (size_t i = 0; i < child_count(); ++i) {
    Widget* c = child(i);
    if (c == traffic_) {
      c->layout_tree({0, area.h - static_cast<float>(traffic_h), w,
                      static_cast<float>(traffic_h)});
    } else {
      c->layout_tree({0, 0, w, area.h - static_cast<float>(traffic_h)});
    }
  }
}

void MainScene::on_paint(IRenderTarget& rt) {
  // 占位底色：tauri 背景色口径（← dt.background_color，T13 场景填充后替换）
  rt.fill_rect(bounds(), Brush{theme::ThemeService::instance()
                                   .palette()
                                   .background_color});
}

}  // namespace clashwin::ui::win
