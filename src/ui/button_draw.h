#pragma once
// 按钮（get3DButtonStyle）统一绘制入口。
//
// 背景：utils/button-styles.ts:17-489 给每套皮肤都写了常态、`&:hover`、
// `&:active`、`&.Mui-disabled` 四段视觉，其中阴影和辉光在三个状态下各不相同
// （retro-3d 悬停是「再加一层」而不是「改一层」）。Win 侧此前由 button.cpp 与
// 四处弹窗各复制了一份绘制代码，每份只认常态一套值 —— 悬停不加深、按下不收窄、
// 禁用描边整块消失（台账 V8），还有个弹窗自创了 blur×1.3 / ×1.4 的系数。
//
// 本头文件是唯一绘制路径：状态差异全部来自 theme::ButtonPaint 的查表结果。
#include <algorithm>

#include "render_target.h"
#include "soft_shadow.h"
#include "theme/theme.h"

namespace clashwin::ui {
namespace button_detail {

inline Color4f mix4(const Color4f& a, const Color4f& b, float k) {
  return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k,
          a.a + (b.a - a.a) * k};
}

// 取某状态生效的柔影层。按下 → `&:active` 那套；否则常态 ↔ `&:hover` 按
// hover_mix 插值（0.15s ease 已由调用方推进）。
// 两层状态层数可能不同（retro-3d 悬停 2 层 / 常态 1 层）：按较多一侧统一，
// 少的一侧用其末层补齐 —— 与渐变站的补齐方式一致，补齐段与原样等价。
inline int resolve_shadow(const theme::ButtonPaint& p, float hover_mix,
                          bool pressed,
                          theme::ButtonPaint::ShadowLayer* out) {
  const theme::ButtonPaint::ShadowLayer* a = p.shadow;
  int an = p.shadow_n;
  const theme::ButtonPaint::ShadowLayer* b = p.shadow;
  int bn = an;
  float k = 0.0f;
  if (pressed) {
    if (p.shadow_active_n > 0) {
      b = p.shadow_active;
      bn = p.shadow_active_n;
      k = 1.0f;
    }  // 否则 CSS 未覆盖 :active 的 boxShadow → 沿用常态
  } else if (p.shadow_hover_n > 0) {
    b = p.shadow_hover;
    bn = p.shadow_hover_n;
    k = hover_mix;
  }
  if (an <= 0 && bn <= 0) return 0;
  const int n = std::max(an, bn);
  for (int i = 0; i < n; ++i) {
    const auto& la = a[std::min(i, an - 1)];
    const auto& lb = b[std::min(i, bn - 1)];
    out[i].color = mix4(la.color, lb.color, k);
    out[i].dy = la.dy + (lb.dy - la.dy) * k;
    out[i].blur = la.blur + (lb.blur - la.blur) * k;
  }
  return n;
}

// 取某状态生效的辉光。返回 false 表示该状态不画（retro-3d 按下时 boxShadow
// 被 inset 整体替换，辉光随之消失）。
inline bool resolve_glow(const theme::ButtonPaint& p, float hover_mix,
                         bool pressed, Color4f* color, float* blur) {
  Color4f c = p.glow;
  float b = p.glow_blur;
  Color4f tc = c;
  float tb = b;
  float k = 0.0f;
  if (pressed) {
    if (p.has_glow_active) {
      tc = p.glow_active;
      tb = p.glow_active_blur;
      k = 1.0f;
    }
  } else if (p.has_glow_hover) {
    tc = p.glow_hover;
    tb = p.glow_hover_blur;
    k = hover_mix;
  }
  *color = mix4(c, tc, k);
  *blur = b + (tb - b) * k;
  return color->a > 0.001f;
}

}  // namespace button_detail

// hover_mix：0 = 常态，1 = 完全悬停（调用方按 0.15s ease 推进）。
// pressed / disabled：按下与禁用是瞬态，不做插值。
inline void draw_button_skin(IRenderTarget& rt, const RectF& r,
                             const theme::ButtonPaint& p, float hover_mix,
                             bool pressed, bool disabled = false) {
  using namespace button_detail;

  // ---- 1. 柔影 + 辉光（先铺在按钮底下） ----
  if (!disabled) {
    theme::ButtonPaint::ShadowLayer ls[2];
    const int n = resolve_shadow(p, hover_mix, pressed, ls);
    for (int i = 0; i < n; ++i) {
      if (ls[i].color.a > 0.001f)
        draw_soft_shadow(rt, r, p.radius, ls[i].color, ls[i].dy, ls[i].blur);
    }
    Color4f gc;
    float gb = 0.0f;
    if (resolve_glow(p, hover_mix, pressed, &gc, &gb))
      draw_glow(rt, r, p.radius, gc, gb);
  }

  // ---- 2. 禁用态：MUI 只保留底色 + 描边（+ 文本色，由调用方取 text_disabled） ----
  if (disabled) {
    Brush b{p.fill_disabled};
    rt.round_rect(r, p.radius, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &b);
    if (p.border_w > 0.01f && p.border_disabled.a > 0.001f)
      rt.round_rect(r, p.radius, Pen{p.border_disabled, p.border_w}, nullptr);
    return;
  }

  // ---- 3. 主体 ----
  if (p.use_grad) {
    const int gn = std::min(p.grad_count, 6);
    const int hn = p.grad_hover_count > 0 ? std::min(p.grad_hover_count, 6) : gn;
    if (gn <= 0) return;
    const int n = std::max(gn, hn);
    const float k = pressed ? 1.0f : hover_mix;
    GradientStop mixed[6];
    const GradientStop* gs = p.grad_hover;
    int draw_n = hn;
    if (k < 0.999f) {
      for (int i = 0; i < n; ++i) {
        const GradientStop& a = p.grad[std::min(i, gn - 1)];
        const GradientStop& b = p.grad_hover[std::min(i, hn - 1)];
        mixed[i] = a;
        mixed[i].color = mix4(a.color, b.color, k);
      }
      draw_n = n;
      gs = mixed;
    }
    rt.fill_round_rect_gradient_angle(r, p.radius, gs, draw_n, p.grad_angle);
  } else {
    // ← :265-275 / :321-329 等：`&:active` 不改底色，按下沿用悬停色
    Color4f fill = pressed ? p.fill_pressed : mix4(p.fill, p.fill_hover, hover_mix);
    Brush b{fill};
    rt.round_rect(r, p.radius, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &b);
  }

  // ---- 4. 描边 ----
  if (p.border_w > 0.01f && p.border.a > 0.001f)
    rt.round_rect(r, p.radius, Pen{p.border, p.border_w}, nullptr);

  // ---- 5. retro-3d 的硬浮雕线 ----
  // ← :89-101 boxShadow 里的 `0 1px 0 0 bevelShadowDark`（底缘 1px 实色线）与
  //   `0 1px 0 rgba(255,255,255,.8)`（同位置高光，被前一条盖住）。
  //   两条都不是 inset，所以位置在底缘而不是顶缘 —— 此前 Win 侧把它画在
  //   r.y+1（顶缘），方向反了。
  // ← :130-136 按下换成两条 inset 线（0 1px / 0 d），落在顶缘。
  if (p.bevel_dark.a > 0.001f) {
    if (pressed) {
      rt.draw_line(PointF{r.x + p.radius, r.y + 1.0f},
                   PointF{r.right() - p.radius, r.y + 1.0f},
                   Pen{p.bevel_dark, 1.0f});
      if (p.border_w > 1.5f) {
        rt.draw_line(PointF{r.x + p.radius, r.y + 2.0f},
                     PointF{r.right() - p.radius, r.y + 2.0f},
                     Pen{p.bevel_dark, 1.0f});
      }
    } else {
      rt.draw_line(PointF{r.x + p.radius, r.bottom() - 1.0f},
                   PointF{r.right() - p.radius, r.bottom() - 1.0f},
                   Pen{p.bevel_dark, 1.0f});
    }
  }
}

}  // namespace clashwin::ui
