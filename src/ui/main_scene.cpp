// 主场景骨架实现（T11）。
#include "main_scene.h"

#include <algorithm>
#include <memory>

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
  set_bounds(area);
  RectF content{area.x, area.y, area.w, area.h};
  int traffic_h = narrow_ ? kTrafficPaneMinimal : kTrafficPaneWide;
  for (size_t i = 0; i < child_count(); ++i) {
    Widget* c = child(i);
    if (c == traffic_) {
      c->layout_tree({content.x, content.bottom() - traffic_h, content.w,
                      static_cast<float>(traffic_h)});
    } else {
      // 上栏（或卸载态的其他子）：撑满流量条上方
      c->layout_tree({content.x, content.y, content.w,
                      content.h - static_cast<float>(traffic_h)});
    }
  }
}

void MainScene::on_paint(IRenderTarget& rt) {
  // 骨架占位底色（T12 接 ThemeParams：light #F5F5F5 / dark #2E303D）
  rt.fill_rect(bounds(), Brush{Color4f::hex(0xF5F5F5)});
}

}  // namespace clashwin::ui::win
