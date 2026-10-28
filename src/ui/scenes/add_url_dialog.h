#pragma once
// 自定义分流规则弹窗（T21）：对齐 clashwin_modal_addurl.png（360×290 px）。
// - 字段：规则输入框 (域名/IP) + 粘贴按钮 + 分流策略偏好三段开关 (DIRECT / PROXY / REJECT)
// - 动作：点击"添加"通过 quick_routing 写入 profiles\Merge.yaml 的 prepend-rules 并触发 enhance
// - 交互：Escape 关闭、Enter 提交、点击遮罩外部关闭
// - 绘制：严格遵守 Direct2D 局部坐标规则 {0, y}
#include <functional>
#include <string>

#include "core/quick_routing.h"
#include "ui/widget.h"

namespace clashwin::ui::scene {

class AddUrlDialog : public Widget {
 public:
  explicit AddUrlDialog(Widget* parent = nullptr);
  ~AddUrlDialog() override = default;

  void open();
  void close();
  bool is_open() const { return open_; }

  const std::string& payload() const { return payload_; }
  void set_payload(const std::string& p);

  core::quick_routing::RuleTarget target() const { return target_; }
  void set_target(core::quick_routing::RuleTarget t);

  const std::string& error_message() const { return error_message_; }

  std::function<void(const std::string& rule)> on_rule_added;
  std::function<void()> on_dismiss;

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;

  bool hit_self(PointF local) const override {
    return open_ && Widget::hit_self(local);
  }

  // 几何区域（供单测与命中检测）
  RectF card_rect() const { return card_rect_; }
  RectF input_rect() const { return input_rect_; }
  RectF paste_btn_rect() const { return btn_paste_rect_; }
  RectF seg_rect() const { return seg_rect_; }
  RectF cancel_btn_rect() const { return btn_cancel_rect_; }
  RectF add_btn_rect() const { return btn_add_rect_; }

 private:
  void do_paste();
  void do_submit();

  bool open_ = false;
  std::string payload_ = "openai.com";
  core::quick_routing::RuleTarget target_ = core::quick_routing::RuleTarget::Proxy;
  std::string error_message_;

  RectF card_rect_;
  RectF btn_close_x_rect_;
  RectF input_rect_;
  RectF btn_paste_rect_;
  RectF seg_rect_;
  RectF seg_direct_rect_;
  RectF seg_proxy_rect_;
  RectF seg_reject_rect_;
  RectF btn_cancel_rect_;
  RectF btn_add_rect_;

  bool hover_close_x_ = false;
  bool hover_paste_ = false;
  bool hover_cancel_ = false;
  bool hover_add_ = false;
  bool input_focused_ = false;
};

}  // namespace clashwin::ui::scene
