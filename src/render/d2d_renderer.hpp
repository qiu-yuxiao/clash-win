#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include "clashwin/ui/theme.h"

using Microsoft::WRL::ComPtr;

namespace ClashWin {

class D2DRenderer {
public:
    D2DRenderer();
    ~D2DRenderer();

    bool Initialize(HWND hwnd);
    void Resize(UINT width, UINT height);
    void BeginDraw();
    HRESULT EndDraw();

    // 基础图元与文字渲染
    void Clear(D2D1_COLOR_F color);
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

    // 4. 底部实时流量指示栏 (MiniTrafficPanel: ↓ 下载速率 + ↑ 上传速率 + 总传输)
    void DrawTrafficBottomBar(float x, float y, float w, float h,
                              uint64_t upSpeed, uint64_t downSpeed,
                              uint64_t upTotal, uint64_t downTotal);

    ID2D1HwndRenderTarget* GetRenderTarget() const { return m_renderTarget.Get(); }

private:
    HRESULT CreateDeviceResources();
    void DiscardDeviceResources();

    HWND m_hwnd = nullptr;
    ComPtr<ID2D1Factory> m_d2dFactory;
    ComPtr<IDWriteFactory> m_dwriteFactory;
    ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    ComPtr<ID2D1SolidColorBrush> m_solidBrush;
};

} // namespace ClashWin
