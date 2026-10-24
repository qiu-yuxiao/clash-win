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
    if (mutex_) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
    WSACleanup();
}

bool SingleInstanceGuard::AcquireOrWakeExisting(int argc, wchar_t* argv[]) {
    // 1. 使用 Win32 互斥体精准判断 ClashWin 本身是否已有进程在运行
    mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\ClashWin_Native_Single_Instance_Mutex");
    bool alreadyRunning = (GetLastError() == ERROR_ALREADY_EXISTS);

    if (alreadyRunning) {
        // 已有 ClashWin 实例在运行，执行向已有实例发通知唤醒
        is_primary_ = false;

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

        auto SendWakeup = [&](uint16_t port) {
            HINTERNET hSession = WinHttpOpen(L"ClashWin-Wakeup/1.0",
                                             WINHTTP_ACCESS_TYPE_NO_PROXY,
                                             WINHTTP_NO_PROXY_NAME,
                                             WINHTTP_NO_PROXY_BYPASS, 0);
            if (!hSession) return false;
            bool ok = false;
            HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", port, 0);
            if (hConnect) {
                HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                                       NULL, WINHTTP_NO_REFERER,
                                                       WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
                if (hRequest) {
                    WinHttpSetTimeouts(hRequest, 300, 300, 300, 300);
                    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                        WinHttpReceiveResponse(hRequest, NULL);
                        ok = true;
                    }
                    WinHttpCloseHandle(hRequest);
                }
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
            return ok;
        };

        if (!SendWakeup(network::kSingletonPort)) {
            SendWakeup(network::kSingletonPortFallback);
        }
        return false;
    }

    // 2. 当前为首个 ClashWin 实例，创建本地监听 socket
    is_primary_ = true;
    listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ == INVALID_SOCKET) {
        return true;
    }

    int opt = 1;
    setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // 优先绑定标准单实例端口 33335；若已被外部进程（如 Clash Mini）占用，平滑降级至 33336
    addr.sin_port = htons(network::kSingletonPort);
    active_port_ = network::kSingletonPort;

    if (bind(listen_socket_, (sockaddr*)&addr, sizeof(addr)) != 0) {
        addr.sin_port = htons(network::kSingletonPortFallback);
        active_port_ = network::kSingletonPortFallback;
        if (bind(listen_socket_, (sockaddr*)&addr, sizeof(addr)) != 0) {
            closesocket(listen_socket_);
            listen_socket_ = INVALID_SOCKET;
            return true;
        }
    }

    if (listen(listen_socket_, 16) != 0) {
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
    }

    return true;
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
