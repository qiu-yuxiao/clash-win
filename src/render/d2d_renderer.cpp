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

void D2DRenderer::DrawProxyItem3Col(float x, float y, float w, float h,
                                    const std::wstring& type, const std::wstring& name,
                                    int delay_ms, bool isSelected, bool isOdd, bool isHot) {
    if (!m_renderTarget) return;

    const auto& theme = ThemeManager::Instance().GetColors();

    // 1. 背景色与斑马纹 (Zebra Striping)
    D2D1_COLOR_F rowBg = theme.window_bg;
    if (isSelected) {
        rowBg = theme.card_active_bg;
    } else if (isHot) {
        rowBg = theme.card_bg;
    } else if (isOdd) {
        rowBg.r = std::clamp(rowBg.r * 1.04f, 0.0f, 1.0f);
        rowBg.g = std::clamp(rowBg.g * 1.04f, 0.0f, 1.0f);
        rowBg.b = std::clamp(rowBg.b * 1.04f, 0.0f, 1.0f);
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
    DrawTextSimple(type, x + 4.0f, y, typeColW - 8.0f, h, theme.text_secondary, 10.5f, false, DWRITE_TEXT_ALIGNMENT_TRAILING);

    // 第一列与第二列之间的 2px 竖向分割线
    m_solidBrush->SetColor(theme.card_shadow);
    m_renderTarget->DrawLine(D2D1::Point2F(x + typeColW, y + 2.0f),
                             D2D1::Point2F(x + typeColW, y + h - 2.0f),
                             m_solidBrush.Get(), 1.0f);

    // 4. 第二列：节点名称 (flex 1，左对齐，单行截断)
    D2D1_COLOR_F nameColor = isSelected ? theme.accent : theme.text_primary;
    DrawTextSimple(name, x + typeColW + 6.0f, y, nameColW - 12.0f, h, nameColor, 11.5f, isSelected, DWRITE_TEXT_ALIGNMENT_LEADING);

    // 第二列与第三列之间的 2px 竖向分割线
    m_renderTarget->DrawLine(D2D1::Point2F(x + typeColW + nameColW, y + 2.0f),
                             D2D1::Point2F(x + typeColW + nameColW, y + h - 2.0f),
                             m_solidBrush.Get(), 1.0f);

    // 5. 第三列：延迟数值 (65px，居中，带有主题速度色彩)
    if (delay_ms >= 0) {
        D2D1_COLOR_F delayColor = (delay_ms <= 100) ? theme.badge_fast : ((delay_ms <= 300) ? theme.badge_medium : theme.badge_slow);
        std::wstring dText = std::to_wstring(delay_ms) + L"ms";
        DrawTextSimple(dText, x + typeColW + nameColW, y, delayColW, h, delayColor, 10.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else if (delay_ms == -1) {
        DrawTextSimple(L"TIMEOUT", x + typeColW + nameColW, y, delayColW, h, theme.badge_slow, 9.5f, false, DWRITE_TEXT_ALIGNMENT_CENTER);
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

    // 1. 协议徽章 (窄窗 <= 260px 下隐藏，对齐 Clash Mini: max-width 480px 下 display: none)
    if (w > 260.0f && !protocol.empty()) {
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

    // 2. 节点名称 (可点击轮换下一个节点，带悬浮光)
    float nameW = w - (curX - x) - delayChipW - 6.0f;
    if (w > 320.0f && !addr.empty()) {
        float addrW = 85.0f;
        DrawTextSimple(name, curX, y, nameW - addrW, h, theme.text_primary, 11.5f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawTextSimple(L"(" + addr + L")", curX + nameW - addrW, y, addrW, h, theme.text_secondary, 9.5f, false, DWRITE_TEXT_ALIGNMENT_LEADING);
    } else {
        (void)addr;
        DrawTextSimple(name, curX, y, nameW, h, theme.text_primary, 11.5f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
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
    } else {
        D2D1_COLOR_F dClr = (delay_ms <= 100 && delay_ms > 0) ? theme.badge_fast : ((delay_ms <= 300 && delay_ms > 0) ? theme.badge_medium : theme.badge_slow);
        m_solidBrush->SetColor(D2D1::ColorF(dClr.r, dClr.g, dClr.b, 0.15f));
        m_renderTarget->FillRoundedRectangle(chipRect, m_solidBrush.Get());
        m_solidBrush->SetColor(dClr);
        m_renderTarget->DrawRoundedRectangle(chipRect, m_solidBrush.Get(), 1.0f);
        std::wstring dStr = (delay_ms > 0) ? (std::to_wstring(delay_ms) + L"ms") : L"TEST";
        DrawTextSimple(dStr, chipX, chipY, delayChipW, chipH, dClr, 9.5f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void D2DRenderer::DrawProxyHead(float x, float y, float w, float h,
                                int sortType, const std::wstring& searchText,
                                bool isTesting, bool searchFocused) {
    if (!m_renderTarget) return;

    const auto& theme = ThemeManager::Instance().GetColors();

    // 1. ⚡ 一键测速按钮 (24x24)
    float btnSize = h;
    D2D1_COLOR_F boltColor = isTesting ? theme.badge_medium : theme.text_primary;
    Draw3DButton(x, y, btnSize, btnSize, L"⚡", false, false, isTesting);

    // 2. ↕ 排序切换按钮 (24x24)
    std::wstring sortLabel = L"↕";
    if (sortType == 1) sortLabel = L"⏱";
    else if (sortType == 2) sortLabel = L"A-Z";
    Draw3DButton(x + btnSize + 4.0f, y, btnSize, btnSize, sortLabel, false, false, (sortType != 0));

    // 3. 🔍 实时输入搜索框 (Flex 1)
    float searchX = x + (btnSize + 4.0f) * 2;
    float searchW = w - searchX + x;
    D2D1_ROUNDED_RECT sRect = D2D1::RoundedRect(D2D1::RectF(searchX, y, searchX + searchW, y + h), 3.0f, 3.0f);

    m_solidBrush->SetColor(theme.card_bg);
    m_renderTarget->FillRoundedRectangle(sRect, m_solidBrush.Get());

    m_solidBrush->SetColor(searchFocused ? theme.accent : theme.card_shadow);
    m_renderTarget->DrawRoundedRectangle(sRect, m_solidBrush.Get(), searchFocused ? 1.5f : 1.0f);

    // 放大镜图标
    DrawTextSimple(L"🔍", searchX + 4.0f, y, 16.0f, h, theme.text_secondary, 9.5f, false, DWRITE_TEXT_ALIGNMENT_CENTER);

    // 输入内容或占位符
    if (searchText.empty()) {
        DrawTextSimple(L"过滤节点...", searchX + 22.0f, y, searchW - 26.0f, h, theme.text_secondary, 10.5f, false);
    } else {
        DrawTextSimple(searchText, searchX + 22.0f, y, searchW - 26.0f, h, theme.text_primary, 10.5f, false);
    }
}

void D2DRenderer::DrawTrafficBottomBar(float x, float y, float w, float h,
                                       uint64_t upSpeed, uint64_t downSpeed,
                                       uint64_t upTotal, uint64_t downTotal) {
    if (!m_renderTarget) return;

    const auto& theme = ThemeManager::Instance().GetColors();

    // 顶部分割线
    m_solidBrush->SetColor(theme.card_shadow);
    m_renderTarget->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x + w, y), m_solidBrush.Get(), 1.0f);

    // 格式化实时速率与总流量
    auto FormatSpeed = [](uint64_t bytesPerSec) -> std::wstring {
        if (bytesPerSec < 1024) return std::to_wstring(bytesPerSec) + L" B/s";
        if (bytesPerSec < 1024 * 1024) return std::to_wstring(bytesPerSec / 1024) + L" KB/s";
        std::wostringstream oss;
        oss << std::fixed << std::setprecision(1) << (static_cast<double>(bytesPerSec) / (1024.0 * 1024.0)) << L" MB/s";
        return oss.str();
    };

    auto FormatTotal = [](uint64_t totalBytes) -> std::wstring {
        if (totalBytes < 1024 * 1024) return std::to_wstring(totalBytes / 1024) + L" K";
        if (totalBytes < 1024 * 1024 * 1024) return std::to_wstring(totalBytes / (1024 * 1024)) + L" M";
        std::wostringstream oss;
        oss << std::fixed << std::setprecision(1) << (static_cast<double>(totalBytes) / (1024.0 * 1024.0 * 1024.0)) << L" G";
        return oss.str();
    };

    std::wstring downText = L"↓ " + FormatSpeed(downSpeed);
    std::wstring upText = L"↑ " + FormatSpeed(upSpeed);
    std::wstring totalText = FormatTotal(downTotal) + L" / " + FormatTotal(upTotal);

    float partW = w / 3.0f;
    DrawTextSimple(downText, x + 6.0f, y + 1.0f, partW, h - 2.0f, theme.badge_fast, 10.0f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
    DrawTextSimple(upText, x + partW, y + 1.0f, partW, h - 2.0f, theme.accent, 10.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    DrawTextSimple(totalText, x + partW * 2, y + 1.0f, partW - 6.0f, h - 2.0f, theme.text_secondary, 9.5f, false, DWRITE_TEXT_ALIGNMENT_TRAILING);
}

} // namespace ClashWin
