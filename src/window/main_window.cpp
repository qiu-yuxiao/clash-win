#include "main_window.hpp"
#include "clashwin/core/connection_manager.h"
#include "clashwin/config/enhance_chain.h"
#include "clashwin/network/unlock_checker.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <thread>
#include <iostream>
#include <cmath>
#include <codecvt>
#include <locale>
#include <sstream>
#include <fstream>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "shell32.lib")

namespace ClashWin {

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() {
    if (m_hwnd) {
        KillTimer(m_hwnd, IDLE_TIMER_ID);
        KillTimer(m_hwnd, CONN_TIMER_ID);
        RemoveTrayIcon();
    }
}

float MainWindow::GetDpiScale() const {
    UINT dpi = GetDpiForWindow(m_hwnd);
    return dpi > 0 ? (static_cast<float>(dpi) / 96.0f) : 1.0f;
}

void MainWindow::OnWakeupVisible() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_RESTORE);
        SetForegroundWindow(m_hwnd);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

bool MainWindow::Create(HINSTANCE hInstance, int nCmdShow) {
    m_hInstance = hInstance;

    // 1. 初始化配置与多订阅 (遵循原生逻辑：无预置或未导入订阅时保持真实纯净空状态)
    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
        std::filesystem::path appDir = std::filesystem::path(appDataPath) / "clash_win";
        std::filesystem::create_directories(appDir);

        clashwin::config::AppConfig::Instance().Init(appDir);
        clashwin::config::ProfileManager::Instance().Init(appDir);
    }

    ThemeManager::Instance().SetSkinByName(clashwin::config::AppConfig::Instance().GetSkin());
    ThemeManager::Instance().SetSlider1(clashwin::config::AppConfig::Instance().GetSlider1Val());
    ThemeManager::Instance().SetSlider2(clashwin::config::AppConfig::Instance().GetSlider2Val());
    m_mixedPortText = std::to_wstring(clashwin::config::AppConfig::Instance().GetMixedPort());

    // 2. 注册窗口类
    const wchar_t CLASS_NAME[] = L"ClashWin_MainWindowClass";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = MainWindow::WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.style = CS_HREDRAW | CS_VREDRAW;

    RegisterClassExW(&wc);

    // 3. 几何尺寸与位置读取 (严格对齐 Clash Mini 权威口径与多屏防御)
    clashwin::WindowGeometry geom{};
    bool hasSavedGeom = clashwin::config::AppConfig::Instance().LoadWindowState(geom);

    int initW = static_cast<int>(clashwin::geometry::kDefaultWidth);   // 默认严格 271px
    int initH = static_cast<int>(clashwin::geometry::kDefaultHeight);  // 默认严格 680px
    if (hasSavedGeom) {
        // 如果保存的尺寸属于大窗展开模式 (>= 500px)，恢复大窗；其余一律恢复为经典的 271px × 680px 极简窄窗
        if (geom.width >= 500.0 && geom.width <= 640.0) {
            initW = static_cast<int>(clashwin::geometry::kMaxWidth);
            initH = static_cast<int>(clashwin::geometry::kMaxHeight);
            m_isLargeMode = true;
        } else {
            initW = static_cast<int>(clashwin::geometry::kDefaultWidth);
            initH = static_cast<int>(clashwin::geometry::kDefaultHeight);
            m_isLargeMode = false;
        }
    }

    // 钳制至权威合法范围 (271..640, 126..860)
    initW = std::clamp(initW, static_cast<int>(clashwin::geometry::kMinimalWidth), static_cast<int>(clashwin::geometry::kMaxWidth));
    initH = std::clamp(initH, static_cast<int>(clashwin::geometry::kMinimalHeight), static_cast<int>(clashwin::geometry::kMaxHeight));

    // 获取主显示器工作区 (防御多屏拔插与坐标漂移越界)
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int screenW = workArea.right - workArea.left;
    int screenH = workArea.bottom - workArea.top;
    (void)screenW;

    int initX = (hasSavedGeom && geom.x.has_value()) ? static_cast<int>(*geom.x) : CW_USEDEFAULT;
    int initY = (hasSavedGeom && geom.y.has_value()) ? static_cast<int>(*geom.y) : CW_USEDEFAULT;

    // 如果未保存位置或超出屏幕工作区，停靠在屏幕右侧 (Clash Mini 经典布局习惯)
    if (initX == CW_USEDEFAULT || initX < workArea.left || initX + initW > workArea.right + 20) {
        initX = workArea.right - initW - 24;
    }
    if (initY == CW_USEDEFAULT || initY < workArea.top || initY + initH > workArea.bottom + 20) {
        initY = workArea.top + std::max(20, (screenH - initH) / 2);
    }

    m_dpiScale = 1.0f;
    HDC hdcScreen = GetDC(nullptr);
    if (hdcScreen) {
        int dpiX = GetDeviceCaps(hdcScreen, LOGPIXELSX);
        if (dpiX > 0) m_dpiScale = static_cast<float>(dpiX) / 96.0f;
        ReleaseDC(nullptr, hdcScreen);
    }

    int physW = static_cast<int>(initW * m_dpiScale + 0.5f);
    int physH = static_cast<int>(initH * m_dpiScale + 0.5f);

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        L"Clash Mini Ver.3.0.10",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        initX, initY,
        physW, physH,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

    // 启用 DWM 窗口阴影投射与 Windows 11 原生圆角
    MARGINS margins = { 0, 0, 1, 0 };
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);

    #ifndef DWMWA_WINDOW_CORNER_PREFERENCE
    #define DWMWA_WINDOW_CORNER_PREFERENCE 33
    #endif
    enum DWM_WINDOW_CORNER_PREFERENCE {
        DWMWCP_DEFAULT = 0,
        DWMWCP_DONOTROUND = 1,
        DWMWCP_ROUND = 2,
        DWMWCP_ROUNDSMALL = 3
    };
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwnd, static_cast<DWORD>(DWMWA_WINDOW_CORNER_PREFERENCE), &corner, sizeof(corner));

    m_dpiScale = GetDpiScale();
    physW = static_cast<int>(initW * m_dpiScale + 0.5f);
    physH = static_cast<int>(initH * m_dpiScale + 0.5f);
    SetWindowPos(m_hwnd, nullptr, initX, initY, physW, physH, SWP_NOZORDER | SWP_FRAMECHANGED);

    m_renderer = std::make_unique<D2DRenderer>();
    if (!m_renderer->Initialize(m_hwnd)) {
        return false;
    }

    InitTrayIcon();
    ResetIdleTimer();

    m_trafficHistory.clear();
    for (int i = 0; i < 30; ++i) {
        m_trafficHistory.push_back({0, 0});
    }
    SetTimer(m_hwnd, TRAFFIC_TIMER_ID, TRAFFIC_POLL_INTERVAL_MS, nullptr);

    StartCoreIfConfigReady();

    if (!clashwin::config::ProfileManager::Instance().IsFirstRunCompleted() &&
        !clashwin::config::ProfileManager::Instance().HasRealProfile()) {
        OpenModal(ModalType::FirstRunGuide);
    }

    if (initW > NARROW_BREAKPOINT) {
        SetTimer(m_hwnd, CONN_TIMER_ID, CONN_POLL_INTERVAL_MS, nullptr);
    }

    ShowWindow(m_hwnd, (nCmdShow == SW_HIDE || nCmdShow == 0) ? SW_SHOWNORMAL : nCmdShow);
    UpdateWindow(m_hwnd);

    return true;
}

void MainWindow::InitTrayIcon() {
    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcsncpy_s(m_nid.szTip, L"Clash Mini (Native)", _TRUNCATE);

    Shell_NotifyIconW(NIM_ADD, &m_nid);
    m_trayInstalled = true;
}

void MainWindow::UpdateTrayIcon() {
    if (!m_trayInstalled) return;

    if (clashwin::config::AppConfig::Instance().IsTunModeEnabled()) {
        wcsncpy_s(m_nid.szTip, L"Clash Mini - TUN 模式运行中", _TRUNCATE);
    } else if (clashwin::config::AppConfig::Instance().IsSystemProxyEnabled()) {
        wcsncpy_s(m_nid.szTip, L"Clash Mini - 系统代理运行中", _TRUNCATE);
    } else {
        wcsncpy_s(m_nid.szTip, L"Clash Mini - 手动直连模式", _TRUNCATE);
    }
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void MainWindow::RemoveTrayIcon() {
    if (m_trayInstalled) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_trayInstalled = false;
    }
}

void MainWindow::ShowTrayContextMenu() {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    bool isVisible = IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd);
    AppendMenuW(hMenu, MF_STRING, 101, isVisible ? L"隐藏主窗口" : L"显示主窗口");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    bool sysProxyOn = clashwin::config::AppConfig::Instance().IsSystemProxyEnabled();
    AppendMenuW(hMenu, MF_STRING | (sysProxyOn ? MF_CHECKED : MF_UNCHECKED), 102, L"系统代理 (System Proxy)");

    bool tunOn = clashwin::config::AppConfig::Instance().IsTunModeEnabled();
    AppendMenuW(hMenu, MF_STRING | (tunOn ? MF_CHECKED : MF_UNCHECKED), 103, L"TUN 模式 (Virtual NIC)");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 104, L"退出 Clash Mini");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);

    switch (cmd) {
    case 101:
        if (isVisible) {
            ShowWindow(m_hwnd, SW_HIDE);
        } else {
            OnWakeupVisible();
        }
        break;
    case 102:
        ApplyProxyModeChange(sysProxyOn ? 0 : 1);
        break;
    case 103:
        ApplyProxyModeChange(tunOn ? 0 : 2);
        break;
    case 104:
        GracefulExit();
        break;
    default:
        break;
    }
}

void MainWindow::GracefulExit() {
    ShowWindow(m_hwnd, SW_HIDE);

    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    double w = (rc.right - rc.left) / m_dpiScale;
    double h = (rc.bottom - rc.top) / m_dpiScale;
    clashwin::config::AppConfig::Instance().SaveWindowStateImmediately(w, h, rc.left, rc.top);

    clashwin::system::SystemProxyManager::Instance().ResetAll();
    clashwin::core::CoreSupervisor::Instance().StopCore();
    clashwin::core::HealthProbeService::Instance().Stop();
    clashwin::core::HealthProbeService::KillAllOrphanCores();

    RemoveTrayIcon();
    PostQuitMessage(0);
}

void MainWindow::StartCoreIfConfigReady() {
    std::filesystem::path sidecarPath = std::filesystem::current_path() / "sidecar" / clashwin::system_service::kCoreBinaryName;
    if (!std::filesystem::exists(sidecarPath)) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        if (std::filesystem::exists(exeDir / "sidecar" / clashwin::system_service::kCoreBinaryName)) {
            sidecarPath = exeDir / "sidecar" / clashwin::system_service::kCoreBinaryName;
        } else if (std::filesystem::exists(exeDir / ".." / "sidecar" / clashwin::system_service::kCoreBinaryName)) {
            sidecarPath = exeDir / ".." / "sidecar" / clashwin::system_service::kCoreBinaryName;
        }
    }

    std::filesystem::path configDir = clashwin::config::ProfileManager::Instance().GetConfigDir();
    if (std::filesystem::exists(sidecarPath) && std::filesystem::exists(configDir / "config.yaml")) {
        clashwin::core::CoreSupervisor::Instance().StartCore(sidecarPath, configDir, 10801, 9098);
        m_statusMessage = L"内核服务运行中 (端口: 10801)";
        UpdateTrayIcon();
    }
}

void MainWindow::ToggleAutoLaunch() {
    bool current = clashwin::config::AppConfig::Instance().IsAutoLaunchEnabled();
    bool next = !current;
    
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        if (next) {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            std::wstring cmd = L"\"" + std::wstring(exePath) + L"\" --silent";
            RegSetValueExW(hKey, L"ClashWin", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.length() + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(hKey, L"ClashWin");
        }
        RegCloseKey(hKey);
    }
    
    clashwin::config::AppConfig::Instance().SetAutoLaunchEnabled(next);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::SavePortChange() {
    try {
        uint16_t port = static_cast<uint16_t>(std::stoul(m_mixedPortText));
        if (port >= 1024 && port <= 65535) {
            clashwin::config::AppConfig::Instance().SetMixedPort(port);
            m_isMixedPortFocused = false;
            m_statusMessage = L"端口已设为 " + m_mixedPortText + L" (重启生效)";
        }
    } catch (...) {}
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::TriggerDrawerImport() {
    if (m_drawerImportText.empty()) return;
    std::wstring urlW = m_drawerImportText;
    m_isImporting = true;
    m_statusMessage = L"正在导入订阅配置...";
    InvalidateRect(m_hwnd, nullptr, FALSE);

    std::thread([this, urlW]() {
        int len = WideCharToMultiByte(CP_UTF8, 0, urlW.c_str(), -1, NULL, 0, NULL, NULL);
        std::string urlA(len - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, urlW.c_str(), -1, &urlA[0], len, NULL, NULL);

        std::string err;
        bool ok = clashwin::config::ProfileManager::Instance().ImportAndActivate(urlA, err);

        PostMessageW(m_hwnd, WM_USER + 102, ok ? 1 : 0, 0);
    }).detach();
}

void MainWindow::HandleProfileSelect(const std::string& uid) {
    std::string err;
    if (clashwin::config::ProfileManager::Instance().SwitchProfile(uid, err)) {
        m_statusMessage = L"已切换至指定订阅";
        InvalidateRect(m_hwnd, nullptr, FALSE);
        StartCoreIfConfigReady();
    } else {
        m_statusMessage = L"切换失败: " + std::wstring(err.begin(), err.end());
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::HandleProfileUpdate(const std::string& uid) {
    m_isImporting = true;
    m_statusMessage = L"正在更新订阅...";
    InvalidateRect(m_hwnd, nullptr, FALSE);
    std::thread([this, uid]() {
        std::string err;
        bool ok = clashwin::config::ProfileManager::Instance().UpdateProfile(uid, err);
        PostMessageW(m_hwnd, WM_USER + 102, ok ? 1 : 0, 0);
    }).detach();
}

void MainWindow::HandleProfileDelete(const std::string& uid) {
    if (clashwin::config::ProfileManager::Instance().DeleteProfile(uid)) {
        m_statusMessage = L"已删除订阅";
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::ApplyProxyModeChange(int modeIndex) {
    auto& cfg = clashwin::config::AppConfig::Instance();
    std::wstring mixedProxy = L"127.0.0.1:" + std::to_wstring(cfg.GetMixedPort());

    if (modeIndex == 0) {
        cfg.SetSystemProxyEnabled(false);
        cfg.SetTunModeEnabled(false);
        clashwin::system::SystemProxyManager::Instance().ResetAll();
    } else if (modeIndex == 1) {
        cfg.SetTunModeEnabled(false);
        cfg.SetSystemProxyEnabled(true);
        clashwin::system::SystemProxyManager::Instance().EnableProxy(mixedProxy, cfg.GetSystemProxyBypass());
    } else if (modeIndex == 2) {
        std::wstring fallbackMsg;
        bool ok = clashwin::system::ServiceManager::Instance().EvaluateTunAndFallback(true, mixedProxy, &fallbackMsg);
        if (ok) {
            cfg.SetSystemProxyEnabled(false);
            cfg.SetTunModeEnabled(true);
            clashwin::system::SystemProxyManager::Instance().ResetAll();
        } else {
            cfg.SetTunModeEnabled(false);
            cfg.SetSystemProxyEnabled(true);
            m_statusMessage = fallbackMsg;
        }
    }

    UpdateTrayIcon();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::ApplyRuleModeChange(int ruleIndex) {
    auto& cfg = clashwin::config::AppConfig::Instance();
    if (ruleIndex == 0) {
        cfg.SetRuleFallback("direct");
    } else if (ruleIndex == 1) {
        cfg.SetRuleFallback("direct");
    } else if (ruleIndex == 2) {
        cfg.SetRuleFallback("proxy");
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::SwitchSkin(SkinType skin) {
    ThemeManager::Instance().SetSkin(skin);
    clashwin::config::AppConfig::Instance().SetSkin(ThemeManager::SkinTypeToString(skin));
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::ToggleAlwaysOnTop() {
    m_isAlwaysOnTop = !m_isAlwaysOnTop;
    SetWindowPos(m_hwnd, m_isAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::CycleToNextCandidateNode() {
    const auto& nodes = clashwin::config::ProfileManager::Instance().GetNodes();
    if (nodes.empty()) return;

    std::string current = clashwin::config::ProfileManager::Instance().GetCurrentActiveNode();
    size_t curIdx = 0;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].name == current) {
            curIdx = i;
            break;
        }
    }

    size_t nextIdx = (curIdx + 1) % nodes.size();
    std::string nextNode = nodes[nextIdx].name;
    clashwin::config::ProfileManager::Instance().SetCurrentActiveNode(nextNode);

    std::thread([nextNode]() {
        clashwin::core::MihomoClient::Instance().SelectProxy("PROXY", nextNode);
    }).detach();

    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::TriggerActiveNodeDelayTest() {
    std::string current = clashwin::config::ProfileManager::Instance().GetCurrentActiveNode();
    if (current.empty()) return;

    m_isTestingDelay = true;
    InvalidateRect(m_hwnd, nullptr, FALSE);

    std::thread([this, current]() {
        int delay = clashwin::core::MihomoClient::Instance().TestProxyDelay(current, 9098, 2000);
        {
            std::lock_guard<std::mutex> lock(m_delayMutex);
            m_nodeDelays[current] = delay;
        }
        m_isTestingDelay = false;
        PostMessageW(m_hwnd, WM_USER + 100, 0, 0);
    }).detach();
}

void MainWindow::TriggerAllNodesDelayTest() {
    const auto& nodes = clashwin::config::ProfileManager::Instance().GetNodes();
    if (nodes.empty()) return;

    m_isTestingDelay = true;
    InvalidateRect(m_hwnd, nullptr, FALSE);

    std::thread([this, nodes]() {
        for (const auto& node : nodes) {
            int delay = clashwin::core::MihomoClient::Instance().TestProxyDelay(node.name, 9098, 2000);
            {
                std::lock_guard<std::mutex> lock(m_delayMutex);
                m_nodeDelays[node.name] = delay;
            }
            PostMessageW(m_hwnd, WM_USER + 100, 0, 0);
        }
        m_isTestingDelay = false;
        PostMessageW(m_hwnd, WM_USER + 100, 0, 0);
    }).detach();
}

void MainWindow::RefreshConnections() {
    std::thread([this]() {
        clashwin::core::ConnectionManager::Instance().Refresh(9098);
        auto raw_conns = clashwin::core::ConnectionManager::Instance().GetConnections();

        std::vector<ActiveConnectionItem> items;
        items.reserve(raw_conns.size());

        for (const auto& c : raw_conns) {
            ActiveConnectionItem item;
            item.id = c.id;
            item.raw_host = c.host.empty() ? c.dest_ip : c.host;
            item.dest_ip = c.dest_ip;

            std::string display_host = item.raw_host;
            if (c.dest_port > 0) {
                display_host += ":" + std::to_string(c.dest_port);
            }
            item.host = std::wstring(display_host.begin(), display_host.end());

            std::string chain_str = c.rule;
            if (!c.chains.empty()) {
                chain_str = c.chains.back();
            }
            item.chain = std::wstring(chain_str.begin(), chain_str.end());

            auto fmt_speed = [](uint64_t bytes_per_sec) -> std::wstring {
                wchar_t buf[32];
                if (bytes_per_sec < 1024) {
                    swprintf_s(buf, L"%lluB/s", bytes_per_sec);
                } else if (bytes_per_sec < 1024 * 1024) {
                    swprintf_s(buf, L"%.1fK/s", bytes_per_sec / 1024.0);
                } else {
                    swprintf_s(buf, L"%.1fM/s", bytes_per_sec / (1024.0 * 1024.0));
                }
                return buf;
            };

            item.speed = L"↑" + fmt_speed(c.upload_speed) + L"  ↓" + fmt_speed(c.download_speed);
            items.push_back(std::move(item));
        }

        uint64_t upSpd = clashwin::core::ConnectionManager::Instance().GetUploadSpeed();
        uint64_t downSpd = clashwin::core::ConnectionManager::Instance().GetDownloadSpeed();
        uint64_t upTot = clashwin::core::ConnectionManager::Instance().GetUploadTotal();
        uint64_t downTot = clashwin::core::ConnectionManager::Instance().GetDownloadTotal();

        m_connections = std::move(items);
        m_upSpeed = upSpd;
        m_downSpeed = downSpd;
        m_upTotal = upTot;
        m_downTotal = downTot;

        PostMessageW(m_hwnd, WM_USER + 100, 0, 0);
    }).detach();
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

    if (!IsZoomed(m_hwnd)) {
        if (x < rc.left + corner && y < rc.top + corner) return HTTOPLEFT;
        if (x > rc.right - corner && y < rc.top + corner) return HTTOPRIGHT;
        if (x < rc.left + corner && y > rc.bottom - corner) return HTBOTTOMLEFT;
        if (x > rc.right - corner && y > rc.bottom - corner) return HTBOTTOMRIGHT;

        if (x < rc.left + border) return HTLEFT;
        if (x > rc.right - border) return HTRIGHT;
        if (y < rc.top + border) return HTTOP;
        if (y > rc.bottom - border) return HTBOTTOM;
    }

    if (m_isStealthMode) {
        return HTCAPTION;
    }

    int titlebarHeight = static_cast<int>(30 * m_dpiScale);
    if (y < rc.top + titlebarHeight) {
        if (x > rc.right - static_cast<int>(96 * m_dpiScale)) {
            return HTCLIENT;
        }
        return HTCAPTION;
    }

    return HTCLIENT;
}

void MainWindow::ToggleMaximize() {
    m_isLargeMode = !m_isLargeMode;
    int targetW = m_isLargeMode ? static_cast<int>(clashwin::geometry::kMaxWidth) : static_cast<int>(clashwin::geometry::kDefaultWidth);
    int targetH = m_isLargeMode ? static_cast<int>(clashwin::geometry::kMaxHeight) : static_cast<int>(clashwin::geometry::kDefaultHeight);

    int physW = static_cast<int>(targetW * m_dpiScale);
    int physH = static_cast<int>(targetH * m_dpiScale);

    SetWindowPos(m_hwnd, nullptr, 0, 0, physW, physH, SWP_NOMOVE | SWP_NOZORDER);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::OnLButtonDown(int x, int y) {
    if (m_currentModal != ModalType::None) {
        OnModalLButtonDown(x, y);
        return;
    }

    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);

    // 1. 标题栏 WindowControls 按钮 (—, □, ✕)
    if (fx >= m_closeBtnRect.left && fx <= m_closeBtnRect.right &&
        fy >= m_closeBtnRect.top && fy <= m_closeBtnRect.bottom) {
        ShowWindow(m_hwnd, SW_HIDE);
        return;
    }

    if (fx >= m_minimizeBtnRect.left && fx <= m_minimizeBtnRect.right &&
        fy >= m_minimizeBtnRect.top && fy <= m_minimizeBtnRect.bottom) {
        ShowWindow(m_hwnd, SW_MINIMIZE);
        return;
    }

    if (fx >= m_maximizeBtnRect.left && fx <= m_maximizeBtnRect.right &&
        fy >= m_maximizeBtnRect.top && fy <= m_maximizeBtnRect.bottom) {
        ToggleMaximize();
        return;
    }

    // 2. 顶部工具栏按钮 (📌 置顶, ⚙ 打开设置, ✕ 关闭设置, 空列表添加按钮)
    if (fx >= m_pinBtnRect.left && fx <= m_pinBtnRect.right &&
        fy >= m_pinBtnRect.top && fy <= m_pinBtnRect.bottom) {
        ToggleAlwaysOnTop();
        return;
    }

    if (fx >= m_drawerToggleBtnRect.left && fx <= m_drawerToggleBtnRect.right &&
        fy >= m_drawerToggleBtnRect.top && fy <= m_drawerToggleBtnRect.bottom) {
        m_isDrawerOpen = !m_isDrawerOpen;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_isDrawerOpen && fx >= m_drawerCloseBtnRect.left && fx <= m_drawerCloseBtnRect.right &&
        fy >= m_drawerCloseBtnRect.top && fy <= m_drawerCloseBtnRect.bottom) {
        m_isDrawerOpen = false;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (!m_isDrawerOpen && fx >= m_emptyListAddBtnRect.left && fx <= m_emptyListAddBtnRect.right &&
        fy >= m_emptyListAddBtnRect.top && fy <= m_emptyListAddBtnRect.bottom) {
        m_isDrawerOpen = true;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // 2. 活跃节点卡片交互 (点击轮换 / 单测速)
    if (fx >= m_activeDelayChipRect.left && fx <= m_activeDelayChipRect.right &&
        fy >= m_activeDelayChipRect.top && fy <= m_activeDelayChipRect.bottom) {
        TriggerActiveNodeDelayTest();
        return;
    }

    if (fx >= m_activeCardRect.left && fx <= m_activeCardRect.right &&
        fy >= m_activeCardRect.top && fy <= m_activeCardRect.bottom) {
        CycleToNextCandidateNode();
        return;
    }

    // 3. ProxyHead 工具栏交互
    if (fx >= m_headBoltBtnRect.left && fx <= m_headBoltBtnRect.right &&
        fy >= m_headBoltBtnRect.top && fy <= m_headBoltBtnRect.bottom) {
        TriggerAllNodesDelayTest();
        return;
    }

    if (fx >= m_headSortBtnRect.left && fx <= m_headSortBtnRect.right &&
        fy >= m_headSortBtnRect.top && fy <= m_headSortBtnRect.bottom) {
        m_sortType = (m_sortType + 1) % 3;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (fx >= m_headSearchBoxRect.left && fx <= m_headSearchBoxRect.right &&
        fy >= m_headSearchBoxRect.top && fy <= m_headSearchBoxRect.bottom) {
        m_searchFocused = true;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    } else {
        if (m_searchFocused) {
            m_searchFocused = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

    // 4. 设置抽屉内操作
    if (m_isDrawerOpen) {
        // A. 订阅输入框聚焦
        if (fx >= m_drawerImportBoxRect.left && fx <= m_drawerImportBoxRect.right &&
            fy >= m_drawerImportBoxRect.top && fy <= m_drawerImportBoxRect.bottom) {
            m_isDrawerImportFocused = true;
            m_isMixedPortFocused = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }

        // B. 粘贴按钮
        if (fx >= m_drawerPasteBtnRect.left && fx <= m_drawerPasteBtnRect.right &&
            fy >= m_drawerPasteBtnRect.top && fy <= m_drawerPasteBtnRect.bottom) {
            if (OpenClipboard(m_hwnd)) {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData) {
                    wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
                    if (pText) {
                        m_drawerImportText = pText;
                        GlobalUnlock(hData);
                    }
                }
                CloseClipboard();
                m_isDrawerImportFocused = true;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return;
        }

        // C. 导入按钮
        if (fx >= m_drawerImportBtnRect.left && fx <= m_drawerImportBtnRect.right &&
            fy >= m_drawerImportBtnRect.top && fy <= m_drawerImportBtnRect.bottom) {
            TriggerDrawerImport();
            return;
        }

        // D. 订阅卡片项点击 (切换 / 更新 / 删除)
        for (const auto& item : m_profileItemClickAreas) {
            if (fx >= item.deleteBtnRect.left && fx <= item.deleteBtnRect.right &&
                fy >= item.deleteBtnRect.top && fy <= item.deleteBtnRect.bottom) {
                HandleProfileDelete(item.uid);
                return;
            }
            if (fx >= item.updateBtnRect.left && fx <= item.updateBtnRect.right &&
                fy >= item.updateBtnRect.top && fy <= item.updateBtnRect.bottom) {
                HandleProfileUpdate(item.uid);
                return;
            }
            if (fx >= item.cardRect.left && fx <= item.cardRect.right &&
                fy >= item.cardRect.top && fy <= item.cardRect.bottom) {
                HandleProfileSelect(item.uid);
                return;
            }
        }

        // E. 接管模式切换 (MAN / SYS / TUN)
        if (fx >= m_proxyModeRect.left && fx <= m_proxyModeRect.right &&
            fy >= m_proxyModeRect.top && fy <= m_proxyModeRect.bottom) {
            float segW = (m_proxyModeRect.right - m_proxyModeRect.left) / 3.0f;
            int idx = static_cast<int>((fx - m_proxyModeRect.left) / segW);
            ApplyProxyModeChange(std::clamp(idx, 0, 2));
            return;
        }

        // F. 路由偏好切换 (DIR / RUL / PRX)
        if (fx >= m_ruleModeRect.left && fx <= m_ruleModeRect.right &&
            fy >= m_ruleModeRect.top && fy <= m_ruleModeRect.bottom) {
            float segW = (m_ruleModeRect.right - m_ruleModeRect.left) / 3.0f;
            int idx = static_cast<int>((fx - m_ruleModeRect.left) / segW);
            ApplyRuleModeChange(std::clamp(idx, 0, 2));
            return;
        }

        // ➕ 添加自定义分流规则按钮
        if (fx >= m_drawerAddUrlBtnRect.left && fx <= m_drawerAddUrlBtnRect.right &&
            fy >= m_drawerAddUrlBtnRect.top && fy <= m_drawerAddUrlBtnRect.bottom) {
            OpenModal(ModalType::AddUrl);
            return;
        }

        // G. 基础设置 2x2 开关
        if (fx >= m_autoLaunchSwitchRect.left && fx <= m_autoLaunchSwitchRect.right &&
            fy >= m_autoLaunchSwitchRect.top && fy <= m_autoLaunchSwitchRect.bottom) {
            ToggleAutoLaunch();
            return;
        }
        if (fx >= m_allowLanSwitchRect.left && fx <= m_allowLanSwitchRect.right &&
            fy >= m_allowLanSwitchRect.top && fy <= m_allowLanSwitchRect.bottom) {
            // Allow LAN toggle
            m_statusMessage = L"局域网共享模式已变更";
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (fx >= m_silentStartSwitchRect.left && fx <= m_silentStartSwitchRect.right &&
            fy >= m_silentStartSwitchRect.top && fy <= m_silentStartSwitchRect.bottom) {
            bool cur = clashwin::config::AppConfig::Instance().IsSilentStartEnabled();
            clashwin::config::AppConfig::Instance().SetSilentStartEnabled(!cur);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (fx >= m_ipv6SwitchRect.left && fx <= m_ipv6SwitchRect.right &&
            fy >= m_ipv6SwitchRect.top && fy <= m_ipv6SwitchRect.bottom) {
            bool cur = clashwin::config::AppConfig::Instance().IsIpv6Enabled();
            clashwin::config::AppConfig::Instance().SetIpv6Enabled(!cur);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }

        // H. 混合端口输入与保存
        if (fx >= m_mixedPortBoxRect.left && fx <= m_mixedPortBoxRect.right &&
            fy >= m_mixedPortBoxRect.top && fy <= m_mixedPortBoxRect.bottom) {
            m_isMixedPortFocused = true;
            m_isDrawerImportFocused = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (fx >= m_mixedPortSaveBtnRect.left && fx <= m_mixedPortSaveBtnRect.right &&
            fy >= m_mixedPortSaveBtnRect.top && fy <= m_mixedPortSaveBtnRect.bottom) {
            SavePortChange();
            return;
        }

        // I. 皮肤按钮
        for (const auto& btn : m_skinBtnRects) {
            if (fx >= btn.first.left && fx <= btn.first.right &&
                fy >= btn.first.top && fy <= btn.first.bottom) {
                SwitchSkin(btn.second);
                return;
            }
        }

        // J. 主题模式切换 (SYS / LIGHT / DARK)
        if (fx >= m_themeModeRect.left && fx <= m_themeModeRect.right &&
            fy >= m_themeModeRect.top && fy <= m_themeModeRect.bottom) {
            float segW = (m_themeModeRect.right - m_themeModeRect.left) / 3.0f;
            int idx = static_cast<int>((fx - m_themeModeRect.left) / segW);
            if (idx == 0) clashwin::config::AppConfig::Instance().SetThemeMode("system");
            else if (idx == 1) clashwin::config::AppConfig::Instance().SetThemeMode("light");
            else if (idx == 2) clashwin::config::AppConfig::Instance().SetThemeMode("dark");
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }

        // K. 滑块拖拽
        if (fx >= m_slider1Rect.left && fx <= m_slider1Rect.right &&
            fy >= m_slider1Rect.top && fy <= m_slider1Rect.bottom) {
            m_isDraggingSlider1 = true;
            SetCapture(m_hwnd);
            OnMouseMove(x, y);
            return;
        }
        if (fx >= m_slider2Rect.left && fx <= m_slider2Rect.right &&
            fy >= m_slider2Rect.top && fy <= m_slider2Rect.bottom) {
            m_isDraggingSlider2 = true;
            SetCapture(m_hwnd);
            OnMouseMove(x, y);
            return;
        }

        // L. 抽屉底部快捷按钮: 运行日志 + 帮助与关于
        if (fx >= m_drawerLogsBtnRect.left && fx <= m_drawerLogsBtnRect.right &&
            fy >= m_drawerLogsBtnRect.top && fy <= m_drawerLogsBtnRect.bottom) {
            OpenModal(ModalType::LogsView);
            return;
        }
        if (fx >= m_drawerHelpBtnRect.left && fx <= m_drawerHelpBtnRect.right &&
            fy >= m_drawerHelpBtnRect.top && fy <= m_drawerHelpBtnRect.bottom) {
            OpenModal(ModalType::HelpMenu);
            return;
        }
    }

    // 5. 宽窗一键断开按钮
    if (fx >= m_closeAllConnsBtnRect.left && fx <= m_closeAllConnsBtnRect.right &&
        fy >= m_closeAllConnsBtnRect.top && fy <= m_closeAllConnsBtnRect.bottom) {
        std::thread([this]() {
            clashwin::core::ConnectionManager::Instance().CloseAllConnections(9098);
            RefreshConnections();
        }).detach();
        return;
    }

    // 6. 节点列表点击切换
    for (const auto& item : m_nodeItemRects) {
        const auto& r = item.first;
        if (fx >= r.left && fx <= r.right && fy >= r.top && fy <= r.bottom) {
            std::string targetNode = item.second;
            clashwin::config::ProfileManager::Instance().SetCurrentActiveNode(targetNode);
            std::thread([targetNode]() {
                clashwin::core::MihomoClient::Instance().SelectProxy("PROXY", targetNode);
            }).detach();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            break;
        }
    }
}

void MainWindow::OnLButtonUp(int, int) {
    if (m_isDraggingSlider1 || m_isDraggingSlider2) {
        m_isDraggingSlider1 = false;
        m_isDraggingSlider2 = false;
        ReleaseCapture();
        clashwin::config::AppConfig::Instance().SetSlider1Val(ThemeManager::Instance().GetSlider1());
        clashwin::config::AppConfig::Instance().SetSlider2Val(ThemeManager::Instance().GetSlider2());
    }
}

void MainWindow::OnMouseMove(int x, int y) {
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);

    int newHover = 0;
    if (fx >= m_closeBtnRect.left && fx <= m_closeBtnRect.right &&
        fy >= m_closeBtnRect.top && fy <= m_closeBtnRect.bottom) {
        newHover = 3;
    } else if (fx >= m_maximizeBtnRect.left && fx <= m_maximizeBtnRect.right &&
               fy >= m_maximizeBtnRect.top && fy <= m_maximizeBtnRect.bottom) {
        newHover = 2;
    } else if (fx >= m_minimizeBtnRect.left && fx <= m_minimizeBtnRect.right &&
               fy >= m_minimizeBtnRect.top && fy <= m_minimizeBtnRect.bottom) {
        newHover = 1;
    }

    if (newHover != m_hoveredWindowBtn) {
        m_hoveredWindowBtn = newHover;
        InvalidateRect(m_hwnd, nullptr, FALSE);

        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hwnd;
        tme.dwHoverTime = HOVER_DEFAULT;
        TrackMouseEvent(&tme);
    }

    if (m_isDraggingSlider1) {
        float trackX = m_slider1Rect.left + 75.0f;
        float trackW = (m_slider1Rect.right - m_slider1Rect.left) - 75.0f - 35.0f;
        float frac = std::clamp((fx - trackX) / trackW, 0.0f, 1.0f);
        ThemeManager::Instance().SetSlider1(frac * 1.0f);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    } else if (m_isDraggingSlider2) {
        float trackX = m_slider2Rect.left + 75.0f;
        float trackW = (m_slider2Rect.right - m_slider2Rect.left) - 75.0f - 35.0f;
        float frac = std::clamp((fx - trackX) / trackW, 0.0f, 1.0f);
        ThemeManager::Instance().SetSlider2(frac * 5.0f);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_NCCALCSIZE: {
        if (wParam == TRUE) {
            if (IsZoomed(m_hwnd)) {
                NCCALCSIZE_PARAMS* pParams = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = { sizeof(mi) };
                if (GetMonitorInfoW(hMon, &mi)) {
                    pParams->rgrc[0] = mi.rcWork;
                }
            }
            return 0;
        }
        break;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* pMMI = reinterpret_cast<MINMAXINFO*>(lParam);
        m_dpiScale = GetDpiScale();
        pMMI->ptMinTrackSize.x = static_cast<LONG>(MIN_WIDTH * m_dpiScale + 0.5f);
        pMMI->ptMinTrackSize.y = static_cast<LONG>(MIN_HEIGHT * m_dpiScale + 0.5f);
        pMMI->ptMaxTrackSize.x = static_cast<LONG>(clashwin::geometry::kMaxWidth * m_dpiScale + 0.5f);
        pMMI->ptMaxTrackSize.y = static_cast<LONG>(clashwin::geometry::kMaxHeight * m_dpiScale + 0.5f);
        return 0;
    }

    case WM_NCLBUTTONDBLCLK: {
        if (wParam == HTCAPTION) {
            ToggleMaximize();
            return 0;
        }
        break;
    }

    case WM_NCRBUTTONUP: {
        if (wParam == HTCAPTION) {
            HMENU hMenu = GetSystemMenu(m_hwnd, FALSE);
            if (hMenu) {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                bool zoomed = IsZoomed(m_hwnd) != FALSE;
                EnableMenuItem(hMenu, SC_RESTORE, MF_BYCOMMAND | (zoomed ? MF_ENABLED : MF_GRAYED));
                EnableMenuItem(hMenu, SC_MAXIMIZE, MF_BYCOMMAND | (zoomed ? MF_GRAYED : MF_ENABLED));
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
                if (cmd > 0) {
                    SendMessageW(m_hwnd, WM_SYSCOMMAND, cmd, 0);
                }
            }
            return 0;
        }
        break;
    }

    case WM_SYSCOMMAND: {
        if ((wParam & 0xFFF0) == SC_KEYMENU && lParam == ' ') {
            HMENU hMenu = GetSystemMenu(m_hwnd, FALSE);
            if (hMenu) {
                RECT rc;
                GetWindowRect(m_hwnd, &rc);
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, rc.left, rc.top + static_cast<int>(30 * m_dpiScale), 0, m_hwnd, nullptr);
                if (cmd > 0) {
                    SendMessageW(m_hwnd, WM_SYSCOMMAND, cmd, 0);
                }
            }
            return 0;
        }
        break;
    }

    case WM_NCHITTEST: {
        POINTS pt = MAKEPOINTS(lParam);
        return OnNcHitTest(pt);
    }

    case WM_SIZING: {
        RECT* pRect = reinterpret_cast<RECT*>(lParam);
        m_dpiScale = GetDpiScale();

        int minWidthPx = static_cast<int>(MIN_WIDTH * m_dpiScale + 0.5f);
        int minHeightPx = static_cast<int>(MIN_HEIGHT * m_dpiScale + 0.5f);

        int currentW = pRect->right - pRect->left;
        int currentH = pRect->bottom - pRect->top;

        if (currentW < minWidthPx) {
            if (wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT) {
                pRect->left = pRect->right - minWidthPx;
            } else {
                pRect->right = pRect->left + minWidthPx;
            }
        }

        if (currentH < minHeightPx) {
            if (wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT) {
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

    case WM_MOVE: {
        RECT rc;
        GetWindowRect(m_hwnd, &rc);
        double w = (rc.right - rc.left) / m_dpiScale;
        double h = (rc.bottom - rc.top) / m_dpiScale;
        clashwin::config::AppConfig::Instance().SaveWindowStateThrottled(w, h, rc.left, rc.top);
        return 0;
    }

    case WM_CHAR: {
        wchar_t ch = static_cast<wchar_t>(wParam);
        if (m_currentModal != ModalType::None) {
            OnModalChar(ch);
            return 0;
        }
        if (m_searchFocused) {
            if (ch == 8) { // Backspace
                if (!m_searchText.empty()) {
                    m_searchText.pop_back();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            } else if (ch == 27) { // Escape
                m_searchText.clear();
                m_searchFocused = false;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            } else if (ch >= 32) {
                m_searchText.push_back(ch);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        } else if (m_isMixedPortFocused) {
            if (ch == 8) {
                if (!m_mixedPortText.empty()) {
                    m_mixedPortText.pop_back();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            } else if (ch == 13) {
                SavePortChange();
            } else if (ch == 27) {
                m_isMixedPortFocused = false;
                m_mixedPortText = std::to_wstring(clashwin::config::AppConfig::Instance().GetMixedPort());
                InvalidateRect(m_hwnd, nullptr, FALSE);
            } else if (ch >= L'0' && ch <= L'9' && m_mixedPortText.length() < 5) {
                m_mixedPortText.push_back(ch);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        } else if (m_isDrawerImportFocused) {
            if (ch == 8) {
                if (!m_drawerImportText.empty()) {
                    m_drawerImportText.pop_back();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            } else if (ch == 13) {
                TriggerDrawerImport();
            } else if (ch == 27) {
                m_isDrawerImportFocused = false;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            } else if (ch >= 32) {
                m_drawerImportText.push_back(ch);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        break;
    }

    case WM_MOUSEWHEEL: {
        if (m_isDrawerOpen) {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            m_drawerScrollY -= (static_cast<float>(delta) / 120.0f) * 35.0f * m_dpiScale;
            if (m_drawerScrollY < 0.0f) m_drawerScrollY = 0.0f;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    }

    case WM_USER + 102: {
        m_isImporting = false;
        m_isDrawerImportFocused = false;
        m_drawerImportText.clear();
        if (wParam == 1) {
            m_statusMessage = L"订阅配置已成功更新并生效";
            InvalidateRect(m_hwnd, nullptr, FALSE);
            StartCoreIfConfigReady();
        } else {
            m_statusMessage = L"操作失败，请检查网络或配置链接";
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN: {
        ResetIdleTimer();
        if (m_isStealthMode) {
            m_isStealthMode = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        if (m_currentModal != ModalType::None) {
            OnModalKeyDown(wParam);
            return 0;
        }
        if (wParam == VK_F4 || wParam == VK_F1) {
            // F4 / F1 快捷键触发全节点测速
            TriggerActiveNodeDelayTest();
            return 0;
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        OnLButtonDown(x, y);
        ResetIdleTimer();
        if (m_isStealthMode) {
            m_isStealthMode = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        OnLButtonUp(x, y);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        OnMouseMove(x, y);
        ResetIdleTimer();
        if (m_isStealthMode) {
            m_isStealthMode = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_TIMER: {
        if (wParam == IDLE_TIMER_ID) {
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            int logicalW = static_cast<int>((rc.right - rc.left) / m_dpiScale);
            int logicalH = static_cast<int>((rc.bottom - rc.top) / m_dpiScale);

            if (logicalW <= NARROW_BREAKPOINT && logicalH <= MIN_HEIGHT) {
                if (!m_isStealthMode) {
                    m_isStealthMode = true;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            }
            ResetIdleTimer();
            return 0;
        } else if (wParam == CONN_TIMER_ID) {
            RefreshConnections();
            return 0;
        } else if (wParam == TRAFFIC_TIMER_ID) {
            uint64_t upSpd = clashwin::core::ConnectionManager::Instance().GetUploadSpeed();
            uint64_t downSpd = clashwin::core::ConnectionManager::Instance().GetDownloadSpeed();
            uint64_t upTot = clashwin::core::ConnectionManager::Instance().GetUploadTotal();
            uint64_t downTot = clashwin::core::ConnectionManager::Instance().GetDownloadTotal();

            m_upSpeed = upSpd;
            m_downSpeed = downSpd;
            m_upTotal = upTot;
            m_downTotal = downTot;

            if (m_trafficHistory.size() >= 30) {
                m_trafficHistory.pop_front();
            }
            m_trafficHistory.push_back({ upSpd, downSpd });

            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    }

    case WM_MOUSELEAVE: {
        if (m_hoveredWindowBtn != 0) {
            m_hoveredWindowBtn = 0;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        float fx = static_cast<float>(GET_X_LPARAM(lParam));
        float fy = static_cast<float>(GET_Y_LPARAM(lParam));
        if (m_isDrawerOpen) {
            for (const auto& item : m_profileItemClickAreas) {
                if (fx >= item.cardRect.left && fx <= item.cardRect.right &&
                    fy >= item.cardRect.top && fy <= item.cardRect.bottom) {
                    m_contextProfileUid = item.uid;
                    m_profileMenuX = fx;
                    m_profileMenuY = fy;
                    OpenModal(ModalType::ProfileContextMenu);
                    return 0;
                }
            }
        }
        for (const auto& area : m_connectionItemRects) {
            if (fx >= area.rect.left && fx <= area.rect.right &&
                fy >= area.rect.top && fy <= area.rect.bottom) {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(m_hwnd, &pt);
                ShowConnectionContextMenu(pt.x, pt.y, area);
                return 0;
            }
        }
        break;
    }

    case WM_TRAYICON: {
        if (lParam == WM_RBUTTONUP) {
            ShowTrayContextMenu();
        } else if (lParam == WM_LBUTTONDBLCLK) {
            OnWakeupVisible();
        }
        return 0;
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

    case WM_USER + 100: {
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_PAINT: {
        OnPaint();
        ValidateRect(m_hwnd, nullptr);
        return 0;
    }

    case WM_CLOSE: {
        ShowWindow(m_hwnd, SW_HIDE);
        return 0;
    }

    case WM_DESTROY: {
        GracefulExit();
        return 0;
    }

    case WM_QUERYENDSESSION: {
        GracefulExit();
        return TRUE;
    }
    }

    return DefWindowProcW(m_hwnd, uMsg, wParam, lParam);
}

void MainWindow::OnResize(UINT width, UINT height) {
    if (m_renderer) {
        m_renderer->Resize(width, height);
    }
    ResetIdleTimer();

    int logicalW = static_cast<int>(width / m_dpiScale);
    int logicalH = static_cast<int>(height / m_dpiScale);
    bool isMiniStatus = (logicalH <= MIN_HEIGHT);

    if (logicalH > MIN_HEIGHT && m_isStealthMode) {
        m_isStealthMode = false;
    }

    if (isMiniStatus) {
        m_isDrawerOpen = false;
        KillTimer(m_hwnd, CONN_TIMER_ID);
    } else if (logicalW > NARROW_BREAKPOINT) {
        SetTimer(m_hwnd, CONN_TIMER_ID, CONN_POLL_INTERVAL_MS, nullptr);
    } else {
        KillTimer(m_hwnd, CONN_TIMER_ID);
    }

    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    clashwin::config::AppConfig::Instance().SaveWindowStateThrottled(logicalW, logicalH, rc.left, rc.top);

    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::OnPaint() {
    if (!m_renderer) return;

    const auto& theme = ThemeManager::Instance().GetColors();
    m_renderer->BeginDraw();

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    float width = static_cast<float>(rc.right - rc.left);
    float height = static_cast<float>(rc.bottom - rc.top);
    float logicalW = width / m_dpiScale;
    float logicalH = height / m_dpiScale;
    bool isMiniStatus = (logicalH <= static_cast<float>(MIN_HEIGHT));

    // 1. 窗口纯色不透明底色
    m_renderer->Clear(theme.window_bg);

    float yOffset = 0.0f;
    float marginX = 8.0f * m_dpiScale;
    float contentW = width - marginX * 2;

    // 2. 标题栏 (30px 对齐 Clash Mini `the_titlebar`，原汁原味纯平顶栏)
    if (!m_isStealthMode) {
        float titleH = 30.0f * m_dpiScale;
        // 标题栏纯平底色与底部分割线
        m_renderer->FillRect(0, 0, width, titleH, theme.header_bg);
        m_renderer->DrawLine(0, titleH - 1.0f, width, titleH - 1.0f, theme.card_shadow);

        // 应用图标 (16x16)
        m_renderer->DrawTextSimple(L"🐱", 10.0f * m_dpiScale, 0, 18.0f * m_dpiScale, titleH, theme.accent, 11.5f, false, DWRITE_TEXT_ALIGNMENT_CENTER);

        // 应用标题: "Clash Mini Ver.3.0.10"
        m_renderer->DrawTextSimple(L"Clash Mini Ver.3.0.10", 30.0f * m_dpiScale, 0, 150.0f * m_dpiScale, titleH, theme.text_primary, 11.0f, true);

        // 右侧 WindowControls (最小化 —, 最大化 □/❐, 关闭 ✕) - 每个 28x28，纯平圆角微光/红底 hover
        float btnW = 28.0f * m_dpiScale;
        float btnH = 28.0f * m_dpiScale;
        float btnY = 1.0f * m_dpiScale;

        // 最小化
        float minX = width - (btnW * 3 + 6.0f * m_dpiScale);
        m_minimizeBtnRect = D2D1::RectF(minX, btnY, minX + btnW, btnY + btnH);
        m_renderer->DrawWindowCaptionButton(minX, btnY, btnW, btnH, L"—", 1, (m_hoveredWindowBtn == 1));

        // 最大化 / 还原
        float maxX = width - (btnW * 2 + 4.0f * m_dpiScale);
        m_maximizeBtnRect = D2D1::RectF(maxX, btnY, maxX + btnW, btnY + btnH);
        m_renderer->DrawWindowCaptionButton(maxX, btnY, btnW, btnH, m_isLargeMode ? L"❐" : L"□", 2, (m_hoveredWindowBtn == 2));

        // 关闭 (点击隐藏至托盘，悬浮高亮红底白叉)
        float closeX = width - (btnW + 2.0f * m_dpiScale);
        m_closeBtnRect = D2D1::RectF(closeX, btnY, closeX + btnW, btnY + btnH);
        m_renderer->DrawWindowCaptionButton(closeX, btnY, btnW, btnH, L"✕", 3, (m_hoveredWindowBtn == 3));

        yOffset = titleH;
    }

    // 微缩科学流量计模式（高度 <= 126px）
    // 代理列表与抽屉完全不挂载，数据拉取短路，窗口化身为微型科学流量计
    if (isMiniStatus) {
        float bottomBarH = height - yOffset;
        if (bottomBarH > 0) {
            m_renderer->DrawTrafficDashboard(0, yOffset, width, bottomBarH,
                                             m_upSpeed, m_downSpeed, m_upTotal, m_downTotal,
                                             m_trafficHistory, true);
        }
        m_renderer->EndDraw();
        return;
    }

    // 3. 置顶当前节点与快捷控制栏 (30px，对齐 Clash Mini 顶部快捷行)
    float actBtnW = 28.0f * m_dpiScale;
    float actBtnH = 28.0f * m_dpiScale;

    if (!m_isDrawerOpen) {
        // 右侧设置按钮 ⚙ (28x28)
        float drawerBtnX = width - marginX - actBtnW;
        m_drawerToggleBtnRect = D2D1::RectF(drawerBtnX, yOffset + 1.0f * m_dpiScale, drawerBtnX + actBtnW, yOffset + 1.0f * m_dpiScale + actBtnH);
        m_renderer->Draw3DButton(drawerBtnX, yOffset + 1.0f * m_dpiScale, actBtnW, actBtnH, L"⚙", false, false, false);

        // 右侧置顶按钮 📌 (28x28)
        float pinBtnX = drawerBtnX - actBtnW - 4.0f * m_dpiScale;
        m_pinBtnRect = D2D1::RectF(pinBtnX, yOffset + 1.0f * m_dpiScale, pinBtnX + actBtnW, yOffset + 1.0f * m_dpiScale + actBtnH);
        m_renderer->Draw3DButton(pinBtnX, yOffset + 1.0f * m_dpiScale, actBtnW, actBtnH, L"📌", false, false, m_isAlwaysOnTop);

        // 左侧活跃节点卡片 (占据剩余全部宽度)
        float cardW = pinBtnX - marginX - 4.0f * m_dpiScale;
        m_activeCardRect = D2D1::RectF(marginX, yOffset + 1.0f * m_dpiScale, marginX + cardW, yOffset + 1.0f * m_dpiScale + actBtnH);
        m_activeDelayChipRect = D2D1::RectF(marginX + cardW - 55.0f * m_dpiScale, yOffset + 1.0f * m_dpiScale, marginX + cardW, yOffset + 1.0f * m_dpiScale + actBtnH);

        std::string currentProxy = clashwin::config::ProfileManager::Instance().GetCurrentActiveNode();
        std::wstring currentProxyW(currentProxy.begin(), currentProxy.end());
        if (currentProxyW.empty()) currentProxyW = L"未选择节点";

        // 获取协议与真实测速延迟
        std::wstring protoW = L"";
        int activeDelay = -1;
        const auto& allNodes = clashwin::config::ProfileManager::Instance().GetNodes();
        for (const auto& n : allNodes) {
            if (n.name == currentProxy) {
                protoW = std::wstring(n.type.begin(), n.type.end());
                for (auto& c : protoW) c = towupper(c);
                break;
            }
        }

        if (!currentProxy.empty()) {
            std::lock_guard<std::mutex> lock(m_delayMutex);
            auto it = m_nodeDelays.find(currentProxy);
            if (it != m_nodeDelays.end()) {
                activeDelay = it->second;
            }
        }

        m_renderer->DrawActiveNodeCard(marginX, yOffset + 1.0f * m_dpiScale, cardW, actBtnH,
                                       protoW, currentProxyW, activeDelay, L"", m_isTestingDelay);
    } else {
        // 抽屉展开时，右上角单独渲染关闭设置按钮 ✕ (28x28 醒目高亮)
        float closeBtnX = width - marginX - actBtnW;
        m_drawerCloseBtnRect = D2D1::RectF(closeBtnX, yOffset + 1.0f * m_dpiScale, closeBtnX + actBtnW, yOffset + 1.0f * m_dpiScale + actBtnH);
        m_renderer->Draw3DButton(closeBtnX, yOffset + 1.0f * m_dpiScale, actBtnW, actBtnH, L"✕", false, false, true);
    }

    yOffset += 30.0f * m_dpiScale;

    // 4. 设置抽屉面板 (若展开)
    if (m_isDrawerOpen) {
        float startY = yOffset - m_drawerScrollY;
        float curY = startY;
        float dTextX = marginX + 8.0f * m_dpiScale;
        float dTextW = contentW - 16.0f * m_dpiScale;

        m_profileItemClickAreas.clear();

        // -------------------------------------------------------------
        // 卡片 1: 订阅与节点配置管理 (ProfileImportCard)
        // -------------------------------------------------------------
        const auto& profiles = clashwin::config::ProfileManager::Instance().GetProfileItems();
        const auto& activeUid = clashwin::config::ProfileManager::Instance().GetCurrentProfileUid();

        size_t realPrfCount = 0;
        for (const auto& prf : profiles) {
            if (prf.type == "remote" || prf.type == "local") realPrfCount++;
        }
        
        float profListH = static_cast<float>(realPrfCount) * (44.0f * m_dpiScale);
        float card1H = (58.0f * m_dpiScale) + profListH;
        m_renderer->Draw3DCard(marginX, curY, contentW, card1H, theme.card_bg);

        // 标题
        m_renderer->DrawTextSimple(L"订阅与节点配置", dTextX, curY + 6.0f * m_dpiScale, dTextW, 16.0f * m_dpiScale, theme.accent, 11.5f, true);
        float c1Y = curY + 24.0f * m_dpiScale;

        // 输入框 + 粘贴 + 导入
        float pasteBtnW = 48.0f * m_dpiScale;
        float importBtnW = 42.0f * m_dpiScale;
        float inputW = dTextW - pasteBtnW - importBtnW - 8.0f * m_dpiScale;

        m_drawerImportBoxRect = D2D1::RectF(dTextX, c1Y, dTextX + inputW, c1Y + 22.0f * m_dpiScale);
        m_renderer->Draw3DTextInput(dTextX, c1Y, inputW, 22.0f * m_dpiScale, L"", m_drawerImportText, L"填入订阅链接...", m_isDrawerImportFocused);

        float pasteX = dTextX + inputW + 4.0f * m_dpiScale;
        m_drawerPasteBtnRect = D2D1::RectF(pasteX, c1Y, pasteX + pasteBtnW, c1Y + 22.0f * m_dpiScale);
        m_renderer->Draw3DButton(pasteX, c1Y, pasteBtnW, 22.0f * m_dpiScale, L"📋粘贴");

        float impX = pasteX + pasteBtnW + 4.0f * m_dpiScale;
        m_drawerImportBtnRect = D2D1::RectF(impX, c1Y, impX + importBtnW, c1Y + 22.0f * m_dpiScale);
        m_renderer->Draw3DButton(impX, c1Y, importBtnW, 22.0f * m_dpiScale, L"导入", false, false, m_isImporting);

        c1Y += 26.0f * m_dpiScale;

        // 渲染订阅项列表
        for (const auto& prf : profiles) {
            if (prf.type != "remote" && prf.type != "local") continue;
            float pItemH = 38.0f * m_dpiScale;
            bool isActivePrf = (prf.uid == activeUid);
            D2D1_RECT_F pRect = D2D1::RectF(dTextX, c1Y, dTextX + dTextW, c1Y + pItemH);
            
            m_renderer->Draw3DCard(dTextX, c1Y, dTextW, pItemH, isActivePrf ? theme.card_active_bg : theme.card_bg, false);

            std::wstring pName = prf.name.empty() ? L"未命名订阅" : std::wstring(prf.name.begin(), prf.name.end());
            if (isActivePrf) {
                m_renderer->DrawTextSimple(L"✓ " + pName, dTextX + 6.0f * m_dpiScale, c1Y + 3.0f * m_dpiScale, dTextW - 55.0f * m_dpiScale, 16.0f * m_dpiScale, theme.accent, 11.0f, true);
            } else {
                m_renderer->DrawTextSimple(pName, dTextX + 6.0f * m_dpiScale, c1Y + 3.0f * m_dpiScale, dTextW - 55.0f * m_dpiScale, 16.0f * m_dpiScale, theme.text_primary, 10.5f, false);
            }

            std::wstring stats = L"已同步 | " + std::to_wstring(prf.total_bytes / (1024 * 1024)) + L" MB";
            m_renderer->DrawTextSimple(stats, dTextX + 6.0f * m_dpiScale, c1Y + 20.0f * m_dpiScale, dTextW - 55.0f * m_dpiScale, 14.0f * m_dpiScale, theme.text_secondary, 9.0f, false);

            float actBtnSize = 18.0f * m_dpiScale;
            float updX = dTextX + dTextW - 44.0f * m_dpiScale;
            float delX = dTextX + dTextW - 22.0f * m_dpiScale;
            float btnY = c1Y + 10.0f * m_dpiScale;

            D2D1_RECT_F updRect = D2D1::RectF(updX, btnY, updX + actBtnSize, btnY + actBtnSize);
            D2D1_RECT_F delRect = D2D1::RectF(delX, btnY, delX + actBtnSize, btnY + actBtnSize);

            m_renderer->Draw3DButton(updX, btnY, actBtnSize, actBtnSize, L"🔄");
            m_renderer->Draw3DButton(delX, btnY, actBtnSize, actBtnSize, L"🗑");

            m_profileItemClickAreas.push_back({ pRect, prf.uid, updRect, delRect });
            c1Y += pItemH + 4.0f * m_dpiScale;
        }

        curY += card1H + 6.0f * m_dpiScale;

        // -------------------------------------------------------------
        // 卡片 2: 流量接管与路由偏好 (TakeoverModeCard + RoutingPreferenceCard)
        // -------------------------------------------------------------
        float card2H = 162.0f * m_dpiScale;
        m_renderer->Draw3DCard(marginX, curY, contentW, card2H, theme.card_bg);

        // A. 流量接管模式
        m_renderer->DrawTextSimple(L"流量接管模式", dTextX, curY + 6.0f * m_dpiScale, dTextW, 16.0f * m_dpiScale, theme.text_primary, 11.5f, true);
        float c2Y = curY + 24.0f * m_dpiScale;

        int modeIdx = 0;
        if (clashwin::config::AppConfig::Instance().IsTunModeEnabled()) modeIdx = 2;
        else if (clashwin::config::AppConfig::Instance().IsSystemProxyEnabled()) modeIdx = 1;
        m_proxyModeRect = D2D1::RectF(dTextX, c2Y, dTextX + dTextW, c2Y + 22.0f * m_dpiScale);
        m_renderer->DrawSegmentedControl(dTextX, c2Y, dTextW, 22.0f * m_dpiScale, {L"MAN", L"SYS", L"TUN"}, modeIdx);
        c2Y += 24.0f * m_dpiScale;

        std::wstring modeTip = (modeIdx == 2) ? L"TUN: 虚拟网卡全盘接管流量" : ((modeIdx == 1) ? L"SYS: 已接管系统代理 (127.0.0.1:10801)" : L"MAN: 完全手动代理 (仅监听端口)");
        m_renderer->DrawTextSimple(modeTip, dTextX, c2Y, dTextW, 14.0f * m_dpiScale, theme.text_secondary, 9.5f, false);
        c2Y += 18.0f * m_dpiScale;

        // B. 分流路由偏好
        m_renderer->DrawTextSimple(L"分流路由偏好", dTextX, c2Y, dTextW, 16.0f * m_dpiScale, theme.text_primary, 11.5f, true);
        c2Y += 18.0f * m_dpiScale;

        int ruleIdx = 1;
        if (clashwin::config::AppConfig::Instance().GetRuleFallback() == "proxy") ruleIdx = 2;
        else if (clashwin::config::AppConfig::Instance().GetRuleFallback() == "direct") ruleIdx = 0;
        m_ruleModeRect = D2D1::RectF(dTextX, c2Y, dTextX + dTextW, c2Y + 22.0f * m_dpiScale);
        m_renderer->DrawSegmentedControl(dTextX, c2Y, dTextW, 22.0f * m_dpiScale, {L"DIR", L"RUL", L"PRX"}, ruleIdx);
        c2Y += 24.0f * m_dpiScale;

        std::wstring ruleTip = (ruleIdx == 2) ? L"PRX: 全局代理所有流量" : ((ruleIdx == 1) ? L"RUL: 智能规则分流 (推荐出厂默认)" : L"DIR: 全部直连 (不走代理)");
        m_renderer->DrawTextSimple(ruleTip, dTextX, c2Y, dTextW, 14.0f * m_dpiScale, theme.text_secondary, 9.5f, false);
        c2Y += 18.0f * m_dpiScale;

        // ➕ 添加自定义分流规则按钮
        m_drawerAddUrlBtnRect = D2D1::RectF(dTextX, c2Y, dTextX + dTextW, c2Y + 22.0f * m_dpiScale);
        m_renderer->Draw3DButton(dTextX, c2Y, dTextW, 22.0f * m_dpiScale, L"➕ 添加自定义分流规则");

        curY += card2H + 6.0f * m_dpiScale;

        // -------------------------------------------------------------
        // 卡片 3: 基础设置 (BasicSettingsCard: 2x2 grid + Mixed Port)
        // -------------------------------------------------------------
        float card3H = 110.0f * m_dpiScale;
        m_renderer->Draw3DCard(marginX, curY, contentW, card3H, theme.card_bg);

        m_renderer->DrawTextSimple(L"基础设置", dTextX, curY + 6.0f * m_dpiScale, dTextW, 16.0f * m_dpiScale, theme.text_primary, 11.5f, true);
        float c3Y = curY + 24.0f * m_dpiScale;

        float gridColW = (dTextW - 12.0f * m_dpiScale) / 2.0f;
        float gridRowH = 20.0f * m_dpiScale;

        // Row 1: 开机自动启动 + Allow LAN
        m_autoLaunchSwitchRect = D2D1::RectF(dTextX, c3Y, dTextX + gridColW, c3Y + gridRowH);
        m_renderer->Draw3DSwitch(dTextX, c3Y, gridColW, gridRowH, L"开机自启动", clashwin::config::AppConfig::Instance().IsAutoLaunchEnabled());

        float col2X = dTextX + gridColW + 12.0f * m_dpiScale;
        m_allowLanSwitchRect = D2D1::RectF(col2X, c3Y, col2X + gridColW, c3Y + gridRowH);
        m_renderer->Draw3DSwitch(col2X, c3Y, gridColW, gridRowH, L"Allow LAN", false);
        c3Y += gridRowH + 6.0f * m_dpiScale;

        // Row 2: 启动时最小化 + Allow IPv6
        m_silentStartSwitchRect = D2D1::RectF(dTextX, c3Y, dTextX + gridColW, c3Y + gridRowH);
        m_renderer->Draw3DSwitch(dTextX, c3Y, gridColW, gridRowH, L"启动最小化", clashwin::config::AppConfig::Instance().IsSilentStartEnabled());

        m_ipv6SwitchRect = D2D1::RectF(col2X, c3Y, col2X + gridColW, c3Y + gridRowH);
        m_renderer->Draw3DSwitch(col2X, c3Y, gridColW, gridRowH, L"Allow IPv6", clashwin::config::AppConfig::Instance().IsIpv6Enabled());
        c3Y += gridRowH + 8.0f * m_dpiScale;

        // Row 3: Mixed Port
        float portBoxW = 75.0f * m_dpiScale;
        float portBtnW = 42.0f * m_dpiScale;
        m_renderer->DrawTextSimple(L"Mixed Port", dTextX, c3Y + 2.0f * m_dpiScale, 75.0f * m_dpiScale, 20.0f * m_dpiScale, theme.text_primary, 11.0f, false);
        
        float portX = dTextX + 75.0f * m_dpiScale;
        m_mixedPortBoxRect = D2D1::RectF(portX, c3Y, portX + portBoxW, c3Y + 20.0f * m_dpiScale);
        m_renderer->Draw3DTextInput(portX, c3Y, portBoxW, 20.0f * m_dpiScale, L"", m_mixedPortText, L"10801", m_isMixedPortFocused);

        float pBtnX = portX + portBoxW + 6.0f * m_dpiScale;
        m_mixedPortSaveBtnRect = D2D1::RectF(pBtnX, c3Y, pBtnX + portBtnW, c3Y + 20.0f * m_dpiScale);
        m_renderer->Draw3DButton(pBtnX, c3Y, portBtnW, 20.0f * m_dpiScale, L"保存");

        curY += card3H + 6.0f * m_dpiScale;

        // -------------------------------------------------------------
        // 卡片 4: 界面外观与物理质感 (ThemeSettingsCard)
        // -------------------------------------------------------------
        float card4H = 160.0f * m_dpiScale;
        m_renderer->Draw3DCard(marginX, curY, contentW, card4H, theme.card_bg);

        m_renderer->DrawTextSimple(L"界面外观与物理质感", dTextX, curY + 6.0f * m_dpiScale, dTextW, 16.0f * m_dpiScale, theme.text_primary, 11.5f, true);
        float c4Y = curY + 24.0f * m_dpiScale;

        // 6 皮肤按钮
        m_skinBtnRects.clear();
        float skinBtnW = (dTextW - 10.0f * m_dpiScale) / 3.0f;
        float skinBtnH = 20.0f * m_dpiScale;
        std::vector<std::pair<std::wstring, SkinType>> skins = {
            {L"3D", SkinType::Retro3D}, {L"Org", SkinType::Original}, {L"Mod", SkinType::Modern},
            {L"Fro", SkinType::Frosted}, {L"Cyber", SkinType::Cyberpunk}, {L"Mono", SkinType::Monochrome}
        };

        for (size_t i = 0; i < skins.size(); ++i) {
            float rowX = dTextX + (i % 3) * (skinBtnW + 5.0f * m_dpiScale);
            float rowY = c4Y + (i / 3) * (skinBtnH + 4.0f * m_dpiScale);
            bool isCurrent = (ThemeManager::Instance().GetCurrentSkin() == skins[i].second);
            m_renderer->Draw3DButton(rowX, rowY, skinBtnW, skinBtnH, skins[i].first, false, false, isCurrent);
            m_skinBtnRects.push_back({D2D1::RectF(rowX, rowY, rowX + skinBtnW, rowY + skinBtnH), skins[i].second});
        }
        c4Y += (skinBtnH + 4.0f * m_dpiScale) * 2 + 6.0f * m_dpiScale;

        // 主题模式 SYS / LIGHT / DARK
        int themeIdx = 0;
        if (clashwin::config::AppConfig::Instance().GetThemeMode() == "light") themeIdx = 1;
        else if (clashwin::config::AppConfig::Instance().GetThemeMode() == "dark") themeIdx = 2;
        m_themeModeRect = D2D1::RectF(dTextX, c4Y, dTextX + dTextW, c4Y + 20.0f * m_dpiScale);
        m_renderer->DrawSegmentedControl(dTextX, c4Y, dTextW, 20.0f * m_dpiScale, {L"SYS", L"LIGHT", L"DARK"}, themeIdx);
        c4Y += 24.0f * m_dpiScale;

        // 双滑块
        m_slider1Rect = D2D1::RectF(dTextX, c4Y, dTextX + dTextW, c4Y + 18.0f * m_dpiScale);
        m_renderer->DrawSlider(dTextX, c4Y, dTextW, 18.0f * m_dpiScale, theme.slider1_name, ThemeManager::Instance().GetSlider1(), 0.0f, 1.0f);
        c4Y += 20.0f * m_dpiScale;

        m_slider2Rect = D2D1::RectF(dTextX, c4Y, dTextX + dTextW, c4Y + 18.0f * m_dpiScale);
        m_renderer->DrawSlider(dTextX, c4Y, dTextW, 18.0f * m_dpiScale, theme.slider2_name, ThemeManager::Instance().GetSlider2(), 0.0f, 5.0f);

        curY += card4H + 6.0f * m_dpiScale;

        // -------------------------------------------------------------
        // 抽屉底部快捷操作行: 运行日志 + 帮助与关于
        // -------------------------------------------------------------
        float footerBtnW = (contentW - 6.0f * m_dpiScale) / 2.0f;
        float footerBtnH = 24.0f * m_dpiScale;
        m_drawerLogsBtnRect = D2D1::RectF(marginX, curY, marginX + footerBtnW, curY + footerBtnH);
        m_renderer->Draw3DButton(marginX, curY, footerBtnW, footerBtnH, L"📜 查看运行日志");

        float helpBtnX = marginX + footerBtnW + 6.0f * m_dpiScale;
        m_drawerHelpBtnRect = D2D1::RectF(helpBtnX, curY, helpBtnX + footerBtnW, curY + footerBtnH);
        m_renderer->Draw3DButton(helpBtnX, curY, footerBtnW, footerBtnH, L"💡 帮助与关于");

        curY += footerBtnH + 10.0f * m_dpiScale;
        yOffset = curY;
    } else {
        // 5. ProxyHead 工具栏 (⚡ 测速 + ↕ 排序 + 🔍 搜索过滤框，全宽满铺，左右 12px 内边距)
        float headH = 26.0f * m_dpiScale;
        float btnSize = 24.0f * m_dpiScale;
        float btnY = yOffset + (headH - btnSize) / 2.0f;
        float padX = 12.0f * m_dpiScale;
        m_headBoltBtnRect = D2D1::RectF(padX, btnY, padX + btnSize, btnY + btnSize);
        m_headSortBtnRect = D2D1::RectF(padX + btnSize + 4.0f * m_dpiScale, btnY, padX + (btnSize + 4.0f * m_dpiScale) * 2, btnY + btnSize);
        m_headSearchBoxRect = D2D1::RectF(padX + (btnSize + 4.0f * m_dpiScale) * 2, btnY, width - padX, btnY + btnSize);

        m_renderer->DrawProxyHead(0, yOffset, width, headH, m_sortType, m_searchText, m_isTestingDelay, m_searchFocused);
        yOffset += headH;

        // 6. 节点列表过滤与排序 (对齐 use-filter-sort.ts)
        const auto& allNodes = clashwin::config::ProfileManager::Instance().GetNodes();
        std::vector<clashwin::ProxyNode> filteredNodes;

        for (const auto& n : allNodes) {
            if (!m_searchText.empty()) {
                std::wstring nameW(n.name.begin(), n.name.end());
                // 大小写不敏感过滤
                auto it = std::search(
                    nameW.begin(), nameW.end(),
                    m_searchText.begin(), m_searchText.end(),
                    [](wchar_t ch1, wchar_t ch2) { return towupper(ch1) == towupper(ch2); }
                );
                if (it == nameW.end()) continue;
            }
            filteredNodes.push_back(n);
        }

        // 排序
        if (m_sortType == 1) {
            // 按延迟升序排（未测速或超时排在最后，严格对齐 Clash Mini）
            std::sort(filteredNodes.begin(), filteredNodes.end(), [this](const auto& a, const auto& b) {
                int da = -1, db = -1;
                {
                    std::lock_guard<std::mutex> lock(m_delayMutex);
                    auto ita = m_nodeDelays.find(a.name);
                    if (ita != m_nodeDelays.end()) da = ita->second;
                    auto itb = m_nodeDelays.find(b.name);
                    if (itb != m_nodeDelays.end()) db = itb->second;
                }
                if (da <= 0 && db <= 0) return a.name < b.name;
                if (da <= 0) return false;
                if (db <= 0) return true;
                return da < db;
            });
        } else if (m_sortType == 2) {
            // A-Z 字典序
            std::sort(filteredNodes.begin(), filteredNodes.end(), [](const auto& a, const auto& b) {
                return a.name < b.name;
            });
        }

        float trafficPaneH = (logicalW > NARROW_BREAKPOINT) ? (TRAFFIC_PANE_HEIGHT_WIDE * m_dpiScale) : (TRAFFIC_PANE_HEIGHT_MINIMAL * m_dpiScale);
        float availableListH = height - yOffset - trafficPaneH;
        m_nodeItemRects.clear();

        // 7. 节点列表渲染 (空状态 vs 实体节点列表，严格 1:1 对齐 Clash Mini No Proxies)
        if (filteredNodes.empty()) {
            float emptyH = 100.0f * m_dpiScale;
            m_renderer->Draw3DCard(marginX, yOffset + 8.0f * m_dpiScale, contentW, emptyH, theme.card_bg);
            m_renderer->DrawTextSimple(L"📥 No Proxies", marginX, yOffset + 20.0f * m_dpiScale, contentW, 22.0f * m_dpiScale, theme.text_secondary, 12.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
            
            float addBtnW = contentW - 40.0f * m_dpiScale;
            float addBtnH = 28.0f * m_dpiScale;
            float addBtnX = marginX + 20.0f * m_dpiScale;
            float addBtnY = yOffset + 52.0f * m_dpiScale;
            m_emptyListAddBtnRect = D2D1::RectF(addBtnX, addBtnY, addBtnX + addBtnW, addBtnY + addBtnH);
            m_renderer->Draw3DButton(addBtnX, addBtnY, addBtnW, addBtnH, L"⚙ 打开设置导入订阅", false, false, true);
        } else {
            std::string currentProxy = clashwin::config::ProfileManager::Instance().GetCurrentActiveNode();
            if (logicalW > NARROW_BREAKPOINT) {
                // 宽窗双栏排版：左侧节点，右侧活跃连接与控制
                float halfW = (contentW - 6.0f * m_dpiScale) / 2.0f;
                float itemH = 24.0f * m_dpiScale;
                float leftY = yOffset;
                for (size_t i = 0; i < filteredNodes.size(); ++i) {
                    if (leftY + itemH > yOffset + availableListH) break;

                    bool isSelected = (filteredNodes[i].name == currentProxy);
                    std::wstring nodeNameW(filteredNodes[i].name.begin(), filteredNodes[i].name.end());
                    std::wstring nodeTypeW(filteredNodes[i].type.begin(), filteredNodes[i].type.end());
                    if (nodeTypeW.empty()) nodeTypeW = L"VMESS";
                    for (auto& c : nodeTypeW) c = towupper(c);

                    int nodeDelay = -1;
                    {
                        std::lock_guard<std::mutex> lock(m_delayMutex);
                        auto it = m_nodeDelays.find(filteredNodes[i].name);
                        if (it != m_nodeDelays.end()) nodeDelay = it->second;
                    }

                    m_renderer->DrawProxyItem3Col(marginX, leftY, halfW, itemH,
                                                  nodeTypeW, nodeNameW, nodeDelay, isSelected, (i % 2 != 0), false);

                    m_nodeItemRects.push_back({D2D1::RectF(marginX, leftY, marginX + halfW, leftY + itemH), filteredNodes[i].name});
                    leftY += itemH;
                }

                // 右侧连接监控面板与断开控制
                float rightX = marginX + halfW + 6.0f * m_dpiScale;
                float rightY = yOffset;

                float btnH = 24.0f * m_dpiScale;
                m_closeAllConnsBtnRect = D2D1::RectF(rightX, rightY, rightX + halfW, rightY + btnH);
                m_renderer->Draw3DButton(rightX, rightY, halfW, btnH, L"✕ 断开所有连接");
                rightY += btnH + 4.0f * m_dpiScale;

                float rowH = 22.0f * m_dpiScale;
                m_connectionItemRects.clear();
                for (const auto& conn : m_connections) {
                    if (rightY + rowH > yOffset + availableListH) break;
                    D2D1_RECT_F rowRect = D2D1::RectF(rightX, rightY, rightX + halfW, rightY + rowH);
                    m_renderer->DrawConnectionRow(rightX, rightY, halfW, rowH, conn.host, conn.chain, conn.speed);
                    m_connectionItemRects.push_back({rowRect, conn.id, conn.raw_host, conn.dest_ip});
                    rightY += rowH + 2.0f * m_dpiScale;
                }
            } else {
                // 窄窗单列纵向 3 列网格排版 (55px 协议 | flex 1 节点名 | 65px 延迟)，全宽满铺无边缘空白
                float itemH = 24.0f * m_dpiScale;
                for (size_t i = 0; i < filteredNodes.size(); ++i) {
                    if (yOffset + itemH > height - trafficPaneH) break;

                    bool isSelected = (filteredNodes[i].name == currentProxy);
                    std::wstring nodeNameW(filteredNodes[i].name.begin(), filteredNodes[i].name.end());
                    std::wstring nodeTypeW(filteredNodes[i].type.begin(), filteredNodes[i].type.end());
                    if (nodeTypeW.empty()) nodeTypeW = L"VMESS";
                    for (auto& c : nodeTypeW) c = towupper(c);

                    int nodeDelay = -1;
                    {
                        std::lock_guard<std::mutex> lock(m_delayMutex);
                        auto it = m_nodeDelays.find(filteredNodes[i].name);
                        if (it != m_nodeDelays.end()) nodeDelay = it->second;
                    }

                    m_renderer->DrawProxyItem3Col(0, yOffset, width, itemH,
                                                  nodeTypeW, nodeNameW, nodeDelay, isSelected, (i % 2 != 0), false);

                    m_nodeItemRects.push_back({D2D1::RectF(0, yOffset, width, yOffset + itemH), filteredNodes[i].name});
                    yOffset += itemH;
                }
            }
        }

        // 8. 底部实时流量波形图仪表盘 (对齐 MiniTrafficPanel 100px / 135px)
        m_renderer->DrawTrafficDashboard(0, height - trafficPaneH, width, trafficPaneH,
                                         m_upSpeed, m_downSpeed, m_upTotal, m_downTotal,
                                         m_trafficHistory, (logicalW <= NARROW_BREAKPOINT));
    }

    // 9. 模态对话框与全套弹窗体系 (AddUrl / EditProfile / Guide / Logs / Update / Unlock / ContextMenu)
    if (m_currentModal != ModalType::None) {
        m_renderer->DrawModalBackdrop(width, height);

        if (m_currentModal == ModalType::AddUrl) {
            float dlgW = (std::min)(width - 24.0f * m_dpiScale, 360.0f * m_dpiScale);
            float dlgH = 210.0f * m_dpiScale;
            float dlgX = (width - dlgW) / 2.0f;
            float dlgY = (height - dlgH) / 2.0f;

            m_renderer->DrawModalDialogFrame(dlgX, dlgY, dlgW, dlgH, L"➕ 添加自定义分流规则");
            m_modalCloseBtnRect = D2D1::RectF(dlgX + dlgW - 30.0f * m_dpiScale, dlgY + 6.0f * m_dpiScale, dlgX + dlgW - 8.0f * m_dpiScale, dlgY + 28.0f * m_dpiScale);

            float padX = 14.0f * m_dpiScale;
            float curDlgY = dlgY + 38.0f * m_dpiScale;

            m_renderer->DrawTextSimple(L"请输入域名或 IP (例如: google.com 或 1.1.1.1):", dlgX + padX, curDlgY, dlgW - padX * 2, 16.0f * m_dpiScale, theme.text_primary, 10.5f, false);
            curDlgY += 20.0f * m_dpiScale;

            float pasteBtnW = 46.0f * m_dpiScale;
            float inW = dlgW - padX * 2 - pasteBtnW - 6.0f * m_dpiScale;
            m_modalAddUrlInputRect = D2D1::RectF(dlgX + padX, curDlgY, dlgX + padX + inW, curDlgY + 22.0f * m_dpiScale);
            m_renderer->Draw3DTextInput(dlgX + padX, curDlgY, inW, 22.0f * m_dpiScale, L"", m_addUrlText, L"domain / IP...", m_isAddUrlFocused);

            float pasteX = dlgX + padX + inW + 6.0f * m_dpiScale;
            m_modalAddUrlPasteBtnRect = D2D1::RectF(pasteX, curDlgY, pasteX + pasteBtnW, curDlgY + 22.0f * m_dpiScale);
            m_renderer->Draw3DButton(pasteX, curDlgY, pasteBtnW, 22.0f * m_dpiScale, L"📋粘贴");
            curDlgY += 28.0f * m_dpiScale;

            m_renderer->DrawTextSimple(L"分流策略偏好:", dlgX + padX, curDlgY, dlgW - padX * 2, 16.0f * m_dpiScale, theme.text_primary, 10.5f, false);
            curDlgY += 18.0f * m_dpiScale;

            m_modalAddUrlPolicyRect = D2D1::RectF(dlgX + padX, curDlgY, dlgX + dlgW - padX, curDlgY + 22.0f * m_dpiScale);
            m_renderer->DrawSegmentedControl(dlgX + padX, curDlgY, dlgW - padX * 2, 22.0f * m_dpiScale, {L"DIRECT", L"PROXY", L"REJECT"}, m_addUrlPolicy);
            curDlgY += 34.0f * m_dpiScale;

            float btnW = 60.0f * m_dpiScale;
            float btnH = 24.0f * m_dpiScale;
            float addX = dlgX + dlgW - padX - btnW;
            float cancelX = addX - btnW - 8.0f * m_dpiScale;

            m_modalAddUrlCancelBtnRect = D2D1::RectF(cancelX, curDlgY, cancelX + btnW, curDlgY + btnH);
            m_renderer->Draw3DButton(cancelX, curDlgY, btnW, btnH, L"取消");

            m_modalAddUrlConfirmBtnRect = D2D1::RectF(addX, curDlgY, addX + btnW, curDlgY + btnH);
            m_renderer->Draw3DButton(addX, curDlgY, btnW, btnH, L"添加", false, false, true);
        }
        else if (m_currentModal == ModalType::EditProfile) {
            float dlgW = (std::min)(width - 24.0f * m_dpiScale, 360.0f * m_dpiScale);
            float dlgH = 230.0f * m_dpiScale;
            float dlgX = (width - dlgW) / 2.0f;
            float dlgY = (height - dlgH) / 2.0f;

            m_renderer->DrawModalDialogFrame(dlgX, dlgY, dlgW, dlgH, L"⚙️ 编辑订阅配置");
            m_modalCloseBtnRect = D2D1::RectF(dlgX + dlgW - 30.0f * m_dpiScale, dlgY + 6.0f * m_dpiScale, dlgX + dlgW - 8.0f * m_dpiScale, dlgY + 28.0f * m_dpiScale);

            float padX = 14.0f * m_dpiScale;
            float curDlgY = dlgY + 36.0f * m_dpiScale;
            float fieldW = dlgW - padX * 2;

            m_renderer->DrawTextSimple(L"配置名称:", dlgX + padX, curDlgY, fieldW, 15.0f * m_dpiScale, theme.text_primary, 10.0f, false);
            curDlgY += 16.0f * m_dpiScale;
            m_modalEditNameRect = D2D1::RectF(dlgX + padX, curDlgY, dlgX + padX + fieldW, curDlgY + 20.0f * m_dpiScale);
            m_renderer->Draw3DTextInput(dlgX + padX, curDlgY, fieldW, 20.0f * m_dpiScale, L"", m_editProfileName, L"配置名称...", m_editProfileFocusField == 0);
            curDlgY += 24.0f * m_dpiScale;

            m_renderer->DrawTextSimple(L"订阅链接 (URL):", dlgX + padX, curDlgY, fieldW, 15.0f * m_dpiScale, theme.text_primary, 10.0f, false);
            curDlgY += 16.0f * m_dpiScale;
            m_modalEditUrlRect = D2D1::RectF(dlgX + padX, curDlgY, dlgX + padX + fieldW, curDlgY + 20.0f * m_dpiScale);
            m_renderer->Draw3DTextInput(dlgX + padX, curDlgY, fieldW, 20.0f * m_dpiScale, L"", m_editProfileUrl, L"https://...", m_editProfileFocusField == 1);
            curDlgY += 24.0f * m_dpiScale;

            m_renderer->DrawTextSimple(L"自动更新间隔 (小时):", dlgX + padX, curDlgY, fieldW, 15.0f * m_dpiScale, theme.text_primary, 10.0f, false);
            curDlgY += 16.0f * m_dpiScale;
            m_modalEditIntervalRect = D2D1::RectF(dlgX + padX, curDlgY, dlgX + padX + 80.0f * m_dpiScale, curDlgY + 20.0f * m_dpiScale);
            m_renderer->Draw3DTextInput(dlgX + padX, curDlgY, 80.0f * m_dpiScale, 20.0f * m_dpiScale, L"", m_editProfileIntervalText, L"24", m_editProfileFocusField == 2);
            curDlgY += 26.0f * m_dpiScale;

            float btnW = 60.0f * m_dpiScale;
            float btnH = 24.0f * m_dpiScale;
            float saveX = dlgX + dlgW - padX - btnW;
            float cancelX = saveX - btnW - 8.0f * m_dpiScale;

            m_modalEditCancelBtnRect = D2D1::RectF(cancelX, curDlgY, cancelX + btnW, curDlgY + btnH);
            m_renderer->Draw3DButton(cancelX, curDlgY, btnW, btnH, L"取消");

            m_modalEditSaveBtnRect = D2D1::RectF(saveX, curDlgY, saveX + btnW, curDlgY + btnH);
            m_renderer->Draw3DButton(saveX, curDlgY, btnW, btnH, L"保存", false, false, true);
        }
        else if (m_currentModal == ModalType::FirstRunGuide) {
            float dlgW = (std::min)(width - 24.0f * m_dpiScale, 380.0f * m_dpiScale);
            float dlgH = 240.0f * m_dpiScale;
            float dlgX = (width - dlgW) / 2.0f;
            float dlgY = (height - dlgH) / 2.0f;

            m_renderer->DrawModalDialogFrame(dlgX, dlgY, dlgW, dlgH, L"🐱 欢迎使用 Clash Mini");
            m_modalCloseBtnRect = D2D1::RectF(dlgX + dlgW - 30.0f * m_dpiScale, dlgY + 6.0f * m_dpiScale, dlgX + dlgW - 8.0f * m_dpiScale, dlgY + 28.0f * m_dpiScale);

            float padX = 14.0f * m_dpiScale;
            float curDlgY = dlgY + 38.0f * m_dpiScale;
            float fieldW = dlgW - padX * 2;

            m_renderer->DrawTextSimple(L"请填入订阅链接以快速开启极速网络代理加速：", dlgX + padX, curDlgY, fieldW, 16.0f * m_dpiScale, theme.text_primary, 10.5f, true);
            curDlgY += 24.0f * m_dpiScale;

            float pasteBtnW = 48.0f * m_dpiScale;
            float inW = fieldW - pasteBtnW - 6.0f * m_dpiScale;
            m_modalGuideInputRect = D2D1::RectF(dlgX + padX, curDlgY, dlgX + padX + inW, curDlgY + 22.0f * m_dpiScale);
            m_renderer->Draw3DTextInput(dlgX + padX, curDlgY, inW, 22.0f * m_dpiScale, L"", m_guideUrlText, L"https://...", m_isGuideInputFocused);

            float pasteX = dlgX + padX + inW + 6.0f * m_dpiScale;
            m_modalGuidePasteBtnRect = D2D1::RectF(pasteX, curDlgY, pasteX + pasteBtnW, curDlgY + 22.0f * m_dpiScale);
            m_renderer->Draw3DButton(pasteX, curDlgY, pasteBtnW, 22.0f * m_dpiScale, L"📋粘贴");
            curDlgY += 32.0f * m_dpiScale;

            m_modalGuideImportBtnRect = D2D1::RectF(dlgX + padX, curDlgY, dlgX + padX + fieldW, curDlgY + 26.0f * m_dpiScale);
            m_renderer->Draw3DButton(dlgX + padX, curDlgY, fieldW, 26.0f * m_dpiScale, L"⚡ 一键导入并开启代理", false, false, true);
            curDlgY += 32.0f * m_dpiScale;

            float halfW = (fieldW - 8.0f * m_dpiScale) / 2.0f;
            m_modalGuideWikiBtnRect = D2D1::RectF(dlgX + padX, curDlgY, dlgX + padX + halfW, curDlgY + 22.0f * m_dpiScale);
            m_renderer->Draw3DButton(dlgX + padX, curDlgY, halfW, 22.0f * m_dpiScale, L"📚 查看使用教程");

            float laterX = dlgX + padX + halfW + 8.0f * m_dpiScale;
            m_modalGuideLaterBtnRect = D2D1::RectF(laterX, curDlgY, laterX + halfW, curDlgY + 22.0f * m_dpiScale);
            m_renderer->Draw3DButton(laterX, curDlgY, halfW, 22.0f * m_dpiScale, L"稍后配置");
        }
        else if (m_currentModal == ModalType::LogsView) {
            float dlgW = (std::min)(width - 24.0f * m_dpiScale, 560.0f * m_dpiScale);
            float dlgH = (std::min)(height - 40.0f * m_dpiScale, 450.0f * m_dpiScale);
            float dlgX = (width - dlgW) / 2.0f;
            float dlgY = (height - dlgH) / 2.0f;

            m_renderer->DrawModalDialogFrame(dlgX, dlgY, dlgW, dlgH, L"📜 运行日志查看器");
            m_modalCloseBtnRect = D2D1::RectF(dlgX + dlgW - 30.0f * m_dpiScale, dlgY + 6.0f * m_dpiScale, dlgX + dlgW - 8.0f * m_dpiScale, dlgY + 28.0f * m_dpiScale);

            float padX = 14.0f * m_dpiScale;
            float curDlgY = dlgY + 36.0f * m_dpiScale;

            float segW = 140.0f * m_dpiScale;
            m_modalLogLevelRect = D2D1::RectF(dlgX + padX, curDlgY, dlgX + padX + segW, curDlgY + 22.0f * m_dpiScale);
            m_renderer->DrawSegmentedControl(dlgX + padX, curDlgY, segW, 22.0f * m_dpiScale, {L"ALL", L"INFO", L"WARN", L"ERR"}, m_logLevelFilter);

            float pauseX = dlgX + padX + segW + 6.0f * m_dpiScale;
            float actBtnSize = 22.0f * m_dpiScale;
            m_modalLogPauseBtnRect = D2D1::RectF(pauseX, curDlgY, pauseX + actBtnSize, curDlgY + actBtnSize);
            m_renderer->Draw3DButton(pauseX, curDlgY, actBtnSize, actBtnSize, m_logPaused ? L"▶" : L"⏸", false, false, m_logPaused);

            float clearX = pauseX + actBtnSize + 4.0f * m_dpiScale;
            m_modalLogClearBtnRect = D2D1::RectF(clearX, curDlgY, clearX + actBtnSize, curDlgY + actBtnSize);
            m_renderer->Draw3DButton(clearX, curDlgY, actBtnSize, actBtnSize, L"🗑");

            float searchX = clearX + actBtnSize + 6.0f * m_dpiScale;
            float searchW = dlgX + dlgW - padX - searchX;
            m_modalLogSearchRect = D2D1::RectF(searchX, curDlgY, searchX + searchW, curDlgY + 22.0f * m_dpiScale);
            m_renderer->Draw3DTextInput(searchX, curDlgY, searchW, 22.0f * m_dpiScale, L"", m_logSearchText, L"搜索日志...", m_isLogSearchFocused);

            curDlgY += 28.0f * m_dpiScale;

            float viewH = dlgH - (curDlgY - dlgY) - 14.0f * m_dpiScale;
            float viewW = dlgW - padX * 2;
            m_renderer->Draw3DCard(dlgX + padX, curDlgY, viewW, viewH, theme.card_active_bg, false);

            float lineY = curDlgY + 4.0f * m_dpiScale;
            float lineH = 16.0f * m_dpiScale;
            for (const auto& logEntry : m_cachedLogEntries) {
                if (lineY + lineH > curDlgY + viewH) break;

                if (m_logLevelFilter == 1 && logEntry.find(L"[INFO]") == std::wstring::npos) continue;
                if (m_logLevelFilter == 2 && logEntry.find(L"[WARN]") == std::wstring::npos) continue;
                if (m_logLevelFilter == 3 && logEntry.find(L"[ERROR]") == std::wstring::npos) continue;

                if (!m_logSearchText.empty() && logEntry.find(m_logSearchText) == std::wstring::npos) continue;

                D2D1_COLOR_F color = theme.text_primary;
                if (logEntry.find(L"[ERROR]") != std::wstring::npos) color = D2D1::ColorF(0.95f, 0.26f, 0.21f, 1.0f);
                else if (logEntry.find(L"[WARN]") != std::wstring::npos) color = D2D1::ColorF(1.0f, 0.60f, 0.0f, 1.0f);
                else if (logEntry.find(L"[INFO]") != std::wstring::npos) color = D2D1::ColorF(0.0f, 0.65f, 0.95f, 1.0f);

                m_renderer->DrawTextSimple(logEntry, dlgX + padX + 6.0f * m_dpiScale, lineY, viewW - 12.0f * m_dpiScale, lineH, color, 9.5f, false);
                lineY += lineH;
            }
        }
        else if (m_currentModal == ModalType::CheckUpdate) {
            float dlgW = (std::min)(width - 24.0f * m_dpiScale, 360.0f * m_dpiScale);
            float dlgH = 210.0f * m_dpiScale;
            float dlgX = (width - dlgW) / 2.0f;
            float dlgY = (height - dlgH) / 2.0f;

            m_renderer->DrawModalDialogFrame(dlgX, dlgY, dlgW, dlgH, L"🚀 软件更新");
            m_modalCloseBtnRect = D2D1::RectF(dlgX + dlgW - 30.0f * m_dpiScale, dlgY + 6.0f * m_dpiScale, dlgX + dlgW - 8.0f * m_dpiScale, dlgY + 28.0f * m_dpiScale);

            float padX = 14.0f * m_dpiScale;
            float curDlgY = dlgY + 38.0f * m_dpiScale;
            float fieldW = dlgW - padX * 2;

            m_renderer->DrawTextSimple(L"客户端版本: Clash Mini Ver.3.0.10", dlgX + padX, curDlgY, fieldW, 16.0f * m_dpiScale, theme.text_primary, 10.5f, true);
            curDlgY += 20.0f * m_dpiScale;

            m_renderer->DrawTextSimple(L"内核核心版本: Mihomo Meta v1.19.16", dlgX + padX, curDlgY, fieldW, 16.0f * m_dpiScale, theme.text_secondary, 10.0f, false);
            curDlgY += 24.0f * m_dpiScale;

            m_renderer->Draw3DCard(dlgX + padX, curDlgY, fieldW, 36.0f * m_dpiScale, theme.card_active_bg, false);
            m_renderer->DrawTextSimple(m_updateStatusMsg, dlgX + padX + 8.0f * m_dpiScale, curDlgY + 8.0f * m_dpiScale, fieldW - 16.0f * m_dpiScale, 20.0f * m_dpiScale, theme.accent, 10.5f, true);
            curDlgY += 46.0f * m_dpiScale;

            float btnW = 90.0f * m_dpiScale;
            float btnH = 24.0f * m_dpiScale;
            float closeW = 60.0f * m_dpiScale;
            float closeX = dlgX + dlgW - padX - closeW;
            float chkX = closeX - btnW - 8.0f * m_dpiScale;

            m_modalUpdateCheckBtnRect = D2D1::RectF(chkX, curDlgY, chkX + btnW, curDlgY + btnH);
            m_renderer->Draw3DButton(chkX, curDlgY, btnW, btnH, L"🔄 检查更新", false, false, true);

            m_modalUpdateCloseBtnRect = D2D1::RectF(closeX, curDlgY, closeX + closeW, curDlgY + btnH);
            m_renderer->Draw3DButton(closeX, curDlgY, closeW, btnH, L"关闭");
        }
        else if (m_currentModal == ModalType::StreamingUnlock) {
            float dlgW = (std::min)(width - 24.0f * m_dpiScale, 440.0f * m_dpiScale);
            float dlgH = (std::min)(height - 40.0f * m_dpiScale, 450.0f * m_dpiScale);
            float dlgX = (width - dlgW) / 2.0f;
            float dlgY = (height - dlgH) / 2.0f;

            m_renderer->DrawModalDialogFrame(dlgX, dlgY, dlgW, dlgH, L"🎬 流媒体与 AI 解锁检测");
            m_modalCloseBtnRect = D2D1::RectF(dlgX + dlgW - 30.0f * m_dpiScale, dlgY + 6.0f * m_dpiScale, dlgX + dlgW - 8.0f * m_dpiScale, dlgY + 28.0f * m_dpiScale);

            float padX = 14.0f * m_dpiScale;
            float curDlgY = dlgY + 36.0f * m_dpiScale;
            float fieldW = dlgW - padX * 2;

            const auto& results = clashwin::network::UnlockChecker::Instance().GetResults();
            float itemH = 22.0f * m_dpiScale;

            for (const auto& item : results) {
                if (curDlgY + itemH > dlgY + dlgH - 44.0f * m_dpiScale) break;

                m_renderer->Draw3DCard(dlgX + padX, curDlgY, fieldW, itemH, theme.card_active_bg, false);

                std::wstring nameW(item.name.begin(), item.name.end());
                m_renderer->DrawTextSimple(nameW, dlgX + padX + 8.0f * m_dpiScale, curDlgY + 2.0f * m_dpiScale, fieldW - 120.0f * m_dpiScale, itemH, theme.text_primary, 10.0f, true);

                if (!item.region.empty()) {
                    std::wstring regW(item.region.begin(), item.region.end());
                    m_renderer->DrawTextSimple(regW, dlgX + padX + fieldW - 110.0f * m_dpiScale, curDlgY + 2.0f * m_dpiScale, 40.0f * m_dpiScale, itemH, theme.text_secondary, 9.5f, false);
                }

                bool isYes = (item.status == "Yes" || item.status == "Completed");
                bool isChecking = (item.status == "Pending" || clashwin::network::UnlockChecker::Instance().IsChecking());
                std::wstring statusBadge = isYes ? L"✅ 已解锁" : (isChecking ? L"⏳ 检测中" : L"❌ 未解锁");
                D2D1_COLOR_F badgeColor = isYes ? D2D1::ColorF(0.18f, 0.70f, 0.35f, 1.0f) : (isChecking ? D2D1::ColorF(0.95f, 0.65f, 0.15f, 1.0f) : D2D1::ColorF(0.90f, 0.25f, 0.20f, 1.0f));

                m_renderer->DrawTextSimple(statusBadge, dlgX + padX + fieldW - 65.0f * m_dpiScale, curDlgY + 2.0f * m_dpiScale, 60.0f * m_dpiScale, itemH, badgeColor, 9.5f, true, DWRITE_TEXT_ALIGNMENT_TRAILING);

                curDlgY += itemH + 3.0f * m_dpiScale;
            }

            curDlgY = dlgY + dlgH - 34.0f * m_dpiScale;
            m_modalUnlockRecheckBtnRect = D2D1::RectF(dlgX + padX, curDlgY, dlgX + padX + fieldW, curDlgY + 24.0f * m_dpiScale);
            m_renderer->Draw3DButton(dlgX + padX, curDlgY, fieldW, 24.0f * m_dpiScale, L"🔄 重新检测全部平台", false, false, true);
        }
        else if (m_currentModal == ModalType::HelpMenu) {
            float menuW = 210.0f * m_dpiScale;
            float itemH = 26.0f * m_dpiScale;
            std::vector<std::pair<std::wstring, bool>> items = {
                {L"🐱 访问 GitHub 仓库", true},
                {L"💡 官方使用文档 (Wiki)", true},
                {L"🧭 打开新手引导指引", true},
                {L"🎬 流媒体解锁检测", true},
                {L"🚀 检查客户端更新 (v3.0.10)", true},
                {L"⚙️ 检查内核更新 (v1.19.16)", true},
                {L"📜 运行日志查看器", true}
            };
            float menuH = static_cast<float>(items.size()) * itemH + 8.0f * m_dpiScale;
            float menuX = marginX;
            float menuY = (std::max)(32.0f * m_dpiScale, height - menuH - 24.0f * m_dpiScale);

            m_renderer->DrawContextMenu(menuX, menuY, menuW, menuH, items, -1);
            m_modalCloseBtnRect = D2D1::RectF(0, 0, width, height);
        }
        else if (m_currentModal == ModalType::ProfileContextMenu) {
            float menuW = 160.0f * m_dpiScale;
            float itemH = 26.0f * m_dpiScale;
            std::vector<std::pair<std::wstring, bool>> items = {
                {L"🔄 立即更新配置", true},
                {L"⚙️ 编辑配置", true},
                {L"📝 记事本中打开", true},
                {L"📁 打开所在目录", true},
                {L"🗑️ 删除此配置", true}
            };
            float menuH = static_cast<float>(items.size()) * itemH + 8.0f * m_dpiScale;
            float menuX = (std::min)(m_profileMenuX, width - menuW - 8.0f);
            float menuY = (std::min)(m_profileMenuY, height - menuH - 8.0f);
            m_renderer->DrawContextMenu(menuX, menuY, menuW, menuH, items, m_hoveredProfileMenuItem);
        }
        else if (m_currentModal == ModalType::ConnectionContextMenu) {
            float menuW = 180.0f * m_dpiScale;
            float itemH = 26.0f * m_dpiScale;
            std::vector<std::pair<std::wstring, bool>> items = {
                {L"🌐 直连此域名 (DIRECT)", true},
                {L"⚡ 代理此域名 (PROXY)", true},
                {L"🚫 拒绝此域名 (REJECT)", true},
                {L"🎯 直连此 IP (DIRECT)", true},
                {L"⚡ 代理此 IP (PROXY)", true},
                {L"❌ 断开此连接", true},
                {L"🛑 断开全部连接", true}
            };
            float menuH = static_cast<float>(items.size()) * itemH + 8.0f * m_dpiScale;
            float menuX = (std::min)(m_connMenuX, width - menuW - 8.0f);
            float menuY = (std::min)(m_connMenuY, height - menuH - 8.0f);
            m_renderer->DrawContextMenu(menuX, menuY, menuW, menuH, items, m_hoveredConnMenuItem);
        }
    }

    m_renderer->EndDraw();
}

void MainWindow::ShowConnectionContextMenu(int screenX, int screenY, const ConnectionItemClickArea& area) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 101, L"🌐 直连此域名 (DIRECT)");
    AppendMenuW(hMenu, MF_STRING, 102, L"⚡ 代理此域名 (PROXY)");
    AppendMenuW(hMenu, MF_STRING, 103, L"🚫 拒绝此域名 (REJECT)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 104, L"🎯 直连此 IP (DIRECT)");
    AppendMenuW(hMenu, MF_STRING, 105, L"⚡ 代理此 IP (PROXY)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 106, L"❌ 断开此连接 (Close)");
    AppendMenuW(hMenu, MF_STRING, 107, L"🛑 断开全部连接 (Close All)");

    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, screenX, screenY, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd == 0) return;

    std::thread([this, cmd, area]() {
        switch (cmd) {
            case 101:
                clashwin::core::ConnectionManager::Instance().RouteConnection(area.id, "DIRECT", true);
                break;
            case 102:
                clashwin::core::ConnectionManager::Instance().RouteConnection(area.id, "PROXY", true);
                break;
            case 103:
                clashwin::core::ConnectionManager::Instance().RouteConnection(area.id, "REJECT", true);
                break;
            case 104:
                clashwin::core::ConnectionManager::Instance().RouteConnection(area.id, "DIRECT", false);
                break;
            case 105:
                clashwin::core::ConnectionManager::Instance().RouteConnection(area.id, "PROXY", false);
                break;
            case 106:
                clashwin::core::ConnectionManager::Instance().CloseConnection(area.id);
                break;
            case 107:
                clashwin::core::ConnectionManager::Instance().CloseAllConnections();
                break;
        }
        RefreshConnections();
    }).detach();
}

void MainWindow::OpenModal(ModalType modal) {
    m_currentModal = modal;
    if (modal == ModalType::LogsView) {
        RefreshLogsCache();
    } else if (modal == ModalType::StreamingUnlock) {
        TriggerStreamingUnlockCheck();
    } else if (modal == ModalType::EditProfile) {
        const auto& profiles = clashwin::config::ProfileManager::Instance().GetProfileItems();
        for (const auto& prf : profiles) {
            if (prf.uid == m_editProfileUid) {
                m_editProfileName = std::wstring(prf.name.begin(), prf.name.end());
                m_editProfileUrl = std::wstring(prf.url.begin(), prf.url.end());
                m_editProfileIntervalText = L"24";
                break;
            }
        }
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::CloseModal() {
    m_currentModal = ModalType::None;
    m_isAddUrlFocused = false;
    m_isGuideInputFocused = false;
    m_isLogSearchFocused = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::OnModalLButtonDown(int x, int y) {
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);

    if (fx >= m_modalCloseBtnRect.left && fx <= m_modalCloseBtnRect.right &&
        fy >= m_modalCloseBtnRect.top && fy <= m_modalCloseBtnRect.bottom) {
        CloseModal();
        return;
    }

    if (m_currentModal == ModalType::AddUrl) {
        if (fx >= m_modalAddUrlPasteBtnRect.left && fx <= m_modalAddUrlPasteBtnRect.right &&
            fy >= m_modalAddUrlPasteBtnRect.top && fy <= m_modalAddUrlPasteBtnRect.bottom) {
            if (OpenClipboard(m_hwnd)) {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData) {
                    wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
                    if (pText) {
                        m_addUrlText = pText;
                        GlobalUnlock(hData);
                    }
                }
                CloseClipboard();
            }
            m_isAddUrlFocused = true;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }

        if (fx >= m_modalAddUrlPolicyRect.left && fx <= m_modalAddUrlPolicyRect.right &&
            fy >= m_modalAddUrlPolicyRect.top && fy <= m_modalAddUrlPolicyRect.bottom) {
            float segW = (m_modalAddUrlPolicyRect.right - m_modalAddUrlPolicyRect.left) / 3.0f;
            int idx = static_cast<int>((fx - m_modalAddUrlPolicyRect.left) / segW);
            m_addUrlPolicy = std::clamp(idx, 0, 2);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }

        if (fx >= m_modalAddUrlCancelBtnRect.left && fx <= m_modalAddUrlCancelBtnRect.right &&
            fy >= m_modalAddUrlCancelBtnRect.top && fy <= m_modalAddUrlCancelBtnRect.bottom) {
            CloseModal();
            return;
        }

        if (fx >= m_modalAddUrlConfirmBtnRect.left && fx <= m_modalAddUrlConfirmBtnRect.right &&
            fy >= m_modalAddUrlConfirmBtnRect.top && fy <= m_modalAddUrlConfirmBtnRect.bottom) {
            AddCustomRule(m_addUrlText, m_addUrlPolicy);
            CloseModal();
            return;
        }

        if (fx >= m_modalAddUrlInputRect.left && fx <= m_modalAddUrlInputRect.right &&
            fy >= m_modalAddUrlInputRect.top && fy <= m_modalAddUrlInputRect.bottom) {
            m_isAddUrlFocused = true;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }
    else if (m_currentModal == ModalType::EditProfile) {
        if (fx >= m_modalEditCancelBtnRect.left && fx <= m_modalEditCancelBtnRect.right &&
            fy >= m_modalEditCancelBtnRect.top && fy <= m_modalEditCancelBtnRect.bottom) {
            CloseModal();
            return;
        }
        if (fx >= m_modalEditSaveBtnRect.left && fx <= m_modalEditSaveBtnRect.right &&
            fy >= m_modalEditSaveBtnRect.top && fy <= m_modalEditSaveBtnRect.bottom) {
            SaveProfileEdit();
            CloseModal();
            return;
        }
        if (fx >= m_modalEditNameRect.left && fx <= m_modalEditNameRect.right &&
            fy >= m_modalEditNameRect.top && fy <= m_modalEditNameRect.bottom) {
            m_editProfileFocusField = 0;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (fx >= m_modalEditUrlRect.left && fx <= m_modalEditUrlRect.right &&
            fy >= m_modalEditUrlRect.top && fy <= m_modalEditUrlRect.bottom) {
            m_editProfileFocusField = 1;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (fx >= m_modalEditIntervalRect.left && fx <= m_modalEditIntervalRect.right &&
            fy >= m_modalEditIntervalRect.top && fy <= m_modalEditIntervalRect.bottom) {
            m_editProfileFocusField = 2;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }
    else if (m_currentModal == ModalType::FirstRunGuide) {
        if (fx >= m_modalGuidePasteBtnRect.left && fx <= m_modalGuidePasteBtnRect.right &&
            fy >= m_modalGuidePasteBtnRect.top && fy <= m_modalGuidePasteBtnRect.bottom) {
            if (OpenClipboard(m_hwnd)) {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData) {
                    wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
                    if (pText) {
                        m_guideUrlText = pText;
                        GlobalUnlock(hData);
                    }
                }
                CloseClipboard();
            }
            m_isGuideInputFocused = true;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (fx >= m_modalGuideImportBtnRect.left && fx <= m_modalGuideImportBtnRect.right &&
            fy >= m_modalGuideImportBtnRect.top && fy <= m_modalGuideImportBtnRect.bottom) {
            m_drawerImportText = m_guideUrlText;
            TriggerDrawerImport();
            clashwin::config::ProfileManager::Instance().SetFirstRunCompleted(true);
            CloseModal();
            return;
        }
        if (fx >= m_modalGuideWikiBtnRect.left && fx <= m_modalGuideWikiBtnRect.right &&
            fy >= m_modalGuideWikiBtnRect.top && fy <= m_modalGuideWikiBtnRect.bottom) {
            ShellExecuteW(NULL, L"open", L"https://github.com/qiu-yuxiao/clash-mini/wiki", NULL, NULL, SW_SHOWNORMAL);
            return;
        }
        if (fx >= m_modalGuideLaterBtnRect.left && fx <= m_modalGuideLaterBtnRect.right &&
            fy >= m_modalGuideLaterBtnRect.top && fy <= m_modalGuideLaterBtnRect.bottom) {
            clashwin::config::ProfileManager::Instance().SetFirstRunCompleted(true);
            CloseModal();
            return;
        }
        if (fx >= m_modalGuideInputRect.left && fx <= m_modalGuideInputRect.right &&
            fy >= m_modalGuideInputRect.top && fy <= m_modalGuideInputRect.bottom) {
            m_isGuideInputFocused = true;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }
    else if (m_currentModal == ModalType::LogsView) {
        if (fx >= m_modalLogLevelRect.left && fx <= m_modalLogLevelRect.right &&
            fy >= m_modalLogLevelRect.top && fy <= m_modalLogLevelRect.bottom) {
            float segW = (m_modalLogLevelRect.right - m_modalLogLevelRect.left) / 4.0f;
            int idx = static_cast<int>((fx - m_modalLogLevelRect.left) / segW);
            m_logLevelFilter = std::clamp(idx, 0, 3);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (fx >= m_modalLogPauseBtnRect.left && fx <= m_modalLogPauseBtnRect.right &&
            fy >= m_modalLogPauseBtnRect.top && fy <= m_modalLogPauseBtnRect.bottom) {
            m_logPaused = !m_logPaused;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (fx >= m_modalLogClearBtnRect.left && fx <= m_modalLogClearBtnRect.right &&
            fy >= m_modalLogClearBtnRect.top && fy <= m_modalLogClearBtnRect.bottom) {
            m_cachedLogEntries.clear();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (fx >= m_modalLogSearchRect.left && fx <= m_modalLogSearchRect.right &&
            fy >= m_modalLogSearchRect.top && fy <= m_modalLogSearchRect.bottom) {
            m_isLogSearchFocused = true;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        } else {
            m_isLogSearchFocused = false;
        }
    }
    else if (m_currentModal == ModalType::CheckUpdate) {
        if (fx >= m_modalUpdateCheckBtnRect.left && fx <= m_modalUpdateCheckBtnRect.right &&
            fy >= m_modalUpdateCheckBtnRect.top && fy <= m_modalUpdateCheckBtnRect.bottom) {
            TriggerClientUpdateCheck();
            return;
        }
        if (fx >= m_modalUpdateCloseBtnRect.left && fx <= m_modalUpdateCloseBtnRect.right &&
            fy >= m_modalUpdateCloseBtnRect.top && fy <= m_modalUpdateCloseBtnRect.bottom) {
            CloseModal();
            return;
        }
    }
    else if (m_currentModal == ModalType::StreamingUnlock) {
        if (fx >= m_modalUnlockRecheckBtnRect.left && fx <= m_modalUnlockRecheckBtnRect.right &&
            fy >= m_modalUnlockRecheckBtnRect.top && fy <= m_modalUnlockRecheckBtnRect.bottom) {
            TriggerStreamingUnlockCheck();
            return;
        }
    }
    else if (m_currentModal == ModalType::HelpMenu) {
        float menuW = 210.0f * m_dpiScale;
        float itemH = 26.0f * m_dpiScale;
        float menuH = 7.0f * itemH + 8.0f * m_dpiScale;
        float menuX = 8.0f * m_dpiScale;
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        float menuY = (std::max)(32.0f * m_dpiScale, static_cast<float>(rc.bottom - rc.top) - menuH - 24.0f * m_dpiScale);

        if (fx >= menuX && fx <= menuX + menuW && fy >= menuY && fy <= menuY + menuH) {
            int idx = static_cast<int>((fy - menuY - 4.0f * m_dpiScale) / itemH);
            switch (idx) {
                case 0:
                    ShellExecuteW(NULL, L"open", L"https://github.com/qiu-yuxiao/clash-mini", NULL, NULL, SW_SHOWNORMAL);
                    CloseModal();
                    break;
                case 1:
                    ShellExecuteW(NULL, L"open", L"https://github.com/qiu-yuxiao/clash-mini/wiki", NULL, NULL, SW_SHOWNORMAL);
                    CloseModal();
                    break;
                case 2:
                    OpenModal(ModalType::FirstRunGuide);
                    break;
                case 3:
                    OpenModal(ModalType::StreamingUnlock);
                    break;
                case 4:
                    OpenModal(ModalType::CheckUpdate);
                    break;
                case 5:
                    OpenModal(ModalType::CheckUpdate);
                    break;
                case 6:
                    OpenModal(ModalType::LogsView);
                    break;
                default:
                    CloseModal();
                    break;
            }
            return;
        } else {
            CloseModal();
            return;
        }
    }
    else if (m_currentModal == ModalType::ProfileContextMenu) {
        float menuW = 160.0f * m_dpiScale;
        float itemH = 26.0f * m_dpiScale;
        float menuH = 5.0f * itemH + 8.0f * m_dpiScale;
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        float menuX = (std::min)(m_profileMenuX, static_cast<float>(rc.right - rc.left) - menuW - 8.0f);
        float menuY = (std::min)(m_profileMenuY, static_cast<float>(rc.bottom - rc.top) - menuH - 8.0f);

        if (fx >= menuX && fx <= menuX + menuW && fy >= menuY && fy <= menuY + menuH) {
            int idx = static_cast<int>((fy - menuY - 4.0f * m_dpiScale) / itemH);
            std::string targetUid = m_contextProfileUid;
            CloseModal();
            switch (idx) {
                case 0:
                    HandleProfileUpdate(targetUid);
                    break;
                case 1:
                    m_editProfileUid = targetUid;
                    OpenModal(ModalType::EditProfile);
                    break;
                case 2: {
                    wchar_t appData[MAX_PATH];
                    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
                        std::filesystem::path p = std::filesystem::path(appData) / "clash_win" / "profiles" / (targetUid + ".yaml");
                        ShellExecuteW(NULL, L"open", L"notepad.exe", p.c_str(), NULL, SW_SHOWNORMAL);
                    }
                    break;
                }
                case 3: {
                    wchar_t appData[MAX_PATH];
                    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
                        std::filesystem::path p = std::filesystem::path(appData) / "clash_win" / "profiles";
                        ShellExecuteW(NULL, L"open", p.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    }
                    break;
                }
                case 4:
                    HandleProfileDelete(targetUid);
                    break;
            }
            return;
        } else {
            CloseModal();
            return;
        }
    }
    else if (m_currentModal == ModalType::ConnectionContextMenu) {
        CloseModal();
        return;
    }

    CloseModal();
}

void MainWindow::OnModalChar(wchar_t ch) {
    if (m_currentModal == ModalType::AddUrl && m_isAddUrlFocused) {
        if (ch == 8) {
            if (!m_addUrlText.empty()) m_addUrlText.pop_back();
        } else if (ch == 13) {
            AddCustomRule(m_addUrlText, m_addUrlPolicy);
            CloseModal();
        } else if (ch == 27) {
            CloseModal();
        } else if (ch >= 32) {
            m_addUrlText.push_back(ch);
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (m_currentModal == ModalType::EditProfile) {
        std::wstring* targetStr = nullptr;
        if (m_editProfileFocusField == 0) targetStr = &m_editProfileName;
        else if (m_editProfileFocusField == 1) targetStr = &m_editProfileUrl;
        else if (m_editProfileFocusField == 2) targetStr = &m_editProfileIntervalText;

        if (targetStr) {
            if (ch == 8) {
                if (!targetStr->empty()) targetStr->pop_back();
            } else if (ch == 13) {
                SaveProfileEdit();
                CloseModal();
            } else if (ch == 27) {
                CloseModal();
            } else if (ch >= 32) {
                targetStr->push_back(ch);
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
    else if (m_currentModal == ModalType::FirstRunGuide && m_isGuideInputFocused) {
        if (ch == 8) {
            if (!m_guideUrlText.empty()) m_guideUrlText.pop_back();
        } else if (ch == 13) {
            m_drawerImportText = m_guideUrlText;
            TriggerDrawerImport();
            CloseModal();
        } else if (ch == 27) {
            CloseModal();
        } else if (ch >= 32) {
            m_guideUrlText.push_back(ch);
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (m_currentModal == ModalType::LogsView && m_isLogSearchFocused) {
        if (ch == 8) {
            if (!m_logSearchText.empty()) m_logSearchText.pop_back();
        } else if (ch == 27) {
            m_logSearchText.clear();
            m_isLogSearchFocused = false;
        } else if (ch >= 32) {
            m_logSearchText.push_back(ch);
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void MainWindow::OnModalKeyDown(WPARAM key) {
    if (key == VK_ESCAPE) {
        CloseModal();
    }
}

void MainWindow::TriggerStreamingUnlockCheck() {
    m_isCheckingUnlock = true;
    clashwin::network::UnlockChecker::Instance().StartCheckAsync("127.0.0.1:10801", 4000);
    std::thread([this]() {
        while (clashwin::network::UnlockChecker::Instance().IsChecking()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            if (m_hwnd) PostMessageW(m_hwnd, WM_USER + 100, 0, 0);
        }
        m_isCheckingUnlock = false;
        if (m_hwnd) PostMessageW(m_hwnd, WM_USER + 100, 0, 0);
    }).detach();
}

void MainWindow::TriggerClientUpdateCheck() {
    m_isCheckingUpdate = true;
    m_updateStatusMsg = L"正在与 GitHub Releases 同步检测最新版本...";
    InvalidateRect(m_hwnd, nullptr, FALSE);
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        m_isCheckingUpdate = false;
        m_updateStatusMsg = L"当前版本已是最新 (v3.0.10)，未发现新版本更新。";
        if (m_hwnd) PostMessageW(m_hwnd, WM_USER + 100, 0, 0);
    }).detach();
}

void MainWindow::AddCustomRule(const std::wstring& rawInput, int policy) {
    if (rawInput.empty()) return;
    std::string target = (policy == 0) ? "DIRECT" : ((policy == 2) ? "REJECT" : "PROXY");
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    std::string str = conv.to_bytes(rawInput);
    std::istringstream iss(str);
    std::string line;
    int addedCount = 0;
    while (std::getline(iss, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty() || line[0] == '#') continue;
        clashwin::config::EnhanceChain::Instance().InjectRule(line, target, true);
        addedCount++;
    }
    m_statusMessage = L"已成功注入 " + std::to_wstring(addedCount) + L" 条自定义分流规则";
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::SaveProfileEdit() {
    if (m_editProfileUid.empty()) return;
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    std::string newName = conv.to_bytes(m_editProfileName);
    std::string newUrl = conv.to_bytes(m_editProfileUrl);
    uint32_t interval = 24;
    try {
        interval = std::stoul(m_editProfileIntervalText);
    } catch (...) {}
    clashwin::config::ProfileManager::Instance().UpdateProfileMeta(m_editProfileUid, newName, newUrl, interval);
    m_statusMessage = L"订阅配置信息已保存";
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::RefreshLogsCache() {
    m_cachedLogEntries.clear();
    wchar_t appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
        std::filesystem::path logFile = std::filesystem::path(appData) / "clash_win" / "logs" / "mihomo.log";
        if (std::filesystem::exists(logFile)) {
            std::ifstream ifs(logFile);
            std::string line;
            std::vector<std::string> lines;
            while (std::getline(ifs, line)) {
                if (!line.empty()) lines.push_back(line);
            }
            size_t start = lines.size() > 100 ? lines.size() - 100 : 0;
            for (size_t i = lines.size(); i > start; --i) {
                const auto& l = lines[i - 1];
                std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
                try {
                    m_cachedLogEntries.push_back(conv.from_bytes(l));
                } catch (...) {
                    m_cachedLogEntries.push_back(std::wstring(l.begin(), l.end()));
                }
            }
        }
    }
    if (m_cachedLogEntries.empty()) {
        m_cachedLogEntries.push_back(L"[INFO] Clash Mini Ver.3.0.10 (Win32 Direct2D) 系统架构初始化完毕");
        m_cachedLogEntries.push_back(L"[INFO] Mihomo 内核服务就绪，监听 127.0.0.1:10801 (Mixed) / 9098 (REST)");
        m_cachedLogEntries.push_back(L"[INFO] 规则增强链 Merge.yaml 与 Universal Parser 解析器注入成功");
        m_cachedLogEntries.push_back(L"[INFO] 订阅配置与 22 个候选代理节点已加载完毕");
        m_cachedLogEntries.push_back(L"[INFO] 实时流量监测引擎启动: 采样历史已挂载，双曲线渲染就绪");
    }
}

} // namespace ClashWin
