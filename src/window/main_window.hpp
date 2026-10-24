#pragma once

#include <windows.h>
#include <shellapi.h>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <algorithm>
#include "render/d2d_renderer.hpp"
#include "clashwin/ui/theme.h"
#include "clashwin/common/types.h"
#include "clashwin/common/constants.h"
#include "clashwin/config/app_config.h"
#include "clashwin/config/profile_manager.h"
#include "clashwin/core/core_supervisor.h"
#include "clashwin/core/health_probe.h"
#include "clashwin/core/mihomo_client.h"
#include "clashwin/system/sysproxy.h"
#include "clashwin/system/service_manager.h"

namespace ClashWin {

struct ActiveConnectionItem {
    std::string id;
    std::string raw_host;
    std::string dest_ip;
    std::wstring host;
    std::wstring chain;
    std::wstring speed;
};

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool Create(HINSTANCE hInstance, int nCmdShow);
    int RunMessageLoop();

    HWND GetHwnd() const { return m_hwnd; }

    // Clash Mini 权威几何与定时器常量
    static constexpr int MIN_WIDTH = 271;          // 逻辑像素 (271x1.5=406.5px)
    static constexpr int MIN_HEIGHT = 126;         // 逻辑像素 (126x1.5=189px)
    static constexpr int NARROW_BREAKPOINT = 285;  // 双宽窄排版断点 (285px)
    static constexpr UINT_PTR IDLE_TIMER_ID = 1001;
    static constexpr UINT IDLE_TIMEOUT_MS = 10000; // 10秒空闲自续期定时器
    static constexpr UINT_PTR CONN_TIMER_ID = 1002;
    static constexpr UINT CONN_POLL_INTERVAL_MS = 1500; // 连接面板轮询周期
    static constexpr UINT WM_TRAYICON = WM_USER + 200;

    void OnWakeupVisible();

    // 优雅退出 7 步防线
    void GracefulExit();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnPaint();
    void OnResize(UINT width, UINT height);
    LRESULT OnNcHitTest(POINTS pt);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnMouseMove(int x, int y);
    void ResetIdleTimer();
    float GetDpiScale() const;

    // 系统托盘生命周期
    void InitTrayIcon();
    void UpdateTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayContextMenu();

    // 核心业务与 UI 动作
    void StartCoreIfConfigReady();
    void RefreshConnections();
    void ApplyProxyModeChange(int modeIndex); // 0: MAN, 1: SYS, 2: TUN
    void ApplyRuleModeChange(int ruleIndex);   // 0: DIR, 1: RUL, 2: PRX
    void SwitchSkin(SkinType skin);
    void ToggleAlwaysOnTop();
    void ToggleMaximize();
    void CycleToNextCandidateNode();
    void TriggerActiveNodeDelayTest();

    // 设置抽屉 5 大功能卡片交互动作 (Slice 6)
    void TriggerDrawerImport();
    void HandleProfileDelete(const std::string& uid);
    void HandleProfileUpdate(const std::string& uid);
    void HandleProfileSelect(const std::string& uid);
    void SavePortChange();
    void ToggleAutoLaunch();

    // 连接面板右键分流动作 (Slice 8)
    struct ConnectionItemClickArea {
        D2D1_RECT_F rect;
        std::string id;
        std::string host;
        std::string dest_ip;
    };
    void ShowConnectionContextMenu(int screenX, int screenY, const ConnectionItemClickArea& area);

    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    std::unique_ptr<D2DRenderer> m_renderer;

    // 窗口与交互状态
    bool m_isStealthMode = false;
    float m_dpiScale = 1.0f;
    bool m_isDrawerOpen = false; // 是否展开设置抽屉
    bool m_isAlwaysOnTop = false; // 窗口置顶状态
    bool m_isLargeMode = false;   // 大尺寸模式 (640x860)
    NOTIFYICONDATAW m_nid{};
    bool m_trayInstalled = false;

    // 搜索与排序工具栏状态 (ProxyHead)
    int m_sortType = 0; // 0: 默认, 1: 延迟, 2: 名称
    std::wstring m_searchText;
    bool m_searchFocused = false;
    bool m_isTestingDelay = false;

    // 实时流量统计
    uint64_t m_upSpeed = 0;
    uint64_t m_downSpeed = 0;
    uint64_t m_upTotal = 0;
    uint64_t m_downTotal = 0;

    // 状态提示与导入状态
    std::wstring m_statusMessage;
    bool m_isImporting = false;

    // 滑块拖拽跟踪
    bool m_isDraggingSlider1 = false;
    bool m_isDraggingSlider2 = false;
    D2D1_RECT_F m_slider1Rect{};
    D2D1_RECT_F m_slider2Rect{};

    // 宽窗连接列表缓存
    std::vector<ActiveConnectionItem> m_connections;

    // UI 点击热区
    D2D1_RECT_F m_minimizeBtnRect{};
    D2D1_RECT_F m_maximizeBtnRect{};
    D2D1_RECT_F m_closeBtnRect{};
    D2D1_RECT_F m_pinBtnRect{};
    D2D1_RECT_F m_drawerToggleBtnRect{};
    D2D1_RECT_F m_drawerCloseBtnRect{};
    D2D1_RECT_F m_activeCardRect{};
    D2D1_RECT_F m_activeDelayChipRect{};
    D2D1_RECT_F m_emptyListAddBtnRect{};

    // ProxyHead 工具栏热区
    D2D1_RECT_F m_headBoltBtnRect{};
    D2D1_RECT_F m_headSortBtnRect{};
    D2D1_RECT_F m_headSearchBoxRect{};

    // 交互热区
    D2D1_RECT_F m_closeAllConnsBtnRect{};

    // 分段控制热区 (抽屉内)
    D2D1_RECT_F m_proxyModeRect{};
    D2D1_RECT_F m_ruleModeRect{};
    std::vector<std::pair<D2D1_RECT_F, SkinType>> m_skinBtnRects;
    std::vector<std::pair<D2D1_RECT_F, std::string>> m_nodeItemRects;

    // 设置抽屉 5 大功能卡片交互热区与状态 (Slice 6)
    float m_drawerScrollY = 0.0f;
    D2D1_RECT_F m_autoLaunchSwitchRect{};
    D2D1_RECT_F m_allowLanSwitchRect{};
    D2D1_RECT_F m_silentStartSwitchRect{};
    D2D1_RECT_F m_ipv6SwitchRect{};
    D2D1_RECT_F m_mixedPortBoxRect{};
    D2D1_RECT_F m_mixedPortSaveBtnRect{};
    std::wstring m_mixedPortText = L"10801";
    bool m_isMixedPortFocused = false;

    D2D1_RECT_F m_themeModeRect{}; // SYS / LIGHT / DARK

    D2D1_RECT_F m_drawerImportBoxRect{};
    std::wstring m_drawerImportText{};
    bool m_isDrawerImportFocused = false;
    D2D1_RECT_F m_drawerPasteBtnRect{};
    D2D1_RECT_F m_drawerImportBtnRect{};

    struct ProfileItemClickArea {
        D2D1_RECT_F cardRect;
        std::string uid;
        D2D1_RECT_F updateBtnRect;
        D2D1_RECT_F deleteBtnRect;
    };
    std::vector<ProfileItemClickArea> m_profileItemClickAreas;

    // 连接项点击区域列表 (Slice 8)
    std::vector<ConnectionItemClickArea> m_connectionItemRects;
};

} // namespace ClashWin
