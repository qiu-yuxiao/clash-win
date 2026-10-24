#pragma once

#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <memory>
#include <functional>
#include "clashwin/common/types.h"
#include "clashwin/common/constants.h"

namespace clashwin::ui {

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    // 创建并注册主窗口
    bool Create(HINSTANCE hInstance, int nCmdShow);

    // 运行 Win32 消息循环
    int RunMessageLoop();

    // 唤醒并呈现主窗口到前台
    void ShowAndActivate();

    // 隐藏窗口（进入轻量常驻模式）
    void HideToTray();

    [[nodiscard]] HWND GetHwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // D2D 资源管理
    HRESULT CreateDeviceIndependentResources();
    HRESULT CreateDeviceResources();
    void DiscardDeviceResources();

    // 绘制与布局
    void OnPaint();
    void OnResize(UINT width, UINT height);
    LRESULT OnNcHitTest(POINTS pt);
    LRESULT OnSizing(UINT edge, RECT* pRect);

    // 交互与空闲定时器
    void ResetIdleTimer();
    void OnIdleTimeout();

    HWND hwnd_{NULL};
    HINSTANCE hinstance_{NULL};
    UINT dpi_{96};
    double dpi_scale_{1.0};

    // 视口与尺寸状态
    ViewportMode viewport_mode_{ViewportMode::NarrowStandard};
    bool is_stealth_mode_{false};
    bool is_dragging_resize_{false};

    // Direct2D / DirectWrite 接口
    ID2D1Factory1* d2d_factory_{nullptr};
    ID2D1HwndRenderTarget* render_target_{nullptr};
    IDWriteFactory* dwrite_factory_{nullptr};
};

} // namespace clashwin::ui
