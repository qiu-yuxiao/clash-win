#include "clashwin/network/unlock_checker.h"
#include <windows.h>
#include <winhttp.h>
#include <thread>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <iostream>

#pragma comment(lib, "winhttp.lib")

namespace clashwin::network {

namespace {

std::string GetCurrentTimestampString() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    struct tm buf;
    localtime_s(&buf, &in_time_t);
    ss << std::put_time(&buf, "%H:%M:%S");
    return ss.str();
}

std::string ExtractLocFromTrace(const std::string& trace_body) {
    std::istringstream iss(trace_body);
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.starts_with("loc=")) {
            std::string loc = line.substr(4);
            loc.erase(0, loc.find_first_not_of(" \t"));
            size_t end = loc.find_last_not_of(" \t");
            if (end != std::string::npos) loc = loc.substr(0, end + 1);
            return loc;
        }
    }
    return "";
}

} // namespace

UnlockChecker& UnlockChecker::Instance() {
    static UnlockChecker instance;
    return instance;
}

const std::vector<std::string>& UnlockChecker::GetDefaultPlatformNames() {
    static const std::vector<std::string> names = {
        "ChatGPT Web",
        "ChatGPT iOS",
        "Claude",
        "Gemini",
        "YouTube Premium",
        "Netflix",
        "Disney+",
        "Spotify",
        "TikTok",
        "哔哩哔哩港澳台",
        "巴哈姆特动画疯"
    };
    return names;
}

UnlockChecker::UnlockChecker() {
    for (const auto& name : GetDefaultPlatformNames()) {
        results_.push_back({name, "Pending", "", ""});
    }
}

std::string UnlockChecker::FormatRegion(const std::string& country_code) {
    if (country_code.empty()) return "";
    std::string upper = country_code;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (upper == "US" || upper == "USA") return "🇺🇸 US";
    if (upper == "HK" || upper == "HKG") return "🇭🇰 HK";
    if (upper == "JP" || upper == "JPN") return "🇯🇵 JP";
    if (upper == "SG" || upper == "SGP") return "🇸🇬 SG";
    if (upper == "TW" || upper == "TWN") return "🇹🇼 TW";
    if (upper == "GB" || upper == "GBR" || upper == "UK") return "🇬🇧 UK";
    if (upper == "DE" || upper == "DEU") return "🇩🇪 DE";
    if (upper == "FR" || upper == "FRA") return "🇫🇷 FR";
    if (upper == "CA" || upper == "CAN") return "🇨🇦 CA";
    if (upper == "AU" || upper == "AUS") return "🇦🇺 AU";
    if (upper == "KR" || upper == "KOR") return "🇰🇷 KR";
    if (upper == "CN" || upper == "CHN") return "🇨🇳 CN";
    return upper;
}

bool UnlockChecker::WinHttpProbe(const std::string& url,
                                const std::string& proxy,
                                uint32_t timeout_ms,
                                int& out_status_code,
                                std::string& out_body,
                                std::string& out_headers) {
    out_status_code = 0;
    out_body.clear();
    out_headers.clear();

    // 解析 URL
    std::wstring wUrl(url.begin(), url.end());
    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = static_cast<DWORD>(-1);
    urlComp.dwHostNameLength = static_cast<DWORD>(-1);
    urlComp.dwUrlPathLength = static_cast<DWORD>(-1);
    urlComp.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wUrl.c_str(), static_cast<DWORD>(wUrl.size()), 0, &urlComp)) {
        return false;
    }

    std::wstring wHost(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring wPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.dwExtraInfoLength > 0) {
        wPath.append(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }
    if (wPath.empty()) wPath = L"/";

    bool is_https = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
    INTERNET_PORT port = urlComp.nPort;

    DWORD accessType = proxy.empty() ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_NAMED_PROXY;
    std::wstring wProxy(proxy.begin(), proxy.end());
    LPCWSTR proxyParam = proxy.empty() ? WINHTTP_NO_PROXY_NAME : wProxy.c_str();

    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) ClashWin/1.0",
                                     accessType, proxyParam, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    WinHttpSetTimeouts(hSession, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 禁用证书强校验避免自签名中间人导致的误报
    if (is_https) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    BOOL sendOk = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sendOk || !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
    out_status_code = static_cast<int>(statusCode);

    // 读取响应内容
    DWORD dwDownloaded = 0;
    std::string responseData;
    do {
        DWORD dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0) break;
        std::vector<char> buf(dwSize + 1);
        if (WinHttpReadData(hRequest, buf.data(), dwSize, &dwDownloaded)) {
            responseData.append(buf.data(), dwDownloaded);
        }
        if (responseData.size() > 1024 * 1024) break; // 保护上限 1MB
    } while (dwDownloaded > 0);

    out_body = std::move(responseData);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return true;
}

UnlockResultItem UnlockChecker::CheckChatGPTWeb(const std::string& proxy, uint32_t timeout_ms) {
    int code = 0;
    std::string body, headers;

    // 1. 获取出口地区
    std::string region;
    if (WinHttpProbe("https://chat.openai.com/cdn-cgi/trace", proxy, timeout_ms, code, body, headers)) {
        region = FormatRegion(ExtractLocFromTrace(body));
    }

    // 2. 检查合规接口
    bool ok = WinHttpProbe("https://api.openai.com/compliance/cookie_requirements", proxy, timeout_ms, code, body, headers);
    std::string status = "Failed";
    if (ok) {
        if (code == 200 || code == 403) {
            if (body.find("unsupported_country") != std::string::npos) {
                status = "No";
            } else {
                status = "Yes";
            }
        }
    }

    return {"ChatGPT Web", status, region, GetCurrentTimestampString()};
}

UnlockResultItem UnlockChecker::CheckChatGPTiOS(const std::string& proxy, uint32_t timeout_ms) {
    int code = 0;
    std::string body, headers;
    bool ok = WinHttpProbe("https://ios.chat.openai.com/", proxy, timeout_ms, code, body, headers);

    std::string status = "Failed";
    if (ok) {
        std::string body_lower = body;
        std::transform(body_lower.begin(), body_lower.end(), body_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (body_lower.find("disallowed isp") != std::string::npos) {
            status = "Disallowed ISP";
        } else if (body_lower.find("request is not allowed") != std::string::npos || code == 200 || code == 403) {
            status = "Yes";
        } else if (body_lower.find("blocked") != std::string::npos) {
            status = "No";
        }
    }
    return {"ChatGPT iOS", status, "", GetCurrentTimestampString()};
}

UnlockResultItem UnlockChecker::CheckClaude(const std::string& proxy, uint32_t timeout_ms) {
    int code = 0;
    std::string body, headers;
    bool ok = WinHttpProbe("https://claude.ai/cdn-cgi/trace", proxy, timeout_ms, code, body, headers);

    std::string status = "Failed";
    std::string region;
    if (ok) {
        std::string loc = ExtractLocFromTrace(body);
        region = FormatRegion(loc);
        if (!loc.empty()) {
            static const std::vector<std::string> blocked = {"CN", "HK", "MO", "RU", "IR", "KP", "SY", "CU", "BY", "AF"};
            if (std::find(blocked.begin(), blocked.end(), loc) != blocked.end()) {
                status = "No";
            } else {
                status = "Yes";
            }
        }
    }
    return {"Claude", status, region, GetCurrentTimestampString()};
}

UnlockResultItem UnlockChecker::CheckGemini(const std::string& proxy, uint32_t timeout_ms) {
    int code = 0;
    std::string body, headers;
    bool ok = WinHttpProbe("https://gemini.google.com", proxy, timeout_ms, code, body, headers);

    std::string status = "Failed";
    std::string region;
    if (ok) {
        size_t pos = body.find(",2,1,200,\"");
        if (pos != std::string::npos) {
            std::string code3 = body.substr(pos + 10, 3);
            region = FormatRegion(code3);
            static const std::vector<std::string> blocked = {"CHN", "HKG", "MAC", "RUS", "BLR", "IRN", "PRK", "SYR", "CUB"};
            if (std::find(blocked.begin(), blocked.end(), code3) != blocked.end()) {
                status = "No";
            } else {
                status = "Yes";
            }
        } else if (code == 200) {
            status = "Yes";
        }
    }
    return {"Gemini", status, region, GetCurrentTimestampString()};
}

UnlockResultItem UnlockChecker::CheckYouTube(const std::string& proxy, uint32_t timeout_ms) {
    int code = 0;
    std::string body, headers;
    bool ok = WinHttpProbe("https://www.youtube.com/premium?hl=en", proxy, timeout_ms, code, body, headers);

    std::string status = "Failed";
    std::string region;
    if (ok) {
        std::string body_lower = body;
        std::transform(body_lower.begin(), body_lower.end(), body_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (body_lower.find("premium is not available in your country") != std::string::npos ||
            body_lower.find("premium is not available in your region") != std::string::npos) {
            status = "No";
        } else if (code >= 200 && code < 300) {
            status = "Yes";
            size_t gl_pos = body.find("\"GL\":\"");
            if (gl_pos != std::string::npos) {
                region = FormatRegion(body.substr(gl_pos + 6, 2));
            }
        }
    }
    return {"YouTube Premium", status, region, GetCurrentTimestampString()};
}

UnlockResultItem UnlockChecker::CheckNetflix(const std::string& proxy, uint32_t timeout_ms) {
    int code1 = 0, code2 = 0;
    std::string body1, body2, headers;
    bool ok1 = WinHttpProbe("https://www.netflix.com/title/81280792", proxy, timeout_ms, code1, body1, headers);
    bool ok2 = WinHttpProbe("https://www.netflix.com/title/70143836", proxy, timeout_ms, code2, body2, headers);

    std::string status = "Failed";
    if (ok1 || ok2) {
        if (code1 == 404 && code2 == 404) {
            status = "Originals Only";
        } else if (code1 == 200 || code2 == 200 || code1 == 301 || code2 == 301) {
            status = "Yes";
        } else if (code1 == 403 || code2 == 403) {
            status = "No";
        }
    }
    return {"Netflix", status, "", GetCurrentTimestampString()};
}

UnlockResultItem UnlockChecker::CheckDisneyPlus(const std::string& proxy, uint32_t timeout_ms) {
    int code = 0;
    std::string body, headers;
    bool ok = WinHttpProbe("https://www.disneyplus.com", proxy, timeout_ms, code, body, headers);

    std::string status = "Failed";
    if (ok) {
        if (code >= 200 && code < 400) {
            status = "Yes";
        } else if (code == 403 || code == 451) {
            status = "No";
        }
    }
    return {"Disney+", status, "", GetCurrentTimestampString()};
}

UnlockResultItem UnlockChecker::CheckSpotify(const std::string& proxy, uint32_t timeout_ms) {
    int code = 0;
    std::string body, headers;
    bool ok = WinHttpProbe("https://www.spotify.com/api/content/v1/country-selector?platform=web&format=json", proxy, timeout_ms, code, body, headers);

    std::string status = "Failed";
    if (ok) {
        if (code == 403 || code == 451) {
            status = "No";
        } else if (code >= 200 && code < 300) {
            status = "Yes";
        }
    }
    return {"Spotify", status, "", GetCurrentTimestampString()};
}

UnlockResultItem UnlockChecker::CheckTikTok(const std::string& proxy, uint32_t timeout_ms) {
    int code = 0;
    std::string body, headers;
    bool ok = WinHttpProbe("https://www.tiktok.com/cdn-cgi/trace", proxy, timeout_ms, code, body, headers);

    std::string status = "Failed";
    std::string region;
    if (ok) {
        std::string loc = ExtractLocFromTrace(body);
        region = FormatRegion(loc);
        if (code >= 200 && code < 400 && !loc.empty()) {
            status = "Yes";
        } else if (code == 403) {
            status = "No";
        }
    }
    return {"TikTok", status, region, GetCurrentTimestampString()};
}

UnlockResultItem UnlockChecker::CheckBilibili(const std::string& proxy, uint32_t timeout_ms) {
    int code = 0;
    std::string body, headers;
    bool ok = WinHttpProbe("https://api.bilibili.com/pgc/player/web/playurl?avid=18281381&cid=29892777&ep_id=183799", proxy, timeout_ms, code, body, headers);

    std::string status = "Failed";
    if (ok) {
        if (body.find("\"code\":0") != std::string::npos) {
            status = "Yes";
        } else if (body.find("-10403") != std::string::npos) {
            status = "No";
        }
    }
    return {"哔哩哔哩港澳台", status, "🇭🇰/🇹🇼", GetCurrentTimestampString()};
}

UnlockResultItem UnlockChecker::CheckBahamut(const std::string& proxy, uint32_t timeout_ms) {
    int code = 0;
    std::string body, headers;
    bool ok = WinHttpProbe("https://ani.gamer.com.tw/", proxy, timeout_ms, code, body, headers);

    std::string status = "Failed";
    if (ok) {
        if (code >= 200 && code < 400) {
            if (body.find("anime") != std::string::npos || body.find("gamer") != std::string::npos) {
                status = "Yes";
            }
        } else if (code == 403) {
            status = "No";
        }
    }
    return {"巴哈姆特动画疯", status, "🇹🇼 TW", GetCurrentTimestampString()};
}

std::vector<UnlockResultItem> UnlockChecker::RunAllChecks(const std::string& proxy_addr, uint32_t timeout_ms) {
    std::vector<UnlockResultItem> res;
    res.push_back(CheckChatGPTWeb(proxy_addr, timeout_ms));
    res.push_back(CheckChatGPTiOS(proxy_addr, timeout_ms));
    res.push_back(CheckClaude(proxy_addr, timeout_ms));
    res.push_back(CheckGemini(proxy_addr, timeout_ms));
    res.push_back(CheckYouTube(proxy_addr, timeout_ms));
    res.push_back(CheckNetflix(proxy_addr, timeout_ms));
    res.push_back(CheckDisneyPlus(proxy_addr, timeout_ms));
    res.push_back(CheckSpotify(proxy_addr, timeout_ms));
    res.push_back(CheckTikTok(proxy_addr, timeout_ms));
    res.push_back(CheckBilibili(proxy_addr, timeout_ms));
    res.push_back(CheckBahamut(proxy_addr, timeout_ms));

    {
        std::lock_guard<std::mutex> lock(mutex_);
        results_ = res;
    }
    return res;
}

void UnlockChecker::StartCheckAsync(const std::string& proxy_addr, uint32_t timeout_ms) {
    if (is_checking_.exchange(true)) return;

    std::thread([this, proxy_addr, timeout_ms]() {
        RunAllChecks(proxy_addr, timeout_ms);
        is_checking_.store(false);
    }).detach();
}

std::vector<UnlockResultItem> UnlockChecker::GetResults() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return results_;
}

} // namespace clashwin::network
