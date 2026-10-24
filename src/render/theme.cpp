#include "clashwin/ui/theme.h"
#include <algorithm>

namespace ClashWin {

ThemeManager& ThemeManager::Instance() {
    static ThemeManager instance;
    return instance;
}

ThemeManager::ThemeManager() {
    UpdateThemeColors();
}

void ThemeManager::SetSkin(SkinType skin) {
    m_currentSkin = skin;
    UpdateThemeColors();
}

void ThemeManager::SetSkinByName(const std::string& skinName) {
    SetSkin(StringToSkinType(skinName));
}

std::string ThemeManager::SkinTypeToString(SkinType skin) {
    switch (skin) {
    case SkinType::Retro3D:    return "retro-3d";
    case SkinType::Original:   return "original";
    case SkinType::Modern:     return "modern";
    case SkinType::Frosted:    return "frosted";
    case SkinType::Cyberpunk:  return "cyberpunk";
    case SkinType::Monochrome: return "monochrome";
    default:                   return "retro-3d";
    }
}

SkinType ThemeManager::StringToSkinType(const std::string& str) {
    if (str == "original") return SkinType::Original;
    if (str == "modern") return SkinType::Modern;
    if (str == "frosted") return SkinType::Frosted;
    if (str == "cyberpunk") return SkinType::Cyberpunk;
    if (str == "monochrome") return SkinType::Monochrome;
    return SkinType::Retro3D;
}

void ThemeManager::UpdateThemeColors() {
    switch (m_currentSkin) {
    case SkinType::Retro3D: // Trump-3D (经典实体米白/温润复古)
        m_colors.window_bg       = D2D1::ColorF(0.88f, 0.87f, 0.85f, 1.0f);
        m_colors.header_bg       = D2D1::ColorF(0.82f, 0.81f, 0.79f, 1.0f);
        m_colors.card_bg         = D2D1::ColorF(0.85f, 0.84f, 0.82f, 1.0f);
        m_colors.card_active_bg  = D2D1::ColorF(0.78f, 0.77f, 0.73f, 1.0f);
        m_colors.card_highlight  = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.85f);  // 顶部白高光
        m_colors.card_shadow     = D2D1::ColorF(0.40f, 0.39f, 0.37f, 0.85f); // 底部深凹槽
        m_colors.text_primary    = D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f);
        m_colors.text_secondary  = D2D1::ColorF(0.42f, 0.40f, 0.38f, 1.0f);
        m_colors.accent          = D2D1::ColorF(0.85f, 0.45f, 0.15f, 1.0f); // 复古橘金
        m_colors.badge_fast      = D2D1::ColorF(0.18f, 0.65f, 0.28f, 1.0f);
        m_colors.badge_medium    = D2D1::ColorF(0.85f, 0.60f, 0.10f, 1.0f);
        m_colors.badge_slow      = D2D1::ColorF(0.85f, 0.25f, 0.25f, 1.0f);
        m_colors.slider1_name    = L"Depth";
        m_colors.slider2_name    = L"Vibrancy";
        break;

    case SkinType::Original: // 原版科技深灰 + Verge 蓝
        m_colors.window_bg       = D2D1::ColorF(0.11f, 0.12f, 0.14f, 1.0f);
        m_colors.header_bg       = D2D1::ColorF(0.14f, 0.15f, 0.18f, 1.0f);
        m_colors.card_bg         = D2D1::ColorF(0.16f, 0.17f, 0.21f, 1.0f);
        m_colors.card_active_bg  = D2D1::ColorF(0.20f, 0.23f, 0.30f, 1.0f);
        m_colors.card_highlight  = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.18f);
        m_colors.card_shadow     = D2D1::ColorF(0.04f, 0.04f, 0.06f, 0.70f);
        m_colors.text_primary    = D2D1::ColorF(0.92f, 0.94f, 0.98f, 1.0f);
        m_colors.text_secondary  = D2D1::ColorF(0.55f, 0.58f, 0.65f, 1.0f);
        m_colors.accent          = D2D1::ColorF(0.22f, 0.55f, 0.95f, 1.0f); // 亮蓝
        m_colors.badge_fast      = D2D1::ColorF(0.25f, 0.85f, 0.45f, 1.0f);
        m_colors.badge_medium    = D2D1::ColorF(0.95f, 0.80f, 0.25f, 1.0f);
        m_colors.badge_slow      = D2D1::ColorF(0.95f, 0.35f, 0.35f, 1.0f);
        m_colors.slider1_name    = L"Radius";
        m_colors.slider2_name    = L"Accent";
        break;

    case SkinType::Modern: // 现代冷黑极简
        m_colors.window_bg       = D2D1::ColorF(0.06f, 0.06f, 0.07f, 1.0f);
        m_colors.header_bg       = D2D1::ColorF(0.09f, 0.09f, 0.11f, 1.0f);
        m_colors.card_bg         = D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f);
        m_colors.card_active_bg  = D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f);
        m_colors.card_highlight  = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f);
        m_colors.card_shadow     = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.85f);
        m_colors.text_primary    = D2D1::ColorF(0.95f, 0.95f, 0.95f, 1.0f);
        m_colors.text_secondary  = D2D1::ColorF(0.50f, 0.50f, 0.55f, 1.0f);
        m_colors.accent          = D2D1::ColorF(0.40f, 0.70f, 1.0f, 1.0f);
        m_colors.badge_fast      = D2D1::ColorF(0.30f, 0.90f, 0.50f, 1.0f);
        m_colors.badge_medium    = D2D1::ColorF(1.0f, 0.80f, 0.20f, 1.0f);
        m_colors.badge_slow      = D2D1::ColorF(1.0f, 0.30f, 0.30f, 1.0f);
        m_colors.slider1_name    = L"Contrast";
        m_colors.slider2_name    = L"Glow";
        break;

    case SkinType::Frosted: // 柔和磨砂质感
        m_colors.window_bg       = D2D1::ColorF(0.18f, 0.20f, 0.24f, 1.0f);
        m_colors.header_bg       = D2D1::ColorF(0.22f, 0.25f, 0.30f, 1.0f);
        m_colors.card_bg         = D2D1::ColorF(0.25f, 0.28f, 0.34f, 1.0f);
        m_colors.card_active_bg  = D2D1::ColorF(0.32f, 0.36f, 0.44f, 1.0f);
        m_colors.card_highlight  = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.25f);
        m_colors.card_shadow     = D2D1::ColorF(0.08f, 0.09f, 0.12f, 0.60f);
        m_colors.text_primary    = D2D1::ColorF(0.96f, 0.97f, 0.99f, 1.0f);
        m_colors.text_secondary  = D2D1::ColorF(0.68f, 0.72f, 0.80f, 1.0f);
        m_colors.accent          = D2D1::ColorF(0.35f, 0.65f, 0.95f, 1.0f);
        m_colors.badge_fast      = D2D1::ColorF(0.35f, 0.90f, 0.60f, 1.0f);
        m_colors.badge_medium    = D2D1::ColorF(0.98f, 0.85f, 0.35f, 1.0f);
        m_colors.badge_slow      = D2D1::ColorF(0.98f, 0.40f, 0.40f, 1.0f);
        m_colors.slider1_name    = L"Blur";
        m_colors.slider2_name    = L"Saturation";
        break;

    case SkinType::Cyberpunk: // 赛博朋克深空黑 + 霓虹青紫
        m_colors.window_bg       = D2D1::ColorF(0.04f, 0.04f, 0.08f, 1.0f);
        m_colors.header_bg       = D2D1::ColorF(0.08f, 0.06f, 0.14f, 1.0f);
        m_colors.card_bg         = D2D1::ColorF(0.10f, 0.08f, 0.18f, 1.0f);
        m_colors.card_active_bg  = D2D1::ColorF(0.18f, 0.12f, 0.32f, 1.0f);
        m_colors.card_highlight  = D2D1::ColorF(0.0f, 1.0f, 0.88f, 0.60f); // 霓虹青光
        m_colors.card_shadow     = D2D1::ColorF(0.95f, 0.10f, 0.60f, 0.50f); // 霓虹粉紫底边
        m_colors.text_primary    = D2D1::ColorF(0.0f, 1.0f, 0.90f, 1.0f);
        m_colors.text_secondary  = D2D1::ColorF(0.65f, 0.50f, 0.85f, 1.0f);
        m_colors.accent          = D2D1::ColorF(1.0f, 0.15f, 0.65f, 1.0f); // 电光品红
        m_colors.badge_fast      = D2D1::ColorF(0.0f, 1.0f, 0.70f, 1.0f);
        m_colors.badge_medium    = D2D1::ColorF(1.0f, 0.90f, 0.10f, 1.0f);
        m_colors.badge_slow      = D2D1::ColorF(1.0f, 0.10f, 0.40f, 1.0f);
        m_colors.slider1_name    = L"Neon";
        m_colors.slider2_name    = L"Scanline";
        break;

    case SkinType::Monochrome: // 水墨工业单色黑白
        m_colors.window_bg       = D2D1::ColorF(0.08f, 0.08f, 0.08f, 1.0f);
        m_colors.header_bg       = D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f);
        m_colors.card_bg         = D2D1::ColorF(0.15f, 0.15f, 0.15f, 1.0f);
        m_colors.card_active_bg  = D2D1::ColorF(0.24f, 0.24f, 0.24f, 1.0f);
        m_colors.card_highlight  = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.30f);
        m_colors.card_shadow     = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.90f);
        m_colors.text_primary    = D2D1::ColorF(0.98f, 0.98f, 0.98f, 1.0f);
        m_colors.text_secondary  = D2D1::ColorF(0.60f, 0.60f, 0.60f, 1.0f);
        m_colors.accent          = D2D1::ColorF(0.90f, 0.90f, 0.90f, 1.0f);
        m_colors.badge_fast      = D2D1::ColorF(0.85f, 0.85f, 0.85f, 1.0f);
        m_colors.badge_medium    = D2D1::ColorF(0.60f, 0.60f, 0.60f, 1.0f);
        m_colors.badge_slow      = D2D1::ColorF(0.35f, 0.35f, 0.35f, 1.0f);
        m_colors.slider1_name    = L"Intensity";
        m_colors.slider2_name    = L"Grain";
        break;
    }
}

} // namespace ClashWin
