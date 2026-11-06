#pragma once
// 主场景骨架（T11）：上栏(撑满)+下栏流量区(135/100)的分区骨架 + 区域挂载点。
// ← _layout.tsx:1257-1783（Upper/Lower Pane）。
// - 上栏：快捷条占位 30px（T13 填 ActiveNodeCard）+ 节点列表挂载点（T13）
// - 下栏：MiniTrafficPanel 挂载点（T14），高 135（窄窗 100，← TRAFFIC_PANE_*）
// - 极小窗（Mini）：上栏真实卸载（remove_child，React unmount 语义），
//   恢复时重新挂载；流量条恒在
// - 内容绘制骨架：上/下栏浅底色占位（T12 主题接入后换 ThemeParams）
#include <string>

#include "breakpoints.h"
#include "container.h"
#include "widget.h"

namespace clashwin::ui::win {

class MainScene : public Widget {
 public:
  explicit MainScene(Widget* parent = nullptr);
  ~MainScene() override;

  // 断点变化应用（MainWindow 订阅 Breakpoints 后转发；含卸载/挂载决策）
  void apply_state(WinState prev, WinState cur);
  void set_narrow(bool narrow);  // 流量条 135/100

  // 挂载点（T13/T14 填充；list_slot 极小窗期间为 nullptr）
  Widget* list_slot();
  Widget* traffic_slot();
  Widget* quickbar_slot();  // 顶栏 30px 快捷条挂载点（T13）
  // T13 场景挂载（所有权留在宿主；Mini 卸载随 upper_ 整体 remove_child）
  // 仅接受 unique_ptr：裸指针重载会静默接管所有权，易与调用方双重释放，已移除。
  void attach_quick_bar(std::unique_ptr<Widget> w) { if (quickbar_) quickbar_->add_child(std::move(w)); }
  void attach_node_list(std::unique_ptr<Widget> w) { if (list_slot_) list_slot_->add_child(std::move(w)); }

  void on_paint(IRenderTarget& rt) override;
  void on_arrange(const RectF& area) override;

 private:
  void mount_upper();    // add_child 恢复
  void unmount_upper();  // remove_child（React unmount 等价）

  Column* upper_ = nullptr;    // 上栏（quickbar + list slot）
  Box* quickbar_ = nullptr;    // 30px 快捷条占位
  Stack* list_slot_ = nullptr; // 节点列表挂载点
  Stack* traffic_ = nullptr;   // 流量条挂载槽（135/100）
  std::unique_ptr<Widget> taken_upper_;  // 卸载期间保活（重新挂载用）
  bool upper_mounted_ = true;
  bool narrow_ = false;
};

}  // namespace clashwin::ui::win
