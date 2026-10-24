#include <windows.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include "window/main_window.hpp"
#include "clashwin/system/singleton.h"

#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    // Per-Monitor V2 高 DPI 自动缩放
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 单实例互斥与端口 33335 唤醒服务
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    clashwin::system::SingleInstanceGuard singleInstance;

    if (!singleInstance.AcquireOrWakeExisting(argc, argv)) {
        // 唤醒已有实例后静默退出
        if (argv) LocalFree(argv);
        return 0;
    }
    if (argv) LocalFree(argv);

    // 初始化 COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;

    int exitCode = 0;
    {
        ClashWin::MainWindow app;
        if (app.Create(hInstance, nCmdShow)) {
            // 启动主实例微型 HTTP 服务以接收后续实例的唤醒与 URL 订阅请求
            singleInstance.StartServer(
                [&app]() {
                    app.OnWakeupVisible();
                },
                [&app](std::string_view scheme_param) {
                    app.OnWakeupVisible();
                }
            );

            exitCode = app.RunMessageLoop();
        } else {
            exitCode = 1;
        }
    }

    CoUninitialize();
    return exitCode;
}
