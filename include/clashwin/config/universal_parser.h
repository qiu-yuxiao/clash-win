#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <cstdint>

namespace clashwin::config {

struct ParsedProxyNode {
    std::string type;
    std::string name;
    std::string server;
    uint16_t port{0};
    std::string password;
    std::string uuid;
    std::string cipher;
    uint32_t alter_id{0};
    bool udp{true};
    bool tls{false};
    bool skip_cert_verify{false};
    std::string sni;
    std::string network; // ws, grpc, tcp
    std::string path;
    std::string host_header;
    std::string flow;
    std::string reality_public_key;
    std::string reality_short_id;
    std::string client_fingerprint;
    std::string obfs;
    std::string obfs_password;
    std::string up;
    std::string down;
    std::string protocol;
    std::string alpn;
    std::string private_key;
    std::string public_key;
    std::string ip;
    std::string ipv6;
    std::string reserved;
    uint16_t mtu{0};
    std::string preshared_key;
    std::string username;

    // Convert node to Clash YAML snippet
    [[nodiscard]] std::string ToYaml(int indent_spaces = 2) const;
};

class UniversalParser {
public:
    // 鲁棒 Base64 解码器：自动清理空白、补齐填充、兼容标准和 URL Safe Base64
    static std::optional<std::string> DecodeBase64Robust(std::string_view input);

    // URL 百分号解码 (Percent-decode)
    static std::string UrlDecode(std::string_view input);

    // 解析 URI 查询参数
    static std::map<std::string, std::string> ParseQueryParams(std::string_view query);

    // 提取 host 与 port (兼容 [2001:db8::1]:443 与 1.2.3.4:443)
    static std::optional<std::pair<std::string, uint16_t>> ExtractHostPort(std::string_view host_port, uint16_t default_port);

    // 规范化带宽字符串（数字补 Mbps）
    static std::string NormalizeBandwidth(std::string_view value);

    // 单协议解析
    static std::optional<ParsedProxyNode> ParseVmess(std::string_view link);
    static std::optional<ParsedProxyNode> ParseVless(std::string_view link);
    static std::optional<ParsedProxyNode> ParseShadowsocks(std::string_view link);
    static std::optional<ParsedProxyNode> ParseShadowsocksR(std::string_view link);
    static std::optional<ParsedProxyNode> ParseTrojan(std::string_view link);
    static std::optional<ParsedProxyNode> ParseHysteria2(std::string_view link);
    static std::optional<ParsedProxyNode> ParseHysteria(std::string_view link);
    static std::optional<ParsedProxyNode> ParseAnyTls(std::string_view link);
    static std::optional<ParsedProxyNode> ParseTuic(std::string_view link);
    static std::optional<ParsedProxyNode> ParseWireGuard(std::string_view link);
    static std::optional<ParsedProxyNode> ParseSocks5(std::string_view link);
    static std::optional<ParsedProxyNode> ParseHttpFallback(std::string_view line);

    // 单行链接自动路由解析
    static std::optional<ParsedProxyNode> ParseNodeLink(std::string_view line);

    // 全量订阅解析：输入可为纯 YAML，或 Base64 编码的多行 URI，或明文多行 URI
    // 返回标准 Clash YAML 字符串，包含 proxies、proxy-groups、rules 等完整节点骨干
    static std::optional<std::string> ParseUriListToYaml(std::string_view content);

    // 解析并返回节点结构体列表
    static std::vector<ParsedProxyNode> ParseUriListToNodes(std::string_view content);
};

} // namespace clashwin::config
