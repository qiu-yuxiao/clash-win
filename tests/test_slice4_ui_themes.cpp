#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <windows.h>
#include "clashwin/ui/theme.h"
#include "render/d2d_renderer.hpp"
#include "clashwin/config/app_config.h"

#define TEST_CHECK(expr, msg) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << " (" #expr ")" << std::endl; \
            std::exit(1); \
        } \
    } while(0)

using namespace ClashWin;

void TestSixThemesAndDualSliders() {
    std::cout << "[Test 1] 验证 6 套 3D 拟物皮肤与双控制滑块体系..." << std::endl;

    auto& tm = ThemeManager::Instance();

    std::vector<std::pair<SkinType, std::string>> allSkins = {
        {SkinType::Retro3D, "retro-3d"},
        {SkinType::Original, "original"},
        {SkinType::Modern, "modern"},
        {SkinType::Frosted, "frosted"},
        {SkinType::Cyberpunk, "cyberpunk"},
        {SkinType::Monochrome, "monochrome"}
    };

    for (const auto& [skin, name] : allSkins) {
        tm.SetSkin(skin);
        TEST_CHECK(tm.GetCurrentSkin() == skin, "皮肤切换必须立即生效");
        TEST_CHECK(ThemeManager::SkinTypeToString(skin) == name, "字符串序列化必须精准对齐");
        TEST_CHECK(ThemeManager::StringToSkinType(name) == skin, "反序列化必须精准还原");

        const auto& c = tm.GetColors();
        TEST_CHECK(c.window_bg.a == 1.0f, "主背景必须 100% 实体纯色渲染 (杜绝半透明撕裂)");
        TEST_CHECK(c.card_bg.a == 1.0f, "卡片底色必须 100% 实体纯色");
        TEST_CHECK(!c.slider1_name.empty(), "滑块 1 专属英文标签必须存在");
        TEST_CHECK(!c.slider2_name.empty(), "滑块 2 专属英文标签必须存在");
    }

    // 验证滑块标签规范
    tm.SetSkin(SkinType::Retro3D);
    TEST_CHECK(tm.GetColors().slider1_name == L"Depth", "Retro3D 滑块 1 必须为 Depth");
    TEST_CHECK(tm.GetColors().slider2_name == L"Vibrancy", "Retro3D 滑块 2 必须为 Vibrancy");

    tm.SetSkin(SkinType::Cyberpunk);
    TEST_CHECK(tm.GetColors().slider1_name == L"Neon", "Cyberpunk 滑块 1 必须为 Neon");
    TEST_CHECK(tm.GetColors().slider2_name == L"Scanline", "Cyberpunk 滑块 2 必须为 Scanline");

    tm.SetSkin(SkinType::Original);
    TEST_CHECK(tm.GetColors().slider1_name == L"Radius", "Original 滑块 1 必须为 Radius");
    TEST_CHECK(tm.GetColors().slider2_name == L"Accent", "Original 滑块 2 必须为 Accent");

    // 验证滑块数值调节
    tm.SetSlider1(0.75f);
    tm.SetSlider2(3.20f);
    TEST_CHECK(tm.GetSlider1() == 0.75f, "滑块 1 数值保存正确");
    TEST_CHECK(tm.GetSlider2() == 3.20f, "滑块 2 数值保存正确");

    std::cout << "  [PASS] 6 套特色皮肤与双控制滑块体系 100% 验证通过！" << std::endl;
}

void TestDirect2DRenderingPipeline() {
    std::cout << "[Test 2] 验证 Direct2D 离屏组件渲染管线与光影倒角..." << std::endl;

    // 创建隐藏测试宿主窗口
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ClashWin_TestRenderHost";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Test", WS_OVERLAPPEDWINDOW, 0, 0, 400, 700, nullptr, nullptr, wc.hInstance, nullptr);
    TEST_CHECK(hwnd != nullptr, "宿主窗口创建成功");

    D2DRenderer renderer;
    bool ok = renderer.Initialize(hwnd);
    TEST_CHECK(ok, "D2DRenderer 初始化成功 (Direct2D + DirectWrite 工厂就绪)");

    renderer.Resize(400, 700);
    renderer.BeginDraw();

    const auto& theme = ThemeManager::Instance().GetColors();
    renderer.Clear(theme.window_bg);

    // 1. 3D 凸起与凹陷卡片
    renderer.Draw3DCard(10, 10, 380, 50, theme.card_bg, false);
    renderer.Draw3DCard(10, 70, 380, 50, theme.card_bg, true); // 按下物理凹陷

    // 2. 3D 按钮
    renderer.Draw3DButton(10, 130, 100, 32, L"Test Normal", false, false, false);
    renderer.Draw3DButton(120, 130, 100, 32, L"Test Hot", true, false, false);
    renderer.Draw3DButton(230, 130, 100, 32, L"Test Active", false, false, true);

    // 3. 三段式工业分段切换开关
    renderer.DrawSegmentedControl(10, 180, 380, 26, {L"MAN", L"SYS", L"TUN"}, 1);

    // 4. 双参数调节滑块
    renderer.DrawSlider(10, 220, 380, 22, L"Depth", 0.5f, 0.0f, 1.0f);

    // 5. 延迟徽章 (<100ms, 100-300ms, >300ms, 超时)
    renderer.DrawLatencyBadge(10, 260, 60, 20, 35);
    renderer.DrawLatencyBadge(80, 260, 60, 20, 180);
    renderer.DrawLatencyBadge(150, 260, 60, 20, 450);
    renderer.DrawLatencyBadge(220, 260, 70, 20, -1);

    // 6. 宽窗连接行
    renderer.DrawConnectionRow(10, 300, 380, 24, L"api.github.com:443", L"PROXY", L"↑1.2K/s ↓45K/s", false);

    HRESULT hr = renderer.EndDraw();
    TEST_CHECK(SUCCEEDED(hr), "Direct2D 渲染完成无报错");

    DestroyWindow(hwnd);
    std::cout << "  [PASS] Direct2D 实体拟物图元与光影倒角全量通过！" << std::endl;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "[ClashWin 切片四] Direct2D 3D 拟物 UI 与 6 套皮肤测试" << std::endl;
    std::cout << "====================================================" << std::endl;

    TestSixThemesAndDualSliders();
    TestDirect2DRenderingPipeline();

    std::cout << "\n>>> 切片四: Direct2D 3D 拟物化与 6 套皮肤全项 100% 验收通过！<<<" << std::endl;
    return 0;
}
