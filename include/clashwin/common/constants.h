#pragma once

#include <cstdint>
#include <string_view>

namespace clashwin {

// ============================================================================
// 窗口几何常量（遵循 Clash Mini 绝对权威规范）
// ============================================================================
namespace geometry {
    // 默认启动尺寸（逻辑像素，客户区口径）
    inline constexpr double kDefaultWidth = 271.0;
    inline constexpr double kDefaultHeight = 680.0;

    // 最小极限尺寸（MINIMAL_WIDTH x MINIMAL_HEIGHT）
    inline constexpr double kMinimalWidth = 271.0;
    inline constexpr double kMinimalHeight = 126.0;

    // 最大限制尺寸
    inline constexpr double kMaxWidth = 640.0;
    inline constexpr double kMaxHeight = 860.0;

    // 响应式排版断点（<= 285px 为窄窗单列模式，> 285px 为大窗模式）
    inline constexpr double kMiniWidthThreshold = 285.0;

    // 物理像素判定容差（防止 125%/175% 非整数 DPI 取整歧义）
    inline constexpr int kMiniPhysicalSlack = 1;

    // 窗口边缘指针缩放手柄尺寸
    inline constexpr int kHandleSize = 10;
    inline constexpr int kCornerHandleSize = 20;

    // 极窄隐匿模式空闲触发时长（毫秒）
    inline constexpr uint32_t kIdleHideDelayMs = 10000;

    // 窗口几何状态落盘节流周期（毫秒）
    inline constexpr int64_t kGeometrySaveIntervalMs = 1000;
}

// ============================================================================
// 网络与 IPC 常量
// ============================================================================
namespace network {
    // 单实例互斥与唤醒 HTTP 端口
    inline constexpr uint16_t kSingletonPort = 33335;
    inline constexpr uint16_t kSingletonPortFallback = 33336;

    // 默认代理服务端口
    inline constexpr uint16_t kDefaultMixedPort = 10801;
    inline constexpr uint16_t kDefaultHttpPort = 7890;
    inline constexpr uint16_t kDefaultSocksPort = 7891;
    inline constexpr uint16_t kDefaultRestPort = 9098;

    // 命名管道路径
    inline constexpr std::wstring_view kMihomoNamedPipe = L"\\\\.\\pipe\\mini-mihomo";

    // 托盘点击防抖阈值（毫秒）
    inline constexpr uint64_t kTrayClickDebounceMs = 300;

    // 健康探针恒定周期（秒）与重试加速（秒）
    inline constexpr uint32_t kHealthProbeRegularIntervalSec = 15;
    inline constexpr uint32_t kHealthProbeRetryIntervalSec = 3;
    inline constexpr uint32_t kHealthProbeFailStrikes = 2;
    inline constexpr uint32_t kFailedServerCooldownSec = 600;

    // 默认系统代理例外名单 (Bypass List)
    inline constexpr std::wstring_view kDefaultProxyBypass =
        L"localhost;127.*;10.*;172.16.*;172.17.*;172.18.*;172.19.*;172.20.*;"
        L"172.21.*;172.22.*;172.23.*;172.24.*;172.25.*;172.26.*;172.27.*;172.28.*;"
        L"172.29.*;172.30.*;172.31.*;192.168.*;<local>";
}

// ============================================================================
// Windows 系统服务与内核常量
// ============================================================================
namespace system_service {
    inline constexpr std::wstring_view kServiceName = L"clash_verge_service";
    inline constexpr std::wstring_view kServiceDisplayName = L"Clash Verge Service";
    inline constexpr std::string_view kCoreBinaryName = "mini-mihomo.exe";
}

} // namespace clashwin
