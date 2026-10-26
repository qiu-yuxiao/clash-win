#pragma once
// 连接面板（T14）：mini 表形态——host/chains 两列 + 行高 20 虚拟滚动。
// ← connections-panel.tsx（面板壳/分段/清空）+ connection-table.tsx
//   （**mini 表仅 host:port 与 chains 两列** ← :314-352；行高 20 ← ROW_HEIGHT；
//   chains 三分类 Proxy/Direct/Reject ← :329-348）。
// 上传/下载五列大表、右键菜单、详情 Popover → T20（占位回调已留）。
// 上传/下载速率列、协议色点为本任务按任务卡的增强（源码 mini 表无），
// 色点取 chains 分类语义色（Proxy=primary/Direct=success/Reject=error）。
#include <functional>
#include <memory>
#include <string>

#include "../container.h"
#include "../label.h"
#include "../scroll_list.h"
#include "../textfield.h"
#include "../widget.h"
#include "connection_data.h"

namespace clashwin::ui::scene {

inline constexpr float kConnRowH = 20;  // ← connection-table.tsx:28 ROW_HEIGHT

// chains 分类色（语义色近似：原版为文字分类，无色点 CSS）
Color4f chains_color(const std::string& cat);

class ConnRowWidget : public Widget {
 public:
  void bind(const ConnRow& row);
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  std::function<void()> on_click;      // 行点击（T20 详情占位）
  std::function<void()> on_close;      // 关闭连接按钮（DELETE /connections/:id）

 private:
  ConnRow row_;
  bool hover_ = false, close_hover_ = false;
};

class ConnectionsView : public Widget {
 public:
  explicit ConnectionsView(Widget* parent = nullptr);
  ~ConnectionsView() override;

  void bind(ConnectionData* data);  // 数据源（订阅节流通知）
  // 过滤（host 子串大小写不敏感）
  void set_filter(const std::string& text);
  size_t visible_count() const { return visible_.size(); }
  ScrollList* list() const { return list_; }

  // 关闭连接（单行 ✕）：hook 注入（生产绑 MihomoClient::close_connection）
  std::function<bool(const std::string& id)> close_connection;
  // 行点击（T20 详情占位）
  std::function<void(const std::string& id)> on_row_click;
  // 清空 closed 历史
  std::function<void()> on_clear_closed;

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;

 private:
  void rebuild_visible();
  void sync_list();

  ConnectionData* data_ = nullptr;
  ScrollList* list_ = nullptr;
  TextField* search_ = nullptr;
  Label* badge_ = nullptr;  // 连接数徽标
  std::string filter_;
  std::vector<size_t> visible_;
};

}  // namespace clashwin::ui::scene
