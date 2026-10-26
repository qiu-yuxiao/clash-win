// 皮肤参数表 · 基础层实现（T12）：色值逐项出处见 palettes.h + 各行注释。
#include "palettes.h"

#include <cmath>
#include <cstring>

namespace clashwin::ui::theme {

const char* name_of(SkinId skin) {
  switch (skin) {
    case SkinId::Retro3d: return "retro-3d";        // 'Trump-3D'（默认）
    case SkinId::Original: return "original";
    case SkinId::ModernFlat: return "modern-flat";  // 'Modern'
    case SkinId::FrostedGlass: return "frosted-glass";  // 'Frosted'
    case SkinId::Cyberpunk: return "cyberpunk";
    case SkinId::Monochrome: return "monochrome";
  }
  return "retro-3d";
}

std::optional<SkinId> skin_from_name(const std::string& name) {
  if (name == "retro-3d") return SkinId::Retro3d;
  if (name == "original") return SkinId::Original;
  if (name == "modern-flat") return SkinId::ModernFlat;
  if (name == "frosted-glass") return SkinId::FrostedGlass;
  if (name == "cyberpunk") return SkinId::Cyberpunk;
  if (name == "monochrome") return SkinId::Monochrome;
  return std::nullopt;
}

// ---------- 色板（← _theme.tsx 全量照抄） ----------
const Palette& base_palette(ThemeMode mode) {
  static const Palette kLight = {
      Color4f::hex(0x007AFF),  // primary_color
      Color4f::hex(0xFC9B76),  // secondary_color
      Color4f::hex(0x000000),  // primary_text
      Color4f::hex(0x3C3C43, 0.60f),  // secondary_text #3C3C4399 → a=0x99/255
      Color4f::hex(0x007AFF),  // info_color
      Color4f::hex(0xFF3B30),  // error_color
      Color4f::hex(0xFF9500),  // warning_color
      Color4f::hex(0x06943D),  // success_color
      Color4f::hex(0xF5F5F5),  // background_color
  };
  static const Palette kDark = {
      Color4f::hex(0x0A84FF),  // primary_color
      Color4f::hex(0xFF9F0A),  // secondary_color
      Color4f::hex(0xFFFFFF),  // primary_text
      Color4f::hex(0xEBEBF5, 0.60f),  // secondary_text #EBEBF599
      Color4f::hex(0x0A84FF),  // info_color
      Color4f::hex(0xFF453A),  // error_color
      Color4f::hex(0xFF9F0A),  // warning_color
      Color4f::hex(0x30D158),  // success_color
      Color4f::hex(0x2E303D),  // background_color
  };
  return mode == ThemeMode::Light ? kLight : kDark;
}

// ---------- hsl → rgb（CSS hsl 语义，h 度数） ----------
Color4f hsl_to_rgb(float h_deg, float s, float l, float a) {
  float h = h_deg;
  while (h < 0) h += 360;
  while (h >= 360) h -= 360;
  float c = (1 - std::fabs(2 * l - 1)) * s;
  float x = c * (1 - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1));
  float m = l - c / 2;
  float r = 0, g = 0, b = 0;
  if (h < 60) { r = c; g = x; }
  else if (h < 120) { r = x; g = c; }
  else if (h < 180) { g = c; b = x; }
  else if (h < 240) { g = x; b = c; }
  else if (h < 300) { r = x; b = c; }
  else { r = c; b = x; }
  return {r + m, g + m, b + m, a};
}

Color4f original_primary(float val2) {
  // ← use-custom-theme.ts:180 resolvedPrimary = hsl(239*val2, 26%, 49%)
  return hsl_to_rgb(239.0f * val2, 0.26f, 0.49f);
}

// ---------- CSS 变量（← use-custom-theme.ts:367-495 逐条） ----------
CssVars css_vars(ThemeMode mode, SkinId skin) {
  bool light = mode == ThemeMode::Light;
  CssVars v{};
  // 通用段（:367-409）
  v.divider_color = light ? Color4f::hex(0x000000, 0.06f)
                          : Color4f::hex(0xFFFFFF, 0.06f);
  v.background_color = light ? Color4f::hex(0xECECEC)
                             : Color4f::hex(0x2E303D);  // dt.background_color
  v.selection_color = light ? Color4f::hex(0xf5f5f5) : Color4f::hex(0x3E3E3E);
  v.scroller_color = light ? Color4f::hex(0x909399, 0.50f)  // #90939980
                           : Color4f::hex(0x555555);
  v.window_border_color = light ? Color4f::hex(0xcccccc) : Color4f::hex(0x1E1E1E);
  v.scrollbar_bg = light ? Color4f::hex(0xf1f1f1) : Color4f::hex(0x2E303D);
  v.scrollbar_thumb = light ? Color4f::hex(0xc1c1c1) : Color4f::hex(0x555555);

  if (skin == SkinId::Original) {
    // original 分支（:394-434）
    v.theme_bg = light ? Color4f::hex(0xf5f5f5) : Color4f::hex(0x121212);
    v.theme_panel_bg = light ? Color4f::hex(0xffffff) : Color4f::hex(0x1e1e1e);
    v.theme_popover_bg = light ? Color4f::hex(0xffffff) : Color4f::hex(0x252525);
    v.theme_border = light ? Color4f::hex(0xe2e8f0) : Color4f::hex(0x333333);
    v.theme_input_bg = light ? Color4f::hex(0xffffff) : Color4f::hex(0x1a1a1a);
    v.theme_input_border = light ? Color4f::hex(0xcbd5e1) : Color4f::hex(0x555555);
    v.theme_border_outer = light ? Color4f::hex(0x000000, 0.05f)
                                 : Color4f::hex(0x000000, 0.25f);
  } else {
    // 其余 5 皮肤共用分支（:436-495）
    v.theme_bg = light ? Color4f::hex(0xf0f5ff) : Color4f::hex(0x0f1423);
    v.theme_panel_bg = light ? Color4f::hex(0xffffff) : Color4f::hex(0x1e2438);
    v.theme_popover_bg = light ? Color4f::hex(0xffffff) : Color4f::hex(0x1e1e23);
    v.theme_border = light ? Color4f::hex(0xd0d7de) : Color4f::hex(0x30363d);
    v.theme_input_bg = light ? Color4f::hex(0xffffff) : Color4f::hex(0x121824);
    v.theme_input_border = light ? Color4f::hex(0xd0d7de) : Color4f::hex(0x30363d);
    v.theme_border_outer = light ? Color4f::hex(0x000000, 0.08f)
                                 : Color4f::hex(0x000000, 0.35f);
  }
  return v;
}

}  // namespace clashwin::ui::theme
