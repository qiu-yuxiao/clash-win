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
#include "ui/context_menu.h"
#include "ui/overlay_fade.h"
#include "ui/textfield.h"
#include "ui/theme/theme.h"
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

  void set_context_menu(ContextMenu* menu) { context_menu_ = menu; }
  void open_context_menu(PointF anchor);

  // 策略偏好（源码 addQuickRoutingRules 的默认行为；保留内部状态兼容旧调用）
  core::quick_routing::RuleTarget target() const { return target_; }
  void set_target(core::quick_routing::RuleTarget t);

  const std::string& error_message() const { return error_message_; }

  static void set_clipboard_reader_for_test(std::function<std::wstring()> fn);

  std::function<void(const std::string& rule)> on_rule_added;
  std::function<void()> on_dismiss;

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;

  // src: src/utils/popup-styles.ts:173-185 窄窗弹窗锁定左侧 271px
  bool hit_self(PointF local) const override {
    return open_ && local.x < std::min(theme::kPopupPaneWidth, bounds().w) &&
           Widget::hit_self(local);
  }
  bool a11y_subtree_visible() const override {
    return open_ && visible() && !a11y_hidden();
  }
  bool a11y_is_modal() const override { return open_; }
  A11yRole a11y_default_role() const override { return A11yRole::Dialog; }
  std::wstring a11y_default_name() const override;
  size_t a11y_virtual_child_count() const override { return 3; }
  A11ySubItem a11y_virtual_child(size_t idx) const override;
  bool a11y_invoke_virtual_child(size_t idx) override;
  bool a11y_set_virtual_child_value(size_t idx, const std::wstring& v) override;

  // 几何区域（供单测与命中检测）
  RectF card_rect() const { return card_rect_; }
  RectF title_rect() const { return title_rect_; }
  RectF input_rect() const { return input_rect_; }
  RectF cancel_btn_rect() const { return btn_cancel_rect_; }
  RectF add_btn_rect() const { return btn_add_rect_; }
  TextField* url_field() const { return url_field_; }

 private:
  void do_submit();
  void copy_payload_to_clipboard();

  bool open_ = false;
  bool syncing_field_ = false;
  // ← MUI Dialog 缺省 Fade 过渡（enter 225ms / leave 195ms）
  OverlayFade fade_;
  std::string payload_;
  core::quick_routing::RuleTarget target_ = core::quick_routing::RuleTarget::Proxy;
  std::string error_message_;
  TextField* url_field_ = nullptr;
  ContextMenu* context_menu_ = nullptr;
  bool all_selected_ = false;

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
