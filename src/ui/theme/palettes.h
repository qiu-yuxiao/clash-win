#pragma once
// 皮肤参数表 · 基础层（T12）：色板 + CSS 变量集。
// 全部色值逐项核对自 ClashVerge（只读参考），出处标注于 palettes.cpp：
// - 色板 ← src/pages/_theme.tsx defaultTheme / defaultDarkTheme
// - CSS 变量 ← src/pages/_layout/hooks/use-custom-theme.ts:367-495
//   （--divider/--background/--selection/--scroller/--window-border/
//     --scrollbar-*/--theme-*/--theme-input-*）
// - 6 套 controlSkin ← utils/button-styles.ts getActiveSkin：
//   retro-3d(默认 'Trump-3D')/original/modern-flat('Modern')/
//   frosted-glass('Frosted')/cyberpunk/monochrome
// - original 强制主色 hsl(239,26%,49%) ← use-custom-theme.ts:180
#include <cstdint>
#include <optional>
#include <string>

#include "../render_target.h"

namespace clashwin::ui::theme {

enum class SkinId { Retro3d, Original, ModernFlat, FrostedGlass, Cyberpunk, Monochrome };
enum class ThemeMode { Light, Dark };

inline constexpr SkinId kDefaultSkin = SkinId::Retro3d;  // ← getActiveSkin fallback

const char* name_of(SkinId skin);                       // "retro-3d" 等
std::optional<SkinId> skin_from_name(const std::string& name);

// ---------- 基础色板（← _theme.tsx，键名与 JSON 一致） ----------
struct Palette {
  Color4f primary_color;
  Color4f secondary_color;
  Color4f primary_text;
  Color4f secondary_text;  // 注意：原值带 alpha（#3C3C4399 / #EBEBF599）
  Color4f info_color;
  Color4f error_color;
  Color4f warning_color;
  Color4f success_color;
  Color4f background_color;
};
const Palette& base_palette(ThemeMode mode);

// hsl → Color4f（original 主色 hsl(239*val2, 26%, 49%) 解析用）
Color4f hsl_to_rgb(float h_deg, float s, float l, float a = 1.0f);

// original 皮肤强制主色（← use-custom-theme.ts:178-181；
// val2=1.0 时 hsl(239,26%,49%) = rgb(91,92,157)，即 --primary-color-rgb 默认值）
Color4f original_primary(float val2 = 1.0f);

// ---------- CSS 变量集（← use-custom-theme.ts setProperty 段） ----------
struct CssVars {
  Color4f divider_color;        // --divider-color
  Color4f background_color;     // --background-color（light 是 #ECECEC！非 #F5F5F5）
  Color4f selection_color;      // --selection-color
  Color4f scroller_color;       // --scroller-color
  Color4f window_border_color;  // --window-border-color
  Color4f scrollbar_bg;         // --scrollbar-bg
  Color4f scrollbar_thumb;      // --scrollbar-thumb
  Color4f theme_bg;             // --theme-bg
  Color4f theme_panel_bg;       // --theme-panel-bg
  Color4f theme_popover_bg;     // --theme-popover-bg
  Color4f theme_border;         // --theme-border
  Color4f theme_input_bg;       // --theme-input-bg
  Color4f theme_input_border;   // --theme-input-border
  Color4f theme_border_outer;   // --theme-border-outer
};
// original 与其余 5 皮肤走不同分支（← use-custom-theme.ts:394-495 if/else）
CssVars css_vars(ThemeMode mode, SkinId skin);

}  // namespace clashwin::ui::theme
