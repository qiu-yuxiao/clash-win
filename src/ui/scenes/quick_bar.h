#pragma once
// 快捷条（T13）：30px 顶栏——当前节点名 + TEST 组测速按钮 + 图钉 + 齿轮。
// ← clashwin_pixel_perfect.png（窄窗基准）+ census §2
//   （ActiveNodeStatusCard 当前节点卡 + 置顶图钉 + 设置齿轮）。
// 另导出 TakeoverSegmented（MAN/SYS/TUN 三段）：数据口径按任务卡——
// 当前 mode 注入、应用经 patch_clash_mode 回调、失败回弹；场景布局由宿主
// 决定（原版在设置抽屉 takeover-mode-card，T17 复用本组件）。
#include <functional>
#include <string>

#include "../button.h"
#include "../container.h"
#include "../icon_w.h"
#include "../label.h"
#include "../widget.h"

namespace clashwin::ui::scene {

class MainWindow;  // main_window.h（回调宿主）

class QuickBar : public Row {
 public:
  explicit QuickBar(Widget* parent = nullptr);
  ~QuickBar() override;

  void set_node_text(const std::wstring& text);  // 当前节点名（primary 色）
  void on_paint(IRenderTarget& rt) override;     // 背景 + divider 底边

  std::function<void()> on_test_group;    // TEST 按钮（组测速）
  std::function<void()> on_toggle_pin;    // 图钉（T19 置顶窗口）
  std::function<void()> on_open_settings; // 齿轮（T17 设置抽屉占位）

 private:
  Label* node_label_ = nullptr;
};

// MAN/SYS/TUN 三段（数据口径：mode 字符串注入 + apply 回调，失败回弹）
class TakeoverSegmented : public Widget {
 public:
  explicit TakeoverSegmented(Widget* parent = nullptr) : Widget(parent) {}
  ~TakeoverSegmented() override;

  // 当前模式（"MAN"/"SYS"/"TUN"）；外部状态变化时同步（不触发 apply）
  void set_mode(const std::string& m);
  const std::string& mode() const { return mode_; }
  // 应用回调：返回 false → 回弹 + on_apply_failed（← 失败回弹+通知）
  std::function<bool(const std::string& new_mode)> apply;
  std::function<void(const std::string& new_mode)> on_apply_failed;

  SizeF on_measure(float max_w, float max_h) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;

 private:
  int hit_segment(const PointF& local) const;
  std::string mode_ = "MAN";
  int pressed_ = -1;
  int hover_ = -1;
};

}  // namespace clashwin::ui::scene
