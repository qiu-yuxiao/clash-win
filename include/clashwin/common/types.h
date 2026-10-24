#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace clashwin {

// 视口展示模式
enum class ViewportMode {
    NarrowStandard, // 窄窗标准态（<= 285px，单列节点卡片）
    LargeExpanded,  // 大窗展开态（> 285px，挂载连接监控表）
    StealthMode     // 极窄隐匿态（<= 285px 且 <= 126px，隐藏标题栏）
};

// 6 套特色皮肤枚举
enum class SkinTheme {
    Retro3D,       // Trump-3D：金条拟物、高浮雕
    Original,      // 经典工业风
    ModernFlat,    // 现代深色立体
    FrostedGlass,  // 暗黑磨砂玻璃
    Cyberpunk,     // 赛博朋克霓虹
    Monochrome     // 黑白硬朗极简
};

// 代理模式策略
enum class RoutingMode {
    Proxy,   // 全局代理
    Direct,  // GFWList 出厂默认（命中走代理，其余直连）
    AddUrl   // 添加网址 / 手动名单
};

// 节点协议类型
enum class ProxyProtocol {
    VMess,
    Shadowsocks,
    Trojan,
    Hysteria2,
    Hysteria,
    WireGuard,
    Direct,
    Reject,
    Unknown
};

// 代理节点基础信息
struct ProxyNode {
    std::string name;
    std::string type;
    std::string server;
    uint16_t port{0};
    int delay_ms{-1}; // -1 表示未测速或超时
    bool is_alive{true};
    bool is_dummy{false};
};

// 订阅源项 (Profile Item)
struct ProfileItem {
    std::string uid;
    std::string type; // "remote" | "local" | "script" | "merge"
    std::string name;
    std::string url;
    std::string file;
    int64_t updated_at{0};
    uint64_t upload_bytes{0};
    uint64_t download_bytes{0};
    uint64_t total_bytes{0};
    uint64_t expire_time{0};
    std::string selected_proxy;
};

// 活跃连接项
struct ConnectionItem {
    std::string id;
    std::string host;
    uint16_t port{0};
    std::string network; // "tcp" | "udp"
    std::string process_path;
    std::string chain_rule; // "DIRECT" | "PROXY" | "REJECT"
    uint64_t upload_bytes{0};
    uint64_t download_bytes{0};
    int64_t start_time{0};
};

// 窗口几何持久化结构
struct WindowGeometry {
    double width{271.0};
    double height{680.0};
    std::optional<double> x;
    std::optional<double> y;
};

} // namespace clashwin
