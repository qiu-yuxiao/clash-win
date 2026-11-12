#pragma once
// Win32 UI Automation (UIA) 原生无障碍读屏引擎接口。
// 豁免: Mini 无对应物 — Clash Mini 依赖 WebView2 DOM/ARIA 暴露无障碍树；
//   Clash Win 为零 WebView 纯 Direct2D 自绘架构，通过原生 COM
//   IRawElementProviderSimple / Fragment / FragmentRoot 将 Widget 树与 13 语种
//   文案直接暴露给 Windows 讲述人（Narrator）、NVDA 等读屏软件（出处: 无）。

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace clashwin::ui {

class Widget;

namespace uia {

// Widget 析构时由 ~Widget() 调用，断开所有活跃 COM Provider 持有的裸指针，防悬空
void on_widget_destroyed(Widget* w);

// Widget 键盘焦点变化时由 Widget::request_focus() 调用，向读屏器广播焦点事件
void on_widget_focused(Widget* w);

// Snackbar 通知弹出时调用，通过 UiaRaiseNotificationEvent 触发读屏器实时语音播报
void notify_live_announcement(const std::wstring& message, uint64_t notice_id);

}  // namespace uia

class UiaWindowBridge {
 public:
  UiaWindowBridge(HWND hwnd, Widget* root, std::function<float()> dpi_scale_fn);
  ~UiaWindowBridge();
  UiaWindowBridge(const UiaWindowBridge&) = delete;
  UiaWindowBridge& operator=(const UiaWindowBridge&) = delete;

  // 绑定或更新宿主 HWND（允许在 CreateWindowExW 前预建桥接以拦截创建期 WM_GETOBJECT）
  void set_hwnd(HWND hwnd);

  // 处理 WM_GETOBJECT：若命中 UiaRootObjectId 返回非 0 LRESULT；否则返回 0 交 DefWindowProcW
  LRESULT handle_wm_getobject(HWND hwnd, WPARAM wp, LPARAM lp);
  LRESULT handle_wm_getobject(WPARAM wp, LPARAM lp);

  // 窗口销毁前释放宿主 Provider 连接（WM_DESTROY 调用）
  void shutdown();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace clashwin::ui
