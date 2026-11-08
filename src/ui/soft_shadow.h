#pragma once
// 软阴影 / 外发光近似（D2D 无 CSS box-shadow）。
//
// Mini 用 CSS box-shadow 表达阴影与发光，且几乎每一处都乘 depth/vibrancy：
//   retro-3d  0 calc(8px * --depth-factor) calc(32px * --depth-factor) rgba(0,0,0, .3 * --depth-factor)
//   retro-3d  0 0 calc(12px * --vibrancy-factor) rgba(255,193,7, .2 * --vibrancy-factor)
//   modern-flat 0 calc(4px * val2) calc(16px * val2) rgba(0,0,0, .10 * val2)
// D2D 没有等价原语，这里用「多层递减 alpha 的圆角矩形」逼近：
//   由外向内画 layers 层，每层外扩 blur*(1-t)，单层 alpha = 总 alpha / layers * kOverlap。
// kOverlap 补偿多层在中心区叠加后的非线性，使中心收敛到接近目标 alpha。
#include <algorithm>
#include <cmath>

#include "render_target.h"

namespace clashwin::ui {

// 外阴影：等价 box-shadow 0 dy blur <color>
// r / radius 为投影体的矩形与圆角（逻辑像素）
inline void draw_soft_shadow(IRenderTarget& rt, const RectF& r, float radius,
                             const Color4f& color, float dy, float blur,
                             int layers = 7) {
  if (color.a <= 0.004f || blur <= 0.5f) return;
  // 豁免: Mini 无对应物 — 多层 alpha 逐层累加会偏淡，用 1.35 补偿；D2D 无 box-shadow 原语，纯实现层（出处: 无）
  constexpr float kOverlap = 1.35f;  // 叠加补偿
  for (int i = layers; i >= 1; --i) {
    const float t = static_cast<float>(i) / static_cast<float>(layers);
    const float grow = blur * t;
    Color4f c = color;
    c.a = color.a / static_cast<float>(layers) * kOverlap;
    if (c.a <= 0.004f) continue;
    RectF rr{r.x - grow, r.y - grow + dy, r.w + grow * 2.0f, r.h + grow * 2.0f};
    Brush b{c};
    rt.round_rect(rr, radius + grow, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &b);
  }
}

// 外发光：等价 box-shadow 0 0 blur <color>（dy 恒为 0）
inline void draw_glow(IRenderTarget& rt, const RectF& r, float radius,
                      const Color4f& color, float blur, int layers = 7) {
  draw_soft_shadow(rt, r, radius, color, 0.0f, blur, layers);
}

// 内发光/内阴影：等价 inset 0 0 blur <color>。
// 实现：在 r 内沿边缘画逐层向内收缩的「描边框」（即大圆角矩形减去小圆角矩形）。
// 这里用「先填满色再挖空」不可行（无混合挖空），改为画 blur 圈 1px 描边，
// 由外向内 alpha 递减——视觉上等价于边缘泛光。
inline void draw_inner_glow(IRenderTarget& rt, const RectF& r, float radius,
                            const Color4f& color, float blur,
                            int layers = 6) {
  if (color.a <= 0.004f || blur <= 0.5f) return;
  for (int i = 0; i < layers; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(layers);
    const float inset = blur * t;
    Color4f c = color;
    c.a = color.a * (1.0f - t) / static_cast<float>(layers) * 2.0f;
    if (c.a <= 0.004f) continue;
    RectF rr{r.x + inset, r.y + inset, r.w - inset * 2.0f, r.h - inset * 2.0f};
    if (rr.is_empty()) continue;
    rt.round_rect(rr, radius > inset ? radius - inset : 0.0f, Pen{c, 1.4f},
                  nullptr);
  }
}

// 上缘内阴影：等价 CSS `inset 0 <dy> <blur> <color>`
// 语义：阴影从盒子上缘向下渗透 dy 距离、向外模糊 blur 像素。
//   retro-3d 开关轨道正是用它做「滑槽」的凹陷感：
//   inset 0 calc(3px*depth) calc(5px*depth) rgba(0,0,0,.7)
// D2D 无 inset 原语，用「上缘向内若干层的横向渐变带」逼近：
//   从上缘往下画若干条 1px 半透明横线，alpha 由强到弱衰减。
inline void draw_left_inset_shadow(IRenderTarget& rt, const RectF& r,
                                   float radius, const Color4f& color, float dx,
                                   float blur, int layers = 5) {
  if (color.a <= 0.004f) return;
  const float depth = dx + blur;
  if (depth <= 0.5f) return;
  const float inset_y = std::min(radius, r.h * 0.5f);
  for (int i = 0; i < layers; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(layers);
    const float x = r.x + depth * t;
    Color4f col = color;
    col.a = color.a * (1.0f - t) / static_cast<float>(layers) * 2.0f;
    if (col.a <= 0.004f) continue;
    const float y0 = r.y + inset_y * (1.0f - t);
    const float y1 = r.bottom() - inset_y * (1.0f - t);
    if (y1 <= y0) continue;
    // 豁免: Mini 无对应物 — 逐列软阴影描边宽度，D2D 无 box-shadow 原语，纯实现层（出处: 无）
    rt.draw_line({x, y0}, {x, y1}, Pen{col, 1.2f});
  }
}

inline void draw_top_inset_shadow(IRenderTarget& rt, const RectF& r,
                                  float radius, const Color4f& color, float dy,
                                  float blur, int layers = 5) {
  if (color.a <= 0.004f) return;
  // 渗透总深度 = dy + blur
  const float depth = dy + blur;
  if (depth <= 0.5f) return;
  // 上下圆角处不画（避免直角凸出圆角外）
  const float inset_x = std::min(radius, r.w * 0.5f);
  for (int i = 0; i < layers; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(layers);
    const float y = r.y + depth * t;
    Color4f c = color;
    // 上缘最浓，向下线性衰减
    c.a = color.a * (1.0f - t) / static_cast<float>(layers) * 2.0f;
    if (c.a <= 0.004f) continue;
    const float half = std::sqrt(std::max(0.0f, radius * radius -
                                                  (radius - y + r.y) *
                                                      (radius - y + r.y)));
    const float x0 = r.x + std::min(inset_x, half);
    const float x1 = r.right() - std::min(inset_x, half);
    if (x1 <= x0) continue;
    // 豁免: Mini 无对应物 — 逐行软阴影描边宽度，D2D 无 box-shadow 原语，纯实现层（出处: 无）
    rt.draw_line({x0, y}, {x1, y}, Pen{c, 1.2f});
  }
}

}  // namespace clashwin::ui
