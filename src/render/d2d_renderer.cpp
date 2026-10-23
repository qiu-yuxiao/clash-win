#include "d2d_renderer.hpp"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace ClashWin {

D2DRenderer::D2DRenderer() = default;
D2DRenderer::~D2DRenderer() {
    DiscardDeviceResources();
}

bool D2DRenderer::Initialize(HWND hwnd) {
    m_hwnd = hwnd;

    // Create D2D factory
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        m_d2dFactory.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    // Create DirectWrite factory
    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())
    );
    if (FAILED(hr)) return false;

    return SUCCEEDED(CreateDeviceResources());
}

HRESULT D2DRenderer::CreateDeviceResources() {
    if (m_renderTarget) return S_OK;

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    HRESULT hr = m_d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        ),
        D2D1::HwndRenderTargetProperties(m_hwnd, size),
        m_renderTarget.GetAddressOf()
    );

    if (SUCCEEDED(hr)) {
        hr = m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::White),
            m_solidBrush.GetAddressOf()
        );
    }

    return hr;
}

void D2DRenderer::DiscardDeviceResources() {
    m_solidBrush.Reset();
    m_renderTarget.Reset();
}

void D2DRenderer::Resize(UINT width, UINT height) {
    if (m_renderTarget) {
        m_renderTarget->Resize(D2D1::SizeU(width, height));
    }
}

void D2DRenderer::BeginDraw() {
    CreateDeviceResources();
    if (m_renderTarget) {
        m_renderTarget->BeginDraw();
    }
}

HRESULT D2DRenderer::EndDraw() {
    if (!m_renderTarget) return S_FALSE;
    HRESULT hr = m_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
    return hr;
}

void D2DRenderer::Clear(D2D1_COLOR_F color) {
    if (m_renderTarget) {
        m_renderTarget->Clear(color);
    }
}

void D2DRenderer::DrawTextSimple(const std::wstring& text, float x, float y, float w, float h, D2D1_COLOR_F color, float fontSize, bool bold) {
    if (!m_renderTarget || !m_dwriteFactory) return;

    ComPtr<IDWriteTextFormat> textFormat;
    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        fontSize,
        L"zh-cn",
        textFormat.GetAddressOf()
    );

    if (SUCCEEDED(hr)) {
        textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        m_solidBrush->SetColor(color);
        D2D1_RECT_F layoutRect = D2D1::RectF(x, y, x + w, y + h);
        m_renderTarget->DrawText(
            text.c_str(),
            static_cast<UINT32>(text.length()),
            textFormat.Get(),
            layoutRect,
            m_solidBrush.Get()
        );
    }
}

void D2DRenderer::Draw3DCard(float x, float y, float w, float h, D2D1_COLOR_F baseColor) {
    if (!m_renderTarget) return;

    D2D1_ROUNDED_RECT rRect = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 4.0f, 4.0f);

    // Card background
    m_solidBrush->SetColor(baseColor);
    m_renderTarget->FillRoundedRectangle(rRect, m_solidBrush.Get());

    // Top & left 3D specular highlight (light edge)
    m_solidBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f));
    m_renderTarget->DrawLine(D2D1::Point2F(x + 2, y + 1), D2D1::Point2F(x + w - 2, y + 1), m_solidBrush.Get(), 1.0f);
    m_renderTarget->DrawLine(D2D1::Point2F(x + 1, y + 2), D2D1::Point2F(x + 1, y + h - 2), m_solidBrush.Get(), 1.0f);

    // Bottom & right 3D bevel shadow (dark edge)
    m_solidBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.35f));
    m_renderTarget->DrawLine(D2D1::Point2F(x + 2, y + h - 1), D2D1::Point2F(x + w - 2, y + h - 1), m_solidBrush.Get(), 1.0f);
    m_renderTarget->DrawLine(D2D1::Point2F(x + w - 1, y + 2), D2D1::Point2F(x + w - 1, y + h - 2), m_solidBrush.Get(), 1.0f);
}

void D2DRenderer::Draw3DButton(float x, float y, float w, float h, const std::wstring& label, bool isHot, bool isPressed) {
    if (!m_renderTarget) return;

    D2D1_ROUNDED_RECT rRect = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 4.0f, 4.0f);

    // Gold/Metallic gradient simulated base
    D2D1_COLOR_F fill = isPressed
        ? D2D1::ColorF(0.72f, 0.52f, 0.04f, 1.0f)
        : (isHot ? D2D1::ColorF(1.0f, 0.85f, 0.2f, 1.0f) : D2D1::ColorF(0.92f, 0.72f, 0.15f, 1.0f));

    m_solidBrush->SetColor(fill);
    m_renderTarget->FillRoundedRectangle(rRect, m_solidBrush.Get());

    // 3D Bevel: pressed inverts highlight/shadow
    D2D1_COLOR_F topEdge = isPressed ? D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.4f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.6f);
    D2D1_COLOR_F bottomEdge = isPressed ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.3f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.5f);

    m_solidBrush->SetColor(topEdge);
    m_renderTarget->DrawLine(D2D1::Point2F(x + 2, y + 1), D2D1::Point2F(x + w - 2, y + 1), m_solidBrush.Get(), 1.5f);
    m_solidBrush->SetColor(bottomEdge);
    m_renderTarget->DrawLine(D2D1::Point2F(x + 2, y + h - 1), D2D1::Point2F(x + w - 2, y + h - 1), m_solidBrush.Get(), 1.5f);

    // Text in dark brown/black with slight 3D emboss
    DrawTextSimple(label, x + (isPressed ? 1.0f : 0.0f), y + (isPressed ? 1.0f : 0.0f), w, h, D2D1::ColorF(0.12f, 0.08f, 0.02f, 1.0f), 12.0f, true);
}

} // namespace ClashWin
