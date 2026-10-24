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
#include "render/d2d_renderer.hpp"
#include "clashwin/ui/theme.h"

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

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ComPtr<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wicFactory)
    );
    if (FAILED(hr)) {
        std::cerr << "Failed to create WIC factory: " << std::hex << hr << std::endl;
        return 1;
    }

    float dpiScale = 1.5f;
    int logicalW = 271;
    int logicalH = 680;
    UINT physW = static_cast<UINT>(logicalW * dpiScale);
    UINT physH = static_cast<UINT>(logicalH * dpiScale);

    ComPtr<IWICBitmap> wicBitmap;
    hr = wicFactory->CreateBitmap(physW, physH, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &wicBitmap);
    if (FAILED(hr)) {
        std::cerr << "Failed to create WIC bitmap: " << std::hex << hr << std::endl;
        return 1;
    }

    ComPtr<ID2D1Factory> d2dFactory;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "Failed to create D2D factory: " << std::hex << hr << std::endl;
        return 1;
    }

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f * dpiScale, 96.0f * dpiScale
    );

    ComPtr<ID2D1RenderTarget> renderTarget;
    hr = d2dFactory->CreateWicBitmapRenderTarget(wicBitmap.Get(), rtProps, &renderTarget);
    if (FAILED(hr)) {
        std::cerr << "Failed to create WIC render target: " << std::hex << hr << std::endl;
        return 1;
    }

    D2DRenderer renderer;
    if (!renderer.InitializeWithRenderTarget(renderTarget)) {
        std::cerr << "Failed to initialize D2DRenderer with render target" << std::endl;
        return 1;
    }

    renderTarget->BeginDraw();

    const auto& theme = ThemeManager::Instance().GetColors();
    renderer.Clear(theme.window_bg);

    float width = static_cast<float>(logicalW);   // In logical pixels because DPI is set on render target!
    float height = static_cast<float>(logicalH);
    float yOffset = 0.0f;
    float marginX = 8.0f;

    // 1. Titlebar (30px)
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

    yOffset = titleH;

    // 2. Top Action Row (30px)
    float actBtnW = 28.0f;
    float actBtnH = 28.0f;

    float drawerBtnX = width - marginX - actBtnW;
    renderer.Draw3DButton(drawerBtnX, yOffset + 1.0f, actBtnW, actBtnH, L"⚙", false, false, false);

    float pinBtnX = drawerBtnX - actBtnW - 4.0f;
    renderer.Draw3DButton(pinBtnX, yOffset + 1.0f, actBtnW, actBtnH, L"📌", false, false, false);

    float cardW = pinBtnX - marginX - 4.0f;
    renderer.DrawActiveNodeCard(marginX, yOffset + 1.0f, cardW, actBtnH, L"VMESS", L"新加坡aw5", 42, L"", false);

    yOffset += 30.0f;

    // 3. ProxyHead (26px, full width)
    float headH = 26.0f;
    renderer.DrawProxyHead(0, yOffset, width, headH, 0, L"", false, false);
    yOffset += headH;

    // 4. Proxy List (Full Width, 24px per row)
    float trafficPaneH = 100.0f;

    struct DummyNode { std::wstring type; std::wstring name; int delay; };
    std::vector<DummyNode> nodes = {
        { L"VMESS", L"新加坡aw5 (112813)", 42 },
        { L"HYSTERIA2", L"日本高速01 (091102)", 68 },
        { L"SS", L"香港专线02 (221940)", 23 },
        { L"VLESS", L"美国洛杉矶 (082104)", 156 },
        { L"TROJAN", L"德国法兰克福 (140211)", 240 },
        { L"VMESS", L"韩国首尔 (170530)", 55 },
        { L"HYSTERIA2", L"台湾台北01 (182012)", 35 },
        { L"SS", L"新加坡BGP (192804)", 48 },
        { L"TROJAN", L"英国伦敦 (201105)", 280 },
        { L"VMESS", L"加拿大温哥华 (210408)", 188 },
        { L"VLESS", L"澳大利亚悉尼 (220501)", 135 },
        { L"SS", L"法国巴黎 (230602)", 265 }
    };

    float itemH = 24.0f;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (yOffset + itemH > height - trafficPaneH) break;
        bool isSelected = (i == 0);
        renderer.DrawProxyItem3Col(0, yOffset, width, itemH, nodes[i].type, nodes[i].name, nodes[i].delay, isSelected, (i % 2 != 0), false);
        yOffset += itemH;
    }

    // 5. Traffic Dashboard (100px, full width)
    std::deque<TrafficPoint> hist;
    for (int k = 0; k < 30; ++k) {
        uint64_t up = static_cast<uint64_t>(1000 + 4000 * (sin(k * 0.4) + 1.0));
        uint64_t down = static_cast<uint64_t>(20000 + 80000 * (cos(k * 0.3) + 1.0));
        hist.push_back({up, down});
    }

    renderer.DrawTrafficDashboard(0, height - trafficPaneH, width, trafficPaneH,
                                 12400, 184500, 142000000, 856000000,
                                 hist, true);

    renderTarget->EndDraw();

    bool ok = SaveWicBitmapToPng(wicFactory.Get(), wicBitmap.Get(), L"clashwin_pixel_perfect.png");
    if (ok) {
        std::wcout << L"Successfully saved clashwin_pixel_perfect.png" << std::endl;
    } else {
        std::cerr << "Failed to save PNG" << std::endl;
        return 1;
    }

    CoUninitialize();
    return 0;
}
