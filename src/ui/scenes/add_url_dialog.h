#pragma once
// 添加网址弹窗（T21，按 Mini 3.2.2 源码 add-url-dialog.tsx 重做；不再按 png 反推）。
// 结构（src: src/pages/_layout/components/add-url-dialog.tsx）：
//   MUI Dialog（:189-205）：paper = get3DCardStyle('default')、p0、overflow hidden、
//     width calc(100%-32px)、maxWidth 380；默认 backdrop rgba(0,0,0,.5)。
//   DialogTitle（:206-216）：15px bold、px 2(16)、py 1.5(12)。
//   DialogContent（:217-234）：px 2、pb 1；TextField multiline minRows 4 maxRows 10
//     fullWidth，样式 get3DInputStyle。
//   DialogActions（:235-250）：px 2、py 1.5、gap 1(8)；取消 = outlined default，
//     添加 = contained primary。
// 无 ✕ 按钮、无策略三段、无粘贴按钮（源码均无）；右键菜单为输入框上下文菜单
// （:253-324 剪切/复制/粘贴/全选/清空）——待后续实现。
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

  // 策略偏好（源码 addQuickRoutingRules 的默认行为；保留内部状态兼容旧调用）
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
  RectF title_rect() const { return title_rect_; }
  RectF input_rect() const { return input_rect_; }
  RectF cancel_btn_rect() const { return btn_cancel_rect_; }
  RectF add_btn_rect() const { return btn_add_rect_; }

 private:
  void do_submit();

  bool open_ = false;
  std::string payload_;
  core::quick_routing::RuleTarget target_ = core::quick_routing::RuleTarget::Proxy;
  std::string error_message_;

  RectF card_rect_;
  RectF title_rect_;
  RectF input_rect_;
  RectF btn_cancel_rect_;
  RectF btn_add_rect_;

  bool hover_cancel_ = false;
  bool hover_add_ = false;
  bool input_focused_ = false;
};

}  // namespace clashwin::ui::scene
