#pragma once
// 容器（T10）：Column/Row/Stack——对齐 React flex 概念的子集（§5.1 布局算法：
// 仅 Column/Row/固定 px/百分比/flex-grow）。
// - Column：主轴垂直；Row：主轴水平
//   spacing 主轴间距；main_align 主轴对齐（start/end/center/space_between）；
//   cross_align 交叉轴对齐（start/center/end/stretch）；padding
//   flex：子 widget 的 flex_weight > 0 者按权重分剩余主轴空间
// - Stack（AbsoluteOverlay）：子按加入顺序绘制（后者在上），默认铺满；
//   z_order 可提升（modal/menu overlay 用）
#pragma once

#include "widget.h"

namespace clashwin::ui {

enum class MainAlign { Start, End, Center, SpaceBetween };
enum class CrossAlign { Start, Center, End, Stretch };

class Column : public Widget {
 public:
  explicit Column(Widget* parent = nullptr) : Widget(parent) {}
  float spacing = 0;
  MainAlign main_align = MainAlign::Start;
  CrossAlign cross_align = CrossAlign::Stretch;

  SizeF on_measure(float max_w, float max_h) override;
  void on_arrange(const RectF& area) override;
};

class Row : public Widget {
 public:
  explicit Row(Widget* parent = nullptr) : Widget(parent) {}
  float spacing = 0;
  MainAlign main_align = MainAlign::Start;
  CrossAlign cross_align = CrossAlign::Stretch;

  SizeF on_measure(float max_w, float max_h) override;
  void on_arrange(const RectF& area) override;
};

class Stack : public Widget {
 public:
  explicit Stack(Widget* parent = nullptr) : Widget(parent) {}
  // 期望尺寸 = 最大子期望尺寸（不铺满时的固有值）
  SizeF on_measure(float max_w, float max_h) override;
  // 子铺满（AbsoluteOverlay 语义）；z 序由 paint/hit_test 按 z_order 处理
  void on_arrange(const RectF& area) override;
};

}  // namespace clashwin::ui
