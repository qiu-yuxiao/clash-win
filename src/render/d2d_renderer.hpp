#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

namespace ClashWin {

class D2DRenderer {
public:
    D2DRenderer();
    ~D2DRenderer();

    bool Initialize(HWND hwnd);
    void Resize(UINT width, UINT height);
    void BeginDraw();
    HRESULT EndDraw();

    // Drawing primitives
    void Clear(D2D1_COLOR_F color);
    void DrawTextSimple(const std::wstring& text, float x, float y, float w, float h, D2D1_COLOR_F color, float fontSize = 14.0f, bool bold = false);
    void Draw3DCard(float x, float y, float w, float h, D2D1_COLOR_F baseColor);
    void Draw3DButton(float x, float y, float w, float h, const std::wstring& label, bool isHot = false, bool isPressed = false);

    ID2D1HwndRenderTarget* GetRenderTarget() const { return m_renderTarget.Get(); }

private:
    HRESULT CreateDeviceResources();
    void DiscardDeviceResources();

    HWND m_hwnd = nullptr;
    ComPtr<ID2D1Factory> m_d2dFactory;
    ComPtr<IDWriteFactory> m_dwriteFactory;
    ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    ComPtr<ID2D1SolidColorBrush> m_solidBrush;
};

} // namespace ClashWin
