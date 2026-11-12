#pragma once
// 流媒体与 AI 解锁检测弹窗（T21）。
// 布局/配色对齐 pages/unlock.tsx：Grid 响应式列（xs1/sm2/md3）、Card outlined
// radius16 + 左 4px 状态色、p1.3、名称 16px/600、32×32 圆测试按钮、状态 Chip
// (success/error/warning/info/default)、地区 outlined info Chip、虚线分隔、
// 右下 0.7rem 检测时间。Mini 为整页；本弹窗为 ClashWin 承载壳（见 cpp 豁免）。
#include <functional>
#include <string>
#include <vector>

#include "config/unlock_cache.h"
#include "ui/widget.h"
#include "ui/i18n_text.h"  // F-D01

namespace clashwin::ui::scene {

class UnlockDialog : public Widget {
 public:
  explicit UnlockDialog(Widget* parent = nullptr);
  ~UnlockDialog() override = default;

  void open();
  void close();
  bool is_open() const { return open_; }
  // 整页模式（Mini /unlock 为整页 pages/unlock.tsx:1206-1219 承载）
  void set_page_mode(bool on) { page_mode_ = on; invalidate(); }
  bool page_mode() const { return page_mode_; }

  const std::vector<config::unlock_cache::UnlockItem>& items() const { return items_; }
  void set_items(std::vector<config::unlock_cache::UnlockItem> items);

  bool is_checking() const { return is_checking_; }
  void start_check();

  std::function<void()> on_dismiss;

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;

  bool hit_self(PointF local) const override {
    return open_ && Widget::hit_self(local);
  }
  bool a11y_subtree_visible() const override {
    return open_ && visible() && !a11y_hidden();
  }
  bool a11y_is_modal() const override { return !page_mode_ && open_; }
  A11yRole a11y_default_role() const override {
    return page_mode_ ? A11yRole::Pane : A11yRole::Dialog;
  }
  std::wstring a11y_default_name() const override;
  size_t a11y_virtual_child_count() const override;
  A11ySubItem a11y_virtual_child(size_t idx) const override;
  bool a11y_invoke_virtual_child(size_t idx) override;

  RectF card_rect() const { return card_rect_; }
  RectF retest_btn_rect() const { return btn_retest_rect_; }

  // src: locales/zh/tests.json:11 「"test": "测试"」；:4 testAll「测试全部」
  std::wstring tooltip_text() const override {
    if (hover_card_ >= 0) return tr("tests.page.title");
    if (hover_retest_) return tr("tests.page.actions.testAll");
    return {};
  }

 private:
  void reload_from_cache();
  struct Ctrl {
    RectF card;
    RectF retest;
    RectF chip;
    RectF region;
  };
  std::vector<Ctrl> ctrls_;

  bool open_ = false;
  bool is_checking_ = false;
  bool page_mode_ = false;
  float scroll_ = 0.0f;     // 整页模式滚动偏移（内容超高时）
  float max_scroll_ = 0.0f;
  std::vector<config::unlock_cache::UnlockItem> items_;

  RectF card_rect_;
  RectF btn_close_x_rect_;
  RectF btn_retest_rect_;
  int hover_card_ = -1;
  bool hover_close_x_ = false;
  bool hover_retest_ = false;
};

}  // namespace clashwin::ui::scene
