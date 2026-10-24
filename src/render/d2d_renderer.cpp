#include "d2d_renderer.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace ClashWin {

D2DRenderer::D2DRenderer() = default;
D2DRenderer::~D2DRenderer() {
    DiscardDeviceResources();
}

bool D2DRenderer::Initialize(HWND hwnd) {
    m_hwnd = hwnd;

    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        m_d2dFactory.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())
    );
    if (FAILED(hr)) return false;

    return SUCCEEDED(CreateDeviceResources());
}

bool D2DRenderer::InitializeWithRenderTarget(ComPtr<ID2D1RenderTarget> target) {
    if (!target) return false;
    m_renderTarget = target;
    m_renderTarget->GetFactory(m_d2dFactory.GetAddressOf());

    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())
    );
    if (FAILED(hr)) return false;

    return SUCCEEDED(m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_solidBrush));
}

HRESULT D2DRenderer::CreateDeviceResources() {
    if (m_renderTarget) return S_OK;

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    ComPtr<ID2D1HwndRenderTarget> hwndRt;
    HRESULT hr = m_d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        ),
        D2D1::HwndRenderTargetProperties(m_hwnd, size),
        hwndRt.GetAddressOf()
    );

    if (SUCCEEDED(hr)) {
        m_renderTarget = hwndRt;
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
        ComPtr<ID2D1HwndRenderTarget> hwndRt;
        if (SUCCEEDED(m_renderTarget.As(&hwndRt))) {
            hwndRt->Resize(D2D1::SizeU(width, height));
        }
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

void D2DRenderer::FillRect(float x, float y, float w, float h, D2D1_COLOR_F color) {
    if (!m_renderTarget) return;
    m_solidBrush->SetColor(color);
    m_renderTarget->FillRectangle(D2D1::RectF(x, y, x + w, y + h), m_solidBrush.Get());
}

void D2DRenderer::DrawLine(float x1, float y1, float x2, float y2, D2D1_COLOR_F color, float strokeWidth) {
    if (!m_renderTarget) return;
    m_solidBrush->SetColor(color);
    m_renderTarget->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), m_solidBrush.Get(), strokeWidth);
}

void D2DRenderer::DrawTextSimple(const std::wstring& text, float x, float y, float w, float h,
                                D2D1_COLOR_F color, float fontSize, bool bold,
                                DWRITE_TEXT_ALIGNMENT align) {
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
        textFormat->SetTextAlignment(align);
        textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

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

void D2DRenderer::Draw3DCard(float x, float y, float w, float h, D2D1_COLOR_F baseColor, bool isPressed) {
    if (!m_renderTarget) return;

    const auto& theme = ThemeManager::Instance().GetColors();
    D2D1_ROUNDED_RECT rRect = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 3.0f, 3.0f);

    m_solidBrush->SetColor(baseColor);
    m_renderTarget->FillRoundedRectangle(rRect, m_solidBrush.Get());

    D2D1_COLOR_F topEdge = isPressed ? theme.card_shadow : theme.card_highlight;
    D2D1_COLOR_F bottomEdge = isPressed ? theme.card_highlight : theme.card_shadow;

    m_solidBrush->SetColor(topEdge);
    m_renderTarget->DrawLine(D2D1::Point2F(x + 2, y + 1), D2D1::Point2F(x + w - 2, y + 1), m_solidBrush.Get(), 1.0f);
    m_renderTarget->DrawLine(D2D1::Point2F(x + 1, y + 2), D2D1::Point2F(x + 1, y + h - 2), m_solidBrush.Get(), 1.0f);

    m_solidBrush->SetColor(bottomEdge);
    m_renderTarget->DrawLine(D2D1::Point2F(x + 2, y + h - 1), D2D1::Point2F(x + w - 2, y + h - 1), m_solidBrush.Get(), 1.0f);
    m_renderTarget->DrawLine(D2D1::Point2F(x + w - 1, y + 2), D2D1::Point2F(x + w - 1, y + h - 2), m_solidBrush.Get(), 1.0f);
}

void D2DRenderer::Draw3DButton(float x, float y, float w, float h, const std::wstring& label,
                               bool isHot, bool isPressed, bool isActive) {
    if (!m_renderTarget) return;

    const auto& theme = ThemeManager::Instance().GetColors();
    D2D1_COLOR_F btnBg = isActive ? theme.card_active_bg : (isHot ? theme.card_bg : theme.card_bg);

    if (isActive) {
        btnBg.r = std::min(1.0f, btnBg.r * 1.15f);
        btnBg.g = std::min(1.0f, btnBg.g * 1.15f);
        btnBg.b = std::min(1.0f, btnBg.b * 1.15f);
    }

    Draw3DCard(x, y, w, h, btnBg, isPressed);

    float textOffsetX = isPressed ? 1.0f : 0.0f;
    float textOffsetY = isPressed ? 1.0f : 0.0f;

    D2D1_COLOR_F textColor = isActive ? theme.accent : theme.text_primary;
    DrawTextSimple(label, x + textOffsetX, y + textOffsetY, w, h, textColor, 11.0f, isActive, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void D2DRenderer::DrawWindowCaptionButton(float x, float y, float w, float h, const std::wstring& glyph, int btnType, bool isHovered) {
    if (!m_renderTarget) return;

    const auto& theme = ThemeManager::Instance().GetColors();
    D2D1_ROUNDED_RECT rRect = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 4.0f, 4.0f);

    D2D1_COLOR_F glyphColor = theme.text_primary;
    if (isHovered) {
        if (btnType == 3) {
            // 关闭按钮悬浮：Windows 经典红色 #E81123，白色图标
            m_solidBrush->SetColor(D2D1::ColorF(0.91f, 0.07f, 0.14f, 1.0f));
            m_renderTarget->FillRoundedRectangle(rRect, m_solidBrush.Get());
            glyphColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        } else {
            // 最小化/最大化悬浮：浅色微光背景
            m_solidBrush->SetColor(D2D1::ColorF(theme.card_active_bg.r, theme.card_active_bg.g, theme.card_active_bg.b, 0.65f));
            m_renderTarget->FillRoundedRectangle(rRect, m_solidBrush.Get());
        }
    }

    // 采用 Direct2D 原生矢量几何绘制，彻底杜绝字体字形缺失或抗锯齿变形缺陷
    m_solidBrush->SetColor(glyphColor);
    float cx = std::floor(x + w * 0.5f);
    float cy = std::floor(y + h * 0.5f);

    if (btnType == 1) {
        // 最小化 (Minimize): 10px 锐利水平横线 (对齐 Windows / MUI Minimize)
        m_renderTarget->DrawLine(
            D2D1::Point2F(cx - 5.0f, cy + 3.0f),
            D2D1::Point2F(cx + 5.0f, cy + 3.0f),
            m_solidBrush.Get(), 1.2f);
    } else if (btnType == 2) {
        // 最大化/还原 (Maximize/Restore): 对齐 Windows / MUI CropSquare & FilterNone
        if (glyph == L"❐") {
            // 还原状态 (FilterNone): 两个 8x8 错开 3px 的重叠方框
            // 1. 后面方框 (右上)
            m_renderTarget->DrawRectangle(
                D2D1::RectF(cx - 2.0f, cy - 5.0f, cx + 5.0f, cy + 2.0f),
                m_solidBrush.Get(), 1.0f);
            // 2. 擦除背景避免前后方框透射重叠
            D2D1_COLOR_F bgClr = isHovered ? D2D1::ColorF(theme.card_active_bg.r, theme.card_active_bg.g, theme.card_active_bg.b, 0.65f) : theme.header_bg;
            m_solidBrush->SetColor(bgClr);
            m_renderTarget->FillRectangle(D2D1::RectF(cx - 5.0f, cy - 2.0f, cx + 2.0f, cy + 5.0f), m_solidBrush.Get());
            // 3. 前地方框 (左下)
            m_solidBrush->SetColor(glyphColor);
            m_renderTarget->DrawRectangle(
                D2D1::RectF(cx - 5.0f, cy - 2.0f, cx + 2.0f, cy + 5.0f),
                m_solidBrush.Get(), 1.0f);
        } else {
            // 单独最大化方框 (CropSquare): 10x10 居中空心矩形
            m_renderTarget->DrawRectangle(
                D2D1::RectF(cx - 5.0f, cy - 5.0f, cx + 5.0f, cy + 5.0f),
                m_solidBrush.Get(), 1.0f);
        }
    } else if (btnType == 3) {
        // 关闭按钮 (Close): 10x10 标准交叉矢量斜线 ✕
        m_renderTarget->DrawLine(
            D2D1::Point2F(cx - 4.5f, cy - 4.5f),
            D2D1::Point2F(cx + 4.5f, cy + 4.5f),
            m_solidBrush.Get(), 1.2f);
        m_renderTarget->DrawLine(
            D2D1::Point2F(cx + 4.5f, cy - 4.5f),
            D2D1::Point2F(cx - 4.5f, cy + 4.5f),
            m_solidBrush.Get(), 1.2f);
    } else {
        DrawTextSimple(glyph, x, y, w, h, glyphColor, 11.5f, false, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void D2DRenderer::DrawSegmentedControl(float x, float y, float w, float h,
                                       const std::vector<std::wstring>& options,
                                       int selectedIndex, int hotIndex) {
    if (!m_renderTarget || options.empty()) return;

    const auto& theme = ThemeManager::Instance().GetColors();

    D2D1_ROUNDED_RECT trackRect = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 4.0f, 4.0f);
    D2D1_COLOR_F trackBg = theme.window_bg;
    trackBg.r *= 0.85f; trackBg.g *= 0.85f; trackBg.b *= 0.85f;
    m_solidBrush->SetColor(trackBg);
    m_renderTarget->FillRoundedRectangle(trackRect, m_solidBrush.Get());

    m_solidBrush->SetColor(theme.card_shadow);
    m_renderTarget->DrawRoundedRectangle(trackRect, m_solidBrush.Get(), 1.0f);

    float segW = (w - 4.0f) / options.size();
    float segH = h - 4.0f;
    float curX = x + 2.0f;
    float curY = y + 2.0f;

    for (size_t i = 0; i < options.size(); ++i) {
        bool isSelected = (static_cast<int>(i) == selectedIndex);
        bool isHot = (static_cast<int>(i) == hotIndex);

        if (isSelected) {
            Draw3DCard(curX, curY, segW, segH, theme.card_active_bg, false);
            m_solidBrush->SetColor(theme.accent);
            m_renderTarget->DrawLine(D2D1::Point2F(curX + 4.0f, curY + segH - 2.0f),
                                     D2D1::Point2F(curX + segW - 4.0f, curY + segH - 2.0f),
                                     m_solidBrush.Get(), 2.0f);
            DrawTextSimple(options[i], curX, curY, segW, segH - 2.0f, theme.accent, 11.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        } else {
            if (isHot) {
                m_solidBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.05f));
                m_renderTarget->FillRectangle(D2D1::RectF(curX, curY, curX + segW, curY + segH), m_solidBrush.Get());
            }
            DrawTextSimple(options[i], curX, curY, segW, segH, theme.text_secondary, 10.5f, false, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        curX += segW;
    }
}

void D2DRenderer::DrawSlider(float x, float y, float w, float h,
                             const std::wstring& label, float value, float minVal, float maxVal,
                             bool isHot) {
    if (!m_renderTarget) return;

    const auto& theme = ThemeManager::Instance().GetColors();

    float labelW = 75.0f;
    float valW = 35.0f;
    float trackX = x + labelW;
    float trackW = w - labelW - valW;
    float trackH = 4.0f;
    float trackY = y + (h - trackH) / 2.0f;

    DrawTextSimple(label, x, y, labelW, h, theme.text_primary, 11.0f, false);

    D2D1_ROUNDED_RECT trackRect = D2D1::RoundedRect(D2D1::RectF(trackX, trackY, trackX + trackW, trackY + trackH), 2.0f, 2.0f);
    m_solidBrush->SetColor(theme.card_shadow);
    m_renderTarget->FillRoundedRectangle(trackRect, m_solidBrush.Get());

    float fraction = std::clamp((value - minVal) / (maxVal - minVal), 0.0f, 1.0f);
    float knobX = trackX + fraction * trackW;
    D2D1_ROUNDED_RECT fillRect = D2D1::RoundedRect(D2D1::RectF(trackX, trackY, knobX, trackY + trackH), 2.0f, 2.0f);
    m_solidBrush->SetColor(theme.accent);
    m_renderTarget->FillRoundedRectangle(fillRect, m_solidBrush.Get());

    float knobRadius = 6.0f;
    D2D1_ELLIPSE knob = D2D1::Ellipse(D2D1::Point2F(knobX, trackY + trackH / 2.0f), knobRadius, knobRadius);
    m_solidBrush->SetColor(isHot ? theme.accent : theme.text_primary);
    m_renderTarget->FillEllipse(knob, m_solidBrush.Get());
    m_solidBrush->SetColor(theme.card_highlight);
    m_renderTarget->DrawEllipse(knob, m_solidBrush.Get(), 1.0f);

    std::wostringstream oss;
    oss << std::fixed << std::setprecision(1) << value;
    DrawTextSimple(oss.str(), trackX + trackW + 6.0f, y, valW, h, theme.text_secondary, 10.0f, false);
}

void D2DRenderer::Draw3DSwitch(float x, float y, float w, float h, const std::wstring& label, bool isChecked, bool isHot) {
    if (!m_renderTarget) return;
    const auto& theme = ThemeManager::Instance().GetColors();
    
    float switchW = 32.0f;
    float switchH = 16.0f;
    float labelW = w - switchW - 4.0f;
    if (labelW > 0.0f) {
        DrawTextSimple(label, x, y, labelW, h, theme.text_primary, 11.0f, false);
    }
    
    float swX = x + w - switchW;
    float swY = y + (h - switchH) / 2.0f;
    float radius = switchH / 2.0f;
    
    D2D1_ROUNDED_RECT capsule = D2D1::RoundedRect(D2D1::RectF(swX, swY, swX + switchW, swY + switchH), radius, radius);
    if (isChecked) {
        m_solidBrush->SetColor(theme.accent);
    } else {
        m_solidBrush->SetColor(theme.card_shadow);
    }
    m_renderTarget->FillRoundedRectangle(capsule, m_solidBrush.Get());
    
    float knobRadius = radius - 2.0f;
    float knobCenterY = swY + radius;
    float knobCenterX = isChecked ? (swX + switchW - radius) : (swX + radius);
    
    D2D1_ELLIPSE knob = D2D1::Ellipse(D2D1::Point2F(knobCenterX, knobCenterY), knobRadius, knobRadius);
    m_solidBrush->SetColor(isHot ? theme.card_highlight : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f));
    m_renderTarget->FillEllipse(knob, m_solidBrush.Get());
}

void D2DRenderer::Draw3DTextInput(float x, float y, float w, float h,
                                  const std::wstring& label, const std::wstring& text,
                                  const std::wstring& placeholder, bool isFocused, bool isHot) {
    if (!m_renderTarget) return;
    const auto& theme = ThemeManager::Instance().GetColors();
    
    float labelW = 0.0f;
    if (!label.empty()) {
        labelW = 60.0f;
        DrawTextSimple(label, x, y, labelW, h, theme.text_primary, 11.0f, false);
    }
    
    float boxX = x + labelW;
    float boxW = w - labelW;
    
    D2D1_ROUNDED_RECT bgRect = D2D1::RoundedRect(D2D1::RectF(boxX, y, boxX + boxW, y + h), 3.0f, 3.0f);
    m_solidBrush->SetColor(theme.card_shadow);
    m_renderTarget->FillRoundedRectangle(bgRect, m_solidBrush.Get());
    
    m_solidBrush->SetColor(isFocused ? theme.accent : (isHot ? theme.card_highlight : theme.card_highlight));
    m_renderTarget->DrawRoundedRectangle(bgRect, m_solidBrush.Get(), isFocused ? 1.5f : 0.8f);
    
    float padX = 6.0f;
    if (!text.empty()) {
        DrawTextSimple(text, boxX + padX, y, boxW - padX * 2, h, theme.text_primary, 11.0f, false);
    } else {
        DrawTextSimple(placeholder, boxX + padX, y, boxW - padX * 2, h, theme.text_secondary, 10.5f, false);
    }
    
    if (isFocused) {
        float textWEstimate = static_cast<float>(text.length()) * 6.5f;
        float caretX = boxX + padX + (std::min)(textWEstimate, boxW - padX * 2.0f);
        m_solidBrush->SetColor(theme.accent);
        m_renderTarget->DrawLine(D2D1::Point2F(caretX, y + 3.0f), D2D1::Point2F(caretX, y + h - 3.0f), m_solidBrush.Get(), 1.2f);
    }
}

void D2DRenderer::DrawLatencyBadge(float x, float y, float w, float h, int delay_ms) {
    if (!m_renderTarget) return;

    const auto& theme = ThemeManager::Instance().GetColors();
    D2D1_COLOR_F badgeColor;
    std::wstring text;

    if (delay_ms < 0 || delay_ms >= 2000) {
        badgeColor = theme.badge_slow;
        text = L"TIMEOUT";
    } else if (delay_ms <= 100) {
        badgeColor = theme.badge_fast;
        text = std::to_wstring(delay_ms) + L"ms";
    } else if (delay_ms <= 300) {
        badgeColor = theme.badge_medium;
        text = std::to_wstring(delay_ms) + L"ms";
    } else {
        badgeColor = theme.badge_slow;
        text = std::to_wstring(delay_ms) + L"ms";
    }

    D2D1_ROUNDED_RECT rRect = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 3.0f, 3.0f);
    m_solidBrush->SetColor(D2D1::ColorF(badgeColor.r, badgeColor.g, badgeColor.b, 0.15f));
    m_renderTarget->FillRoundedRectangle(rRect, m_solidBrush.Get());

    m_solidBrush->SetColor(D2D1::ColorF(badgeColor.r, badgeColor.g, badgeColor.b, 0.60f));
    m_renderTarget->DrawRoundedRectangle(rRect, m_solidBrush.Get(), 1.0f);

    DrawTextSimple(text, x, y, w, h, badgeColor, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void D2DRenderer::DrawConnectionRow(float x, float y, float w, float h,
                                    const std::wstring& host, const std::wstring& chain,
                                    const std::wstring& speed, bool isSelected) {
    if (!m_renderTarget) return;

    const auto& theme = ThemeManager::Instance().GetColors();
    D2D1_COLOR_F rowBg = isSelected ? theme.card_active_bg : theme.card_bg;

    Draw3DCard(x, y, w, h, rowBg, false);

    float col1W = w * 0.50f;
    float col2W = w * 0.28f;
    float col3W = w * 0.22f;

    DrawTextSimple(host, x + 8.0f, y, col1W - 12.0f, h, theme.text_primary, 10.5f, false);

    float chainBadgeW = std::min(col2W - 8.0f, 70.0f);
    D2D1_ROUNDED_RECT badgeRect = D2D1::RoundedRect(D2D1::RectF(x + col1W, y + 4.0f, x + col1W + chainBadgeW, y + h - 4.0f), 3.0f, 3.0f);
    m_solidBrush->SetColor(D2D1::ColorF(theme.accent.r, theme.accent.g, theme.accent.b, 0.20f));
    m_renderTarget->FillRoundedRectangle(badgeRect, m_solidBrush.Get());
    m_solidBrush->SetColor(theme.accent);
    m_renderTarget->DrawRoundedRectangle(badgeRect, m_solidBrush.Get(), 1.0f);
    DrawTextSimple(chain, x + col1W, y, chainBadgeW, h, theme.accent, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);

    DrawTextSimple(speed, x + col1W + col2W, y, col3W - 8.0f, h, theme.text_secondary, 10.0f, false, DWRITE_TEXT_ALIGNMENT_TRAILING);
}

// =========================================================================
// Clash Mini 1:1 像素级精准对齐组件实现
// =========================================================================

std::wstring D2DRenderer::TrimNodeName(const std::wstring& name) {
    if (name.length() >= 9) {
        size_t len = name.length();
        if (name[len - 1] == L')' && name[len - 8] == L'(' && name[len - 9] == L' ') {
            bool allDigits = true;
            for (size_t i = len - 7; i <= len - 2; ++i) {
                if (name[i] < L'0' || name[i] > L'9') {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits) {
                return name.substr(0, len - 9);
            }
        }
    }
    return name;
}

void D2DRenderer::DrawProxyItem3Col(float x, float y, float w, float h,
                                    const std::wstring& type, const std::wstring& name,
                                    int delay_ms, bool isSelected, bool isOdd, bool isHot) {
    if (!m_renderTarget) return;

    const auto& theme = ThemeManager::Instance().GetColors();
    (void)isHot;

    // 1. 斑马纹与选中背景 (对齐 Clash Mini: 奇数行淡色斑马纹，选中行主题色微光)
    D2D1_COLOR_F rowBg = isOdd ? D2D1::ColorF(theme.card_active_bg.r, theme.card_active_bg.g, theme.card_active_bg.b, 0.35f)
                               : D2D1::ColorF(theme.card_bg.r, theme.card_bg.g, theme.card_bg.b, 0.05f);
    if (isSelected) {
        rowBg = D2D1::ColorF(theme.accent.r, theme.accent.g, theme.accent.b, 0.20f);
    }

    m_solidBrush->SetColor(rowBg);
    m_renderTarget->FillRectangle(D2D1::RectF(x, y, x + w, y + h), m_solidBrush.Get());

    // 2. 选中指示条：左侧 3px solid 激活色 (对齐 Clash Mini: borderLeft 3px)
    if (isSelected) {
        m_solidBrush->SetColor(theme.accent);
        m_renderTarget->FillRectangle(D2D1::RectF(x, y, x + 3.0f, y + h), m_solidBrush.Get());
    }

    float typeColW = 55.0f;
    float delayColW = 65.0f;
    float nameColW = w - typeColW - delayColW;

    // 3. 第一列：协议类型 (55px，大写，右对齐，2px 分割线)
    DrawTextSimple(type, x + 2.0f, y, typeColW - 6.0f, h, theme.text_secondary, 10.5f, false, DWRITE_TEXT_ALIGNMENT_TRAILING);

    // 第一列与第二列之间的 2px 竖向分割线
    m_solidBrush->SetColor(theme.card_shadow);
    m_renderTarget->DrawLine(D2D1::Point2F(x + typeColW, y),
                             D2D1::Point2F(x + typeColW, y + h),
                             m_solidBrush.Get(), 1.0f);

    // 4. 第二列：节点名称 (flex 1，左对齐，单行截断，剥离 6 位时间戳)
    std::wstring displayName = TrimNodeName(name);
    D2D1_COLOR_F nameColor = isSelected ? theme.accent : theme.text_primary;
    DrawTextSimple(displayName, x + typeColW + 8.0f, y, nameColW - 14.0f, h, nameColor, 11.5f, isSelected, DWRITE_TEXT_ALIGNMENT_LEADING);

    // 第二列与第三列之间的 2px 竖向分割线
    m_renderTarget->DrawLine(D2D1::Point2F(x + typeColW + nameColW, y),
                             D2D1::Point2F(x + typeColW + nameColW, y + h),
                             m_solidBrush.Get(), 1.0f);

    // 5. 第三列：延迟数值 (65px，居中，带有主题速度色彩)
    // 严格对齐 Clash Mini (proxy-item.tsx):
    // 若 delay_ms <= 0 或 -1 (未测速)，完全留白，不绘制任何假数据或文字；
    // 仅当实际产生延迟测速数值 (delay_ms > 0) 时，以毫秒呈现；>= 2000ms 判死显示 Timeout
    if (delay_ms >= 2000) {
        DrawTextSimple(L"Timeout", x + typeColW + nameColW, y, delayColW, h, theme.badge_slow, 9.5f, false, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else if (delay_ms > 0) {
        D2D1_COLOR_F delayColor = (delay_ms < 250) ? theme.badge_fast : ((delay_ms < 600) ? theme.badge_medium : theme.badge_slow);
        std::wstring dText = std::to_wstring(delay_ms) + L"ms";
        DrawTextSimple(dText, x + typeColW + nameColW, y, delayColW, h, delayColor, 10.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // 6. 底部分割槽线 (2px solid divider)
    m_solidBrush->SetColor(theme.card_shadow);
    m_renderTarget->DrawLine(D2D1::Point2F(x, y + h - 1.0f), D2D1::Point2F(x + w, y + h - 1.0f), m_solidBrush.Get(), 1.0f);
}

void D2DRenderer::DrawActiveNodeCard(float x, float y, float w, float h,
                                     const std::wstring& protocol, const std::wstring& name,
                                     int delay_ms, const std::wstring& addr, bool isTesting) {
    if (!m_renderTarget) return;

    const auto& theme = ThemeManager::Instance().GetColors();

    // 活跃卡片底板 (28px 实体拟物卡片)
    Draw3DCard(x, y, w, h, theme.card_active_bg, false);

    float curX = x + 8.0f;
    float delayChipW = 50.0f;

    // 1. 协议徽章 (窄窗 <= 350px 下隐藏，严格对齐 Clash Mini: @media (max-width: 480px) display: none)
    if (w > 350.0f && !protocol.empty()) {
        float protoW = 44.0f;
        float protoH = h - 8.0f;
        D2D1_ROUNDED_RECT protoRect = D2D1::RoundedRect(D2D1::RectF(curX, y + 4.0f, curX + protoW, y + 4.0f + protoH), 3.0f, 3.0f);
        m_solidBrush->SetColor(D2D1::ColorF(theme.accent.r, theme.accent.g, theme.accent.b, 0.15f));
        m_renderTarget->FillRoundedRectangle(protoRect, m_solidBrush.Get());
        m_solidBrush->SetColor(theme.accent);
        m_renderTarget->DrawRoundedRectangle(protoRect, m_solidBrush.Get(), 1.0f);
        DrawTextSimple(protocol, curX, y, protoW, h, theme.accent, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        curX += protoW + 6.0f;
    }

    // 2. 节点名称 (可点击轮换下一个节点，带悬浮光，剥离 6 位时间戳)
    std::wstring displayName = TrimNodeName(name);
    float nameW = w - (curX - x) - delayChipW - 6.0f;
    if (w > 450.0f && !addr.empty()) {
        float addrW = 85.0f;
        DrawTextSimple(displayName, curX, y, nameW - addrW, h, theme.text_primary, 11.5f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawTextSimple(L"(" + addr + L")", curX + nameW - addrW, y, addrW, h, theme.text_secondary, 9.5f, false, DWRITE_TEXT_ALIGNMENT_LEADING);
    } else {
        (void)addr;
        DrawTextSimple(displayName, curX, y, nameW, h, theme.text_primary, 11.5f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    // 3. 延迟测速徽章 (可点击触发单测速)
    float chipX = x + w - delayChipW - 4.0f;
    float chipY = y + 4.0f;
    float chipH = h - 8.0f;
    D2D1_ROUNDED_RECT chipRect = D2D1::RoundedRect(D2D1::RectF(chipX, chipY, chipX + delayChipW, chipY + chipH), 3.0f, 3.0f);

    if (isTesting) {
        m_solidBrush->SetColor(D2D1::ColorF(1.0f, 0.8f, 0.2f, 0.25f));
        m_renderTarget->FillRoundedRectangle(chipRect, m_solidBrush.Get());
        DrawTextSimple(L"测速中...", chipX, chipY, delayChipW, chipH, theme.accent, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else if (delay_ms > 0 && delay_ms < 2000) {
        D2D1_COLOR_F dClr = (delay_ms < 250) ? theme.badge_fast : ((delay_ms < 600) ? theme.badge_medium : theme.badge_slow);
        m_solidBrush->SetColor(D2D1::ColorF(dClr.r, dClr.g, dClr.b, 0.15f));
        m_renderTarget->FillRoundedRectangle(chipRect, m_solidBrush.Get());
        m_solidBrush->SetColor(dClr);
        m_renderTarget->DrawRoundedRectangle(chipRect, m_solidBrush.Get(), 1.0f);
        std::wstring dStr = std::to_wstring(delay_ms) + L"ms";
        DrawTextSimple(dStr, chipX, chipY, delayChipW, chipH, dClr, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else if (delay_ms >= 2000) {
        m_solidBrush->SetColor(D2D1::ColorF(theme.badge_slow.r, theme.badge_slow.g, theme.badge_slow.b, 0.15f));
        m_renderTarget->FillRoundedRectangle(chipRect, m_solidBrush.Get());
        m_solidBrush->SetColor(theme.badge_slow);
        m_renderTarget->DrawRoundedRectangle(chipRect, m_solidBrush.Get(), 1.0f);
        DrawTextSimple(L"Timeout", chipX, chipY, delayChipW, chipH, theme.badge_slow, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
        // 未测速 (delay_ms <= 0 或 -1)
        m_solidBrush->SetColor(theme.card_bg);
        m_renderTarget->FillRoundedRectangle(chipRect, m_solidBrush.Get());
        m_solidBrush->SetColor(theme.card_shadow);
        m_renderTarget->DrawRoundedRectangle(chipRect, m_solidBrush.Get(), 1.0f);
        DrawTextSimple(L"TEST", chipX, chipY, delayChipW, chipH, theme.text_secondary, 9.5f, false, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void D2DRenderer::DrawProxyHead(float x, float y, float w, float h,
                                int sortType, const std::wstring& searchText,
                                bool isTesting, bool searchFocused) {
    if (!m_renderTarget) return;

    const auto& theme = ThemeManager::Instance().GetColors();

    float padX = 12.0f;
    float btnSize = 24.0f;
    float btnY = y + (h - btnSize) / 2.0f;

    // 1. ⚡ 一键测速按钮 (24x24)
    Draw3DButton(x + padX, btnY, btnSize, btnSize, L"⚡", false, false, isTesting);

    // 2. ↕ 排序切换按钮 (24x24)
    std::wstring sortLabel = L"↕";
    if (sortType == 1) sortLabel = L"⏱";
    else if (sortType == 2) sortLabel = L"A-Z";
    Draw3DButton(x + padX + btnSize + 4.0f, btnY, btnSize, btnSize, sortLabel, false, false, (sortType != 0));

    // 3. 🔍 实时输入搜索框 (Flex 1)
    float searchX = x + padX + (btnSize + 4.0f) * 2;
    float searchW = w - (searchX - x) - padX;
    float searchH = btnSize;
    D2D1_ROUNDED_RECT sRect = D2D1::RoundedRect(D2D1::RectF(searchX, btnY, searchX + searchW, btnY + searchH), 3.0f, 3.0f);

    m_solidBrush->SetColor(theme.card_bg);
    m_renderTarget->FillRoundedRectangle(sRect, m_solidBrush.Get());

    m_solidBrush->SetColor(searchFocused ? theme.accent : theme.card_shadow);
    m_renderTarget->DrawRoundedRectangle(sRect, m_solidBrush.Get(), searchFocused ? 1.5f : 1.0f);

    // 放大镜图标
    DrawTextSimple(L"🔍", searchX + 4.0f, btnY, 16.0f, searchH, theme.text_secondary, 9.5f, false, DWRITE_TEXT_ALIGNMENT_CENTER);

    // 输入内容或占位符
    if (searchText.empty()) {
        DrawTextSimple(L"过滤节点...", searchX + 22.0f, btnY, searchW - 26.0f, searchH, theme.text_secondary, 10.5f, false);
    } else {
        DrawTextSimple(searchText, searchX + 22.0f, btnY, searchW - 26.0f, searchH, theme.text_primary, 10.5f, false);
    }

    // 底部分割线 1px solid
    m_solidBrush->SetColor(theme.card_shadow);
    m_renderTarget->DrawLine(D2D1::Point2F(x, y + h - 1.0f), D2D1::Point2F(x + w, y + h - 1.0f), m_solidBrush.Get(), 1.0f);
}

void D2DRenderer::DrawTrafficDashboard(float x, float y, float w, float h,
                                      uint64_t upSpeed, uint64_t downSpeed,
                                      uint64_t upTotal, uint64_t downTotal,
                                      const std::deque<TrafficPoint>& history,
                                      bool isMinimalWidth) {
    if (!m_renderTarget) return;

    const auto& theme = ThemeManager::Instance().GetColors();

    // 1. 顶部分割线
    m_solidBrush->SetColor(theme.card_shadow);
    m_renderTarget->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x + w, y), m_solidBrush.Get(), 1.0f);

    float padX = isMinimalWidth ? 6.0f : 12.0f;
    float padTop = isMinimalWidth ? 3.0f : 8.0f;
    float padBottom = isMinimalWidth ? 2.0f : 4.0f;

    float cw = w - padX * 2.0f;
    if (cw <= 10.0f) return;

    // 格式化实时速率与总流量
    auto FormatSpeed = [](uint64_t bytesPerSec) -> std::wstring {
        if (bytesPerSec < 1024) return std::to_wstring(bytesPerSec) + L" B/s";
        if (bytesPerSec < 1024 * 1024) {
            std::wostringstream oss;
            oss << std::fixed << std::setprecision(1) << (bytesPerSec / 1024.0) << L" KB/s";
            return oss.str();
        }
        std::wostringstream oss;
        oss << std::fixed << std::setprecision(1) << (bytesPerSec / (1024.0 * 1024.0)) << L" MB/s";
        return oss.str();
    };

    auto FormatTotal = [](uint64_t totalBytes) -> std::wstring {
        if (totalBytes < 1024 * 1024) return std::to_wstring(totalBytes / 1024) + L" KB";
        if (totalBytes < 1024 * 1024 * 1024) {
            std::wostringstream oss;
            oss << std::fixed << std::setprecision(1) << (totalBytes / (1024.0 * 1024.0)) << L" MB";
            return oss.str();
        }
        std::wostringstream oss;
        oss << std::fixed << std::setprecision(2) << (totalBytes / (1024.0 * 1024.0 * 1024.0)) << L" GB";
        return oss.str();
    };

    float metricsAreaH = isMinimalWidth ? 38.0f : 22.0f;
    float graphH = h - padTop - padBottom - metricsAreaH - (isMinimalWidth ? 3.0f : 6.0f);
    if (graphH < 20.0f) graphH = 20.0f;

    float gx = x + padX;
    float gy = y + padTop;
    float gw = cw;

    // 2. 绘制 7 层对数网格参考线 (0, 10, 100, 1K, 10K, 100K, 1M, 10M)
    float dy = graphH / 7.0f;
    m_solidBrush->SetColor(D2D1::ColorF(theme.card_shadow.r, theme.card_shadow.g, theme.card_shadow.b, 0.18f));
    for (int k = 1; k <= 6; ++k) {
        float lineY = gy + k * dy;
        m_renderTarget->DrawLine(D2D1::Point2F(gx, lineY), D2D1::Point2F(gx + gw, lineY), m_solidBrush.Get(), 0.8f);
    }

    // 3. 绘制平滑对数流量波形曲线 (上行紫色 #9C27B0, 下行蓝色 #007AFF)
    auto countY = [graphH, dy](uint64_t v) -> float {
        if (v == 0) return graphH - 1.0f;
        if (v <= 10) return graphH - (static_cast<float>(v) / 10.0f) * dy;
        if (v <= 100) return graphH - (static_cast<float>(v) / 100.0f + 1.0f) * dy;
        if (v <= 1024) return graphH - (static_cast<float>(v) / 1024.0f + 2.0f) * dy;
        if (v <= 10240) return graphH - (static_cast<float>(v) / 10240.0f + 3.0f) * dy;
        if (v <= 102400) return graphH - (static_cast<float>(v) / 102400.0f + 4.0f) * dy;
        if (v <= 1048576) return graphH - (static_cast<float>(v) / 1048576.0f + 5.0f) * dy;
        if (v <= 10485760) return graphH - (static_cast<float>(v) / 10485760.0f + 6.0f) * dy;
        return 1.0f;
    };

    size_t nPoints = history.size();
    if (nPoints >= 2) {
        float dx = gw / 29.0f;

        // A. 下行曲线 (Down: Clash Verge Blue / #007AFF)
        ComPtr<ID2D1PathGeometry> downPath;
        if (SUCCEEDED(m_d2dFactory->CreatePathGeometry(&downPath))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(downPath->Open(&sink))) {
                float startX = gx + gw - static_cast<float>(nPoints - 1) * dx;
                float startY = gy + countY(history[0].down);
                sink->BeginFigure(D2D1::Point2F(startX, startY), D2D1_FIGURE_BEGIN_HOLLOW);

                for (size_t i = 1; i < nPoints; ++i) {
                    float px = gx + gw - static_cast<float>(nPoints - 1 - i) * dx;
                    float py = gy + countY(history[i].down);
                    sink->AddLine(D2D1::Point2F(px, py));
                }
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
            }
            m_solidBrush->SetColor(D2D1::ColorF(0.0f, 0.48f, 1.0f, 0.90f));
            m_renderTarget->DrawGeometry(downPath.Get(), m_solidBrush.Get(), 2.2f);
        }

        // B. 上行曲线 (Up: Purple / #9C27B0)
        ComPtr<ID2D1PathGeometry> upPath;
        if (SUCCEEDED(m_d2dFactory->CreatePathGeometry(&upPath))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(upPath->Open(&sink))) {
                float startX = gx + gw - static_cast<float>(nPoints - 1) * dx;
                float startY = gy + countY(history[0].up);
                sink->BeginFigure(D2D1::Point2F(startX, startY), D2D1_FIGURE_BEGIN_HOLLOW);

                for (size_t i = 1; i < nPoints; ++i) {
                    float px = gx + gw - static_cast<float>(nPoints - 1 - i) * dx;
                    float py = gy + countY(history[i].up);
                    sink->AddLine(D2D1::Point2F(px, py));
                }
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
            }
            m_solidBrush->SetColor(D2D1::ColorF(0.61f, 0.15f, 0.69f, 0.85f));
            m_renderTarget->DrawGeometry(upPath.Get(), m_solidBrush.Get(), 2.2f);
        }
    }

    // 4. 绘制 3D 拟物速率指示胶囊卡片
    float metricsY = gy + graphH + (isMinimalWidth ? 3.0f : 6.0f);

    std::wstring downSpdStr = FormatSpeed(downSpeed);
    std::wstring downTotStr = FormatTotal(downTotal);
    std::wstring upSpdStr = FormatSpeed(upSpeed);
    std::wstring upTotStr = FormatTotal(upTotal);

    if (isMinimalWidth) {
        // 窄窗双行堆叠 (每行 17.5px)
        float cardH = 17.5f;

        // 行 1: 下行胶囊卡片 (Download)
        Draw3DCard(gx, metricsY, gw, cardH, D2D1::ColorF(0.85f, 0.89f, 0.92f, 1.0f), false);
        DrawTextSimple(L"↓ 下载:", gx + 6.0f, metricsY, 48.0f, cardH, D2D1::ColorF(0.0f, 0.45f, 0.75f, 1.0f), 9.5f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawTextSimple(downSpdStr, gx + 54.0f, metricsY, gw - 130.0f, cardH, theme.text_primary, 10.5f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawTextSimple(L"总计: " + downTotStr, gx + gw - 76.0f, metricsY, 72.0f, cardH, theme.text_secondary, 9.0f, false, DWRITE_TEXT_ALIGNMENT_TRAILING);

        // 行 2: 上行胶囊卡片 (Upload)
        float row2Y = metricsY + cardH + 3.0f;
        Draw3DCard(gx, row2Y, gw, cardH, D2D1::ColorF(0.95f, 0.88f, 0.80f, 1.0f), false);
        DrawTextSimple(L"↑ 上传:", gx + 6.0f, row2Y, 48.0f, cardH, D2D1::ColorF(0.85f, 0.40f, 0.05f, 1.0f), 9.5f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawTextSimple(upSpdStr, gx + 54.0f, row2Y, gw - 130.0f, cardH, theme.text_primary, 10.5f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawTextSimple(L"总计: " + upTotStr, gx + gw - 76.0f, row2Y, 72.0f, cardH, theme.text_secondary, 9.0f, false, DWRITE_TEXT_ALIGNMENT_TRAILING);
    } else {
        // 宽窗单行双列并排 (高度 22px)
        float cardW = (gw - 8.0f) / 2.0f;
        float cardH = 22.0f;

        // 左列: 下行胶囊卡片 (Download)
        Draw3DCard(gx, metricsY, cardW, cardH, D2D1::ColorF(0.85f, 0.89f, 0.92f, 1.0f), false);
        DrawTextSimple(L"↓ 下载:", gx + 8.0f, metricsY, 48.0f, cardH, D2D1::ColorF(0.0f, 0.45f, 0.75f, 1.0f), 10.0f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawTextSimple(downSpdStr, gx + 58.0f, metricsY, cardW - 145.0f, cardH, theme.text_primary, 11.0f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawTextSimple(L"总计: " + downTotStr, gx + cardW - 85.0f, metricsY, 78.0f, cardH, theme.text_secondary, 9.5f, false, DWRITE_TEXT_ALIGNMENT_TRAILING);

        // 右列: 上行胶囊卡片 (Upload)
        float upCardX = gx + cardW + 8.0f;
        Draw3DCard(upCardX, metricsY, cardW, cardH, D2D1::ColorF(0.95f, 0.88f, 0.80f, 1.0f), false);
        DrawTextSimple(L"↑ 上传:", upCardX + 8.0f, metricsY, 48.0f, cardH, D2D1::ColorF(0.85f, 0.40f, 0.05f, 1.0f), 10.0f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawTextSimple(upSpdStr, upCardX + 58.0f, metricsY, cardW - 145.0f, cardH, theme.text_primary, 11.0f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawTextSimple(L"总计: " + upTotStr, upCardX + cardW - 85.0f, metricsY, 78.0f, cardH, theme.text_secondary, 9.5f, false, DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
}

void D2DRenderer::DrawTrafficBottomBar(float x, float y, float w, float h,
                                       uint64_t upSpeed, uint64_t downSpeed,
                                       uint64_t upTotal, uint64_t downTotal) {
    std::deque<TrafficPoint> dummy;
    DrawTrafficDashboard(x, y, w, h, upSpeed, downSpeed, upTotal, downTotal, dummy, (w <= 285.0f));
}

void D2DRenderer::DrawModalBackdrop(float w, float h) {
    if (!m_renderTarget) return;
    m_solidBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.50f));
    m_renderTarget->FillRectangle(D2D1::RectF(0, 0, w, h), m_solidBrush.Get());
}

void D2DRenderer::DrawModalDialogFrame(float x, float y, float w, float h, const std::wstring& title) {
    if (!m_renderTarget) return;
    const auto& theme = ThemeManager::Instance().GetColors();

    // 1. 拟物底板
    D2D1_ROUNDED_RECT cardRect = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 6.0f, 6.0f);
    m_solidBrush->SetColor(theme.card_bg);
    m_renderTarget->FillRoundedRectangle(cardRect, m_solidBrush.Get());
    m_solidBrush->SetColor(theme.card_shadow);
    m_renderTarget->DrawRoundedRectangle(cardRect, m_solidBrush.Get(), 1.5f);

    // 2. 标题文字
    float titleH = 32.0f;
    DrawTextSimple(title, x + 14.0f, y + 6.0f, w - 44.0f, titleH, theme.text_primary, 13.0f, true);

    // 3. 右上角关闭按钮 (✕)
    float closeBtnSize = 22.0f;
    float closeX = x + w - closeBtnSize - 8.0f;
    float closeY = y + 6.0f;
    Draw3DButton(closeX, closeY, closeBtnSize, closeBtnSize, L"✕");

    // 4. 标题底部分割线
    m_solidBrush->SetColor(theme.card_shadow);
    m_renderTarget->DrawLine(D2D1::Point2F(x + 2.0f, y + titleH + 2.0f),
                             D2D1::Point2F(x + w - 2.0f, y + titleH + 2.0f),
                             m_solidBrush.Get(), 1.0f);
}

void D2DRenderer::DrawContextMenu(float x, float y, float w, float h,
                                 const std::vector<std::pair<std::wstring, bool>>& items,
                                 int hoveredIndex) {
    if (!m_renderTarget) return;
    const auto& theme = ThemeManager::Instance().GetColors();

    D2D1_ROUNDED_RECT menuRect = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 4.0f, 4.0f);
    m_solidBrush->SetColor(theme.card_active_bg);
    m_renderTarget->FillRoundedRectangle(menuRect, m_solidBrush.Get());
    m_solidBrush->SetColor(theme.card_shadow);
    m_renderTarget->DrawRoundedRectangle(menuRect, m_solidBrush.Get(), 1.0f);

    float itemH = 26.0f;
    float curY = y + 4.0f;
    for (size_t i = 0; i < items.size(); ++i) {
        if (static_cast<int>(i) == hoveredIndex) {
            m_solidBrush->SetColor(D2D1::ColorF(theme.accent.r, theme.accent.g, theme.accent.b, 0.22f));
            m_renderTarget->FillRectangle(D2D1::RectF(x + 2.0f, curY, x + w - 2.0f, curY + itemH), m_solidBrush.Get());
        }
        D2D1_COLOR_F textColor = items[i].second ? theme.text_primary : theme.text_secondary;
        DrawTextSimple(items[i].first, x + 10.0f, curY, w - 20.0f, itemH, textColor, 11.0f, false);
        curY += itemH;
    }
}

} // namespace ClashWin
