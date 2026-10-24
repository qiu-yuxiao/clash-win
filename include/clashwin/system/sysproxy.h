#pragma once

#include <string>
#include <string_view>
#include <mutex>
#include <windows.h>

namespace clashwin::system {

enum class ProxyMode {
    Direct = 0,      // 无代理（直连）
    SystemProxy = 1, // 全局系统代理 (ProxyServer)
    Pac = 2          // 自动配置脚本 (AutoConfigURL)
};

class SystemProxyManager {
public:
    static SystemProxyManager& Instance();

    // 启用系统代理 (铁律顺序：先清除 PAC，再写入并启用 ProxyServer)
    bool EnableProxy(const std::wstring& server_address, const std::wstring& bypass_list = L"");

    // 关闭系统代理
    bool DisableProxy();

    // 启用 PAC 模式 (铁律顺序：先清除 ProxyServer 与 ProxyEnable，再写入 AutoConfigURL)
    bool EnablePac(const std::wstring& pac_url);

    // 关闭 PAC 模式
    bool DisablePac();

    // 彻底清空并重置所有代理设置 (ProxyEnable=0, ProxyServer="", AutoConfigURL 清除)
    bool ResetAll();

    // 查询当前系统代理开关状态
    bool IsProxyEnabled() const;

    // 查询当前 PAC 状态
    bool IsPacEnabled() const;

    // 获取当前代理模式
    ProxyMode GetCurrentMode() const;

    // 获取当前配置的代理服务器地址
    std::wstring GetCurrentProxyServer() const;

    // 获取当前配置的 PAC 脚本 URL
    std::wstring GetCurrentPacUrl() const;

    // 获取当前配置的绕过名单
    std::wstring GetCurrentBypassList() const;

    // 安装进程异常与控制台注销保护钩子，确保崩溃与退出时 100% 自动复原注册表
    void InstallCrashRollbackHandler();

private:
    SystemProxyManager() = default;
    ~SystemProxyManager();

    // 广播设置变更给当前会话所有程序与浏览器 (双步广播)
    void NotifySettingsChanged();

    mutable std::mutex mutex_;
    bool crash_handler_installed_{false};
};

// RAII 代理复原守卫：在析构时调用 ResetAll()
class SystemProxyGuard {
public:
    SystemProxyGuard() = default;
    ~SystemProxyGuard() {
        SystemProxyManager::Instance().ResetAll();
    }
    SystemProxyGuard(const SystemProxyGuard&) = delete;
    SystemProxyGuard& operator=(const SystemProxyGuard&) = delete;
};

} // namespace clashwin::system
