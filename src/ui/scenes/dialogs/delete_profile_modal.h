#pragma once
// DeleteProfileConfirmModal —— 删除配置二次确认弹窗（BUG-390，拆分自 utility_modals.h）
// src: src/pages/_layout/components/dialogs/delete-profile-dialog.tsx:1-102
#include <algorithm>
#include <functional>
#include <string>

#include "ui/overlay_fade.h"
#include "ui/theme/theme.h"
#include "ui/widget.h"

namespace clashwin::ui::scene {

// src: src/pages/_layout/components/dialogs/delete-profile-dialog.tsx:1-102
class DeleteProfileConfirmModal : public Widget {
 public:
  explicit DeleteProfileConfirmModal(Widget* parent = nullptr);

  void open(const std::string& uid, const std::string& profile_name);
  void close();
  bool is_open() const { return open_; }

  const std::string& uid() const { return uid_; }
  const std::string& profile_name() const { return profile_name_; }

  std::function<void(const std::string& uid)> on_confirm;
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
  size_t a11y_virtual_child_count() const override { return 3u; }
  A11ySubItem a11y_virtual_child(size_t idx) const override;
  bool a11y_invoke_virtual_child(size_t idx) override;

  RectF card_rect() const { return card_rect_; }
  RectF cancel_btn_rect() const { return btn_cancel_rect_; }
  RectF delete_btn_rect() const { return btn_delete_rect_; }

 private:
  bool open_ = false;
  OverlayFade fade_;
  std::string uid_;
  std::string profile_name_;

  RectF card_rect_{};
  RectF btn_cancel_rect_{};
  RectF btn_delete_rect_{};

  bool hover_cancel_ = false;
  bool hover_delete_ = false;
  bool pressed_cancel_ = false;
  bool pressed_delete_ = false;
};

}  // namespace clashwin::ui::scene
