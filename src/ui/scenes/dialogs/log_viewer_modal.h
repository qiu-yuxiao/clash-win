#pragma once
// LogViewerModal —— 运行日志查看弹窗（拆分自 utility_modals.h）
// src: src/pages/_layout/components/dialogs/logs-dialog.tsx:1-73
#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "ui/context_menu.h"
#include "ui/i18n_text.h"
#include "ui/overlay_fade.h"
#include "ui/textfield.h"
#include "ui/theme/theme.h"
#include "ui/widget.h"

namespace clashwin::ui::scene {

struct LogEntry {
  std::string level;  // "debug", "info", "warn", "error"
  std::string time;
  std::string message;
};

// src: src/pages/_layout/components/dialogs/logs-dialog.tsx:1-73
class LogViewerModal : public Widget {
 public:
  static constexpr size_t kMaxLogLines = 1000;
  static constexpr uint32_t kFlushDelayMs = 50;

  explicit LogViewerModal(Widget* parent = nullptr);
  ~LogViewerModal() override = default;

  void open();
  void close();
  bool is_open() const { return open_; }

  void push_log(const std::string& level, const std::string& msg, const std::string& time_str = "");
  void push_log_line(const std::string& raw_line);
  void flush_incoming();

  size_t log_count() const { return logs_.size(); }
  size_t incoming_count() const { return incoming_queue_.size(); }
  void clear_logs();

  // src: src/pages/logs.tsx:116-142
  std::wstring tooltip_text() const override {
    if (hover_pause_) return paused_ ? tr("shared.actions.resume") : tr("shared.actions.pause");
    if (hover_order_) return desc_ ? tr("logs.actions.showAscending") : tr("logs.actions.showDescending");
    if (hover_clear_) return tr("shared.actions.clear");
    return {};
  }

  bool is_paused() const { return paused_; }
  void set_paused(bool p);

  bool is_desc() const { return desc_; }
  void set_desc(bool d);

  const std::string& level_filter() const { return level_filter_; }
  void set_level_filter(const std::string& lvl);

  const std::string& search_query() const { return search_query_; }
  void set_search_query(const std::string& q);
  TextField* search_field() const { return search_field_; }

  std::function<void()> on_dismiss;
  std::function<void(bool paused)> on_pause_changed;

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
  size_t a11y_virtual_child_count() const override { return 6u; }
  A11ySubItem a11y_virtual_child(size_t idx) const override;
  bool a11y_invoke_virtual_child(size_t idx) override;
  bool a11y_set_virtual_child_value(size_t idx, const std::wstring& v) override;

  RectF card_rect() const { return card_rect_; }
  RectF log_area_rect() const { return log_area_rect_; }
  float scroll_y() const { return scroll_y_; }
  void set_scroll_y(float y);
  float max_scroll() const;
  bool stick_to_bottom() const { return stick_to_bottom_; }

 private:
  bool matches_filter(const LogEntry& entry) const;
  float measure_entry_height(const LogEntry& entry, float item_w) const;
  void recompute_total_height();
  void open_search_menu(PointF anchor);

  bool open_ = false;
  OverlayFade fade_;  // ← MUI Dialog 缺省 Fade（enter 225 / leave 195）
  bool paused_ = false;
  bool desc_ = false;
  std::string level_filter_ = "all";  // all, debug, info, warn, error
  MenuList* lvl_menu_ = nullptr;      // src: src/pages/logs.tsx:179-191
  TextField* search_field_ = nullptr;
  ContextMenu* search_menu_ = nullptr;
  bool syncing_search_ = false;
  std::string search_query_;

  std::deque<LogEntry> logs_;
  std::vector<LogEntry> incoming_queue_;
  std::chrono::steady_clock::time_point last_flush_time_;

  float scroll_y_ = 0.0f;
  float total_content_h_ = 0.0f;
  bool stick_to_bottom_ = true;
  float line_height_ = 20.0f;  // src: src/pages/logs.tsx:227

  RectF card_rect_;
  RectF btn_close_x_rect_;
  RectF toolbar_rect_;
  RectF btn_pause_rect_;
  RectF btn_order_rect_;
  RectF btn_clear_rect_;
  RectF seg_level_rect_;
  RectF search_box_rect_;
  RectF log_area_rect_;

  bool hover_close_x_ = false;
  bool hover_pause_ = false;
  bool hover_order_ = false;
  bool hover_clear_ = false;
};

}  // namespace clashwin::ui::scene
