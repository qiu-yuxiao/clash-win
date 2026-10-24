#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <chrono>

namespace clashwin::core {

struct ConnectionItem {
    std::string id;
    std::string network;      // tcp / udp
    std::string type;         // HTTP / HTTPS / Socks5
    std::string source_ip;
    uint16_t source_port{0};
    std::string dest_ip;
    uint16_t dest_port{0};
    std::string host;
    std::string process;
    std::string process_path;
    uint64_t upload{0};
    uint64_t download{0};
    uint64_t upload_speed{0};
    uint64_t download_speed{0};
    std::string start_time;
    std::string rule;
    std::string rule_payload;
    std::vector<std::string> chains;
};

class ConnectionManager {
public:
    static ConnectionManager& Instance();

    // 刷新连接列表（调用 MihomoClient GET /connections）
    bool Refresh(uint16_t port = 9098);

    // 获取当前缓存的所有活跃连接
    [[nodiscard]] std::vector<ConnectionItem> GetConnections() const;

    // 获取总流量与即时速率
    [[nodiscard]] uint64_t GetDownloadTotal() const { return download_total_; }
    [[nodiscard]] uint64_t GetUploadTotal() const { return upload_total_; }
    [[nodiscard]] uint64_t GetDownloadSpeed() const { return cur_download_speed_; }
    [[nodiscard]] uint64_t GetUploadSpeed() const { return cur_upload_speed_; }

    // 断开单个连接
    bool CloseConnection(const std::string& id, uint16_t port = 9098);

    // 断开所有活跃连接 (DELETE /connections)
    bool CloseAllConnections(uint16_t port = 9098);

    // 右键快捷分流：将连接的目标域名或目标 IP 快速注入为顶层优先规则，并立即断开该连接
    // target: "DIRECT" / "PROXY" / "REJECT"
    // use_domain: true 则注入域名 (DOMAIN-SUFFIX)，false 则注入 IP (IP-CIDR)
    bool RouteConnection(const std::string& conn_id, const std::string& target, bool use_domain, uint16_t port = 9098);

    // 解析 JSON 响应
    void ParseConnectionsJson(const std::string& json_str);

private:
    ConnectionManager() = default;
    ~ConnectionManager() = default;

    mutable std::mutex mutex_;
    std::vector<ConnectionItem> connections_;
    uint64_t download_total_{0};
    uint64_t upload_total_{0};
    uint64_t cur_download_speed_{0};
    uint64_t cur_upload_speed_{0};
};

} // namespace clashwin::core
