#pragma once
// 快捷条（T13）：30px 顶栏（src: _layout.tsx:1318 height:'30px'）。
//   容器无背景声明（透明，src: _layout.tsx:1310-1322）；
//   选中卡 28px 高 + 135° 四站渐变 + 圆角 6
//   （src: active-node-card.tsx:241、button-styles.ts:538-539/549）；
//   内容按 Mini flex 顺序：「活跃节点：」标签 → 协议胶囊 → 12px 粗体节点名
//   → 延迟 Chip → (地址)，gap 12（src: active-node-card.tsx:240/249/267/288/359）；
//   右两枚 28×28 3D 按钮（src: _layout.tsx:1345-1346 / 1378-1379）。
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
#include "ui/i18n_text.h"  // F-D01

namespace clashwin::ui::scene {

class MainWindow;  // main_window.h（回调宿主）

class QuickBar : public Row {
 public:
  explicit QuickBar(Widget* parent = nullptr);
  ~QuickBar() override;

  void set_node_text(const std::wstring& text);  // 当前节点名（12px 粗体）
  // 协议类型（mihomo type，如 "ss"/"vmess"）+ 节点地址 "server:port"
  // src: active-node-card.tsx:278 / :356-369
  void set_node_meta(const std::string& type, const std::wstring& addr);
  // 徽章延迟数：has_node=false 隐藏；-1 = 未测（Mini 仍显示 Chip「-」）
  // src: active-node-card.tsx:311-344（activeNodeName && Chip；delay===-1 → formatDelay '-'）
  void set_badge(int delay, bool has_node = true);
  // 延迟 Chip 命中区（on_arrange 计算；测试/宿主查点击位置用）
  const RectF& chip_rect() const { return chip_rect_; }
  void set_pinned(bool p);    // 图钉激活态（深棕色）
  SizeF on_measure(float max_w, float max_h) override;  // 固定高 30
  // 卡片内容：标签/协议/名称/延迟/地址，窄窗按 580/480/400 断点隐藏
  void on_arrange(const RectF& area) override;
  // 节点名 hover/active 着色 + 点击轮换（src: active-node-card.tsx:284-301）
  bool on_mouse(const MouseEvent& e) override;
  void on_paint(IRenderTarget& rt) override;  // 渐变卡/协议胶囊/徽章胶囊/钮底

  // src: active-node-card.tsx:281 仅节点名文本区域显示 rotateNodeTooltip
  std::wstring tooltip_text() const override {
    return (node_hover_ && node_label_ && !node_label_->text.empty())
               ? std::wstring(tr("settings.mini.rotateNodeTooltip"))
               : std::wstring();
  }
  std::optional<RectF> tooltip_anchor_rect() const override {
    if (node_hover_ && node_label_) return node_label_->global_bounds();
    return std::nullopt;
  }

  A11yRole a11y_default_role() const override { return A11yRole::ToolBar; }
  size_t a11y_virtual_child_count() const override;
  A11ySubItem a11y_virtual_child(size_t index) const override;
  bool a11y_invoke_virtual_child(size_t index) override;

  std::function<void()> on_toggle_pin;    // 图钉（T19 置顶窗口）
  std::function<void()> on_open_settings; // 齿轮（T17 设置抽屉占位）
  std::function<void()> on_cycle_node;    // 点击节点名 → 轮换下一个
  // 点击延迟 Chip → 单点测速（src: active-node-card.tsx:311-344 onClick={handleTestDelay}）
  std::function<void()> on_test_delay;

 private:
  Label* node_label_ = nullptr;
  Label* active_label_ = nullptr;  // 「活跃节点：」（≤580 隐藏）
  Label* proto_label_ = nullptr;   // 协议胶囊文字（≤480 隐藏）
  Label* addr_label_ = nullptr;    // "(server:port)"（≤400 隐藏）
  IconW* wifi_ = nullptr;
  Label* delay_label_ = nullptr;
  IconW* pin_ = nullptr;
  IconW* gear_ = nullptr;
  bool has_badge_ = false;
  bool node_hover_ = false;    // 节点名 hover（src: :296-298）
  bool node_pressed_ = false;  // 节点名按下（src: :299-301）
  bool chip_pressed_ = false;  // 延迟 Chip 按下（可点测速）
  RectF chip_rect_{};          // 延迟 Chip 命中区（on_arrange 记录）
  int delay_ = -1;  // 当前延迟（Chip 五档色/文本）
  bool pinned_ = false;
  bool has_proto_ = false;  // 有协议类型 → 胶囊可见
  RectF proto_pill_{};      // 协议胶囊外框（on_paint 描边/铺底）
  // src: _layout.tsx:1365-1366 transition 'transform 0.2s ease, color 0.2s ease'
  float pin_rot_ = 0.0f;       // 当前图钉旋转角（度）
  float pin_rot_from_ = 0.0f;  // 过渡起点
  float pin_rot_to_ = 0.0f;    // 过渡终点
  double pin_anim_start_ms_ = 0;  // 过渡起始时刻（0 = 无过渡）
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

  // src: locales/zh/settings.json:544-546 takeoverTooltipManual/System/Tun
  std::wstring tooltip_text() const override {
    if (hover_ == 0) return tr("settings.mini.takeoverTooltipManual");
    if (hover_ == 1) return tr("settings.mini.takeoverTooltipSystem");
    if (hover_ == 2) return tr("settings.mini.takeoverTooltipTun");
    return {};
  }
  // src: takeover-mode-card.tsx:95-246 <Tooltip placement="top" arrow> 锚定当前悬停分段
  std::optional<RectF> tooltip_anchor_rect() const override {
    if (hover_ < 0 || hover_ >= 3 || bounds().w <= 0.0f) return std::nullopt;
    const RectF g = global_bounds();
    const float seg_w = g.w / 3.0f;
    return RectF{g.x + static_cast<float>(hover_) * seg_w, g.y, seg_w, g.h};
  }
  bool tooltip_prefer_top_arrow() const override { return true; }

  A11yRole a11y_default_role() const override { return A11yRole::Group; }
  size_t a11y_virtual_child_count() const override { return 3; }
  A11ySubItem a11y_virtual_child(size_t index) const override;
  bool a11y_invoke_virtual_child(size_t index) override;

 private:
  int hit_segment(const PointF& local) const;
  std::string mode_ = "MAN";
  int pressed_ = -1;
  int hover_ = -1;
};

}  // namespace clashwin::ui::scene
