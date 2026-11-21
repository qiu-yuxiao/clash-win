#pragma once
// CoreUpdateModal —— 内核升级弹窗（拆分自 utility_modals.h）
// src: src/pages/_layout/components/dialogs/core-update-dialog.tsx:1-180
#include <algorithm>
#include <functional>
#include <string>

#include "ui/overlay_fade.h"
#include "ui/theme/theme.h"
#include "ui/widget.h"

namespace clashwin::ui::scene {

// src: src/pages/_layout/components/dialogs/core-update-dialog.tsx:1-180
class CoreUpdateModal : public Widget {
 public:
  explicit CoreUpdateModal(Widget* parent = nullptr);
  ~CoreUpdateModal() override;

  void open(const std::string& current_ver, const std::string& latest_ver);
  void close();
  bool is_open() const { return open_; }

  const std::string& current_version() const { return current_version_; }
  const std::string& latest_version() const { return latest_version_; }
  int progress() const { return progress_; }
  const std::string& status() const { return status_; }

  void set_external_event_driver(bool ext);
  bool external_event_driver() const { return external_event_driver_; }

  void handle_progress_event(const std::string& status, int progress, const std::string& msg);

  std::function<void()> on_start_upgrade;
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
  size_t a11y_virtual_child_count() const override { return 3u; }
  A11ySubItem a11y_virtual_child(size_t idx) const override;
  bool a11y_invoke_virtual_child(size_t idx) override;

  RectF card_rect() const { return card_rect_; }
  RectF upgrade_btn_rect() const { return btn_upgrade_rect_; }

 private:
  void subscribe_events();
  void unsubscribe_events();

  bool open_ = false;
  bool external_event_driver_ = false;
  OverlayFade fade_;  // ← MUI Dialog 缺省 Fade（enter 225 / leave 195）
  bool is_upgrading_ = false;
  std::string current_version_ = "v1.19.27";
  std::string latest_version_ = "v1.19.28";
  int progress_ = 0;  // src: src/pages/_layout/components/dialogs/core-update-dialog.tsx
  std::string status_ = "idle";
  std::string message_;
  uint64_t listener_id_ = 0;

  RectF card_rect_;
  RectF btn_close_x_rect_;
  // 版本信息盒（← src/pages/_layout/components/dialogs/core-update-dialog.tsx）
  RectF meta_box_rect_;
  RectF progress_bar_rect_;
  RectF btn_close_rect_;
  RectF btn_upgrade_rect_;

  bool hover_close_x_ = false;
  bool hover_close_ = false;
  bool hover_upgrade_ = false;
};

}  // namespace clashwin::ui::scene
