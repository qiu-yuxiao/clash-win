#pragma once

#include <string>
#include <windows.h>

namespace clashwin::system {

enum class ServiceState {
    Ready,                   // 服务正在运行 (SERVICE_RUNNING)
    StartRequired,           // 服务已安装但处于停止状态 (SERVICE_STOPPED)
    InstallRequired,         // 服务未安装 (ERROR_SERVICE_DOES_NOT_EXIST)
    Unavailable              // SCM 访问失败或服务异常
};

class ServiceManager {
public:
    static ServiceManager& Instance();

    // 检查当前进程是否具有管理员特权 (Token Membership: Administrators)
    static bool IsProcessElevated();

    // 查询 clash_verge_service 服务状态
    ServiceState QueryServiceState(std::wstring* out_detail = nullptr);

    // 尝试启动服务
    bool StartClashService(std::wstring* out_error = nullptr);

    // 尝试停止服务
    bool StopClashService(std::wstring* out_error = nullptr);

    // 安装系统服务 (调用 clash-verge-service-install.exe)
    bool InstallClashService(std::wstring* out_error = nullptr);

    // 卸载系统服务 (调用 clash-verge-service-uninstall.exe)
    bool UninstallClashService(std::wstring* out_error = nullptr);

    // 评估 TUN 模式是否可用；如果不可用且非管理员，自动执行降级逻辑 (BUG-298 防御)
    // 返回 true 表示可以使用 TUN (当前为管理员或服务 Ready)；
    // 返回 false 表示不可用，已自动安全回退系统代理
    bool EvaluateTunAndFallback(bool request_tun,
                                const std::wstring& mixed_proxy_addr,
                                std::wstring* out_fallback_msg = nullptr);

private:
    ServiceManager() = default;
    ~ServiceManager() = default;

    static constexpr const wchar_t* kServiceName = L"clash_verge_service";
};

} // namespace clashwin::system
