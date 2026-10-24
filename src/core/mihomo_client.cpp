#include <windows.h>
#include <winhttp.h>
#include "clashwin/core/mihomo_client.h"
#include <sstream>
#include <iostream>

#pragma comment(lib, "winhttp.lib")

namespace clashwin::core {

MihomoClient& MihomoClient::Instance() {
    static MihomoClient instance;
    return instance;
}

bool MihomoClient::HttpRequest(const std::wstring& method, const std::wstring& path,
                              const std::string& body, std::string& out_resp, uint16_t port) {
    HINTERNET hSession = WinHttpOpen(L"ClashWin-MihomoClient/1.0",
                                     WINHTTP_ACCESS_TYPE_NO_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, method.c_str(), path.c_str(),
                                           NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 设置 2 秒超时
    WinHttpSetTimeouts(hRequest, 1000, 1000, 2000, 2000);

    LPCWSTR headers = L"Content-Type: application/json\r\n";
    BOOL bSend = WinHttpSendRequest(hRequest,
                                   headers, (DWORD)wcslen(headers),
                                   body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                                   (DWORD)body.size(),
                                   (DWORD)body.size(), 0);

    if (bSend) {
        bSend = WinHttpReceiveResponse(hRequest, NULL);
    }

    if (!bSend) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 检查状态码
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    std::string resp_data;
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;

        std::vector<char> buffer(dwSize + 1, 0);
        if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
            resp_data.append(buffer.data(), dwDownloaded);
        }
    } while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    out_resp = std::move(resp_data);
    return (statusCode >= 200 && statusCode < 300);
}

bool MihomoClient::CheckHealth(uint16_t port) {
    std::string resp;
    return HttpRequest(L"GET", L"/version", "", resp, port);
}

std::string MihomoClient::GetVersion(uint16_t port) {
    std::string resp;
    if (HttpRequest(L"GET", L"/version", "", resp, port)) {
        auto pos = resp.find("\"version\":");
        if (pos != std::string::npos) {
            auto start = resp.find('"', pos + 10);
            if (start != std::string::npos) {
                auto end = resp.find('"', start + 1);
                if (end != std::string::npos) {
                    return resp.substr(start + 1, end - start - 1);
                }
            }
        }
        return resp;
    }
    return "";
}

std::string MihomoClient::GetCurrentSelectedProxy(uint16_t port) {
    std::string resp;
    if (HttpRequest(L"GET", L"/proxies/PROXY", "", resp, port)) {
        auto pos = resp.find("\"now\":");
        if (pos != std::string::npos) {
            auto start = resp.find('"', pos + 6);
            if (start != std::string::npos) {
                auto end = resp.find('"', start + 1);
                if (end != std::string::npos) {
                    return resp.substr(start + 1, end - start - 1);
                }
            }
        }
    }
    return "";
}

bool MihomoClient::SelectProxy(const std::string& group_name, const std::string& proxy_name, uint16_t port) {
    std::wstring wGroup(group_name.begin(), group_name.end());
    std::wstring path = L"/proxies/" + wGroup;
    std::string body = "{\"name\":\"" + proxy_name + "\"}";
    std::string resp;
    return HttpRequest(L"PUT", path, body, resp, port);
}

int MihomoClient::TestProxyDelay(const std::string& proxy_name, uint16_t port, uint32_t timeout_ms) {
    std::wstring wName(proxy_name.begin(), proxy_name.end());
    std::wstring path = L"/proxies/" + wName + L"/delay?timeout=" + std::to_wstring(timeout_ms) + L"&url=http://www.gstatic.com/generate_204";
    std::string resp;
    if (HttpRequest(L"GET", path, "", resp, port)) {
        auto pos = resp.find("\"delay\":");
        if (pos != std::string::npos) {
            int delay = std::stoi(resp.substr(pos + 8));
            return delay;
        }
    }
    return -1;
}

bool MihomoClient::GetConnectionsRaw(std::string& out_resp, uint16_t port) {
    return HttpRequest(L"GET", L"/connections", "", out_resp, port);
}

bool MihomoClient::CloseConnection(const std::string& id, uint16_t port) {
    std::wstring wId(id.begin(), id.end());
    std::wstring path = L"/connections/" + wId;
    std::string resp;
    return HttpRequest(L"DELETE", path, "", resp, port);
}

bool MihomoClient::CloseAllConnections(uint16_t port) {
    std::string resp;
    return HttpRequest(L"DELETE", L"/connections", "", resp, port);
}

} // namespace clashwin::core
