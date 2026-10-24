#pragma once

#include "clashwin/common/types.h"
#include "clashwin/common/constants.h"
#include <string>
#include <optional>
#include <filesystem>
#include <atomic>
#include <mutex>

namespace clashwin::config {

class AppConfig {
public:
    static AppConfig& Instance();

    void Init(const std::filesystem::path& app_dir);

    // 加载与保存 verge.yaml
    bool LoadVergeConfig();
    bool SaveVergeConfig();

    // 窗口几何状态读写与节流保护 (window_state.json)
    bool LoadWindowState(WindowGeometry& out_geom);
    void SaveWindowStateThrottled(double width, double height, std::optional<double> x, std::optional<double> y);
    void SaveWindowStateImmediately(double width, double height, std::optional<double> x, std::optional<double> y);

    // 基础配置属性 Getter / Setter
    [[nodiscard]] const std::string& GetThemeMode() const { return theme_mode_; }
    void SetThemeMode(std::string_view mode) { theme_mode_ = mode; SaveVergeConfig(); }

    [[nodiscard]] const std::string& GetSkin() const { return skin_; }
    void SetSkin(std::string_view skin) { skin_ = skin; SaveVergeConfig(); }

    [[nodiscard]] float GetSlider1Val() const { return slider1_val_; }
    void SetSlider1Val(float val) { slider1_val_ = val; SaveVergeConfig(); }

    [[nodiscard]] float GetSlider2Val() const { return slider2_val_; }
    void SetSlider2Val(float val) { slider2_val_ = val; SaveVergeConfig(); }

    [[nodiscard]] const std::string& GetRuleFallback() const { return rule_fallback_; }
    void SetRuleFallback(std::string_view fallback) { rule_fallback_ = fallback; SaveVergeConfig(); }

    [[nodiscard]] bool IsSystemProxyEnabled() const { return enable_system_proxy_; }
    void SetSystemProxyEnabled(bool enabled) { enable_system_proxy_ = enabled; SaveVergeConfig(); }

    [[nodiscard]] bool IsTunModeEnabled() const { return enable_tun_mode_; }
    void SetTunModeEnabled(bool enabled) { enable_tun_mode_ = enabled; SaveVergeConfig(); }

    [[nodiscard]] bool IsIpv6Enabled() const { return enable_ipv6_; }
    void SetIpv6Enabled(bool enabled) { enable_ipv6_ = enabled; SaveVergeConfig(); }

    [[nodiscard]] bool IsAutoLaunchEnabled() const { return enable_auto_launch_; }
    void SetAutoLaunchEnabled(bool enabled) { enable_auto_launch_ = enabled; SaveVergeConfig(); }

    [[nodiscard]] bool IsSilentStartEnabled() const { return enable_silent_start_; }
    void SetSilentStartEnabled(bool enabled) { enable_silent_start_ = enabled; SaveVergeConfig(); }

    [[nodiscard]] bool IsAllowLanEnabled() const { return enable_allow_lan_; }
    void SetAllowLanEnabled(bool enabled) { enable_allow_lan_ = enabled; SaveVergeConfig(); }

    [[nodiscard]] uint16_t GetMixedPort() const { return verge_mixed_port_; }
    void SetMixedPort(uint16_t port) { verge_mixed_port_ = port; SaveVergeConfig(); }

    // 外部控制器 Bearer 秘钥（首次初始化时生成并持久化，写入内核 config 的 secret）
    [[nodiscard]] const std::string& GetSecret() const { return api_secret_; }

    [[nodiscard]] const std::wstring& GetSystemProxyBypass() const { return system_proxy_bypass_; }
    void SetSystemProxyBypass(const std::wstring& bypass) { system_proxy_bypass_ = bypass; SaveVergeConfig(); }

private:
    AppConfig();
    ~AppConfig();

    std::filesystem::path app_dir_;
    std::filesystem::path verge_yaml_path_;
    std::filesystem::path window_state_json_path_;

    // 配置字段
    uint32_t config_version_{1};
    std::string theme_mode_{"system"};
    std::string skin_{"retro-3d"};
    float slider1_val_{0.5f};
    float slider2_val_{2.5f};
    std::string rule_fallback_{"direct"}; // "direct" 出厂默认 (GFWList)
    bool enable_system_proxy_{false};
    bool enable_tun_mode_{false};
    bool enable_ipv6_{false};
    bool enable_auto_launch_{false};
    bool enable_silent_start_{false};
    bool enable_allow_lan_{false};
    bool auto_close_connection_{true};
    uint16_t verge_mixed_port_{10801};
    std::string api_secret_;
    std::string proxy_host_{"127.0.0.1"};
    std::wstring system_proxy_bypass_{network::kDefaultProxyBypass};

    // 几何状态落盘同步与版本锁
    std::atomic<uint64_t> geometry_seq_{0};
    std::atomic<uint64_t> written_seq_{0};
    std::atomic<int64_t> last_save_time_ms_{0};
    std::atomic<bool> trailing_write_armed_{false};
    std::mutex write_mutex_;
    WindowGeometry pending_geom_{};

    void FlushPendingGeometry();
    void WriteGeometrySync(uint64_t seq, const WindowGeometry& geom);
};

} // namespace clashwin::config
