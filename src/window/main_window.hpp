#pragma once

#include <windows.h>
#include <shellapi.h>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <algorithm>
#include <mutex>
#include <unordered_map>
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

enum class ModalType {
    None,
    AddUrl,
    EditProfile,
    FirstRunGuide,
    LogsView,
    CheckUpdate,
    StreamingUnlock,
    HelpMenu,
    ProfileContextMenu,
    ConnectionContextMenu
};

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
    static constexpr float TRAFFIC_PANE_HEIGHT_MINIMAL = 100.0f; // 窄窗常驻流量面板高度 (100px)
    static constexpr float TRAFFIC_PANE_HEIGHT_WIDE = 135.0f;    // 宽窗常驻流量面板高度 (135px)
    static constexpr UINT_PTR IDLE_TIMER_ID = 1001;
    static constexpr UINT IDLE_TIMEOUT_MS = 10000; // 10秒空闲自续期定时器
    static constexpr UINT_PTR CONN_TIMER_ID = 1002;
    static constexpr UINT CONN_POLL_INTERVAL_MS = 1500; // 连接面板轮询周期
    static constexpr UINT_PTR TRAFFIC_TIMER_ID = 1003;
    static constexpr UINT TRAFFIC_POLL_INTERVAL_MS = 1000; // 流量波形图 1s 采样周期
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
    void TriggerAllNodesDelayTest();

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

    // 模态弹窗与对话框系统 (全套 1:1 对话框体系)
    void OpenModal(ModalType modal);
    void CloseModal();
    void OnModalLButtonDown(int x, int y);
    void OnModalChar(wchar_t ch);
    void OnModalKeyDown(WPARAM key);
    void TriggerStreamingUnlockCheck();
    void TriggerClientUpdateCheck();
    void AddCustomRule(const std::wstring& rawInput, int policy);
    void SaveProfileEdit();
    void RefreshLogsCache();

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
    std::unordered_map<std::string, int> m_nodeDelays;
    std::mutex m_delayMutex;

    // 实时流量统计与 30 采样历史 (波形图)
    uint64_t m_upSpeed = 0;
    uint64_t m_downSpeed = 0;
    uint64_t m_upTotal = 0;
    uint64_t m_downTotal = 0;
    std::deque<TrafficPoint> m_trafficHistory;
    int m_hoveredWindowBtn = 0; // 0: none, 1: min, 2: max, 3: close

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

    // 模态对话框与全套窗口体系状态
    ModalType m_currentModal = ModalType::None;
    D2D1_RECT_F m_modalCloseBtnRect{};

    // AddUrl 对话框
    std::wstring m_addUrlText{};
    int m_addUrlPolicy = 1; // 0: DIRECT, 1: PROXY, 2: REJECT
    D2D1_RECT_F m_modalAddUrlInputRect{};
    D2D1_RECT_F m_modalAddUrlPasteBtnRect{};
    D2D1_RECT_F m_modalAddUrlPolicyRect{};
    D2D1_RECT_F m_modalAddUrlCancelBtnRect{};
    D2D1_RECT_F m_modalAddUrlConfirmBtnRect{};
    bool m_isAddUrlFocused = false;

    // EditProfile 对话框
    std::string m_editProfileUid{};
    std::wstring m_editProfileName{};
    std::wstring m_editProfileUrl{};
    std::wstring m_editProfileIntervalText = L"24";
    int m_editProfileFocusField = 0; // 0: name, 1: url, 2: interval
    D2D1_RECT_F m_modalEditNameRect{};
    D2D1_RECT_F m_modalEditUrlRect{};
    D2D1_RECT_F m_modalEditIntervalRect{};
    D2D1_RECT_F m_modalEditCancelBtnRect{};
    D2D1_RECT_F m_modalEditSaveBtnRect{};

    // FirstRunGuide 对话框
    std::wstring m_guideUrlText{};
    bool m_isGuideInputFocused = false;
    D2D1_RECT_F m_modalGuideInputRect{};
    D2D1_RECT_F m_modalGuidePasteBtnRect{};
    D2D1_RECT_F m_modalGuideImportBtnRect{};
    D2D1_RECT_F m_modalGuideWikiBtnRect{};
    D2D1_RECT_F m_modalGuideLaterBtnRect{};

    // LogsView 对话框
    int m_logLevelFilter = 0; // 0: ALL, 1: INFO, 2: WARN, 3: ERROR
    std::wstring m_logSearchText{};
    bool m_isLogSearchFocused = false;
    bool m_logPaused = false;
    float m_logScrollY = 0.0f;
    std::vector<std::wstring> m_cachedLogEntries;
    D2D1_RECT_F m_modalLogLevelRect{};
    D2D1_RECT_F m_modalLogSearchRect{};
    D2D1_RECT_F m_modalLogPauseBtnRect{};
    D2D1_RECT_F m_modalLogClearBtnRect{};

    // CheckUpdate 对话框
    bool m_isCheckingUpdate = false;
    std::wstring m_updateStatusMsg = L"当前版本已是最新 (v3.0.10)";
    D2D1_RECT_F m_modalUpdateCheckBtnRect{};
    D2D1_RECT_F m_modalUpdateCloseBtnRect{};

    // StreamingUnlock 对话框
    bool m_isCheckingUnlock = false;
    D2D1_RECT_F m_modalUnlockRecheckBtnRect{};

    // HelpMenu 弹窗
    D2D1_RECT_F m_modalHelpWikiBtnRect{};
    D2D1_RECT_F m_modalHelpClientUpdateBtnRect{};
    D2D1_RECT_F m_modalHelpCoreUpdateBtnRect{};
    D2D1_RECT_F m_modalHelpUnlockBtnRect{};
    D2D1_RECT_F m_modalHelpGuideBtnRect{};

    // Profile 右键菜单
    std::string m_contextProfileUid{};
    float m_profileMenuX = 0.0f;
    float m_profileMenuY = 0.0f;
    int m_hoveredProfileMenuItem = -1;
    std::vector<std::pair<D2D1_RECT_F, int>> m_profileMenuItemRects;

    // Connection 右键菜单
    ConnectionItemClickArea m_contextConnection{};
    float m_connMenuX = 0.0f;
    float m_connMenuY = 0.0f;
    int m_hoveredConnMenuItem = -1;
    std::vector<std::pair<D2D1_RECT_F, int>> m_connMenuItemRects;

    // 抽屉底部快捷按钮
    D2D1_RECT_F m_drawerAddUrlBtnRect{};
    D2D1_RECT_F m_drawerHelpBtnRect{};
    D2D1_RECT_F m_drawerLogsBtnRect{};
};

} // namespace ClashWin
