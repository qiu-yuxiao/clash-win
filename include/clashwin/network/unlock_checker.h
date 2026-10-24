#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <optional>
#include <cstdint>

namespace clashwin::network {

struct UnlockResultItem {
    std::string name;       // 平台名称 (例如 "ChatGPT Web", "Claude", "Netflix")
    std::string status;     // "Yes", "No", "Originals Only", "Disallowed ISP", "Failed", "Pending"
    std::string region;     // 地区代码/国旗 (例如 "US", "JP", "HK", "SG")
    std::string check_time; // 探测时间戳
};

class UnlockChecker {
public:
    static UnlockChecker& Instance();

    // 默认平台列表
    static const std::vector<std::string>& GetDefaultPlatformNames();

    // 异步触发全量 11 项解锁检测
    void StartCheckAsync(const std::string& proxy_addr = "127.0.0.1:10801", uint32_t timeout_ms = 4000);

    // 同步执行全量检测
    std::vector<UnlockResultItem> RunAllChecks(const std::string& proxy_addr = "127.0.0.1:10801", uint32_t timeout_ms = 4000);

    // 单项平台检测方法
    UnlockResultItem CheckChatGPTWeb(const std::string& proxy, uint32_t timeout_ms);
    UnlockResultItem CheckChatGPTiOS(const std::string& proxy, uint32_t timeout_ms);
    UnlockResultItem CheckClaude(const std::string& proxy, uint32_t timeout_ms);
    UnlockResultItem CheckGemini(const std::string& proxy, uint32_t timeout_ms);
    UnlockResultItem CheckYouTube(const std::string& proxy, uint32_t timeout_ms);
    UnlockResultItem CheckNetflix(const std::string& proxy, uint32_t timeout_ms);
    UnlockResultItem CheckDisneyPlus(const std::string& proxy, uint32_t timeout_ms);
    UnlockResultItem CheckSpotify(const std::string& proxy, uint32_t timeout_ms);
    UnlockResultItem CheckTikTok(const std::string& proxy, uint32_t timeout_ms);
    UnlockResultItem CheckBilibili(const std::string& proxy, uint32_t timeout_ms);
    UnlockResultItem CheckBahamut(const std::string& proxy, uint32_t timeout_ms);

    // 获取检测结果与当前运行状态
    [[nodiscard]] std::vector<UnlockResultItem> GetResults() const;
    [[nodiscard]] bool IsChecking() const { return is_checking_; }

    // WinHTTP 基础探针
    static bool WinHttpProbe(const std::string& url,
                             const std::string& proxy,
                             uint32_t timeout_ms,
                             int& out_status_code,
                             std::string& out_body,
                             std::string& out_headers);

    // 地区代码转国旗 Emoji (例如 "US" -> "🇺🇸US")
    static std::string FormatRegion(const std::string& country_code);

private:
    UnlockChecker();
    ~UnlockChecker() = default;

    mutable std::mutex mutex_;
    std::vector<UnlockResultItem> results_;
    std::atomic<bool> is_checking_{false};
};

} // namespace clashwin::network
