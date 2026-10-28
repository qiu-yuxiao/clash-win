#pragma once
// 连接详情弹窗（T20）：对齐 connection-detail.tsx 13 项元数据展示。
// - 13 项元数据全量展示（域名/累计流量/实时速率/规则链/命中规则/进程/时间/源/目的/类型）
// - 活跃连接支持右下角"断开连接"按钮
// - 点击遮罩外部或按 Escape 键关闭
// - 严格遵守 Direct2D 局部坐标规则（以 {0, y} 相对本控件绘制，禁止 double offset）
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../widget.h"
#include "connection_data.h"

namespace clashwin::ui::scene {

class ConnectionDetailDialog : public Widget {
 public:
  explicit ConnectionDetailDialog(Widget* parent = nullptr);
  ~ConnectionDetailDialog() override = default;

  void open(const ConnItem& item, bool closed);
  void close();
  bool is_open() const { return open_; }

  const ConnItem& item() const { return item_; }
  bool is_closed() const { return closed_; }

  // 13 字段键值对导出（供单元测试精准断言）
  std::vector<std::pair<std::wstring, std::wstring>> fields() const;

  // 断开连接回调（生产绑 DELETE /connections/:id）
  std::function<void(const std::string& id)> on_close_connection;
  std::function<void()> on_dismiss;

  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;

  bool hit_self(PointF local) const override {
    return open_ && Widget::hit_self(local);
  }

 private:
  bool open_ = false;
  bool closed_ = false;
  ConnItem item_;
  RectF card_rect_;
  RectF btn_close_conn_rect_;
  RectF btn_dismiss_rect_;
  bool hover_close_conn_ = false;
  bool hover_dismiss_ = false;
};

}  // namespace clashwin::ui::scene
