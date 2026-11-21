#pragma once
// MainWindow 拆分单元共享的内部常量与时钟工具。

#include <cstdint>
#include <windows.h>

namespace clashwin::ui::win::detail {

inline constexpr wchar_t kWndClass[] = L"ClashWinMainWindow";
// 单实例唤醒消息（T01 kAppWakeup 常量收编到主窗口模块）
inline constexpr UINT kAppWakeup = WM_APP + 1;
// 定时器 id（WM_TIMER）
inline constexpr UINT_PTR kTimerFrame = 1;       // 33ms 帧泵（§5.4 30fps 合帧）
inline constexpr UINT_PTR kTimerIdle = 2;        // 1s stealth 空闲扫描（自续期）
inline constexpr UINT_PTR kTimerNodeRetry = 3;   // 取数失败 2s 重试（内核配置就绪竞态）
inline constexpr UINT_PTR kTimerFirstRun = 4;    // 首次引导 500ms 延迟（_layout.tsx:351-353）
// 首启就绪轮询驱动 250ms（1.5s 轮询闸门 + 20s 超时由 dialog tick 判定）
inline constexpr UINT_PTR kTimerFirstReady = 5;  // src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:34-36
// TUN 可用性轮询 5s（← use-system-state.ts:65-67：启动期 5s / 常态 30s，
// 取更密的启动期值，成本仅为一次服务 IPC 探测）
inline constexpr UINT_PTR kTimerTunGuard = 6;
// BUG-358：滑块拖动 200ms 防抖落盘定时器
inline constexpr UINT_PTR kTimerSkinPersist = 7;  // src: src/pages/_layout/hooks/use-skin-controls.ts:4
// BUG-360：过滤框 700ms 可取消自动测速防抖定时器
inline constexpr UINT_PTR kTimerFilterAutoTest = 8;  // src: src/components/proxy/proxy-head.tsx:148
inline constexpr UINT_PTR kTimerDelayCachePrune = 9;  // 5min 清理过期延迟缓存（src/services/delay.ts:100）
inline constexpr int kResizeBorder = 10;     // ← resize-handles.tsx HANDLE_SIZE
inline constexpr int kCornerSize = 20;       // ← HANDLE_SIZE*2（角落热区）
// 关闭键拖动判定（原版 mouseup 位移 <5 恢复标题栏）
inline constexpr float kStealthClickSlack = 5.0f;

inline int64_t now_ms() { return static_cast<int64_t>(GetTickCount64()); }

}  // namespace clashwin::ui::win::detail
