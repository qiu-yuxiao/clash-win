#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include "clashwin/system/singleton.h"
#include "clashwin/common/constants.h"
#include <thread>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")

namespace clashwin::system {

SingleInstanceGuard::SingleInstanceGuard() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

SingleInstanceGuard::~SingleInstanceGuard() {
    StopServer();
    WSACleanup();
}

bool SingleInstanceGuard::AcquireOrWakeExisting(int argc, wchar_t* argv[]) {
    listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ == INVALID_SOCKET) {
        return false;
    }

    // 设置地址与端口重用
    int opt = 1;
    setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(network::kSingletonPort);

    // 尝试绑定
    if (bind(listen_socket_, (sockaddr*)&addr, sizeof(addr)) == 0) {
        // 绑定成功，当前为主实例
        if (listen(listen_socket_, 16) == 0) {
            is_primary_ = true;
            return true;
        }
    }

    // 绑定失败，说明已有主实例在运行，执行向已有实例发通知唤醒
    closesocket(listen_socket_);
    listen_socket_ = INVALID_SOCKET;
    is_primary_ = false;

    // 检查是否有 clash:// 格式参数
    std::wstring scheme_param;
    for (int i = 1; i < argc; ++i) {
        if (wcsncmp(argv[i], L"clash:", 6) == 0) {
            scheme_param = argv[i];
            break;
        }
    }

    std::wstring path = scheme_param.empty() 
        ? L"/commands/visible" 
        : (L"/commands/scheme?param=" + scheme_param);

    // 使用 WinHTTP 发起简短唤醒请求
    HINTERNET hSession = WinHttpOpen(L"ClashWin-Wakeup/1.0",
                                     WINHTTP_ACCESS_TYPE_NO_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession) {
        HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", network::kSingletonPort, 0);
        if (hConnect) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                                   NULL, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
            if (hRequest) {
                // 500ms 超时
                WinHttpSetTimeouts(hRequest, 500, 500, 500, 500);
                WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
                WinHttpReceiveResponse(hRequest, NULL);
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
    }

    return false; // 不是主实例
}

void SingleInstanceGuard::StartServer(std::function<void()> on_wake_visible,
                                     std::function<void(std::string_view scheme_param)> on_wake_scheme) {
    if (!is_primary_ || listen_socket_ == INVALID_SOCKET || running_) return;

    running_ = true;
    std::thread([this, on_wake_visible, on_wake_scheme]() {
        while (running_) {
            SOCKET client = accept(listen_socket_, NULL, NULL);
            if (client == INVALID_SOCKET) {
                if (!running_) break;
                continue;
            }

            char buffer[1024] = {0};
            int bytes = recv(client, buffer, sizeof(buffer) - 1, 0);
            if (bytes > 0) {
                std::string req(buffer, bytes);
                if (req.find("GET /commands/visible") != std::string::npos) {
                    if (on_wake_visible) on_wake_visible();
                } else if (req.find("GET /commands/scheme?param=") != std::string::npos) {
                    auto pos = req.find("param=");
                    if (pos != std::string::npos) {
                        auto end_pos = req.find(' ', pos);
                        std::string param = req.substr(pos + 6, end_pos - (pos + 6));
                        if (on_wake_scheme) on_wake_scheme(param);
                    }
                }

                const char* resp = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
                send(client, resp, (int)strlen(resp), 0);
            }
            closesocket(client);
        }
    }).detach();
}

void SingleInstanceGuard::StopServer() {
    running_ = false;
    if (listen_socket_ != INVALID_SOCKET) {
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
    }
}

} // namespace clashwin::system
