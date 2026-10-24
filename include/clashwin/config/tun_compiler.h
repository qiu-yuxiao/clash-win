#pragma once

#include <string>
#include <vector>

namespace clashwin::config {

struct TunCompilerOptions {
    bool enable_tun{false};
    bool user_ipv6{false};
    std::string rule_fallback{"direct"}; // "direct" or "proxy"
    std::string default_latency_test{"https://cp.cloudflare.com/generate_204"};
};

class TunCompiler {
public:
    // 将原配置 YAML 编译增强，注入 TUN 虚拟网卡配置、IPv6 防泄漏、fake-ip 与完整 Agreement 规则集
    static std::string CompileConfig(const std::string& base_yaml, const TunCompilerOptions& options);

    // 辅助检查：获取默认 fake-ip 过滤列表
    static const std::vector<std::string>& GetDefaultFakeIpFilters();

    // 辅助检查：获取强制注入的直连 LAN 规则列表 (IPv4 + IPv6 + IPv4-mapped IPv6)
    static std::vector<std::string> GetMandatoryDirectRules();
};

} // namespace clashwin::config
