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
  // src: src/pages/_layout.tsx:526-535 仅有一层容器，padding 由该层承担；
  //   本 Box 只是 30px 承载槽，零内距与源码等价（内距见 quick_bar.cpp set_padding）
  qb->set_padding(0, 0, 0, 0);
  qb->set_z_order(9998);  // src: src/pages/_layout.tsx:534「zIndex: 9998」
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
    // 极小窗模式（对齐 Clash Mini _layout.tsx:1268-1277 与 :1765-1785）：
    // 刚手动调至最低窗口（titlebar 可见，内容区高 ~96px）时，upper_ 占位折叠为 0（隐藏），
    // 完整流量曲线占满全高；静置标题栏自动隐藏后（内容区高 126px），upper_ 展开 30px 显示活跃节点。
    const bool show_upper = (area.h >= 110.0f);
    if (upper_) {
      upper_->set_visible(show_upper);
      upper_->layout_tree({0, 0, w, show_upper ? 30.0f : 0.0f});
    }
    if (traffic_) {
      traffic_->set_visible(true);
      const float traffic_y = show_upper ? 30.0f : 0.0f;
      traffic_->layout_tree({0, traffic_y, w, std::max(0.0f, area.h - traffic_y)});
    }
  } else {
    // src: src/pages/_layout.tsx:483-489, 682-697 Upper flex: '1 1 0%', minHeight: 0; Lower flex: 0 1 135px, maxHeight: 100%
    const float avail_h = (std::max)(0.0f, area.h);
    const float target_traffic_h =
        static_cast<float>(narrow_ ? kTrafficPaneMinimal : kTrafficPaneWide);
    const float traffic_h = (std::min)(avail_h, target_traffic_h);
    const float upper_h = (std::max)(0.0f, avail_h - traffic_h);
    if (upper_) {
      upper_->set_visible(true);
      upper_->layout_tree({0, 0, w, upper_h});
    }
    if (traffic_) {
      traffic_->set_visible(true);
      traffic_->layout_tree({0, upper_h, w, traffic_h});
    }
  }
}

void MainScene::on_paint(IRenderTarget& rt) {
  // src: src/pages/_layout.tsx:466-478 .layout-content 透明承接 .layout theme_bg 与 .layout::before 极光
  // 底色由 MainWindow::paint_window 先铺 theme_bg + 极光，本层不铺不透明底色避免遮挡极光
  (void)rt;
}

}  // namespace clashwin::ui::win
