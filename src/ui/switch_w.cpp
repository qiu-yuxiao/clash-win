// SwitchW 实现（T10）。
#include "switch_w.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "soft_shadow.h"
#include "theme/theme.h"
#include "util/log.h"

namespace clashwin::ui {

bool SwitchW::set_checked(bool v, bool fire) {
  if (checked_ == v) return false;
  checked_ = v;
  anim_ = v ? 1.0f : 0.0f;  // 即时落位（拖拽进度由 move 驱动）
  invalidate();
  if (fire && on_change) on_change(checked_);
  return true;
}

SizeF SwitchW::on_measure(float max_w, float max_h) {
  (void)max_w; (void)max_h;
  // 尺寸随皮肤：retro/original/modern 28×14、frosted 28×14、cyberpunk 18×10、
  //   monochrome 28×14（← base-switch.tsx sizeSmall 强制尺寸）
  const auto sp = theme::ThemeService::instance().switch_paint();
  width_ = sp.track_w;
  height_ = sp.track_h;
  return {width_, height_};
}

void SwitchW::update_drag(float local_x) {
  // 旋钮可移动区间 = 轨道宽 - 旋钮直径 - 左右 margin（← base-switch.tsx thumb margin）
  const auto sp = theme::ThemeService::instance().switch_paint();
  const float half = sp.thumb_d / 2.0f + sp.thumb_margin;
  const float range = width_ - sp.thumb_d - sp.thumb_margin * 2.0f;
  float t = range > 0 ? (local_x - half) / range : 0.0f;
  anim_ = t < 0 ? 0 : (t > 1 ? 1 : t);
  dragging_ = true;
  drag_moved_ = true;
  invalidate();
}

bool SwitchW::on_mouse(const MouseEvent& e) {
  switch (e.type) {
    case MouseEventType::Enter:
      // ★ 悬停态（← Mini `&:hover .MuiSwitch-thumb`）
      if (!thumb_hovered_) {
        thumb_hovered_ = true;
        invalidate();
      }
      return true;
    case MouseEventType::Leave:
      if (thumb_hovered_) {
        thumb_hovered_ = false;
        invalidate();
      }
      return true;
    case MouseEventType::Down:
      if (!enabled()) return true;
      press_x_ = e.pos.x;
      drag_moved_ = false;
      return true;
    case MouseEventType::Move:
      if (!enabled()) return false;
      if ((e.buttons & kMouseLeft) &&
          (drag_moved_ || std::fabs(e.pos.x - press_x_) > 3.0f)) {
        update_drag(e.pos.x);  // 超过阈值才算拖动（单击不动）
        return true;
      }
      return drag_moved_;
    case MouseEventType::Up:
      if (!enabled()) return true;
      if (drag_moved_) {
        dragging_ = false;
        drag_moved_ = false;
        set_checked(anim_ >= 0.5f, true);  // <0.5 回弹
      } else {
        set_checked(!checked_, true);  // 单击切换（无拖动位移）
      }
      return true;
    default:
      return false;
  }
}

void SwitchW::on_paint(IRenderTarget& rt) {
  // T12：6 套皮肤统一走 ThemeService::switch_paint()（← base-switch.tsx:15-556）。
  //
  // ★ 轨道（滑槽）可见性（用户反馈 2026-11-07）：retro-3d 槽底是
  //   rgba(212,175,55,.15)*.85 的半透明金，叠在同色卡片底上几乎无对比。
  //   Mini 里槽的立体感来自 boxShadow 的**上缘内阴影**（inset 0 3d 5d
  //   rgba(0,0,0,.7)）与**底部 1px 高光线**（0 1px 0 rgba(255,255,255,.08)）。
  //   之前 Win 侧两者都缺 → 只剩一个悬空的旋钮，"滑槽看不见"。
  auto& svc = theme::ThemeService::instance();
  const theme::SwitchPaint sp = svc.switch_paint();
  const float w = bounds().w > 0 ? bounds().w : sp.track_w;
  const float h = bounds().h > 0 ? bounds().h : sp.track_h;
  const RectF track{0, 0, w, h};
  // MUI disabled：轨道/旋钮按 0.38 叠乘（MUI Switch `.Mui-disabled` 轨道
  //   opacity .38，thumb 同档）——src: MUI Switch disabled 语义。
  const float dis = enabled() ? 1.0f : 0.38f;
  const float a = anim_;

  // ---- 轨道：off 态 ----
  Color4f off_bg = sp.off_bg;
  off_bg.a *= (1.0f - a) * dis;
  Brush off_b{off_bg};
  Color4f off_edge = sp.off_border;
  off_edge.a *= dis;
  rt.round_rect(track, sp.track_radius, Pen{off_edge, sp.off_bw}, &off_b);
  // 上缘内阴影（inset 0 dy blur）——"槽"的凹陷感
  if (sp.off_inset && a < 0.999f) {
    Color4f ic = sp.off_inset_color;
    ic.a *= (1.0f - a) * dis;
    draw_top_inset_shadow(rt, track, sp.track_radius, ic, sp.off_inset_dy,
                          sp.off_inset_blur);
  }
  // 底部 1px 高光线（0 1px 0 color）
  if (sp.off_bottom_hi && a < 0.999f) {
    Color4f hc = sp.off_bottom_hi_color;
    hc.a *= (1.0f - a) * dis;
    const float half = std::min(sp.track_radius, w * 0.5f);
    rt.draw_line({half, h - 0.5f}, {w - half, h - 0.5f},
                 Pen{hc, sp.off_bottom_hi_w});
  }

  // ---- 轨道：on 态 ----
  if (a > 0.001f) {
    if (sp.on_grad) {
      GradientStop st[3];
      for (int i = 0; i < sp.on_grad_n; ++i) {
        st[i] = sp.on_grad_stops[i];
        st[i].color.a *= a * dis;
      }
      // 角度 180 = CSS to bottom；src: base-switch.tsx:99
      rt.fill_round_rect_gradient_angle(track, sp.track_radius, st,
                                        sp.on_grad_n, 180.0f);
    } else {
      Color4f on = sp.on_bg;
      on.a *= a * dis;
      Brush on_b{on};
      rt.round_rect(track, sp.track_radius, Pen{Color4f{0, 0, 0, 0}, 0.0f},
                    &on_b);
    }
    if (sp.on_bw > 0.01f) {
      Color4f oe = sp.on_border;
      oe.a *= a * dis;
      rt.round_rect(track, sp.track_radius, Pen{oe, sp.on_bw}, nullptr);
    }
    // on 态槽的内阴影 + 底部高光（src: base-switch.tsx:60-79）
    if (sp.on_inset) {
      Color4f ic = sp.on_inset_color;
      ic.a *= a * dis;
      draw_top_inset_shadow(rt, track, sp.track_radius, ic, sp.on_inset_dy,
                            sp.on_inset_blur);
    }
    if (sp.on_bottom_hi) {
      Color4f hc = sp.on_bottom_hi_color;
      hc.a *= a * dis;
      const float half = std::min(sp.track_radius, w * 0.5f);
      rt.draw_line({half, h - 0.5f}, {w - half, h - 0.5f},
                   Pen{hc, sp.on_bottom_hi_w});
    }
    if (sp.on_glow) {
      Color4f g = sp.on_glow_color;
      g.a *= a * dis;
      draw_glow(rt, track, sp.track_radius, g, sp.on_glow_blur);
    }
  }

  // ---- 旋钮 ----
  // ★ 悬停态（← Mini `&:hover .MuiSwitch-thumb`）：悬停整个开关即生效。
  const bool hover = thumb_hovered_ && sp.thumb_hover;
  const float tscale = hover ? sp.thumb_hover_scale : 1.0f;
  const float td = sp.thumb_d * tscale;
  const float t_radius = sp.thumb_radius * tscale;
  const float tx = sp.thumb_margin + a * (w - sp.thumb_d - sp.thumb_margin * 2.0f);
  // 缩放围绕旋钮中心
  const float tcx = tx + sp.thumb_d / 2.0f;
  const float tcy = (h - sp.thumb_d) / 2.0f + sp.thumb_d / 2.0f;
  const float ty = tcy - td / 2.0f;
  const float tx0 = tcx - td / 2.0f;
  const RectF tr{tx0, ty, td, td};

  // ★ hover 阴影覆盖（← `:hover` 的 boxShadow 覆盖常态）
  const bool use_hs = hover && sp.thumb_hover_shadow;
  if (use_hs || sp.thumb_shadow) {
    Color4f sc = use_hs ? sp.thumb_hover_shadow_color : sp.thumb_shadow_color;
    sc.a *= dis;
    draw_soft_shadow(rt, tr, t_radius, sc,
                     use_hs ? sp.thumb_hover_shadow_dy : sp.thumb_shadow_dy,
                     use_hs ? sp.thumb_hover_shadow_blur
                            : sp.thumb_shadow_blur);
  }
  const bool on_side = a > 0.5f;
  const GradientStop* gs = on_side ? sp.on_thumb_stops : sp.off_thumb_stops;
  const int gn = on_side ? sp.on_thumb_n : sp.off_thumb_n;
  const bool use_grad = on_side ? sp.on_thumb_grad : sp.off_thumb_grad;
  if (use_grad && gn > 0) {
    GradientStop st[4];
    for (int i = 0; i < gn; ++i) {
      st[i] = gs[i];
      st[i].color.a *= dis;
      // ★ hover 亮度（← `filter: brightness(1.15)`，retro-3d 定义）
      if (hover && sp.thumb_hover_brightness != 1.0f) {
        const float b = sp.thumb_hover_brightness;
        st[i].color.r = std::min(1.0f, st[i].color.r * b);
        st[i].color.g = std::min(1.0f, st[i].color.g * b);
        st[i].color.b = std::min(1.0f, st[i].color.b * b);
      }
    }
    // CSS circle at 35% 35%：半径取 farthest-corner
    const float corner_r = std::sqrt(2.0f) * (td * 0.65f);
    rt.fill_round_rect_radial(tr, t_radius, st, gn, 0.35f, 0.35f,
                              corner_r, corner_r);
  } else {
    Color4f tc = on_side ? sp.on_thumb : sp.off_thumb;
    tc.a *= dis;
    if (hover && sp.thumb_hover_brightness != 1.0f) {
      const float b = sp.thumb_hover_brightness;
      tc.r = std::min(1.0f, tc.r * b);
      tc.g = std::min(1.0f, tc.g * b);
      tc.b = std::min(1.0f, tc.b * b);
    }
    Brush tb{tc};
    rt.round_rect(tr, t_radius, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &tb);
  }
  const float bw = on_side ? sp.on_thumb_bw : sp.off_thumb_bw;
  if (bw > 0.01f) {
    Color4f bc = on_side ? sp.on_thumb_border : sp.off_thumb_border;
    bc.a *= dis;
    rt.round_rect(tr, t_radius, Pen{bc, bw}, nullptr);
  }
  // 旋钮内浮雕（inset 0 dy 0 color）：顶部高光线 + 底部暗线
  //   src: base-switch.tsx:88-98（retro-3d）
  // ★ hover 覆盖（← `:hover` 的 inset 顶高光 rgba(255,255,255,.8|.6)）
  const bool use_hih = hover && sp.thumb_hover_inset_hi;
  if (use_hih || (on_side ? sp.on_thumb_inset_hi : sp.off_thumb_inset_hi)) {
    Color4f c = use_hih
                    ? sp.thumb_hover_inset_hi_color
                    : (on_side ? sp.on_thumb_inset_hi_color
                               : sp.off_thumb_inset_hi_color);
    c.a *= dis;
    const float dy = use_hih ? sp.thumb_hover_inset_hi_dy
                             : (on_side ? sp.on_thumb_inset_hi_dy
                                        : sp.off_thumb_inset_hi_dy);
    const float half = std::min(t_radius, td * 0.5f);
    rt.draw_line({tr.x + half, tr.y + dy}, {tr.right() - half, tr.y + dy},
                 Pen{c, 1.0f});
  }
  const bool use_hdk = hover && sp.thumb_hover_inset_dark;
  if (use_hdk || (on_side ? sp.on_thumb_inset_dark : sp.off_thumb_inset_dark)) {
    Color4f c = use_hdk
                    ? sp.thumb_hover_inset_dark_color
                    : (on_side ? sp.on_thumb_inset_dark_color
                               : sp.off_thumb_inset_dark_color);
    c.a *= dis;
    const float dy = use_hdk ? sp.thumb_hover_inset_dark_dy
                             : (on_side ? sp.on_thumb_inset_dark_dy
                                        : sp.off_thumb_inset_dark_dy);
    const float half = std::min(t_radius, td * 0.5f);
    rt.draw_line({tr.x + half, tr.bottom() - 1.0f + dy},
                 {tr.right() - half, tr.bottom() - 1.0f + dy}, Pen{c, 1.0f});
  }
  // ★ hover 辉光覆盖（← cyberpunk `:hover` boxShadow 0 0 10px currentColor）
  const bool use_hg = hover && sp.thumb_hover_glow;
  if (use_hg || sp.thumb_glow) {
    Color4f g = use_hg ? (on_side ? sp.on_thumb_hover_glow_color
                                  : sp.thumb_hover_glow_color)
                       : sp.thumb_glow_color;
    g.a *= dis;
    draw_glow(rt, tr, t_radius, g,
              use_hg ? sp.thumb_hover_glow_blur : sp.thumb_glow_blur);
  }
}

}  // namespace clashwin::ui
