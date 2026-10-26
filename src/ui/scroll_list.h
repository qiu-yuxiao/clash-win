#pragma once
// ScrollList（T10）：虚拟滚动列表——对齐 @tanstack/react-virtual 语义。
// - 仅物化可视区 + overscan（1000+ 项只创建可视窗口个数的 Widget）
// - item_height 固定（节点列表行高 39 归 T13 场景参数）
// - 工厂 factory(index) 生成行 Widget；物化窗口滑动时增量创建/卸载
// - 滚轮：每 120 delta 滚 3 行（Windows 惯例）；滚动条自绘（右侧 6px）
#include <functional>
#include <unordered_map>
#include <vector>

#include "widget.h"

namespace clashwin::ui {

class ScrollList : public Widget {
 public:
  using Factory = std::function<std::unique_ptr<Widget>(int index)>;

  explicit ScrollList(Widget* parent = nullptr) : Widget(parent) {}

  void set_content(int count, float item_height, Factory factory,
                   int overscan = 5);
  int item_count() const { return item_count_; }
  float item_height() const { return item_height_; }

  float scroll_offset() const { return offset_; }
  void set_scroll_offset(float v);  // clamp + 重物化 + invalidate
  float max_scroll() const;
  int overscan() const { return overscan_; }

  // 已物化行 index 集合（测试断言：可视区+overscan 而非全量）
  std::vector<int> materialized_indices() const;
  Widget* materialized_row(int index) const;

  std::function<void()> on_scroll;

  SizeF on_measure(float max_w, float max_h) override;
  void on_arrange(const RectF& area) override;
  bool on_mouse(const MouseEvent& e) override;
  void on_paint(IRenderTarget& rt) override;

  // 绘制参数
  Color4f scrollbar_ = Color4f::hex(0x000000, 0.25f);
  bool themed_ = false;  // T12：true 时取 --scrollbar-thumb
  float scrollbar_w_ = 6;

 private:
  void sync_children(float viewport_h);  // 物化窗口对齐 offset_
  int first_visible(float viewport_h) const {
    return static_cast<int>(offset_ / item_height_);
  }

  int item_count_ = 0;
  float item_height_ = 0;
  int overscan_ = 5;
  float offset_ = 0;
  Factory factory_;
  std::unordered_map<int, std::unique_ptr<Widget>> rows_;
};

}  // namespace clashwin::ui
