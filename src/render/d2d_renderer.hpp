#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <deque>
#include "clashwin/ui/theme.h"

using Microsoft::WRL::ComPtr;

namespace ClashWin {

struct TrafficPoint {
    uint64_t up = 0;
    uint64_t down = 0;
};

class D2DRenderer {
public:
    D2DRenderer();
    ~D2DRenderer();

    bool Initialize(HWND hwnd);
    bool InitializeWithRenderTarget(Microsoft::WRL::ComPtr<ID2D1RenderTarget> target);
    void Resize(UINT width, UINT height);
    void BeginDraw();
    HRESULT EndDraw();

    // 节点名称规整化：清除 6 位时间戳后缀 (对齐 Clash Mini: replace(/\s\(\d{6}\)$/, ''))
    static std::wstring TrimNodeName(const std::wstring& name);

    // 基础图元与文字渲染
    void Clear(D2D1_COLOR_F color);
    void FillRect(float x, float y, float w, float h, D2D1_COLOR_F color);
    void DrawLine(float x1, float y1, float x2, float y2, D2D1_COLOR_F color, float strokeWidth = 1.0f);
    void DrawTextSimple(const std::wstring& text, float x, float y, float w, float h,
                        D2D1_COLOR_F color, float fontSize = 14.0f, bool bold = false,
                        DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);

    // 3D 实体拟物卡片与凹凸倒角
    void Draw3DCard(float x, float y, float w, float h, D2D1_COLOR_F baseColor, bool isPressed = false);

    // 3D 实体按钮
    void Draw3DButton(float x, float y, float w, float h, const std::wstring& label,
                      bool isHot = false, bool isPressed = false, bool isActive = false);

    // 工业级三段式分段切换开关 (MAN / SYS / TUN, DIR / RUL / PRX)
    void DrawSegmentedControl(float x, float y, float w, float h,
                              const std::vector<std::wstring>& options,
                              int selectedIndex, int hotIndex = -1);

    // 标题栏原生风格窗口控制按钮 (最小化、最大化、关闭，纯平无突兀 3D 倒角，精准对齐 Clash Mini WindowControls)
    void DrawWindowCaptionButton(float x, float y, float w, float h, const std::wstring& glyph, int btnType, bool isHovered);

    // 双参数调节滑块
    void DrawSlider(float x, float y, float w, float h,
                    const std::wstring& label, float value, float minVal, float maxVal,
                    bool isHot = false);

    // 3D 拟物胶囊开关 (Switch)
    void Draw3DSwitch(float x, float y, float w, float h, const std::wstring& label, bool isChecked, bool isHot = false);

    // 3D 拟物内嵌输入框 (TextInput)
    void Draw3DTextInput(float x, float y, float w, float h,
                         const std::wstring& label, const std::wstring& text,
                         const std::wstring& placeholder, bool isFocused, bool isHot = false);

    // 宽窗活跃连接观测表格行
    void DrawConnectionRow(float x, float y, float w, float h,
                           const std::wstring& host, const std::wstring& chain,
                           const std::wstring& speed, bool isSelected = false);

    // 节点延迟徽章
    void DrawLatencyBadge(float x, float y, float w, float h, int delay_ms);

    // =========================================================================
    // Clash Mini 1:1 像素级精准复刻专属组件
    // =========================================================================

    // 1. 严格 3 列网格节点行 (55px 协议 | 2px 竖线 | flex 1 节点名 | 2px 竖线 | 65px 延迟)
    void DrawProxyItem3Col(float x, float y, float w, float h,
                           const std::wstring& type, const std::wstring& name,
                           int delay_ms, bool isSelected, bool isOdd, bool isHot);

    // 2. 活跃节点状态卡片 (28px 规格：协议徽章 + 节点名轮换 + 延迟测速徽章 + 节点IP)
    void DrawActiveNodeCard(float x, float y, float w, float h,
                            const std::wstring& protocol, const std::wstring& name,
                            int delay_ms, const std::wstring& addr, bool isTesting);

    // 3. 搜索与排序工具条 (ProxyHead: ⚡一键测速 + ↕ 3态排序 + 🔍 实时输入搜索框)
    void DrawProxyHead(float x, float y, float w, float h,
                       int sortType, const std::wstring& searchText,
                       bool isTesting, bool searchFocused);

    // 4. 底部实时流量波形图仪表盘 (MiniTrafficPanel: 实时贝塞尔平滑波形图 + 3D 下载/上传速率胶囊卡片)
    void DrawTrafficDashboard(float x, float y, float w, float h,
                              uint64_t upSpeed, uint64_t downSpeed,
                              uint64_t upTotal, uint64_t downTotal,
                              const std::deque<TrafficPoint>& history,
                              bool isMinimalWidth);

    // 兼容历史调用
    void DrawTrafficBottomBar(float x, float y, float w, float h,
                              uint64_t upSpeed, uint64_t downSpeed,
                              uint64_t upTotal, uint64_t downTotal);

    ID2D1RenderTarget* GetRenderTarget() const { return m_renderTarget.Get(); }

private:
    HRESULT CreateDeviceResources();
    void DiscardDeviceResources();

    HWND m_hwnd = nullptr;
    ComPtr<ID2D1Factory> m_d2dFactory;
    ComPtr<IDWriteFactory> m_dwriteFactory;
    ComPtr<ID2D1RenderTarget> m_renderTarget;
    ComPtr<ID2D1SolidColorBrush> m_solidBrush;
};

} // namespace ClashWin
