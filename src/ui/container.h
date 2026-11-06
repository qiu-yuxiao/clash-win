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

// Box：带背景/圆角的最简容器（子铺满内容区）
class Box : public Widget {
 public:
  explicit Box(Widget* parent = nullptr) : Widget(parent) {}
  Brush background{Color4f(0, 0, 0, 0)};  // 豁免: Mini 无对应物 — 容器缺省透明背景（Widget 基类缺省）
  Pen border{Color4f(0, 0, 0, 0), 0};  // 豁免: Mini 无对应物 — 容器缺省无边框（Widget 基类缺省）
  float corner_radius = 0;
  // 【QBFIX】期望尺寸 = 最大子 + padding（基类返回 {0,0} 致 Column 里
  //   flex_weight=0 的 quickbar Box 测得高 0 → 整条不可见）
  SizeF on_measure(float max_w, float max_h) override {
    float mw = 0, mh = 0;
    for (size_t i = 0; i < child_count(); ++i) {
      if (!child(i)->visible()) continue;
      SizeF s = child(i)->measure(max_w > 0 ? max_w - padding().h() : max_w,
                                  max_h > 0 ? max_h - padding().v() : max_h);
      if (s.w > mw) mw = s.w;
      if (s.h > mh) mh = s.h;
    }
    return {mw + padding().h(), mh + padding().v()};
  }
  void on_paint(IRenderTarget& rt) override {
    // on_paint 局部坐标约定：画 {0,0,w,h}（画 bounds() 会在非零 bounds 时偏移）
    if (background.color.a > 0 || border.width > 0)
      rt.round_rect({0, 0, bounds().w, bounds().h}, corner_radius, border,
                    &background);
  }
};

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
