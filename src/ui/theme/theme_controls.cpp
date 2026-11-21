// ThemeService 控件族样式表（Button / Segmented / Input / Switch / Slider × 6 皮肤）。
// src: src/utils/button-styles/index.ts
#include "theme.h"
#include "theme_internal.h"

#include <cstring>

namespace clashwin::ui::theme {

using detail::with_alpha;

// ---------- Button（← get3DButtonStyle colorType 分支 × 6 皮肤） ----------
// 【渲染折算】retro-3d 渐变（6 站点）→ 取中段站色为 fill、hover 渐变取其
// 中段站色；多层 bevel boxShadow（×depth/vibrancy）由 Button::draw_button_bg
// 的重写点用 depth_factor()/vibrancy_factor() 绘制。
ButtonPaint ThemeService::button_paint(const char* color_type,
                                      bool outlined) const {
  bool light = is_light();
  bool is_primary = std::strcmp(color_type, "primary") == 0;
  bool is_error = std::strcmp(color_type, "error") == 0;
  // 语义化因子（← Mini 四变量注入 + layout.scss !important 覆盖）
  //   dep → --depth-factor（retro-3d 的 3D 深度）；original/modern-flat 被强制 0
  //   g   → --control-skin-val1（original=Radius / modern=Roundness：圆角）
  //   v   → --vibrancy-factor ≡ --control-skin-val2
  const float dep = depth();
  const float g = val1();
  const float v = vibrancy();
  const Color4f err = palette_.error_color;
  // ← :165 / :235 theme.palette.error.dark：MUI augmentColor 的 dark = darken(main, 0.3)
  const Color4f err_dark = Color4f::lerp(err, Color4f::hex(0x000000), 0.30f);
  ButtonPaint p{};
  p.text_disabled = light ? Color4f::hex(0x9e9e9e) : Color4f::hex(0x6e6e75);
  p.fill_disabled = light ? Color4f::hex(0xe0e0e0) : Color4f::hex(0x32323a);

  switch (skin_) {
    case SkinId::Retro3d: {
      // 金色 3D（← :39-140）：135° 多停渐变 + 浮雕 + 外发光 + 文字阴影 + 位移
      p.use_grad = true;
      p.border_w = 2.0f * dep;   // ← :84 border: calc(2px * --depth-factor)
      p.radius = 6;              // ← :83 borderRadius '6px'（不乘 depth）
      p.weight = 700;            // bold
      p.translate_hover = -2.0f;   // transform: translateY(-2px)
      p.translate_active = 3.0f;   // transform: translateY(3px)
      // ← :89-101 常态 boxShadow 的柔影层：0 3d 2d rgba(0,0,0,.35 | .6)
      p.shadow[0] = {Color4f::hex(0x000000, light ? 0.35f : 0.60f),
                     3.0f * dep, 2.0f * dep};
      p.shadow_n = 1;
      // ← :112-126 悬停柔影加深到 .4 | .7，并再叠一层 0 4d 4d rgba(0,0,0,.2 | .3)
      p.shadow_hover[0] = {Color4f::hex(0x000000, light ? 0.40f : 0.70f),
                           3.0f * dep, 2.0f * dep};
      p.shadow_hover[1] = {Color4f::hex(0x000000, light ? 0.20f : 0.30f),
                           4.0f * dep, 4.0f * dep};
      p.shadow_hover_n = 2;
      // ← :130-136 按下改用 0 d 2px rgba(0,0,0,.5 | .7)，柔影同时收窄
      p.shadow_active[0] = {Color4f::hex(0x000000, light ? 0.50f : 0.70f),
                            1.0f * dep, 2.0f};
      p.shadow_active_n = 1;
      if (is_primary) {
        p.text = Color4f::hex(0x2C1F03);
        p.border = light ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00);
        p.bevel_dark = light ? Color4f::hex(0x8A6D00) : Color4f::hex(0x5D4037);
        p.bevel_hi = light ? Color4f::hex(0xFFFFFF, 0.8f) : Color4f::hex(0xFFFFFF, 0.08f);
        p.glow = Color4f::hex(0xFFC107, 0.2f * v);  // 0.2*vibrancy
        p.glow_blur = 8.0f * v;
        // ← :70-73 hover 变 0 0 12v rgba(255,193,7,.4v)；← :128-137 按下辉光消失
        p.has_glow_hover = true;
        p.glow_hover = Color4f::hex(0xFFC107, 0.4f * v);
        p.glow_hover_blur = 12.0f * v;
        p.has_glow_active = true;
        p.text_shadow = Color4f::hex(0xFFFFFF, 0.45f); p.text_shadow_dy = 1.0f;
        p.grad[0] = {0.0f, Color4f::hex(0xFFE082)};
        p.grad[1] = {0.20f, Color4f::hex(0xFFD54F)};
        p.grad[2] = {0.45f, Color4f::hex(0xFFC107)};
        p.grad[3] = {0.70f, Color4f::hex(0xFFB300)};
        p.grad[4] = {0.85f, Color4f::hex(0xD4AF37)};
        p.grad[5] = {1.0f, Color4f::hex(0xB8860B)};
        p.grad_count = 6;
        p.grad_hover[0] = {0.0f, Color4f::hex(0xFFF59D)};
        p.grad_hover[1] = {0.20f, Color4f::hex(0xFFE082)};
        p.grad_hover[2] = {0.45f, Color4f::hex(0xFFD54F)};
        p.grad_hover[3] = {0.70f, Color4f::hex(0xFFC107)};
        p.grad_hover[4] = {0.85f, Color4f::hex(0xFFB300)};
        p.grad_hover[5] = {1.0f, Color4f::hex(0xD4AF37)};
      } else if (is_error) {
        p.text = Color4f::hex(0xFFFFFF);
        p.border = light ? Color4f::hex(0xB71C1C) : Color4f::hex(0x6B0505);
        p.bevel_dark = light ? Color4f::hex(0x8B0000) : Color4f::hex(0x4A0404);
        p.bevel_hi = light ? Color4f::hex(0xFFFFFF, 0.8f) : Color4f::hex(0xFFFFFF, 0.08f);
        // ← :66-68 glowColor：error 用 rgba(213,0,0, 0.15*vibrancy)
        p.glow = Color4f::hex(0xD50000, 0.15f * v);
        p.glow_blur = 8.0f * v;
        // ← :70-73 hover 是 0.35*vibrancy / 12v
        p.has_glow_hover = true;
        p.glow_hover = Color4f::hex(0xD50000, 0.35f * v);
        p.glow_hover_blur = 12.0f * v;
        p.has_glow_active = true;
        p.text_shadow = Color4f::hex(0x000000, 0.4f); p.text_shadow_dy = -1.0f;
        p.grad[0] = {0.0f, Color4f::hex(0xFF8A80)};
        p.grad[1] = {0.30f, Color4f::hex(0xFF5252)};
        p.grad[2] = {0.75f, Color4f::hex(0xD50000)};
        p.grad[3] = {1.0f, Color4f::hex(0x880E4F)};
        p.grad_count = 4;
        p.grad_hover[0] = {0.0f, Color4f::hex(0xFFAB91)};
        p.grad_hover[1] = {0.30f, Color4f::hex(0xFF8A80)};
        p.grad_hover[2] = {0.75f, Color4f::hex(0xFF5252)};
        p.grad_hover[3] = {1.0f, Color4f::hex(0xC62828)};
      } else {
        p.text = light ? Color4f::hex(0x3C2F0F) : Color4f::hex(0x2C1F03);
        p.border = light ? Color4f::hex(0xD4AF37) : Color4f::hex(0x8A6D00);
        p.bevel_dark = light ? Color4f::hex(0xB8860B) : Color4f::hex(0x755500);
        p.bevel_hi = light ? Color4f::hex(0xFFFFFF, 0.8f) : Color4f::hex(0xFFFFFF, 0.08f);
        p.glow = Color4f::hex(0xFFC107, 0.2f * v);
        p.glow_blur = 8.0f * v;
        p.has_glow_hover = true;
        p.glow_hover = Color4f::hex(0xFFC107, 0.4f * v);
        p.glow_hover_blur = 12.0f * v;
        p.has_glow_active = true;
        p.text_shadow = Color4f::hex(0xFFFFFF, 0.45f); p.text_shadow_dy = 1.0f;
        if (outlined) {
          // ← :57-63 outlined && default：linear-gradient(to bottom,
          //   rgba(255,253,231,.4) → rgba(255,245,157,.2)) / 暗色两档
          p.grad_angle = 180.0f;
          p.grad[0] = {0.0f, light ? Color4f::hex(0xFFFDE7, 0.40f)
                                   : Color4f::hex(0x2A2413, 0.40f)};
          p.grad[1] = {1.0f, light ? Color4f::hex(0xFFF59D, 0.20f)
                                   : Color4f::hex(0x17140B, 0.20f)};
          p.grad_count = 2;
          // ← :40-52 outlined default：border #D4AF37/#8A6D00、文字 #3C2F0F/#FFE082
          p.border = light ? Color4f::hex(0xD4AF37) : Color4f::hex(0x8A6D00);
          p.text = light ? Color4f::hex(0x3C2F0F) : Color4f::hex(0xFFE082);
          // src: src/utils/button-styles/button.ts:98-100 outlined 悬停四停不透明浅金渐变
          if (light) {
            p.grad_hover[0] = {0.00f, Color4f::hex(0xFFFDE7)};
            p.grad_hover[1] = {0.20f, Color4f::hex(0xFFF9C4)};
            p.grad_hover[2] = {0.50f, Color4f::hex(0xFFF59D)};
            p.grad_hover[3] = {1.00f, Color4f::hex(0xFFD54F)};
          } else {
            p.grad_hover[0] = {0.00f, Color4f::hex(0xFFF9C4)};
            p.grad_hover[1] = {0.20f, Color4f::hex(0xFFF59D)};
            p.grad_hover[2] = {0.50f, Color4f::hex(0xFBC02D)};
            p.grad_hover[3] = {1.00f, Color4f::hex(0xF57F17)};
          }
          p.grad_hover_count = 4;
        } else if (light) {
          p.grad[0] = {0.0f, Color4f::hex(0xFFFDE7)};
          p.grad[1] = {0.35f, Color4f::hex(0xFFF9C4)};
          p.grad[2] = {0.70f, Color4f::hex(0xFFF59D)};
          p.grad[3] = {1.0f, Color4f::hex(0xFFE082)};
          p.grad_count = 4;
          p.grad_hover[0] = {0.0f, Color4f::hex(0xFFFDE7)};
          p.grad_hover[1] = {0.20f, Color4f::hex(0xFFF9C4)};
          p.grad_hover[2] = {0.50f, Color4f::hex(0xFFF59D)};
          p.grad_hover[3] = {1.0f, Color4f::hex(0xFFD54F)};
        } else {
          p.grad[0] = {0.0f, Color4f::hex(0xFFF59D)};
          p.grad[1] = {0.35f, Color4f::hex(0xFBC02D)};
          p.grad[2] = {0.70f, Color4f::hex(0xF57F17)};
          p.grad[3] = {1.0f, Color4f::hex(0xE65100)};
          p.grad_count = 4;
          p.grad_hover[0] = {0.0f, Color4f::hex(0xFFF9C4)};
          p.grad_hover[1] = {0.20f, Color4f::hex(0xFFF59D)};
          p.grad_hover[2] = {0.50f, Color4f::hex(0xFBC02D)};
          p.grad_hover[3] = {1.0f, Color4f::hex(0xF57F17)};
        }
      }
      p.fill = p.grad[p.grad_count / 2].color;   // last_fill_ 兜底
      p.fill_hover = p.grad_hover[p.grad_count / 2].color;
      p.fill_pressed = p.fill;  // active = inset bevel（沿用 default 渐变）
      // 禁用态 ← :138-145 `&.Mui-disabled`
      p.fill_disabled = light ? Color4f::hex(0xe0e0e0) : Color4f::hex(0x32323a);
      p.text_disabled = light ? Color4f::hex(0x9e9e9e) : Color4f::hex(0x6e6e75);
      p.border_disabled = light ? Color4f::hex(0xbdbdbd) : Color4f::hex(0x4a4a52);
      break;
    }
    case SkinId::Original: {
      // ← :144-196：primary=var(--primary-main) 白字；default 白底/暗 #2e303d
      p.fill = is_primary ? primary() : (is_error ? err
                                            : (light ? Color4f::hex(0xffffff)
                                                     : Color4f::hex(0x2e303d)));
      p.fill_hover = is_primary ? Color4f::lerp(primary(), Color4f::hex(0x000000), 0.05f)
                                : (is_error ? err_dark
                                            : (light ? Color4f::hex(0xf5f5f5)
                                                     : Color4f::hex(0x242530)));
      p.fill_pressed = p.fill_hover;
      p.text = (is_primary || is_error) ? Color4f::hex(0xFFFFFF) : text_primary();
      p.border = is_primary ? primary()
                         : (is_error ? err : vars_.theme_border);
      p.radius = 4.0f * g;   // ← :186 borderRadius calc(4px * val1)
      p.border_w = 1.0f;     // ← :185 border: '1px solid'
      p.weight = 500;
      p.translate_active = 1.0f;   // transform: translateY(1px)
      if (is_primary || is_error) {
        p.shadow[0] = {Color4f::hex(0x000000, 0.15f), 1.0f, 3.0f};
        p.shadow_n = 1;
        // ← :199-200 hover 0 2px 6px rgba(0,0,0,.2)
        p.shadow_hover[0] = {Color4f::hex(0x000000, 0.20f), 2.0f, 6.0f};
        p.shadow_hover_n = 1;
        // ← :204-205 active 0 0 2px rgba(0,0,0,.15)
        p.shadow_active[0] = {Color4f::hex(0x000000, 0.15f), 0.0f, 2.0f};
        p.shadow_active_n = 1;
      }
      // 禁用态 ← :207-213
      p.fill_disabled = light ? Color4f::hex(0xf5f5f5) : Color4f::hex(0x1e1e1e);
      p.border_disabled = light ? Color4f::hex(0xe0e0e0) : Color4f::hex(0x333333);
      p.text_disabled = light ? Color4f::hex(0xc0c0c0) : Color4f::hex(0x555555);
      break;
    }
    case SkinId::ModernFlat: {
      // ← :199-255：primary=palette.primary；default #f3f4f6/#1f2937
      p.fill = is_primary ? primary() : (is_error ? err
                                            : (light ? Color4f::hex(0xf3f4f6)
                                                     : Color4f::hex(0x1f2937)));
      // ← :230-237 hoverBg：primary 用 palette.primary.dark、error 用 error.dark
      p.fill_hover = is_primary ? Color4f::lerp(primary(), Color4f::hex(0x000000), 0.30f)
                             : (is_error ? err_dark
                                         : (light ? Color4f::hex(0xe5e7eb)
                                                  : Color4f::hex(0x374151)));
      p.fill_pressed = p.fill_hover;
      p.text = (is_primary || is_error) ? Color4f::hex(0xFFFFFF) : text_primary();
      p.border = is_primary ? primary()
                         : (is_error ? err
                                     : (light ? Color4f::hex(0xd1d5db)
                                              : Color4f::hex(0x4b5563)));
      p.radius = 6.0f * g;   // ← :258 borderRadius calc(6px * val1)
      p.border_w = 1.0f;     // ← :257 border: '1px solid'
      p.weight = 700;
      p.translate_active = 1.0f;   // transform: translateY(1px)
      // ← :263-264 boxShadow 0 2v 4v rgba(0,0,0, .06v)
      p.shadow[0] = {Color4f::hex(0x000000, 0.06f * v), 2.0f * v, 4.0f * v};
      p.shadow_n = 1;
      // ← :268-269 hover 0 3v 6v rgba(0,0,0, .06*2*v)
      p.shadow_hover[0] = {Color4f::hex(0x000000, 0.06f * 2.0f * v), 3.0f * v,
                           6.0f * v};
      p.shadow_hover_n = 1;
      // ← :273-274 active 0 1v 2v rgba(0,0,0, .06v)
      p.shadow_active[0] = {Color4f::hex(0x000000, 0.06f * v), 1.0f * v,
                            2.0f * v};
      p.shadow_active_n = 1;
      // 禁用态 ← :276-282
      p.fill_disabled = light ? Color4f::hex(0xf3f4f6) : Color4f::hex(0x1f2937);
      p.border_disabled = light ? Color4f::hex(0xe5e7eb) : Color4f::hex(0x374151);
      p.text_disabled = light ? Color4f::hex(0x9ca3af) : Color4f::hex(0x4b5563);
      break;
    }
    case SkinId::FrostedGlass: {
      // src: src/utils/button-styles/button.ts:279-296 primary / error / default 填充与 hover
      p.fill = is_primary ? with_alpha(primary(), 0.25f)
                          : (is_error ? Color4f::hex(0xEF4444, 0.25f)
                                      : (light ? Color4f::hex(0x000000, 0.06f)
                                               : Color4f::hex(0xFFFFFF, 0.08f)));
      p.fill_hover = is_primary
                         ? with_alpha(primary(), 0.40f)
                         : (is_error ? Color4f::hex(0xEF4444, 0.40f)
                                     : (light ? Color4f::hex(0x000000, 0.12f)
                                              : Color4f::hex(0xFFFFFF, 0.15f)));
      p.fill_pressed = p.fill_hover;
      // src: src/utils/button-styles/button.ts:303-308 error 态保留可读红色文字/边框，其余沿用主文色
      p.text = is_error ? (light ? Color4f::hex(0xB91C1C) : Color4f::hex(0xFCA5A5))
                        : text_primary();
      p.border = is_error ? Color4f::hex(0xEF4444, 0.45f)
                          : (light ? Color4f::hex(0x000000, 0.12f)
                                   : Color4f::hex(0xFFFFFF, 0.12f));
      p.radius = 8;               // ← :318 borderRadius '8px'（不乘 val1）
      p.border_w = 1.0f;          // ← :317 border: 1px solid rgba(...)
      p.weight = 400;
      p.translate_hover = -1.0f;   // translateY(-1px)
      p.translate_active = 1.0f;   // translateY(1px)
      // ← :320 boxShadow 0 4px 12px rgba(0,0,0,.08)
      p.shadow[0] = {Color4f::hex(0x000000, 0.08f), 4.0f, 12.0f};
      p.shadow_n = 1;
      // ← :323-324 hover 0 6px 16px rgba(0,0,0,.12)
      p.shadow_hover[0] = {Color4f::hex(0x000000, 0.12f), 6.0f, 16.0f};
      p.shadow_hover_n = 1;
      // ← :327-328 active 0 2px 4px rgba(0,0,0,.04)
      p.shadow_active[0] = {Color4f::hex(0x000000, 0.04f), 2.0f, 4.0f};
      p.shadow_active_n = 1;
      // 禁用态 ← :330-343
      p.fill_disabled = Color4f::hex(light ? 0x000000 : 0xFFFFFF, 0.03f);
      p.border_disabled = Color4f::hex(light ? 0x000000 : 0xFFFFFF, light ? 0.08f : 0.05f);
      p.text_disabled = Color4f::hex(light ? 0x000000 : 0xFFFFFF, light ? 0.26f : 0.25f);
      break;
    }
    case SkinId::Cyberpunk: {
      // ← :350-420：霓虹描边 + boxShadow 0 0 calc(5d) borderClr
      Color4f neon = is_primary ? Color4f::hex(0x00f0ff)
                             : (is_error ? Color4f::hex(0xff0055)
                                      : Color4f::hex(0x39ff14));
      p.fill = light ? Color4f::hex(0xffffff)
                     : (is_primary ? Color4f::hex(0x0a0e17)
                                : (is_error ? Color4f::hex(0x1c0a0a)
                                         : Color4f::hex(0x0d1117)));
      p.fill_hover = light ? (is_primary ? Color4f::hex(0xe0f7fa)
                                      : (is_error ? Color4f::hex(0xffebee)
                                                  : Color4f::hex(0xe8f5e9)))
                           : (is_primary ? Color4f::hex(0x101726)
                                      : (is_error ? Color4f::hex(0x2c1010)
                                                  : Color4f::hex(0x161b22)));
      p.fill_pressed = p.fill_hover;
      // ← :381-389 textClr / borderClr 按 colorType × light 分别取值
      p.text = light ? (is_primary ? Color4f::hex(0x006064)
                                : (is_error ? Color4f::hex(0xc2185b)
                                            : Color4f::hex(0x1b5e20)))
                     : neon;
      p.border = light ? (is_primary ? Color4f::hex(0x00bcd4)
                                  : (is_error ? Color4f::hex(0xff0055)
                                              : Color4f::hex(0x4caf50)))
                       : neon;
      p.radius = 0;               // ← :401 borderRadius '0px'
      p.border_w = 1.0f;          // ← :400 border: '1px solid'
      p.weight = 700;
      p.translate_hover = -1.0f;    // translateY(-1px)
      p.translate_active = 1.0f;    // translateY(1px)
      // ← :404 boxShadow 0 0 calc(5px * depth) borderClr（常态即亮）
      p.glow = with_alpha(p.border, 0.55f);
      p.glow_blur = 5.0f * dep;
      // ← :407 hover 0 0 10d；← :412 active 0 0 2d（颜色恒为 borderClr）
      p.has_glow_hover = true;
      p.glow_hover = p.glow;
      p.glow_hover_blur = 10.0f * dep;
      p.has_glow_active = true;
      p.glow_active = p.glow;
      p.glow_active_blur = 2.0f * dep;
      // 禁用态 ← :414-420
      p.fill_disabled = light ? Color4f::hex(0xf5f5f5) : Color4f::hex(0x0d1117);
      p.border_disabled = light ? Color4f::hex(0xe0e0e0) : Color4f::hex(0x21262d);
      p.text_disabled = light ? Color4f::hex(0x9e9e9e) : Color4f::hex(0x484f58);
      break;
    }
    case SkinId::Monochrome: {
      // ← :424-486：黑白对调
      p.fill = is_primary ? (light ? Color4f::hex(0x000000) : Color4f::hex(0xffffff))
                       : (is_error ? (light ? Color4f::hex(0x333333)
                                            : Color4f::hex(0xcccccc))
                                   : (light ? Color4f::hex(0xffffff)
                                            : Color4f::hex(0x000000)));
      p.fill_hover = is_primary
                         ? (light ? Color4f::hex(0x333333) : Color4f::hex(0xcccccc))
                         : (light ? Color4f::hex(0xcccccc) : Color4f::hex(0x333333));
      p.fill_pressed = p.fill_hover;
      p.text = is_primary ? (light ? Color4f::hex(0xffffff) : Color4f::hex(0x000000))
                       : (light ? Color4f::hex(0x000000) : Color4f::hex(0xffffff));
      p.border = light ? Color4f::hex(0x000000) : Color4f::hex(0xffffff);
      // src: src/assets/styles/index.scss:257-269 .MuiButton-root radius = 8*val1
      p.radius = 8.0f * g;
      p.border_w = 1.0f * v;  // ← :454 calc(1px * --vibrancy-factor)
      p.weight = 700;
      p.translate_active = 1.0f;   // transform: translateY(1px)
      // 禁用态 ← :478-484
      p.fill_disabled = light ? Color4f::hex(0xf5f5f5) : Color4f::hex(0x222222);
      p.border_disabled = light ? Color4f::hex(0xcccccc) : Color4f::hex(0x444444);
      p.text_disabled = light ? Color4f::hex(0x999999) : Color4f::hex(0x666666);
      break;
    }
  }
  return p;
}

// ---------- Segmented（← get3DSegmentedContainer/Active/TextColor） ----------
SegmentedPaint ThemeService::segmented_paint() const {
  bool light = is_light();
  // 语义化因子（← Mini 四变量注入 + layout.scss !important 覆盖）
  const float d = depth();       // --depth-factor
  const float v = vibrancy();    // --vibrancy-factor
  const float g = val1();        // --control-skin-val1
  SegmentedPaint p{};
  // src: src/pages/_layout/components/takeover-mode-card.tsx:46,113 仅 retro-3d Dark 覆盖未选字色
  p.text = (skin_ == SkinId::Retro3d && !light)
               ? Color4f::hex(0x2C1F03, 0.75f)
               : palette_.secondary_text;
  switch (skin_) {
    case SkinId::Retro3d:
      p.container_bg = light ? Color4f::hex(0xD4AF37, 0.06f)
                             : Color4f::hex(0xD4AF37, 0.08f);
      p.container_border = light ? Color4f::hex(0xD4AF37, 0.35f)
                                 : Color4f::hex(0xD4AF37, 0.22f);
      p.active_bg = Color4f::hex(0xFFC107);  // 渐变 45% 站
      p.active_border = light ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00);
      p.active_text = Color4f::hex(0x2C1F03);  // ← ActiveTextColor retro
      p.active_radius = 3;
      // ← :1006-1007 选中块 135° 六停金渐变
      p.active_grad[0] = {0.00f, Color4f::hex(0xFFE082)};
      p.active_grad[1] = {0.20f, Color4f::hex(0xFFD54F)};
      p.active_grad[2] = {0.45f, Color4f::hex(0xFFC107)};
      p.active_grad[3] = {0.70f, Color4f::hex(0xFFB300)};
      p.active_grad[4] = {0.85f, Color4f::hex(0xD4AF37)};
      p.active_grad[5] = {1.00f, Color4f::hex(0xB8860B)};
      p.active_grad_count = 6;
      p.active_grad_angle = 135.0f;
      // ← :934-938 容器 boxShadow：inset 0 2.5d 4.5d α.2(亮) / 0 3d 5d α.7(暗)
      p.container_inset = true;
      p.container_inset_color = light ? Color4f::hex(0x000000, 0.20f)
                                      : Color4f::hex(0x000000, 0.70f);
      p.container_inset_dy = (light ? 2.5f : 3.0f) * d;
      p.container_inset_blur = (light ? 4.5f : 5.0f) * d;
      p.container_top_hi = light ? Color4f::hex(0xFFFFFF, 0.80f)
                                 : Color4f::hex(0xFFFFFF, 0.08f);
      // ← :1009-1020 选中块 boxShadow：多层硬 bevel（1d/2d）+ 金辉光 0 0 6v/8v
      p.active_has_bevel = true;
      p.active_bevel_dark = light ? Color4f::hex(0x8A6D00)
                                  : Color4f::hex(0x5D4037);
      p.active_bevel_dy = 2.0f * d;
      p.active_has_glow = true;
      p.active_glow_color = Color4f::hex(0xFFC107, (light ? 0.20f : 0.25f) * v);
      p.active_glow_blur = (light ? 6.0f : 8.0f) * v;
      break;
    case SkinId::Original:
      p.container_bg = light ? Color4f::hex(0xf1f5f9) : Color4f::hex(0x1e1f29);
      p.container_border = light ? Color4f::hex(0xcbd5e1) : Color4f::hex(0x475569);
      p.active_bg = primary();  // var(--primary-main)
      p.active_text = Color4f::hex(0xFFFFFF);
      // ← :1033 borderRadius calc(3px * var(--control-skin-val1))
      p.active_radius = 3.0f * g;
      // ← :954 容器 borderRadius calc(4px * var(--control-skin-val1))
      p.container_radius = 4.0f * g;
      break;
    case SkinId::ModernFlat:
      p.container_bg = light ? Color4f::hex(0xf3f4f6) : Color4f::hex(0x111827);
      p.container_border = light ? Color4f::hex(0xd1d5db) : Color4f::hex(0x4b5563);
      p.active_bg = primary();
      p.active_text = Color4f::hex(0xFFFFFF);
      // ← :1048 borderRadius calc(4px * var(--control-skin-val1))
      p.active_radius = 4.0f * g;
      break;
    case SkinId::FrostedGlass:
      p.container_bg = Color4f::hex(light ? 0x000000 : 0xFFFFFF, 0.06f);
      p.container_border = Color4f::hex(light ? 0x000000 : 0xFFFFFF, 0.12f);
      p.active_bg = with_alpha(primary(), 0.30f);
      p.active_border = Color4f::hex(light ? 0x000000 : 0xFFFFFF, 0.20f);
      p.active_text = light ? Color4f::hex(0x000000, 0.85f)
                            : Color4f::hex(0xFFFFFF);
      p.active_radius = 4;
      break;
    case SkinId::Cyberpunk:
      p.container_border = light ? Color4f::hex(0x00b0ff) : Color4f::hex(0x39ff14);
      p.container_bg = light ? Color4f::hex(0xffffff) : Color4f::hex(0x05070c);
      p.active_bg = light ? Color4f::hex(0x00b0ff) : Color4f::hex(0x39ff14);
      p.active_text = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x000000);
      p.active_radius = 0;
      // ← :1081-1082 选中块 boxShadow：0 0 calc(8px * var(--depth-factor)) 霓虹色
      p.active_has_glow = true;
      p.active_glow_color = light ? Color4f::hex(0x00B0FF) : Color4f::hex(0x39FF14);
      p.active_glow_blur = 8.0f * d;
      break;
    case SkinId::Monochrome:
      p.container_border = light ? Color4f::hex(0x000000) : Color4f::hex(0xffffff);
      p.container_bg = light ? Color4f::hex(0xffffff) : Color4f::hex(0x000000);
      p.active_bg = light ? Color4f::hex(0x000000) : Color4f::hex(0xffffff);
      p.active_text = light ? Color4f::hex(0xffffff) : Color4f::hex(0x000000);
      // src: src/utils/button-styles/segmented.ts:183 borderRadius: '0px'
      p.active_radius = 0.0f;
      p.container_radius = 0.0f;
      break;
  }
  return p;
}

// ---------- Input（← get3DInputStyle 骨架：input bg/border） ----------
InputPaint ThemeService::input_paint() const {
  const bool light = is_light();
  const float d = depth();       // --depth-factor
  const float v = vibrancy();    // --vibrancy-factor ≡ --control-skin-val2
  const float g = val1();        // --control-skin-val1
  InputPaint p{};
  p.bg = vars_.theme_input_bg;
  p.border = vars_.theme_input_border;
  p.border_focused = with_alpha(primary(), 1.0f);
  p.radius = 4.0f;
  p.border_w = 1.0f;
  p.border_w_focused = 1.0f;
  p.text = text_primary();
  p.placeholder = with_alpha(p.text, p.text.a * 0.5f);

  switch (skin_) {
    case SkinId::Retro3d:
      // :739-774：底 rgba(255,253,231,.8)/rgba(26,22,14,.4)、描边 2d
      p.bg = light ? Color4f::hex(0xFFFDE7, 0.8f)
                   : Color4f::hex(0x1A160E, 0.4f);
      p.border = light ? Color4f::hex(0xD4AF37, 0.35f)
                       : Color4f::hex(0xD4AF37, 0.25f);
      // src: src/utils/button-styles/input.ts:29-34
      p.border_hover = Color4f::hex(0xD4AF37, light ? 0.60f : 0.50f);
      p.border_w = 2.0f * d;
      p.border_w_focused = 2.0f * d;
      p.border_focused = light ? Color4f::hex(0xB8860B) : Color4f::hex(0xFFA000);
      p.has_inset = true;
      p.inset_color = light ? Color4f::hex(0x000000, 0.20f)
                            : Color4f::hex(0x000000, 0.60f);
      p.inset_dy = (light ? 3.0f : 4.0f) * d;
      p.inset_blur = (light ? 5.0f : 6.0f) * d;
      p.inset_top_hi = light ? Color4f::hex(0xFFFFFF, 0.80f)
                             : Color4f::hex(0xFFFFFF, 0.08f);
      p.focus_glow = true;
      p.focus_glow_color = Color4f::hex(0xFFC107, 0.35f * v);
      p.focus_glow_blur = 8.0f * v;
      p.text = light ? Color4f::hex(0x2C1F03) : Color4f::hex(0xFFFDE7);
      p.placeholder = with_alpha(p.text, 0.5f);
      break;

    case SkinId::Original:
      // :779-806：白底/暗 #1e1f29、1px #cbd5e1/#475569、radius 4*val1、focus --primary-main 1.5px
      p.bg = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x1E1F29);
      p.text = light ? Color4f::hex(0x1E293B) : Color4f::hex(0xF1F5F9);
      p.placeholder = with_alpha(p.text, 0.5f);
      p.border = light ? Color4f::hex(0xCBD5E1) : Color4f::hex(0x475569);
      // src: src/utils/button-styles/input.ts:64-66
      p.border_hover = light ? Color4f::hex(0x94A3B8) : Color4f::hex(0x64748B);
      p.border_w = 1.0f;
      p.border_focused = primary();
      p.border_w_focused = 1.5f;
      p.radius = 4.0f * g;   // ← :787 borderRadius calc(4px * var(--control-skin-val1))
      break;

    case SkinId::ModernFlat:
      // :808-837：白底/暗 #111827、radius 6*val1、1px #d1d5db/#4b5563
      p.bg = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x111827);
      p.border = light ? Color4f::hex(0xD1D5DB) : Color4f::hex(0x4B5563);
      // src: src/utils/button-styles/input.ts:91-93
      p.border_hover = primary();
      p.border_focused = primary();
      p.radius = 6.0f * g;   // ← :811 borderRadius calc(6px * var(--control-skin-val1))
      p.has_inset = true;
      p.inset_color = Color4f::hex(0x000000, 0.06f * v);
      p.inset_dy = 1.0f * v;
      p.inset_blur = 3.0f * v;
      break;

    case SkinId::FrostedGlass:
      // src: src/utils/button-styles/input.ts:104-124 & src/pages/_layout/hooks/use-custom-theme.ts:248
      p.bg = light ? Color4f::hex(0xFFFFFF, 0.55f)
                   : Color4f::hex(0xFFFFFF, 0.05f);
      // src: src/utils/button-styles/input.ts:112-117
      p.border = light ? Color4f::hex(0xFFFFFF, 0.65f)
                       : Color4f::hex(0xFFFFFF, 0.10f);
      p.border_hover = Color4f::hex(0xFFFFFF, 0.25f);
      p.border_focused = primary();
      // src: src/pages/_layout/hooks/use-custom-theme.ts:248 borderRadius 4
      p.radius = 4.0f;
      break;

    case SkinId::Cyberpunk:
      // :868-906：白底/暗 #030508、1px #cbd5e1/#ff0055
      p.bg = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x030508);
      p.text = light ? Color4f::hex(0x0F172A) : Color4f::hex(0xFF0055);
      p.placeholder = with_alpha(p.text, 0.5f);
      p.border = light ? Color4f::hex(0xCBD5E1) : Color4f::hex(0xFF0055);
      // src: src/utils/button-styles/input.ts:132,874-876
      p.border_hover = light ? Color4f::hex(0xFF0055) : Color4f::hex(0x00F0FF);
      p.border_focused = Color4f::hex(0xFF0055);
      if (light) {
        p.has_inset = true;
        p.inset_color = Color4f::hex(0x000000, 0.05f);
        p.inset_dy = 1.0f;
        p.inset_blur = 3.0f;
        p.focus_ring = 3.0f;
        p.focus_glow_color = Color4f::hex(0xFF0055, 0.15f);
      } else {
        p.focus_glow = true;
        p.focus_glow_color = Color4f::hex(0xFF0055, 0.35f * d);
        p.focus_glow_blur = 8.0f * d;
      }
      p.radius = 4.0f;
      break;

    case SkinId::Monochrome:
      // :908-926：黑白底、描边 1*v、focus 2*v（不乘 depth）
      p.bg = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x000000);
      p.text = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      p.placeholder = with_alpha(p.text, 0.5f);
      p.border = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      p.border_hover = p.border;
      p.border_focused = p.border;
      p.border_w = 1.0f * v;
      p.border_w_focused = 2.0f * v;
      // src: src/assets/styles/index.scss:257-269 .MuiOutlinedInput-root radius = 8*val1
      p.radius = 8.0f * g;
      break;
  }
  return p;
}

// ---------- Switch（← base-switch.tsx × 6 皮肤） ----------
SwitchPaint ThemeService::switch_paint() const {
  const bool light = is_light();
  const float d = depth();       // --depth-factor（retro-3d 的 3D 深度）
  const float v = vibrancy();    // --vibrancy-factor
  const float g = val1();        // --control-skin-val1（original 的轨道圆角）
  SwitchPaint p{};
  p.off_bg = Color4f::hex(0xD4AF37, light ? 0.25f : 0.15f);
  p.off_border = Color4f::hex(0xD4AF37, light ? 0.30f : 0.18f);
  p.off_bw = 1.0f;
  p.on_bg = Color4f::hex(0xFFC107);
  p.off_thumb = Color4f::hex(0xFFD54F);
  p.on_thumb = Color4f::hex(0xFFD54F);

  switch (skin_) {
    case SkinId::Retro3d: {
      // :15-140：28×14、track r7、off rgba(212,175,55,.25/.15)*.85 +
      //   1px rgba(.3/.18)、on = to-bottom 三停 @.85、thumb 14 radial 四停
      p.track_w = 28.0f;
      p.track_h = 14.0f;
      p.track_radius = 7.0f;
      p.off_bg = Color4f::hex(0xD4AF37, (light ? 0.25f : 0.15f) * 0.85f);
      // src: src/components/base/base-switch.tsx:195
      p.off_border = Color4f::hex(0xD4AF37, light ? 0.30f : 0.18f);
      p.off_bw = 1.0f;
      // inset 0 calc(2d|3d) calc(4d|5d) rgba(0,0,0,.25|.7)
      p.off_inset = true;
      p.off_inset_color = Color4f::hex(0x000000, light ? 0.25f : 0.70f);
      p.off_inset_dy = (light ? 2.0f : 3.0f) * d;
      p.off_inset_blur = (light ? 4.0f : 5.0f) * d;
      // 0 1px 0 rgba(255,255,255,.8|.08)
      p.off_bottom_hi = true;
      p.off_bottom_hi_color = Color4f::hex(0xFFFFFF, light ? 0.80f : 0.08f);
      p.off_bottom_hi_w = 1.0f;
      p.on_grad = true;
      p.on_grad_stops[0] = {0.0f, Color4f::hex(0xFFE082, 0.85f)};
      p.on_grad_stops[1] = {0.5f, Color4f::hex(0xFFC107, 0.85f)};
      p.on_grad_stops[2] = {1.0f, Color4f::hex(0xB8860B, 0.85f)};
      p.on_grad_n = 3;
      p.on_bw = 0.0f;
      // :60-79 on 态轨道 boxShadow
      p.on_inset = true;
      p.on_inset_color = Color4f::hex(0x000000, light ? 0.30f : 0.75f);
      p.on_inset_dy = (light ? 2.0f : 3.0f) * d;
      p.on_inset_blur = (light ? 4.0f : 5.0f) * d;
      p.on_bottom_hi = true;
      p.on_bottom_hi_color = Color4f::hex(0xFFFFFF, light ? 0.80f : 0.08f);
      p.on_bottom_hi_w = 1.0f;
      p.thumb_d = 14.0f;
      p.thumb_radius = 7.0f;
      // 刻意偏差（2026-11-20 用户裁决）：Trump-3D 开关圆点（含关态与开态）统一复用主题设置滑轨的
      //   四停不透明 3D 亮金圆珠样式与顶部高光（取消压暗底线），由凹槽与发光金槽区分开关态。
      // src: src/pages/_layout/utils/style-helpers.tsx:191-194
      p.off_thumb_grad = true;
      p.off_thumb_stops[0] = {0.00f, Color4f::hex(light ? 0xFFFDE7 : 0xFFF9C4)};
      p.off_thumb_stops[1] = {0.45f, Color4f::hex(light ? 0xFFD54F : 0xFFC107)};
      p.off_thumb_stops[2] = {0.80f, Color4f::hex(0xFFA000)};
      p.off_thumb_stops[3] = {1.00f, Color4f::hex(light ? 0xB8860B : 0x8A6D00)};
      p.off_thumb_n = 4;
      p.off_thumb_border = light ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00);
      p.off_thumb_bw = 1.0f;
      // src: src/pages/_layout/utils/style-helpers.tsx:197,200 顶部内高光
      p.off_thumb_inset_hi = true;
      p.off_thumb_inset_hi_color = Color4f::hex(0xFFFFFF, light ? 0.80f : 0.40f);
      p.off_thumb_inset_hi_dy = 1.0f * d;
      p.off_thumb_inset_dark = false;
      p.on_thumb_grad = true;
      // src: src/pages/_layout/utils/style-helpers.tsx:191-194
      p.on_thumb_stops[0] = p.off_thumb_stops[0];
      p.on_thumb_stops[1] = p.off_thumb_stops[1];
      p.on_thumb_stops[2] = p.off_thumb_stops[2];
      p.on_thumb_stops[3] = p.off_thumb_stops[3];
      p.on_thumb_n = 4;
      p.on_thumb_border = p.off_thumb_border;
      p.on_thumb_bw = 1.0f;
      // src: src/pages/_layout/utils/style-helpers.tsx:197,200 顶部内高光
      p.on_thumb_inset_hi = true;
      p.on_thumb_inset_hi_color = p.off_thumb_inset_hi_color;
      p.on_thumb_inset_hi_dy = 1.0f * d;
      p.on_thumb_inset_dark = false;
      // src: src/pages/_layout/utils/style-helpers.tsx:196,199 常态外阴影
      p.thumb_shadow = true;
      p.thumb_shadow_color = light ? Color4f::hex(0x000000, 0.25f)
                                   : Color4f::hex(0x000000, 0.60f);
      p.thumb_shadow_dy = (light ? 3.0f : 4.0f) * d;
      p.thumb_shadow_blur = (light ? 6.0f : 8.0f) * d;
      // src: src/components/base/base-switch.tsx:158 on 态轨道外发光
      p.on_glow = true;
      p.on_glow_color = Color4f::hex(0xFFC107, 0.40f * v);
      p.on_glow_blur = 8.0f * v;
      // src: src/pages/_layout/utils/style-helpers.tsx:204-213 悬停放大、提亮、高光、阴影与金发光
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.2f;
      p.thumb_hover_brightness = 1.15f;
      p.thumb_hover_shadow = true;
      p.thumb_hover_shadow_color = light ? Color4f::hex(0x000000, 0.35f)
                                         : Color4f::hex(0x000000, 0.70f);
      p.thumb_hover_shadow_dy = (light ? 5.0f : 6.0f) * d;
      p.thumb_hover_shadow_blur = (light ? 10.0f : 12.0f) * d;
      // src: src/pages/_layout/utils/style-helpers.tsx:209,212 悬停顶高光
      p.thumb_hover_inset_hi = true;
      p.thumb_hover_inset_hi_color =
          Color4f::hex(0xFFFFFF, light ? 0.90f : 0.50f);
      p.thumb_hover_inset_hi_dy = 1.0f * d;
      p.thumb_hover_inset_dark = false;
      // src: src/pages/_layout/utils/style-helpers.tsx:210,213 悬停金辉光
      p.thumb_hover_glow = true;
      p.thumb_hover_glow_color =
          Color4f::hex(0xFFC107, (light ? 0.35f : 0.40f) * v);
      p.on_thumb_hover_glow_color = p.thumb_hover_glow_color;
      p.thumb_hover_glow_blur = (light ? 12.0f : 14.0f) * v;
      break;
    }
    case SkinId::Original:
      // :145-198：28×14、track r7*val1、off #e2e8f0/#30363d 无边
      p.track_w = 28.0f;
      p.track_h = 14.0f;
      p.track_radius = 7.0f * g;   // ← base-switch.tsx:188 calc(7px * var(--control-skin-val1))
      p.off_bg = light ? Color4f::hex(0xE2E8F0) : Color4f::hex(0x30363D);
      p.off_bw = 0.0f;
      p.on_bg = primary();
      p.on_bw = 0.0f;
      p.thumb_radius = 7.0f * g;   // ← base-switch.tsx:194 calc(7px * var(--control-skin-val1))
      // src: src/components/base/base-switch.tsx:276
      p.off_thumb = light ? Color4f::hex(0xCBD5E1) : Color4f::hex(0x475569);
      p.off_thumb_bw = 0.0f;
      p.on_thumb = Color4f::hex(0xFFFFFF);
      p.on_thumb_bw = 0.0f;
      break;

    case SkinId::ModernFlat:
      // :200-278：28×14、off #e5e7eb/#374151、on 主色、thumb 白无边
      p.track_w = 28.0f;
      p.track_h = 14.0f;
      p.track_radius = 7.0f;
      p.off_bg = light ? Color4f::hex(0xE5E7EB) : Color4f::hex(0x374151);
      p.off_bw = 0.0f;
      p.on_bg = primary();
      p.on_bw = 0.0f;
      // src: src/components/base/base-switch.tsx:347
      p.off_thumb = light ? Color4f::hex(0xD1D5DB) : Color4f::hex(0x9CA3AF);
      p.off_thumb_bw = 0.0f;
      p.on_thumb = Color4f::hex(0xFFFFFF);
      p.on_thumb_bw = 0.0f;
      // thumb boxShadow '0 1px 2px rgba(0,0,0,0.15)'（:262）
      p.thumb_shadow = true;
      p.thumb_shadow_color = Color4f::hex(0x000000, 0.15f);
      p.thumb_shadow_dy = 1.0f;
      p.thumb_shadow_blur = 2.0f;
      // ← base-switch.tsx:361-363 `&:hover .MuiSwitch-thumb`
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.1f;
      break;

    case SkinId::FrostedGlass:
      // :280-372：28×14、thumb 12 + margin 1
      p.track_w = 28.0f;
      p.track_h = 14.0f;
      p.track_radius = 7.0f;
      p.off_bg = light ? Color4f::hex(0x000000, 0.06f)
                       : Color4f::hex(0xFFFFFF, 0.08f);
      p.off_border = light ? Color4f::hex(0x000000, 0.08f)
                           : Color4f::hex(0xFFFFFF, 0.10f);
      p.off_bw = 1.0f;
      p.on_bg = with_alpha(primary(), light ? 0.5f : 0.35f);
      p.on_border = light ? with_alpha(primary(), 0.2f)
                          : Color4f::hex(0xFFFFFF, 0.15f);
      p.on_bw = 1.0f;
      p.thumb_d = 12.0f;
      p.thumb_margin = 1.0f;
      p.thumb_radius = 6.0f;
      // src: src/components/base/base-switch.tsx:432-435
      p.off_thumb = Color4f::hex(0xFFFFFF, light ? 0.80f : 0.30f);
      p.off_thumb_border = light ? with_alpha(primary(), 0.30f)
                                 : Color4f::hex(0xFFFFFF, 0.20f);
      p.off_thumb_bw = 1.0f;
      p.on_thumb = light ? Color4f::hex(0xFFFFFF, 0.95f)
                         : Color4f::hex(0xFFFFFF, 0.9f);
      p.on_thumb_border = light ? with_alpha(primary(), 0.4f)
                                : Color4f::hex(0xFFFFFF, 0.3f);
      p.on_thumb_bw = 1.0f;
      if (light) {
        // :350 thumb boxShadow '0 1px 2px rgba(0,0,0,0.1)'（暗态 none）
        p.thumb_shadow = true;
        p.thumb_shadow_color = Color4f::hex(0x000000, 0.10f);
        p.thumb_shadow_dy = 1.0f;
        p.thumb_shadow_blur = 2.0f;
      }
      // src: src/components/base/base-switch.tsx:447-449 frosted-glass hover scale(1.1)
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.10f;
      break;

    case SkinId::Cyberpunk:
      // :374-452：buildSquareSwitchStyle ← sizeSmall 强制 18×10
      p.track_w = 18.0f;
      p.track_h = 10.0f;
      p.track_radius = 0.0f;
      p.off_bg = light ? Color4f::hex(0xF1F5F9) : Color4f::hex(0x05070C);
      p.off_border = Color4f::hex(0xFF0055);
      p.off_bw = 1.0f;
      p.on_bg = light ? Color4f::hex(0xE0F7FA) : Color4f::hex(0x0A0E17);
      p.on_border = light ? Color4f::hex(0x00B0FF) : Color4f::hex(0x00F0FF);
      p.on_bw = 1.0f;
      // base-switch.tsx:37-51 `.MuiSwitch-sizeSmall` 强制 thumb 8×8 margin 1 方角
      p.thumb_d = 8.0f;
      p.thumb_margin = 1.0f;
      p.thumb_radius = 0.0f;
      p.off_thumb = Color4f::hex(0xFF0055);
      p.off_thumb_bw = 0.0f;
      p.thumb_glow = true;
      p.thumb_glow_color = Color4f::hex(0xFF0055, 0.85f);
      p.thumb_glow_blur = 4.0f;
      // src: src/components/base/base-switch.tsx:457,462 on 态常驻青色辉光 6px
      p.on_thumb_glow_color = Color4f::hex(light ? 0x00B0FF : 0x00F0FF, 0.85f);
      p.on_thumb_glow_blur = 6.0f;
      p.on_thumb = p.on_border;
      p.on_thumb_bw = 0.0f;
      // ← base-switch.tsx:442-444 `&:hover .MuiSwitch-thumb`
      p.thumb_hover = true;
      p.thumb_hover_glow = true;
      p.thumb_hover_glow_color = Color4f::hex(0xFF0055, 0.85f);
      p.on_thumb_hover_glow_color =
          Color4f::hex(light ? 0x00B0FF : 0x00F0FF, 0.85f);
      p.thumb_hover_glow_blur = 10.0f;
      break;
    case SkinId::Monochrome:
      // :478-496（BUG-384）：buildSquareSwitchStyle —— 与 cyberpunk 同一构造器
      p.track_w = 18.0f;
      p.track_h = 10.0f;
      p.track_radius = 0.0f;
      p.off_bg = light ? Color4f::hex(0xF4F4F5) : Color4f::hex(0x0A0A0A);
      p.off_border = Color4f::hex(0x737373);
      p.off_bw = 1.0f;
      p.on_bg = light ? Color4f::hex(0xD4D4D4) : Color4f::hex(0x262626);
      p.on_border = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      p.on_bw = 1.0f;
      p.thumb_d = 8.0f;
      p.thumb_margin = 1.0f;
      p.thumb_radius = 0.0f;
      p.off_thumb = Color4f::hex(0x737373);
      p.off_thumb_bw = 0.0f;
      p.on_thumb = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      p.on_thumb_bw = 0.0f;
      p.thumb_shadow = false;
      p.thumb_hover = false;
      p.track_hover = true;
      p.track_hover_border = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      break;
  }
  return p;
}

SliderPaint ThemeService::slider_paint() const {
  // src: src/pages/_layout/utils/style-helpers.tsx:153-219
  const bool light = is_light();
  const float d = depth();       // --depth-factor（retro-3d 旋钮阴影深度）
  const float v = vibrancy();    // --vibrancy-factor（retro-3d 金色辉光）
  const float g = val1();        // --control-skin-val1（modern-flat 圆角）
  SliderPaint p{};
  p.rail = Color4f::hex(0x000000, 0.20f);
  p.track = primary();
  p.thumb = Color4f::hex(0xFFFFFF);

  switch (skin_) {
    case SkinId::Retro3d: {
      // :251-330：rail h5 rgba(212,175,55,.25/.15)*.85、1px #B8860B/#8A6D00
      p.thickness = 5.0f;
      p.thumb_w = p.thumb_h = 14.0f;
      p.thumb_radius = 7.0f;
      p.rail = Color4f::hex(0xD4AF37, (light ? 0.25f : 0.15f) * 0.85f);
      p.rail_border = light ? Color4f::hex(0xB8860B) : Color4f::hex(0x8A6D00);
      p.rail_bw = 1.0f;
      p.rail_radius = 2.5f;
      p.rail_inset = true;
      p.rail_inset_color = light ? Color4f::hex(0x000000, 0.30f)
                                 : Color4f::hex(0x000000, 0.75f);
      p.rail_inset_dy = 3.0f * d;    // src: theme-settings-card.tsx:241
      p.rail_inset_blur = 5.0f * d;  // src: theme-settings-card.tsx:241
      p.rail_top_hi = light ? Color4f::hex(0xFFFFFF, 0.80f)
                            : Color4f::hex(0xFFFFFF, 0.08f);
      p.track_grad = true;
      p.track_grad_stops[0] = {0.0f, Color4f::hex(0xFFE082)};
      p.track_grad_stops[1] = {0.5f, Color4f::hex(0xFFC107)};
      p.track_grad_stops[2] = {1.0f, Color4f::hex(0xB8860B)};
      p.track_grad_n = 3;
      p.track_radius = 2.5f;
      p.thumb_grad = true;
      p.thumb_stops[0] = {0.00f, Color4f::hex(light ? 0xFFFDE7 : 0xFFF9C4)};
      p.thumb_stops[1] = {0.45f, Color4f::hex(light ? 0xFFD54F : 0xFFC107)};
      p.thumb_stops[2] = {0.80f, Color4f::hex(0xFFA000)};
      p.thumb_stops[3] = {1.00f, Color4f::hex(light ? 0xB8860B : 0x8A6D00)};
      p.thumb_grad_n = 4;
      p.thumb_border = light ? Color4f::hex(0x8A6D00) : Color4f::hex(0x634E00);
      p.thumb_bw = 1.0f;
      p.thumb_shadow = true;
      p.thumb_shadow_color = light ? Color4f::hex(0x000000, 0.25f)
                                   : Color4f::hex(0x000000, 0.60f);
      p.thumb_shadow_dy = (light ? 3.0f : 4.0f) * d;
      p.thumb_shadow_blur = (light ? 6.0f : 8.0f) * d;
      p.thumb_glow = true;
      p.thumb_glow_color =
          Color4f::hex(0xFFC107, (light ? 0.25f : 0.30f) * v);
      p.thumb_glow_blur = (light ? 8.0f : 10.0f) * v;
      // ← style-helpers.tsx:285/288 thumb 内凹顶高光
      p.thumb_inset_hi = true;
      p.thumb_inset_hi_color = light ? Color4f::hex(0xFFFFFF, 0.80f)
                                     : Color4f::hex(0xFFFFFF, 0.40f);
      p.thumb_inset_hi_dy = 1.0f * d;
      // ← style-helpers.tsx:306-307 `:active` thumb 按下内凹
      p.thumb_active_inset = true;
      p.thumb_active_inset_color =
          Color4f::hex(0x000000, light ? 0.15f : 0.50f);
      p.thumb_active_inset_dx = (light ? 1.0f : 1.5f) * d;
      p.thumb_active_inset_dy = (light ? 1.0f : 1.5f) * d;
      p.thumb_active_inset_blur = (light ? 2.0f : 2.5f) * d;
      // ← style-helpers.tsx:292-305 `'&:hover, &.Mui-focusVisible'`
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.2f;
      p.thumb_hover_brightness = 1.15f;
      p.thumb_hover_shadow = true;
      p.thumb_hover_shadow_color = light ? Color4f::hex(0x000000, 0.35f)
                                         : Color4f::hex(0x000000, 0.70f);
      p.thumb_hover_shadow_dy = (light ? 5.0f : 6.0f) * d;
      p.thumb_hover_shadow_blur = (light ? 10.0f : 12.0f) * d;
      p.thumb_hover_inset_hi = true;
      p.thumb_hover_inset_hi_color =
          Color4f::hex(0xFFFFFF, light ? 0.90f : 0.50f);
      p.thumb_hover_inset_hi_dy = 1.0f * d;
      p.thumb_hover_glow = true;
      p.thumb_hover_glow_color =
          Color4f::hex(0xFFC107, (light ? 0.35f : 0.40f) * v);
      p.thumb_hover_glow_blur = (light ? 12.0f : 14.0f) * v;
      break;
    }
    case SkinId::Original:
      // :333-360：rail h4 #cbd5e1/#30363d、track h4 --primary-main、thumb 12
      p.thickness = 4.0f;
      p.thumb_w = p.thumb_h = 12.0f;
      p.thumb_radius = 6.0f;
      p.rail = light ? Color4f::hex(0xCBD5E1) : Color4f::hex(0x30363D);
      p.track = primary();
      p.thumb = Color4f::hex(0xFFFFFF);
      p.thumb_border = primary();
      p.thumb_bw = 2.0f;
      p.thumb_shadow = true;
      p.thumb_shadow_color = Color4f::hex(0x000000, 0.15f);
      p.thumb_shadow_dy = 1.0f;
      p.thumb_shadow_blur = 2.0f;
      // ← style-helpers.tsx:334-336 `&:hover, &.Mui-focusVisible`
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.15f;
      break;

    case SkinId::ModernFlat:
      // :363-392：rail h4 #e5e7eb/#374151 radius 2*val1、track 主色、thumb 12
      p.thickness = 4.0f;
      p.thumb_w = p.thumb_h = 12.0f;
      p.thumb_radius = 6.0f * g;   // ← :380 borderRadius calc(6px * var(--control-skin-val1))
      p.rail = light ? Color4f::hex(0xE5E7EB) : Color4f::hex(0x374151);
      p.rail_radius = 2.0f * g;   // ← :368 calc(2px * var(--control-skin-val1))
      p.track = primary();
      p.track_radius = 2.0f * g;  // ← :375 calc(2px * var(--control-skin-val1))
      p.thumb = Color4f::hex(0xFFFFFF);
      p.thumb_border = primary();
      p.thumb_bw = 2.0f;
      p.thumb_shadow = true;
      p.thumb_shadow_color = Color4f::hex(0x000000, 0.15f);
      p.thumb_shadow_dy = 1.0f;
      p.thumb_shadow_blur = 2.0f;
      // ← style-helpers.tsx:363-365 `&:hover, &.Mui-focusVisible`
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.15f;
      break;

    case SkinId::FrostedGlass:
      // :395-437：rail h4 rgba(0,0,0,.06)/rgba(255,255,255,.08) + 1px 边
      p.thickness = 4.0f;
      p.thumb_w = p.thumb_h = 12.0f;
      p.thumb_radius = 6.0f;
      p.rail = light ? Color4f::hex(0x000000, 0.06f)
                     : Color4f::hex(0xFFFFFF, 0.08f);
      p.rail_border = light ? Color4f::hex(0x000000, 0.08f)
                            : Color4f::hex(0xFFFFFF, 0.10f);
      p.rail_bw = 1.0f;
      p.track = with_alpha(primary(), light ? 0.6f : 0.5f);
      p.thumb = light ? Color4f::hex(0xFFFFFF, 0.9f)
                      : Color4f::hex(0xFFFFFF, 0.8f);
      p.thumb_border = light ? with_alpha(primary(), 0.3f)
                             : Color4f::hex(0xFFFFFF, 0.3f);
      p.thumb_bw = 1.0f;
      if (light) {
        p.thumb_shadow = true;
        p.thumb_shadow_color = Color4f::hex(0x000000, 0.12f);
        p.thumb_shadow_dy = 1.0f;
        p.thumb_shadow_blur = 3.0f;
      }
      // ← style-helpers.tsx:306-313 `&:hover, &.Mui-focusVisible`
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.15f;
      p.thumb_hover_fill = true;
      p.thumb_hover_fill_color = Color4f::hex(0xFFFFFF, 1.0f);
      if (light) {
        p.thumb_hover_shadow = true;
        p.thumb_hover_shadow_color = Color4f::hex(0x000000, 0.18f);
        p.thumb_hover_shadow_dy = 2.0f;
        p.thumb_hover_shadow_blur = 5.0f;
      } else {
        p.thumb_hover_glow = true;
        p.thumb_hover_glow_color = Color4f::hex(0xFFFFFF, 0.40f);
        p.thumb_hover_glow_blur = 4.0f;
      }
      break;

    case SkinId::Cyberpunk:
      // :317-344：rail h2 白/#05070c + 1px #ff0055、track h2 #4caf50/#39ff14
      p.thickness = 2.0f;
      p.thumb_w = 8.0f;
      p.thumb_h = 12.0f;
      p.thumb_radius = 0.0f;
      p.rail = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x05070C);
      p.rail_border = Color4f::hex(0xFF0055);
      p.rail_bw = 1.0f;
      p.rail_radius = 0.0f;
      p.track = light ? Color4f::hex(0x4CAF50) : Color4f::hex(0x39FF14);
      p.track_radius = 0.0f;
      p.thumb = p.track;
      p.thumb_glow = true;
      p.thumb_glow_color = with_alpha(p.track, 0.75f);
      p.thumb_glow_blur = 5.0f * d;  // src: theme-settings-card.tsx:303
      // ← style-helpers.tsx:338-341 `&:hover, &.Mui-focusVisible`
      p.thumb_hover = true;
      p.thumb_hover_scale = 1.15f;
      p.thumb_hover_glow = true;
      p.thumb_hover_glow_color = with_alpha(p.track, 0.75f);
      p.thumb_hover_glow_blur = 8.0f * d;  // src: theme-settings-card.tsx:303
      break;

    case SkinId::Monochrome:
      // :482-508：rail h2 白/黑 + 1px 黑白、track h2 黑白、thumb 10×10 方角
      p.thickness = 2.0f;
      p.thumb_w = p.thumb_h = 10.0f;
      p.thumb_radius = 0.0f;
      p.rail = light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x000000);
      p.rail_border = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      // src: src/pages/_layout/utils/style-helpers.tsx:352 fixed 1px border
      p.rail_bw = 1.0f;
      p.rail_radius = 0.0f;
      p.track = light ? Color4f::hex(0x000000) : Color4f::hex(0xFFFFFF);
      p.track_radius = 0.0f;
      p.thumb = p.track;
      break;
  }
  return p;
}

}  // namespace clashwin::ui::theme
