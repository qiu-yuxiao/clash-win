#include <windows.h>
#include <shellscalingapi.h>
#include "window/main_window.hpp"

#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "comctl32.lib")

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPWSTR /*lpCmdLine*/,
    _In_ int nCmdShow)
{
    // Enable Per-Monitor V2 DPI awareness for pixel-perfect high DPI rendering
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Single-instance mutex
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"ClashWin_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existingWnd = FindWindowW(L"ClashWin_MainWindowClass", nullptr);
        if (existingWnd) {
            ShowWindow(existingWnd, SW_RESTORE);
            SetForegroundWindow(existingWnd);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // Initialize COM for Direct2D and shell
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;

    int exitCode = 0;
    {
        ClashWin::MainWindow app;
        if (app.Create(hInstance, nCmdShow)) {
            exitCode = app.RunMessageLoop();
        } else {
            exitCode = 1;
        }
    }

    CoUninitialize();

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return exitCode;
}
