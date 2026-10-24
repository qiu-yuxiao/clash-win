#pragma once

#include "clashwin/common/types.h"
#include <string>
#include <vector>
#include <optional>

namespace clashwin::core {

class MihomoClient {
public:
    static MihomoClient& Instance();

    // 检查内核 REST API 连通性
    bool CheckHealth(uint16_t port = 9098);

    // 获取内核版本
    std::string GetVersion(uint16_t port = 9098);

    // 获取当前选中的节点名称（从 PROXY 组）
    std::string GetCurrentSelectedProxy(uint16_t port = 9098);

    // 切换当前活跃节点 (PUT /proxies/PROXY {"name": "..."})
    bool SelectProxy(const std::string& group_name, const std::string& proxy_name, uint16_t port = 9098);

    // 触发单个节点测速，返回延迟毫秒数（-1 表示失败）
    int TestProxyDelay(const std::string& proxy_name, uint16_t port = 9098, uint32_t timeout_ms = 2000);

    // 获取所有连接原始 JSON (GET /connections)
    bool GetConnectionsRaw(std::string& out_resp, uint16_t port = 9098);

    // 断开单个连接 (DELETE /connections/{id})
    bool CloseConnection(const std::string& id, uint16_t port = 9098);

    // 断开所有活跃连接 (DELETE /connections)
    bool CloseAllConnections(uint16_t port = 9098);

private:
    MihomoClient() = default;
    ~MihomoClient() = default;

    bool HttpRequest(const std::wstring& method, const std::wstring& path,
                     const std::string& body, std::string& out_resp, uint16_t port);
};

} // namespace clashwin::core
