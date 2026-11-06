#pragma once
// 实用工具弹窗族（T21）：对齐 Clash Mini v3.2.1 layout-dialogs 与日志查看器。
// 1. EditProfileModal: 名称、URL、更新间隔（分）编辑与保存
// 2. ClientUpdateModal: 版本检查、更新日志展示、进度条与立即更新
// 3. CoreUpdateModal: 内核版本对比、EventBus "core-upgrade-progress" 监听 (5->100)
// 4. LogViewerModal: 50ms 批处理刷新、1000 行环形截断、级别过滤、搜索、暂停/继续
// - 严格遵守 Direct2D 局部坐标规则 {0, y}
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "events/event_bus.h"
#include "ui/context_menu.h"
#include "ui/widget.h"
#include "ui/i18n_text.h"  // F-D01

namespace clashwin::ui::scene {

// ==========================================
// 1. EditProfileModal（配置编辑弹窗）
//   src: layout-dialogs.tsx:119-245（Mini 原生 Dialog：名称/订阅地址/更新周期）
// ==========================================
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

  void set_name(const std::string& n) { name_ = n; invalidate(); }
  void set_url(const std::string& u) { url_ = u; invalidate(); }
  void set_interval_mins(int mins) { interval_mins_ = mins; invalidate(); }

  std::function<void(const std::string& name, const std::string& url, int interval)> on_save;
  std::function<void()> on_dismiss;

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;

  bool hit_self(PointF local) const override { return open_ && Widget::hit_self(local); }

  RectF card_rect() const { return card_rect_; }
  RectF save_btn_rect() const { return btn_save_rect_; }

 private:
  bool open_ = false;
  bool is_local_ = false;
  std::string name_;
  std::string url_;
  int interval_mins_ = 1440;

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

// ==========================================
// 2. ClientUpdateModal（客户端更新弹窗）
// ==========================================
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

  // 接收并处理 "client-update-progress" 事件（← use-client-update.ts:73-105）
  void handle_progress_event(const std::string& status, int progress, const std::string& msg);

  bool is_downloading() const { return is_downloading_; }
  void set_downloading(bool dl) { is_downloading_ = dl; invalidate(); }

  std::function<void()> on_start_update;
  std::function<void()> on_dismiss;

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;

  bool hit_self(PointF local) const override { return open_ && Widget::hit_self(local); }

  RectF card_rect() const { return card_rect_; }
  RectF update_btn_rect() const { return btn_update_rect_; }

 private:
  void subscribe_events();
  void unsubscribe_events();

  bool open_ = false;
  bool is_downloading_ = false;
  std::string current_version_;  // open() 必赋值（不设陈旧占位，避免误显示旧版本号）
  std::string latest_version_;
  std::string changelog_;
  std::string download_url_;
  std::string status_ = "idle";
  int progress_ = 0;
  std::string status_msg_;
  uint64_t listener_id_ = 0;

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

// ==========================================
// 3. CoreUpdateModal（内核升级弹窗）
// ==========================================
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

  // 接收并处理 "core-upgrade-progress" 事件
  void handle_progress_event(const std::string& status, int progress, const std::string& msg);

  std::function<void()> on_start_upgrade;
  std::function<void()> on_dismiss;

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;

  bool hit_self(PointF local) const override { return open_ && Widget::hit_self(local); }

  RectF card_rect() const { return card_rect_; }
  RectF upgrade_btn_rect() const { return btn_upgrade_rect_; }

 private:
  void subscribe_events();
  void unsubscribe_events();

  bool open_ = false;
  bool is_upgrading_ = false;
  std::string current_version_ = "v1.19.27";
  std::string latest_version_ = "v1.19.28";
  int progress_ = 0;
  std::string status_ = "idle";
  std::string message_;
  uint64_t listener_id_ = 0;

  RectF card_rect_;
  RectF btn_close_x_rect_;
  RectF progress_bar_rect_;
  RectF btn_close_rect_;
  RectF btn_upgrade_rect_;

  bool hover_close_x_ = false;
  bool hover_close_ = false;
  bool hover_upgrade_ = false;
};

// ==========================================
// 4. LogViewerModal（运行日志查看弹窗）
// ==========================================
struct LogEntry {
  std::string level;  // "debug", "info", "warn", "error"
  std::string time;
  std::string message;
};

class LogViewerModal : public Widget {
 public:
  static constexpr size_t kMaxLogLines = 1000;
  static constexpr uint32_t kFlushDelayMs = 50;

  explicit LogViewerModal(Widget* parent = nullptr);
  ~LogViewerModal() override = default;

  void open();
  void close();
  bool is_open() const { return open_; }

  // 追加日志（进待批处理缓冲区）
  void push_log(const std::string& level, const std::string& msg, const std::string& time_str = "");
  void push_log_line(const std::string& raw_line);

  // 立即触发 50ms 批处理落盘至可见展示缓冲区
  void flush_incoming();

  size_t log_count() const { return logs_.size(); }
  size_t incoming_count() const { return incoming_queue_.size(); }
  void clear_logs();

  // src: logs.tsx:116-121 / :133-142（shared.actions.pause/resume；logs.actions.showDescending/Ascending）
  std::wstring tooltip_text() const override {
    if (hover_pause_) return paused_ ? tr("shared.actions.resume") : tr("shared.actions.pause");
    if (hover_order_) return desc_ ? tr("logs.actions.showAscending") : tr("logs.actions.showDescending");
    if (hover_clear_) return tr("shared.actions.clear");
    return {};
  }

  bool is_paused() const { return paused_; }
  void set_paused(bool p) { paused_ = p; invalidate(); }

  bool is_desc() const { return desc_; }
  void set_desc(bool d) { desc_ = d; invalidate(); }

  const std::string& level_filter() const { return level_filter_; }
  void set_level_filter(const std::string& lvl) { level_filter_ = lvl; invalidate(); }

  const std::string& search_query() const { return search_query_; }
  void set_search_query(const std::string& q) { search_query_ = q; invalidate(); }

  std::function<void()> on_dismiss;

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;

  bool hit_self(PointF local) const override { return open_ && Widget::hit_self(local); }

  RectF card_rect() const { return card_rect_; }

 private:
  bool matches_filter(const LogEntry& entry) const;

  bool open_ = false;
  bool paused_ = false;
  bool desc_ = false;
  std::string level_filter_ = "all";  // all, debug, info, warn, error
  MenuList* lvl_menu_ = nullptr;      // 级别下拉弹出列表（logs.tsx:179-191）
  std::string search_query_;

  std::deque<LogEntry> logs_;           // 环形截断 1000 条
  std::vector<LogEntry> incoming_queue_;  // 待 50ms 刷新的暂存队列
  std::chrono::steady_clock::time_point last_flush_time_;

  float scroll_y_ = 0.0f;
  float line_height_ = 20.0f;

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
