#pragma once
// ClientUpdateModal —— 客户端更新弹窗（拆分自 utility_modals.h）
// src: src/pages/_layout/components/dialogs/client-update-dialog.tsx:1-200
#include <algorithm>
#include <functional>
#include <string>

#include "ui/overlay_fade.h"
#include "ui/theme/theme.h"
#include "ui/widget.h"

namespace clashwin::ui::scene {

// src: src/pages/_layout/components/dialogs/client-update-dialog.tsx:1-200
class ClientUpdateModal : public Widget {
 public:
  explicit ClientUpdateModal(Widget* parent = nullptr);
  ~ClientUpdateModal() override;

  void open(const std::string& current_ver, const std::string& latest_ver,
            const std::string& changelog, const std::string& download_url = "");
  void close();
  bool is_open() const { return open_; }

  const std::string& current_version() const { return current_version_; }
  const std::string& latest_version() const { return latest_version_; }
  const std::string& changelog() const { return changelog_; }
  const std::string& download_url() const { return download_url_; }
  int progress() const { return progress_; }
  void set_progress(int p, const std::string& msg = "");

  // src: src/pages/_layout/hooks/use-client-update.ts:73-105
  void handle_progress_event(const std::string& status, int progress, const std::string& msg);

  bool is_downloading() const { return is_downloading_; }
  void set_downloading(bool dl) { is_downloading_ = dl; invalidate(); }

  std::function<void()> on_start_update;
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
  RectF update_btn_rect() const { return btn_update_rect_; }

 private:
  void subscribe_events();
  void unsubscribe_events();

  bool open_ = false;
  OverlayFade fade_;  // ← MUI Dialog 缺省 Fade（enter 225 / leave 195）
  bool is_downloading_ = false;
  std::string current_version_;
  std::string latest_version_;
  std::string changelog_;
  std::string download_url_;
  std::string status_ = "idle";
  int progress_ = 0;  // src: src/pages/_layout/components/dialogs/client-update-dialog.tsx
  std::string status_msg_;
  uint64_t listener_id_ = 0;
  float changelog_scroll_y_ = 0.0f;  // src: src/pages/_layout/components/dialogs/client-update-dialog.tsx
  float changelog_text_h_ = 0.0f;

  RectF card_rect_;
  RectF btn_close_x_rect_;
  RectF changelog_box_rect_;
  RectF progress_bar_rect_;
  RectF btn_close_rect_;
  RectF btn_update_rect_;

  bool hover_close_x_ = false;
  bool hover_close_ = false;
  bool hover_update_ = false;
};

}  // namespace clashwin::ui::scene
