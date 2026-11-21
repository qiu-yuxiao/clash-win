#pragma once
// 设置抽屉族共享样式与颜色辅助（拆分自 settings_drawer.cpp）
// src: src/pages/_layout/components/settings-drawer.tsx:1-60
#include <windows.h>

#include <string>

#include "../../i18n/i18n.h"
#include "../theme/theme.h"

namespace clashwin::ui::scene::detail {

inline constexpr float kCardPad = 8.0f;  // src: src/pages/_layout/components/basic-settings-card.tsx:139
inline constexpr float kCardR = 6.0f;    // src: src/utils/button-styles/card.ts:50 borderRadius 6px

inline bool is_light() { return theme::ThemeService::instance().is_light(); }

// src: src/utils/button-styles/card.ts get3DCardStyle(theme,'default').color
inline Color4f card_text() {
  return theme::ThemeService::instance().card_paint("default").text_color;
}

// src: src/pages/_layout/components/theme-settings-card.tsx:357 / :406
inline Color4f slider_value_color() {
  if (theme::ThemeService::instance().skin() == theme::SkinId::Retro3d && !is_light()) {
    return Color4f::hex(0x2C1F03);
  }
  return is_light() ? Color4f::hex(0x3C3C43, 0.6f)
                    : Color4f::hex(0xEBEBF5, 0.6f);
}

// src: src/pages/_layout/components/settings-drawer.tsx borderRight '1px solid' theme.palette.divider
inline Color4f divider_color() {
  return is_light() ? Color4f::hex(0x000000, 0.12f)
                    : Color4f::hex(0xFFFFFF, 0.12f);
}

// src: src/pages/_layout/components/routing-preference-card.tsx:96 zh/zhtw 13px, 其他语言 11px
inline float routing_font_for_lang() {
  const std::string& lang = clashwin::i18n::I18n::instance().language();
  return (lang == "zh" || lang == "zhtw") ? 13.0f : 11.0f;
}

// Windows 应用暗色（HKCU\...\Themes\Personalize AppsUseLightTheme，0=暗）
inline bool system_is_dark() {
  DWORD v = 1, sz = sizeof(v);
  LONG rc = RegGetValueW(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz);
  return rc == ERROR_SUCCESS && v == 0;
}

inline const char* current_skin_name(theme::SkinId skin) {
  switch (skin) {
    case theme::SkinId::Retro3d: return "retro-3d";
    case theme::SkinId::Original: return "original";
    case theme::SkinId::ModernFlat: return "modern-flat";
    case theme::SkinId::FrostedGlass: return "frosted-glass";
    case theme::SkinId::Cyberpunk: return "cyberpunk";
    case theme::SkinId::Monochrome: return "monochrome";
  }
  return "retro-3d";
}

}  // namespace clashwin::ui::scene::detail
