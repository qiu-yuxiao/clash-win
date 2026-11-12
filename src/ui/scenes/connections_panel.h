#pragma once
// 连接面板（T20）：完整实现 v3.2.1 路径控制/活跃历史分段/Chains 64px 循环排序/虚拟列表/右键菜单/详情弹窗。
// 逐行对照：
// - connections-panel.tsx（副标题 11px 加粗、3D 活跃/历史分段 140×18、断开全部、清空历史）
// - connection-table.tsx（Host 列 flex:1、Chains 列固定 64px 居中、▲/▼ 循环排序、右键 6 项菜单、行点击详情）
// - ScrollList 虚拟滚动，hidden scrollbar（show_scrollbar = false），支持平滑滚轮
// - 严格遵守 Direct2D 局部坐标规则（以 {0, y} 相对本控件绘制，禁止 double offset）
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../button.h"
#include "../context_menu.h"
#include "../scroll_list.h"
#include "../textfield.h"
#include "../widget.h"
#include "connection_data.h"
#include "connection_detail_dialog.h"

namespace clashwin::ui::scene {

inline constexpr float kConnPanelRowH = 20.0f;  // ← ROW_HEIGHT = 20

enum class SortCol {
  None,
  Host,
  Chains,
};

enum class SortDir {
  None,
  Asc,
  Desc,
};

// 连接表外框 + 空态覆盖层：需盖在 ScrollList 之上
//   src: connection-table.tsx:48 border: '5px double var(--theme-border)' radius 4
//   src: connections-panel.tsx:232-233 filterConn 为空 → BaseEmpty（"空空如也"）
class ConnTableOverlay : public Widget {
 public:
  explicit ConnTableOverlay(Widget* parent = nullptr) : Widget(parent) {
    set_z_order(80);
  }
  bool empty = false;
  void on_paint(IRenderTarget& rt) override;
  bool hit_self(PointF) const override { return false; }
  bool a11y_subtree_visible() const override {
    return empty && visible() && !a11y_hidden();
  }
  A11yRole a11y_default_role() const override { return A11yRole::Text; }
  std::wstring a11y_default_name() const override;
};

class ConnPanelRowWidget : public Widget {
 public:
  explicit ConnPanelRowWidget(Widget* parent = nullptr) : Widget(parent) {}

  void bind(const ConnItem& item, bool is_closed, size_t row_index);
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;

  std::function<void(const ConnItem& item, RectF row_global)> on_click;
  std::function<void(const ConnItem& item)> on_close;
  std::function<void(const ConnItem& item, PointF local_pt, RectF row_global)> on_context_menu;

  const ConnItem& item() const { return item_; }

  A11yRole a11y_default_role() const override { return A11yRole::ListItem; }
  std::wstring a11y_default_name() const override;
  bool a11y_is_focusable() const override { return enabled(); }
  bool a11y_can_invoke() const override {
    return enabled() && static_cast<bool>(on_click);
  }
  bool a11y_invoke() override {
    if (!enabled() || !on_click) return false;
    on_click(item_, global_bounds());
    return true;
  }

 private:
  ConnItem item_;
  bool is_closed_ = false;
  size_t row_index_ = 0;
  bool hover_ = false;
  bool close_hover_ = false;
};

class ConnectionsPanel : public Widget {
 public:
  enum class Tab {
    Active,
    Closed,
  };

  explicit ConnectionsPanel(Widget* parent = nullptr);
  ~ConnectionsPanel() override;

  void bind(ConnectionData* data);

  void set_tab(Tab tab);
  Tab tab() const { return tab_; }

  void set_filter(const std::string& text);
  const std::string& filter() const { return filter_; }

  void cycle_sort_host();
  void cycle_sort_chains();

  SortCol sort_column() const { return sort_col_; }
  SortDir sort_direction() const { return sort_dir_; }

  size_t visible_count() const { return visible_items_.size(); }
  const std::vector<ConnItem>& visible_items() const { return visible_items_; }

  ScrollList* list() const { return list_; }
  ConnectionDetailDialog* detail_dialog() const { return detail_dialog_; }
  ContextMenu* context_menu() const { return context_menu_; }

  // 外部动作挂载
  std::function<void(const std::string& id)> on_close_one;
  std::function<void()> on_close_all;
  std::function<void()> on_clear_history;

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;

  A11yRole a11y_default_role() const override { return A11yRole::Group; }
  std::wstring a11y_default_name() const override;
  size_t a11y_virtual_child_count() const override { return 4; }
  A11ySubItem a11y_virtual_child(size_t idx) const override;
  bool a11y_invoke_virtual_child(size_t idx) override;

  void refresh_visible();

 private:
  void show_context_menu(const ConnItem& item, PointF anchor, RectF row_global);
  void apply_quick_route(const ConnItem& item, int target);

  ConnectionData* data_ = nullptr;
  uint64_t data_sub_ = 0;

  Tab tab_ = Tab::Active;
  std::string filter_;
  SortCol sort_col_ = SortCol::None;
  SortDir sort_dir_ = SortDir::None;

  std::vector<ConnItem> visible_items_;

  // 子控件
  TextField* search_ = nullptr;
  Button* btn_disconnect_all_ = nullptr;
  Button* btn_clear_history_ = nullptr;
  ScrollList* list_ = nullptr;
  ConnTableOverlay* table_overlay_ = nullptr;
  ContextMenu* context_menu_ = nullptr;
  ConnectionDetailDialog* detail_dialog_ = nullptr;

  // 点击热区
  RectF tab_active_rect_;
  RectF tab_closed_rect_;
  RectF header_host_rect_;
  RectF header_chains_rect_;
};

}  // namespace clashwin::ui::scene
