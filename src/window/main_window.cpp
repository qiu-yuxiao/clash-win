#include "main_window.hpp"
#include <windowsx.h>
#include <dwmapi.h>
#include <shellscalingapi.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")

namespace ClashWin {

MainWindow::MainWindow() = default;
MainWindow::~MainWindow() {
    if (m_hwnd) {
        KillTimer(m_hwnd, IDLE_TIMER_ID);
    }
}

float MainWindow::GetDpiScale() const {
    UINT dpi = GetDpiForWindow(m_hwnd);
    return dpi > 0 ? (static_cast<float>(dpi) / 96.0f) : 1.0f;
}

bool MainWindow::Create(HINSTANCE hInstance, int nCmdShow) {
    m_hInstance = hInstance;

    // Register Window Class
    const wchar_t CLASS_NAME[] = L"ClashWin_MainWindowClass";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = MainWindow::WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // Direct2D handles drawing
    wc.style = CS_HREDRAW | CS_VREDRAW;

    RegisterClassExW(&wc);

    // Initial logical size: 300 x 400
    int initW = 320;
    int initH = 460;

    // Create frameless window with native resize and shadow
    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        L"Clash Win",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        initW, initH,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

    m_dpiScale = GetDpiScale();

    // Scale initial window size to physical pixels
    int physW = static_cast<int>(initW * m_dpiScale);
    int physH = static_cast<int>(initH * m_dpiScale);
    SetWindowPos(m_hwnd, nullptr, 0, 0, physW, physH, SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);

    // Initialize Direct2D
    m_renderer = std::make_unique<D2DRenderer>();
    if (!m_renderer->Initialize(m_hwnd)) {
        return false;
    }

    // Start idle timer
    ResetIdleTimer();

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);

    return true;
}

int MainWindow::RunMessageLoop() {
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = nullptr;

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<MainWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hwnd = hwnd;
    } else {
        pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->HandleMessage(uMsg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void MainWindow::ResetIdleTimer() {
    if (m_hwnd) {
        SetTimer(m_hwnd, IDLE_TIMER_ID, IDLE_TIMEOUT_MS, nullptr);
    }
}

LRESULT MainWindow::OnNcHitTest(POINTS pt) {
    RECT rc;
    GetWindowRect(m_hwnd, &rc);

    int border = static_cast<int>(6 * m_dpiScale);
    int corner = static_cast<int>(14 * m_dpiScale);

    int x = pt.x;
    int y = pt.y;

    // Corner checks
    if (x < rc.left + corner && y < rc.top + corner) return HTTOPLEFT;
    if (x > rc.right - corner && y < rc.top + corner) return HTTOPRIGHT;
    if (x < rc.left + corner && y > rc.bottom - corner) return HTBOTTOMLEFT;
    if (x > rc.right - corner && y > rc.bottom - corner) return HTBOTTOMRIGHT;

    // Edge checks
    if (x < rc.left + border) return HTLEFT;
    if (x > rc.right - border) return HTRIGHT;
    if (y < rc.top + border) return HTTOP;
    if (y > rc.bottom - border) return HTBOTTOM;

    // In stealth mode, titlebar is hidden, whole client is draggable
    if (m_isStealthMode) {
        return HTCAPTION;
    }

    // Titlebar area (top 30px) is draggable
    int titlebarHeight = static_cast<int>(30 * m_dpiScale);
    if (y < rc.top + titlebarHeight) {
        // Exclude right side close/min buttons (last 60px)
        if (x > rc.right - static_cast<int>(60 * m_dpiScale)) {
            return HTCLIENT;
        }
        return HTCAPTION;
    }

    return HTCLIENT;
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_NCCALCSIZE: {
        // Return 0 to remove standard window frame and caption completely
        if (wParam == TRUE) {
            return 0;
        }
        break;
    }

    case WM_NCHITTEST: {
        POINTS pt = MAKEPOINTS(lParam);
        return OnNcHitTest(pt);
    }

    case WM_SIZING: {
        // Implementation of Design Contract 2.4: Anti-downward-drift WM_SIZING hook
        RECT* pRect = reinterpret_cast<RECT*>(lParam);
        m_dpiScale = GetDpiScale();

        int minWidthPx = static_cast<int>(MIN_WIDTH * m_dpiScale + 0.5f);
        int minHeightPx = static_cast<int>(MIN_HEIGHT * m_dpiScale + 0.5f);

        int currentW = pRect->right - pRect->left;
        int currentH = pRect->bottom - pRect->top;

        // Clamp width
        if (currentW < minWidthPx) {
            if (wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT) {
                pRect->left = pRect->right - minWidthPx;
            } else {
                pRect->right = pRect->left + minWidthPx;
            }
        }

        // Clamp height
        if (currentH < minHeightPx) {
            if (wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT) {
                // Pin top to bottom - minHeight to completely eliminate downward drift
                pRect->top = pRect->bottom - minHeightPx;
            } else {
                pRect->bottom = pRect->top + minHeightPx;
            }
        }
        return TRUE;
    }

    case WM_SIZE: {
        UINT w = LOWORD(lParam);
        UINT h = HIWORD(lParam);
        OnResize(w, h);
        return 0;
    }

    case WM_TIMER: {
        if (wParam == IDLE_TIMER_ID) {
            // Check if window is in minimal mode (width <= 285 logical, height <= 126 logical)
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            int logicalW = static_cast<int>((rc.right - rc.left) / m_dpiScale);
            int logicalH = static_cast<int>((rc.bottom - rc.top) / m_dpiScale);

            if (logicalW <= 285 && logicalH <= MIN_HEIGHT) {
                if (!m_isStealthMode) {
                    m_isStealthMode = true;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            }
            // Always re-arm timer (Contract 2.6)
            ResetIdleTimer();
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_KEYDOWN: {
        // User interaction cancels stealth mode
        ResetIdleTimer();
        if (m_isStealthMode) {
            m_isStealthMode = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        break;
    }

    case WM_DPICHANGED: {
        m_dpiScale = GetDpiScale();
        RECT* pNewRect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(m_hwnd, nullptr,
            pNewRect->left, pNewRect->top,
            pNewRect->right - pNewRect->left,
            pNewRect->bottom - pNewRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_PAINT: {
        OnPaint();
        ValidateRect(m_hwnd, nullptr);
        return 0;
    }

    case WM_DESTROY: {
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcW(m_hwnd, uMsg, wParam, lParam);
}

void MainWindow::OnResize(UINT width, UINT height) {
    if (m_renderer) {
        m_renderer->Resize(width, height);
    }
    ResetIdleTimer();

    // If user expanded window beyond minimal height, exit stealth mode immediately
    int logicalH = static_cast<int>(height / m_dpiScale);
    if (logicalH > MIN_HEIGHT && m_isStealthMode) {
        m_isStealthMode = false;
    }

    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::OnPaint() {
    if (!m_renderer) return;

    m_renderer->BeginDraw();

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    float width = static_cast<float>(rc.right - rc.left);
    float height = static_cast<float>(rc.bottom - rc.top);

    // Deep Dark Skeuomorphic Background
    m_renderer->Clear(D2D1::ColorF(0.08f, 0.08f, 0.09f, 1.0f));

    float yOffset = 0.0f;

    // Titlebar (30px logical, only when NOT stealth mode)
    if (!m_isStealthMode) {
        float titleH = 30.0f * m_dpiScale;
        // Titlebar subtle background
        m_renderer->Draw3DCard(0, 0, width, titleH, D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f));
        m_renderer->DrawTextSimple(L"Clash Win", 12.0f * m_dpiScale, 0, 120.0f * m_dpiScale, titleH, D2D1::ColorF(0.85f, 0.85f, 0.88f, 1.0f), 12.0f, true);

        // Window controls placeholder
        m_renderer->DrawTextSimple(L"—  ✕", width - 50.0f * m_dpiScale, 0, 40.0f * m_dpiScale, titleH, D2D1::ColorF(0.6f, 0.6f, 0.6f, 1.0f), 12.0f, false);
        yOffset = titleH;
    }

    // Active Node Card (30px logical)
    float nodeCardH = 32.0f * m_dpiScale;
    float marginX = 8.0f * m_dpiScale;
    float cardW = width - marginX * 2;
    m_renderer->Draw3DCard(marginX, yOffset + 4.0f * m_dpiScale, cardW, nodeCardH, D2D1::ColorF(0.15f, 0.15f, 0.18f, 1.0f));

    // Active Node Text
    m_renderer->DrawTextSimple(L"⚡ 香港 01 [BGP 专线] · 42ms", marginX + 8.0f * m_dpiScale, yOffset + 4.0f * m_dpiScale, cardW - 80.0f * m_dpiScale, nodeCardH, D2D1::ColorF(0.95f, 0.78f, 0.25f, 1.0f), 12.0f, true);

    // Mini 3D Action Button
    float btnW = 50.0f * m_dpiScale;
    float btnH = 22.0f * m_dpiScale;
    m_renderer->Draw3DButton(width - marginX - btnW - 5.0f * m_dpiScale, yOffset + 9.0f * m_dpiScale, btnW, btnH, L"切换");

    // Status / Traffic Panel Area
    float trafficY = yOffset + nodeCardH + 10.0f * m_dpiScale;
    if (trafficY + 30.0f < height) {
        m_renderer->DrawTextSimple(L"↑ 12.4 KB/s   ↓ 284.1 KB/s", marginX, trafficY, cardW, 20.0f * m_dpiScale, D2D1::ColorF(0.5f, 0.8f, 0.5f, 1.0f), 11.0f, false);
    }

    m_renderer->EndDraw();
}

} // namespace ClashWin
