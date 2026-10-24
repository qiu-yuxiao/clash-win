#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <wincodec.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <iostream>
#include <vector>
#include <string>
#include <deque>
#include <cmath>
#include <shlobj.h>
#include <filesystem>
#include "render/d2d_renderer.hpp"
#include "clashwin/ui/theme.h"
#include "clashwin/config/profile_manager.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;
using namespace ClashWin;

bool SaveWicBitmapToPng(IWICImagingFactory* wicFactory, IWICBitmap* wicBitmap, const wchar_t* filename) {
    ComPtr<IWICStream> stream;
    HRESULT hr = wicFactory->CreateStream(&stream);
    if (FAILED(hr)) return false;

    hr = stream->InitializeFromFilename(filename, GENERIC_WRITE);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapEncoder> encoder;
    hr = wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) return false;

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr)) return false;

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) return false;

    UINT w, h;
    wicBitmap->GetSize(&w, &h);
    frame->SetSize(w, h);

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppPBGRA;
    frame->SetPixelFormat(&pixelFormat);

    hr = frame->WriteSource(wicBitmap, nullptr);
    if (FAILED(hr)) return false;

    hr = frame->Commit();
    if (FAILED(hr)) return false;

    hr = encoder->Commit();
    return SUCCEEDED(hr);
}

void DrawCommonTitlebar(D2DRenderer& renderer, float width) {
    const auto& theme = ThemeManager::Instance().GetColors();
    float titleH = 30.0f;
    renderer.FillRect(0, 0, width, titleH, theme.header_bg);
    renderer.DrawLine(0, titleH - 1.0f, width, titleH - 1.0f, theme.card_shadow);

    renderer.DrawTextSimple(L"🐱", 10.0f, 0, 18.0f, titleH, theme.accent, 11.5f, false, DWRITE_TEXT_ALIGNMENT_CENTER);
    renderer.DrawTextSimple(L"Clash Mini Ver.3.0.10", 30.0f, 0, 150.0f, titleH, theme.text_primary, 11.0f, true);

    float btnW = 28.0f;
    float btnH = 28.0f;
    float btnY = 1.0f;

    float minX = width - (btnW * 3 + 6.0f);
    renderer.DrawWindowCaptionButton(minX, btnY, btnW, btnH, L"—", 1, false);

    float maxX = width - (btnW * 2 + 4.0f);
    renderer.DrawWindowCaptionButton(maxX, btnY, btnW, btnH, L"□", 2, false);

    float closeX = width - (btnW + 2.0f);
    renderer.DrawWindowCaptionButton(closeX, btnY, btnW, btnH, L"✕", 3, false);
}

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ComPtr<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wicFactory)
    );
    if (FAILED(hr)) return 1;

    ComPtr<ID2D1Factory> d2dFactory;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf());
    if (FAILED(hr)) return 1;

    float dpiScale = 1.5f;
    int logicalW = 271;
    int logicalH = 680;
    UINT physW = static_cast<UINT>(logicalW * dpiScale);
    UINT physH = static_cast<UINT>(logicalH * dpiScale);

    // 加载真实用户订阅节点 (对齐 ProfileManager)
    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
        std::filesystem::path appDir = std::filesystem::path(appDataPath) / "clash_win";
        clashwin::config::ProfileManager::Instance().Init(appDir);
    }
    const auto& realNodes = clashwin::config::ProfileManager::Instance().GetNodes();
    std::string currentActive = clashwin::config::ProfileManager::Instance().GetCurrentActiveNode();
    if (currentActive.empty() && !realNodes.empty()) {
        currentActive = realNodes[0].name;
    }
    std::wstring activeNameW(currentActive.begin(), currentActive.end());

    struct DummyNode { std::wstring type; std::wstring name; int delay; };
    std::vector<DummyNode> nodes;
    std::wstring activeProtoW = L"";
    int activeDelay = -1;

    if (!realNodes.empty()) {
        for (size_t i = 0; i < realNodes.size(); ++i) {
            std::wstring nameW(realNodes[i].name.begin(), realNodes[i].name.end());
            std::wstring typeW(realNodes[i].type.begin(), realNodes[i].type.end());
            if (typeW.empty()) typeW = L"VMESS";
            for (auto& c : typeW) c = towupper(c);
            int d = realNodes[i].delay_ms;
            nodes.push_back({ typeW, nameW, d });
            if (realNodes[i].name == currentActive) {
                activeProtoW = typeW;
                activeDelay = d;
            }
        }
    } else {
        activeNameW = L"未选择节点";
    }

    std::deque<TrafficPoint> hist;
    for (int k = 0; k < 30; ++k) {
        uint64_t up = static_cast<uint64_t>(1000 + 4000 * (sin(k * 0.4) + 1.0));
        uint64_t down = static_cast<uint64_t>(20000 + 80000 * (cos(k * 0.3) + 1.0));
        hist.push_back({up, down});
    }

    auto CreateTarget = [&](ComPtr<IWICBitmap>& outBmp, ComPtr<ID2D1RenderTarget>& outRt, D2DRenderer& outRen) {
        wicFactory->CreateBitmap(physW, physH, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &outBmp);
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f * dpiScale, 96.0f * dpiScale
        );
        d2dFactory->CreateWicBitmapRenderTarget(outBmp.Get(), rtProps, &outRt);
        outRen.InitializeWithRenderTarget(outRt);
    };

    // =========================================================================
    // 1. 生成 clashwin_pixel_perfect.png (主窗口)
    // =========================================================================
    {
        ComPtr<IWICBitmap> bmp;
        ComPtr<ID2D1RenderTarget> rt;
        D2DRenderer ren;
        CreateTarget(bmp, rt, ren);

        rt->BeginDraw();
        const auto& theme = ThemeManager::Instance().GetColors();
        ren.Clear(theme.window_bg);

        float width = static_cast<float>(logicalW);
        float height = static_cast<float>(logicalH);
        float marginX = 8.0f;

        DrawCommonTitlebar(ren, width);
        float yOffset = 30.0f;

        float actBtnW = 28.0f;
        float actBtnH = 28.0f;
        float drawerBtnX = width - marginX - actBtnW;
        ren.Draw3DButton(drawerBtnX, yOffset + 1.0f, actBtnW, actBtnH, L"⚙", false, false, false);

        float pinBtnX = drawerBtnX - actBtnW - 4.0f;
        ren.Draw3DButton(pinBtnX, yOffset + 1.0f, actBtnW, actBtnH, L"📌", false, false, false);

        float cardW = pinBtnX - marginX - 4.0f;
        ren.DrawActiveNodeCard(marginX, yOffset + 1.0f, cardW, actBtnH, activeProtoW, activeNameW, activeDelay, L"", false);
        yOffset += 30.0f;

        float headH = 26.0f;
        ren.DrawProxyHead(0, yOffset, width, headH, 0, L"", false, false);
        yOffset += headH;

        float trafficPaneH = 100.0f;
        float itemH = 24.0f;
        if (nodes.empty()) {
            float emptyH = 100.0f;
            ren.Draw3DCard(marginX, yOffset + 8.0f, width - marginX * 2, emptyH, theme.card_bg);
            ren.DrawTextSimple(L"📥 No Proxies", marginX, yOffset + 20.0f, width - marginX * 2, 22.0f, theme.text_secondary, 12.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
            ren.Draw3DButton(marginX + 20.0f, yOffset + 52.0f, width - marginX * 2 - 40.0f, 28.0f, L"⚙ 打开设置导入订阅", false, false, true);
        } else {
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (yOffset + itemH > height - trafficPaneH) break;
                bool isSelected = (!currentActive.empty() ? (nodes[i].name == activeNameW) : (i == 0));
                ren.DrawProxyItem3Col(0, yOffset, width, itemH, nodes[i].type, nodes[i].name, nodes[i].delay, isSelected, (i % 2 != 0), false);
                yOffset += itemH;
            }
        }

        ren.DrawTrafficDashboard(0, height - trafficPaneH, width, trafficPaneH,
                                 12400, 184500, 142000000, 856000000,
                                 hist, true);
        rt->EndDraw();
        SaveWicBitmapToPng(wicFactory.Get(), bmp.Get(), L"clashwin_pixel_perfect.png");
    }

    // =========================================================================
    // 2. 生成 clashwin_drawer.png (设置抽屉)
    // =========================================================================
    {
        ComPtr<IWICBitmap> bmp;
        ComPtr<ID2D1RenderTarget> rt;
        D2DRenderer ren;
        CreateTarget(bmp, rt, ren);

        rt->BeginDraw();
        const auto& theme = ThemeManager::Instance().GetColors();
        ren.Clear(theme.window_bg);

        float width = static_cast<float>(logicalW);
        float marginX = 8.0f;
        float contentW = width - marginX * 2;

        DrawCommonTitlebar(ren, width);
        float yOffset = 30.0f;

        float actBtnW = 28.0f;
        float actBtnH = 28.0f;
        float closeBtnX = width - marginX - actBtnW;
        ren.Draw3DButton(closeBtnX, yOffset + 1.0f, actBtnW, actBtnH, L"✕", false, false, true);
        yOffset += 30.0f;

        float curY = yOffset;
        float dTextX = marginX + 8.0f;
        float dTextW = contentW - 16.0f;

        // Card 1: 订阅
        float card1H = 58.0f + 44.0f;
        ren.Draw3DCard(marginX, curY, contentW, card1H, theme.card_bg);
        ren.DrawTextSimple(L"订阅与节点配置", dTextX, curY + 6.0f, dTextW, 16.0f, theme.accent, 11.5f, true);
        float c1Y = curY + 24.0f;

        float pasteBtnW = 48.0f;
        float importBtnW = 42.0f;
        float inputW = dTextW - pasteBtnW - importBtnW - 8.0f;
        ren.Draw3DTextInput(dTextX, c1Y, inputW, 22.0f, L"", L"", L"填入订阅链接...", false);
        ren.Draw3DButton(dTextX + inputW + 4.0f, c1Y, pasteBtnW, 22.0f, L"📋粘贴");
        ren.Draw3DButton(dTextX + inputW + pasteBtnW + 8.0f, c1Y, importBtnW, 22.0f, L"导入");
        c1Y += 26.0f;

        ren.Draw3DCard(dTextX, c1Y, dTextW, 38.0f, theme.card_active_bg, false);
        ren.DrawTextSimple(L"✓ 默认主配置 (RdzAUK1VxD4t.yaml)", dTextX + 6.0f, c1Y + 3.0f, dTextW - 55.0f, 16.0f, theme.accent, 11.0f, true);
        ren.DrawTextSimple(L"已同步 | 22 节点 | 1.8 MB", dTextX + 6.0f, c1Y + 20.0f, dTextW - 55.0f, 14.0f, theme.text_secondary, 9.0f, false);
        ren.Draw3DButton(dTextX + dTextW - 44.0f, c1Y + 10.0f, 18.0f, 18.0f, L"🔄");
        ren.Draw3DButton(dTextX + dTextW - 22.0f, c1Y + 10.0f, 18.0f, 18.0f, L"🗑");

        curY += card1H + 6.0f;

        // Card 2: 接管与路由偏好 (+ 添加自定义分流规则)
        float card2H = 162.0f;
        ren.Draw3DCard(marginX, curY, contentW, card2H, theme.card_bg);
        ren.DrawTextSimple(L"流量接管模式", dTextX, curY + 6.0f, dTextW, 16.0f, theme.text_primary, 11.5f, true);
        float c2Y = curY + 24.0f;
        ren.DrawSegmentedControl(dTextX, c2Y, dTextW, 22.0f, {L"MAN", L"SYS", L"TUN"}, 1);
        c2Y += 24.0f;
        ren.DrawTextSimple(L"SYS: 已接管系统代理 (127.0.0.1:10801)", dTextX, c2Y, dTextW, 14.0f, theme.text_secondary, 9.5f, false);
        c2Y += 18.0f;

        ren.DrawTextSimple(L"分流路由偏好", dTextX, c2Y, dTextW, 16.0f, theme.text_primary, 11.5f, true);
        c2Y += 18.0f;
        ren.DrawSegmentedControl(dTextX, c2Y, dTextW, 22.0f, {L"DIR", L"RUL", L"PRX"}, 1);
        c2Y += 24.0f;
        ren.DrawTextSimple(L"RUL: 智能规则分流 (推荐出厂默认)", dTextX, c2Y, dTextW, 14.0f, theme.text_secondary, 9.5f, false);
        c2Y += 18.0f;

        ren.Draw3DButton(dTextX, c2Y, dTextW, 22.0f, L"➕ 添加自定义分流规则");
        curY += card2H + 6.0f;

        // Card 3: 基础设置
        float card3H = 110.0f;
        ren.Draw3DCard(marginX, curY, contentW, card3H, theme.card_bg);
        ren.DrawTextSimple(L"基础设置", dTextX, curY + 6.0f, dTextW, 16.0f, theme.text_primary, 11.5f, true);
        float c3Y = curY + 24.0f;
        float gridColW = (dTextW - 12.0f) / 2.0f;
        ren.Draw3DSwitch(dTextX, c3Y, gridColW, 20.0f, L"开机自启动", true);
        ren.Draw3DSwitch(dTextX + gridColW + 12.0f, c3Y, gridColW, 20.0f, L"Allow LAN", false);
        c3Y += 26.0f;
        ren.Draw3DSwitch(dTextX, c3Y, gridColW, 20.0f, L"启动最小化", false);
        ren.Draw3DSwitch(dTextX + gridColW + 12.0f, c3Y, gridColW, 20.0f, L"Allow IPv6", true);
        c3Y += 28.0f;

        ren.DrawTextSimple(L"Mixed Port", dTextX, c3Y + 2.0f, 75.0f, 20.0f, theme.text_primary, 11.0f, false);
        ren.Draw3DTextInput(dTextX + 75.0f, c3Y, 75.0f, 20.0f, L"", L"10801", L"10801", false);
        ren.Draw3DButton(dTextX + 156.0f, c3Y, 42.0f, 20.0f, L"保存");
        curY += card3H + 6.0f;

        // Card 4: 皮肤与滑块
        float card4H = 160.0f;
        ren.Draw3DCard(marginX, curY, contentW, card4H, theme.card_bg);
        ren.DrawTextSimple(L"界面外观与物理质感", dTextX, curY + 6.0f, dTextW, 16.0f, theme.text_primary, 11.5f, true);
        float c4Y = curY + 24.0f;

        float sBtnW = (dTextW - 10.0f) / 3.0f;
        std::vector<std::wstring> skinNames = { L"3D", L"Org", L"Mod", L"Fro", L"Cyber", L"Mono" };
        for (size_t i = 0; i < skinNames.size(); ++i) {
            float rx = dTextX + (i % 3) * (sBtnW + 5.0f);
            float ry = c4Y + (i / 3) * 24.0f;
            ren.Draw3DButton(rx, ry, sBtnW, 20.0f, skinNames[i], false, false, (i == 0));
        }
        c4Y += 54.0f;
        ren.DrawSegmentedControl(dTextX, c4Y, dTextW, 20.0f, {L"SYS", L"LIGHT", L"DARK"}, 0);
        c4Y += 24.0f;
        ren.DrawSlider(dTextX, c4Y, dTextW, 18.0f, L"金条拟物景深 (Depth)", 0.65f, 0.0f, 1.0f);
        c4Y += 20.0f;
        ren.DrawSlider(dTextX, c4Y, dTextW, 18.0f, L"光泽饱和度 (Vibrancy)", 1.2f, 0.0f, 5.0f);
        curY += card4H + 6.0f;

        // Drawer Bottom Footer Buttons
        float footerBtnW = (contentW - 6.0f) / 2.0f;
        ren.Draw3DButton(marginX, curY, footerBtnW, 24.0f, L"📜 查看运行日志");
        ren.Draw3DButton(marginX + footerBtnW + 6.0f, curY, footerBtnW, 24.0f, L"💡 帮助与关于");

        rt->EndDraw();
        SaveWicBitmapToPng(wicFactory.Get(), bmp.Get(), L"clashwin_drawer.png");
    }

    // =========================================================================
    // 3. 生成 clashwin_modal_addurl.png (添加自定义分流规则模态框)
    // =========================================================================
    {
        ComPtr<IWICBitmap> bmp;
        ComPtr<ID2D1RenderTarget> rt;
        D2DRenderer ren;
        CreateTarget(bmp, rt, ren);

        rt->BeginDraw();
        const auto& theme = ThemeManager::Instance().GetColors();
        ren.Clear(theme.window_bg);

        float width = static_cast<float>(logicalW);
        float height = static_cast<float>(logicalH);

        DrawCommonTitlebar(ren, width);
        ren.DrawTrafficDashboard(0, height - 100.0f, width, 100.0f, 12400, 184500, 142000000, 856000000, hist, true);

        // 渲染遮罩与弹窗
        ren.DrawModalBackdrop(width, height);
        float dlgW = (std::min)(width - 24.0f, 360.0f);
        float dlgH = 210.0f;
        float dlgX = (width - dlgW) / 2.0f;
        float dlgY = (height - dlgH) / 2.0f;

        ren.DrawModalDialogFrame(dlgX, dlgY, dlgW, dlgH, L"➕ 添加自定义分流规则");

        float padX = 14.0f;
        float curDlgY = dlgY + 38.0f;
        ren.DrawTextSimple(L"请输入域名或 IP (例如: google.com 或 1.1.1.1):", dlgX + padX, curDlgY, dlgW - padX * 2, 16.0f, theme.text_primary, 10.5f, false);
        curDlgY += 20.0f;

        float pasteBtnW = 46.0f;
        float inW = dlgW - padX * 2 - pasteBtnW - 6.0f;
        ren.Draw3DTextInput(dlgX + padX, curDlgY, inW, 22.0f, L"", L"openai.com", L"domain / IP...", true);
        ren.Draw3DButton(dlgX + padX + inW + 6.0f, curDlgY, pasteBtnW, 22.0f, L"📋粘贴");
        curDlgY += 28.0f;

        ren.DrawTextSimple(L"分流策略偏好:", dlgX + padX, curDlgY, dlgW - padX * 2, 16.0f, theme.text_primary, 10.5f, false);
        curDlgY += 18.0f;
        ren.DrawSegmentedControl(dlgX + padX, curDlgY, dlgW - padX * 2, 22.0f, {L"DIRECT", L"PROXY", L"REJECT"}, 1);
        curDlgY += 34.0f;

        float btnW = 60.0f;
        ren.Draw3DButton(dlgX + dlgW - padX - btnW * 2 - 8.0f, curDlgY, btnW, 24.0f, L"取消");
        ren.Draw3DButton(dlgX + dlgW - padX - btnW, curDlgY, btnW, 24.0f, L"添加", false, false, true);

        rt->EndDraw();
        SaveWicBitmapToPng(wicFactory.Get(), bmp.Get(), L"clashwin_modal_addurl.png");
    }

    // =========================================================================
    // 4. 生成 clashwin_modal_unlock.png (流媒体解锁模态框)
    // =========================================================================
    {
        ComPtr<IWICBitmap> bmp;
        ComPtr<ID2D1RenderTarget> rt;
        D2DRenderer ren;
        CreateTarget(bmp, rt, ren);

        rt->BeginDraw();
        const auto& theme = ThemeManager::Instance().GetColors();
        ren.Clear(theme.window_bg);

        float width = static_cast<float>(logicalW);
        float height = static_cast<float>(logicalH);

        DrawCommonTitlebar(ren, width);
        ren.DrawTrafficDashboard(0, height - 100.0f, width, 100.0f, 12400, 184500, 142000000, 856000000, hist, true);

        ren.DrawModalBackdrop(width, height);
        float dlgW = (std::min)(width - 24.0f, 440.0f);
        float dlgH = (std::min)(height - 40.0f, 450.0f);
        float dlgX = (width - dlgW) / 2.0f;
        float dlgY = (height - dlgH) / 2.0f;

        ren.DrawModalDialogFrame(dlgX, dlgY, dlgW, dlgH, L"🎬 流媒体与 AI 解锁检测");

        float padX = 14.0f;
        float curDlgY = dlgY + 36.0f;
        float fieldW = dlgW - padX * 2;

        struct UnlockSample { std::wstring name; std::wstring region; bool unlocked; };
        std::vector<UnlockSample> sampleList = {
            { L"OpenAI ChatGPT Web", L"🇺🇸US", true },
            { L"Anthropic Claude", L"🇺🇸US", true },
            { L"Google Gemini", L"🇯🇵JP", true },
            { L"YouTube Premium", L"🇸🇬SG", true },
            { L"Netflix", L"🇸🇬SG", true },
            { L"Disney+", L"🇸🇬SG", true },
            { L"Spotify", L"🇸🇬SG", true },
            { L"TikTok", L"🇯🇵JP", true },
            { L"Bilibili 港澳台", L"🇭🇰HK", true },
            { L"Bahamut 动画疯", L"🇹🇼TW", false }
        };

        float itemH = 22.0f;
        for (const auto& item : sampleList) {
            ren.Draw3DCard(dlgX + padX, curDlgY, fieldW, itemH, theme.card_active_bg, false);
            ren.DrawTextSimple(item.name, dlgX + padX + 8.0f, curDlgY + 2.0f, fieldW - 120.0f, itemH, theme.text_primary, 10.0f, true);
            ren.DrawTextSimple(item.region, dlgX + padX + fieldW - 110.0f, curDlgY + 2.0f, 40.0f, itemH, theme.text_secondary, 9.5f, false);
            if (item.unlocked) {
                ren.DrawTextSimple(L"✅ 已解锁", dlgX + padX + fieldW - 65.0f, curDlgY + 2.0f, 60.0f, itemH, D2D1::ColorF(0.18f, 0.70f, 0.35f, 1.0f), 9.5f, true, DWRITE_TEXT_ALIGNMENT_TRAILING);
            } else {
                ren.DrawTextSimple(L"❌ 未解锁", dlgX + padX + fieldW - 65.0f, curDlgY + 2.0f, 60.0f, itemH, D2D1::ColorF(0.90f, 0.25f, 0.20f, 1.0f), 9.5f, true, DWRITE_TEXT_ALIGNMENT_TRAILING);
            }
            curDlgY += itemH + 3.0f;
        }

        curDlgY = dlgY + dlgH - 34.0f;
        ren.Draw3DButton(dlgX + padX, curDlgY, fieldW, 24.0f, L"🔄 重新检测全部平台", false, false, true);

        rt->EndDraw();
        SaveWicBitmapToPng(wicFactory.Get(), bmp.Get(), L"clashwin_modal_unlock.png");
    }

    std::cout << "All UI preview PNGs successfully generated!" << std::endl;
    CoUninitialize();
    return 0;
}
