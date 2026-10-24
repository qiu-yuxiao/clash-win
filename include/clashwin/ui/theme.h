#pragma once

#include <d2d1.h>
#include <string>
#include <vector>

namespace ClashWin {

enum class SkinType {
    Retro3D = 0,    // Trump-3D: 经典拟物象牙白/复古灰，物理凸凹按压
    Original = 1,   // 原版深灰科技质感 + Verge 蓝高光
    Modern = 2,     // 现代极简冷黑 + 锐利倒角边缘
    Frosted = 3,    // 磨砂拟物实体 + 柔和光影边
    Cyberpunk = 4,  // 赛博朋克深空黑 + 霓虹青紫动态高光
    Monochrome = 5  // 水墨工业黑白单色 + 高对比度实体边框
};

struct ThemeColors {
    D2D1_COLOR_F window_bg;
    D2D1_COLOR_F header_bg;
    D2D1_COLOR_F card_bg;
    D2D1_COLOR_F card_active_bg;
    D2D1_COLOR_F card_highlight; // 顶/左高光边 (3D 凸起)
    D2D1_COLOR_F card_shadow;    // 底/右阴影边 (3D 倒角)
    D2D1_COLOR_F text_primary;
    D2D1_COLOR_F text_secondary;
    D2D1_COLOR_F accent;
    D2D1_COLOR_F badge_fast;     // < 100ms 绿色
    D2D1_COLOR_F badge_medium;   // 100-300ms 黄色
    D2D1_COLOR_F badge_slow;     // > 300ms / 错误 红色
    std::wstring slider1_name;   // 专属滑块 1 标签
    std::wstring slider2_name;   // 专属滑块 2 标签
};

class ThemeManager {
public:
    static ThemeManager& Instance();

    SkinType GetCurrentSkin() const { return m_currentSkin; }
    void SetSkin(SkinType skin);
    void SetSkinByName(const std::string& skinName);

    const ThemeColors& GetColors() const { return m_colors; }

    float GetSlider1() const { return m_slider1; }
    void SetSlider1(float val) { m_slider1 = val; }

    float GetSlider2() const { return m_slider2; }
    void SetSlider2(float val) { m_slider2 = val; }

    static std::string SkinTypeToString(SkinType skin);
    static SkinType StringToSkinType(const std::string& str);

private:
    ThemeManager();
    void UpdateThemeColors();

    SkinType m_currentSkin = SkinType::Retro3D;
    float m_slider1 = 0.5f;
    float m_slider2 = 2.5f;
    ThemeColors m_colors{};
};

} // namespace ClashWin
