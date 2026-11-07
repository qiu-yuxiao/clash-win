#pragma once
// 卡片（get3DCardStyle）统一绘制入口。
// 背景：utils/button-styles.ts:492-731 get3DCardStyle(theme, cardType) 是 4 卡型 ×
// 6 皮肤的完整查表；Win 侧此前在 quick_bar / traffic_pane / settings_drawer / 各
// 弹窗各写一份硬编码金色渐变，导致「皮肤切了但卡片还是金色」。所有卡片改吃
// ThemeService::card_paint() 给出的参数，本头文件负责把它落到 IRenderTarget 上。
#include "render_target.h"
#include "soft_shadow.h"
#include "theme/theme.h"

namespace clashwin::ui {

// corner_mask：bit0=TL bit1=TR bit2=BR bit3=BL。
// src: mini-traffic-panel.tsx:102-104/:171-173 —— 成对指标卡在接缝侧内角半径 0。
inline void draw_card(IRenderTarget& rt, const RectF& r,
                      const theme::CardPaint& cp,
                      int corner_mask = 0xF) {
  // 阴影/发光先铺（D2D 无 box-shadow，用多层外扩近似）
  if (cp.has_shadow)
    draw_soft_shadow(rt, r, cp.radius, cp.shadow_color, cp.shadow_dy,
                     cp.shadow_blur);
  if (cp.has_glow) draw_glow(rt, r, cp.radius, cp.glow_color, cp.glow_blur);

  if (cp.grad_bg) {
    rt.fill_round_rect_gradient_angle(r, cp.radius, cp.grad, cp.grad_count,
                                      cp.grad_angle, corner_mask);
  } else if (cp.bg.a > 0.001f) {
    Brush bg{cp.bg};
    rt.round_rect(r, cp.radius, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &bg);
  }
  if (cp.border_w > 0.0f) {
    rt.round_rect(r, cp.radius, Pen{cp.border, cp.border_w}, nullptr);
  }
  // 底缘 1px 实线（← retro-3d boxShadow 首层 '0 1px 0 0 ${bevelShadowDark}'；
  //   其余 5 套皮肤无此层 → has_bottom_bevel=false，不画）
  if (cp.has_bottom_bevel) {
    Brush bv{cp.bottom_bevel_color};
    rt.fill_rect({r.x + cp.radius, r.bottom() - 1, r.w - cp.radius * 2, 1}, bv);
  }
}

// 卡型固定名字（避免每处写字面量拼错）。card_paint 按值返回，这里也按值。
inline theme::CardPaint card_default() {
  return theme::ThemeService::instance().card_paint("default");
}
inline theme::CardPaint card_primary() {
  return theme::ThemeService::instance().card_paint("primary");
}
inline theme::CardPaint card_upload() {
  return theme::ThemeService::instance().card_paint("upload");
}
inline theme::CardPaint card_download() {
  return theme::ThemeService::instance().card_paint("download");
}

}  // namespace clashwin::ui
