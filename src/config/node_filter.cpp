#include "clashwin/config/node_filter.h"
#include <algorithm>
#include <vector>
#include <cctype>

namespace clashwin::config {

static std::string ToLower(std::string_view s) {
    std::string res;
    res.reserve(s.size());
    for (char c : s) {
        res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return res;
}

static std::string RemoveSpaces(std::string_view s) {
    std::string res;
    res.reserve(s.size());
    for (char c : s) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            res.push_back(c);
        }
    }
    return res;
}

bool NodeFilter::IsDummyNode(std::string_view name) {
    if (name.empty()) return true;

    std::string lower = ToLower(name);

    // 1. 英文子串命中（不区分大小写）
    static const char* kEnglishKeywords[] = {
        "traffic", "expire", "renew", "reset", "left",
        "http://", "https://", "sub.", "t.me/"
    };
    for (const auto* kw : kEnglishKeywords) {
        if (lower.find(kw) != std::string::npos) {
            return true;
        }
    }

    // 2. 中文广告过滤词（铁律：必须严格前缀比较 starts_with，严禁包含 contains）
    std::string clean = RemoveSpaces(name);
    static const std::string_view kChinesePrefixes[] = {
        "剩余流量", "过期时间", "重置时间", "购买订阅",
        "套餐到期", "官网", "发布页", "通知", "公告", "更新"
    };

    for (const auto& prefix : kChinesePrefixes) {
        if (clean.starts_with(prefix)) {
            return true;
        }
    }

    return false;
}

bool NodeFilter::IsValidNodeServer(std::string_view server) {
    if (server.empty()) return false;
    if (server == "127.0.0.1" || server == "0.0.0.0" || server == "1" || server == "localhost") {
        return false;
    }
    // 必须包含点（IPv4 或域名）或冒号（IPv6）
    return (server.find('.') != std::string_view::npos || server.find(':') != std::string_view::npos);
}

} // namespace clashwin::config
