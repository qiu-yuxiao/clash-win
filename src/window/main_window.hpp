#pragma once

#include <windows.h>
#include <memory>
#include "render/d2d_renderer.hpp"

namespace ClashWin {

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool Create(HINSTANCE hInstance, int nCmdShow);
    int RunMessageLoop();

    HWND GetHwnd() const { return m_hwnd; }

    // Design Contract 2.3 Constants
    static constexpr int MIN_WIDTH = 271;       // Logical pixels
    static constexpr int MIN_HEIGHT = 126;      // Logical pixels (126 * 1.5 = 189 exact physical px)
    static constexpr UINT_PTR IDLE_TIMER_ID = 1001;
    static constexpr UINT IDLE_TIMEOUT_MS = 10000; // 10s auto-hide

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnPaint();
    void OnResize(UINT width, UINT height);
    LRESULT OnNcHitTest(POINTS pt);
    void ResetIdleTimer();
    float GetDpiScale() const;

    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    std::unique_ptr<D2DRenderer> m_renderer;

    // Window states
    bool m_isStealthMode = false;
    bool m_isTrackingMouse = false;
    float m_dpiScale = 1.0f;
};

} // namespace ClashWin
