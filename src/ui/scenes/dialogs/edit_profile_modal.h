#pragma once
// EditProfileModal —— 配置编辑弹窗（拆分自 utility_modals.h）
// src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx:1-192
#include <algorithm>
#include <functional>
#include <string>

#include "ui/context_menu.h"
#include "ui/overlay_fade.h"
#include "ui/textfield.h"
#include "ui/theme/theme.h"
#include "ui/widget.h"

namespace clashwin::ui::scene {

// src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx:1-192
class EditProfileModal : public Widget {
 public:
  explicit EditProfileModal(Widget* parent = nullptr);
  ~EditProfileModal() override = default;

  void open(const std::string& name, const std::string& url, int interval_mins,
            bool is_local = false);
  void close();
  bool is_open() const { return open_; }

  const std::string& name() const { return name_; }
  const std::string& url() const { return url_; }
  int interval_mins() const { return interval_mins_; }
  bool is_local() const { return is_local_; }

  void set_name(const std::string& n);
  void set_url(const std::string& u);
  void set_interval_mins(int mins);

  std::function<void(const std::string& name, const std::string& url, int interval)> on_save;
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
  bool a11y_subtree_visible() const override { return open_ && visible() && !a11y_hidden(); }
  bool a11y_is_modal() const override { return open_; }
  A11yRole a11y_default_role() const override { return A11yRole::Dialog; }
  std::wstring a11y_default_name() const override;
  size_t a11y_virtual_child_count() const override { return is_local_ ? 3u : 5u; }
  A11ySubItem a11y_virtual_child(size_t idx) const override;
  bool a11y_invoke_virtual_child(size_t idx) override;
  bool a11y_set_virtual_child_value(size_t idx, const std::wstring& v) override;

  RectF card_rect() const { return card_rect_; }
  RectF save_btn_rect() const { return btn_save_rect_; }
  TextField* name_field() const { return name_field_; }
  TextField* url_field() const { return url_field_; }
  TextField* interval_field() const { return interval_field_; }

 private:
  void trigger_save();
  void open_field_menu(TextField* tf, PointF anchor);

  bool open_ = false;
  OverlayFade fade_;  // ← MUI Dialog 缺省 Fade（enter 225 / leave 195）
  bool is_local_ = false;
  bool syncing_fields_ = false;
  std::string name_;
  std::string url_;
  int interval_mins_ = 1440;  // src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx

  TextField* name_field_ = nullptr;
  TextField* url_field_ = nullptr;
  TextField* interval_field_ = nullptr;
  ContextMenu* field_menu_ = nullptr;

  RectF card_rect_;
  RectF name_input_rect_;
  RectF url_input_rect_;
  RectF interval_input_rect_;
  RectF btn_cancel_rect_;
  RectF btn_save_rect_;

  bool hover_cancel_ = false;
  bool hover_save_ = false;
  int focused_field_ = 0;  // 0: none, 1: name, 2: url, 3: interval
};

}  // namespace clashwin::ui::scene
