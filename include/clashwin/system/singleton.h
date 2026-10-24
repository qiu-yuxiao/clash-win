#pragma once

#include <string>
#include <string_view>
#include <functional>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace clashwin::system {

class SingleInstanceGuard {
public:
    SingleInstanceGuard();
    ~SingleInstanceGuard();

    // 检查是否为主实例。若已存在主实例，则自动向其发送唤醒/订阅请求，并返回 false
    bool AcquireOrWakeExisting(int argc, wchar_t* argv[]);

    // 启动主实例的本地微型 HTTP 监听服务（处理 /commands/visible 与 /commands/scheme）
    void StartServer(std::function<void()> on_wake_visible,
                     std::function<void(std::string_view scheme_param)> on_wake_scheme);

    void StopServer();

private:
    SOCKET listen_socket_{INVALID_SOCKET};
    HANDLE mutex_{nullptr};
    uint16_t active_port_{33335};
    bool is_primary_{false};
    bool running_{false};
};

} // namespace clashwin::system
