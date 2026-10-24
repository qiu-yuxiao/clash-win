#include "clashwin/system/service_manager.h"
#include "clashwin/system/sysproxy.h"
#include <shellapi.h>
#include <filesystem>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace clashwin::system {

ServiceManager& ServiceManager::Instance() {
    static ServiceManager instance;
    return instance;
}

bool ServiceManager::IsProcessElevated() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

ServiceState ServiceManager::QueryServiceState(std::wstring* out_detail) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) {
        if (out_detail) *out_detail = L"无法打开服务控制管理器 (SCM)";
        return ServiceState::Unavailable;
    }

    SC_HANDLE hService = OpenServiceW(hSCM, kServiceName, SERVICE_QUERY_STATUS);
    if (!hService) {
        DWORD err = GetLastError();
        CloseServiceHandle(hSCM);
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            if (out_detail) *out_detail = L"服务 clash_verge_service 未安装";
            return ServiceState::InstallRequired;
        }
        if (out_detail) *out_detail = L"无法打开指定服务: 错误码 " + std::to_wstring(err);
        return ServiceState::Unavailable;
    }

    SERVICE_STATUS_PROCESS ssp = {0};
    DWORD bytesNeeded = 0;
    ServiceState result = ServiceState::Unavailable;

    if (QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        switch (ssp.dwCurrentState) {
        case SERVICE_RUNNING:
            result = ServiceState::Ready;
            if (out_detail) *out_detail = L"服务处于运行中状态 (Ready)";
            break;
        case SERVICE_STOPPED:
            result = ServiceState::StartRequired;
            if (out_detail) *out_detail = L"服务已安装但已停止 (StartRequired)";
            break;
        case SERVICE_START_PENDING:
            result = ServiceState::Ready;
            if (out_detail) *out_detail = L"服务正在启动中 (StartPending)";
            break;
        default:
            result = ServiceState::Unavailable;
            if (out_detail) *out_detail = L"服务处于异常或过渡状态: " + std::to_wstring(ssp.dwCurrentState);
            break;
        }
    } else {
        if (out_detail) *out_detail = L"查询服务状态失败: 错误码 " + std::to_wstring(GetLastError());
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    return result;
}

bool ServiceManager::StartClashService(std::wstring* out_error) {
    if (IsProcessElevated()) {
        SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
        if (!hSCM) {
            if (out_error) *out_error = L"无法连接 SCM";
            return false;
        }

        SC_HANDLE hService = OpenServiceW(hSCM, kServiceName, SERVICE_START | SERVICE_QUERY_STATUS);
        if (!hService) {
            CloseServiceHandle(hSCM);
            if (out_error) *out_error = L"无法打开服务 (可能未安装)";
            return false;
        }

        BOOL ok = StartServiceW(hService, 0, NULL);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_SERVICE_ALREADY_RUNNING) {
                ok = TRUE;
            } else {
                if (out_error) *out_error = L"StartServiceW 失败: 错误码 " + std::to_wstring(err);
            }
        }

        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        return ok != FALSE;
    }

    // 非管理员：使用 runas 触发 UAC 提权执行 sc.exe start
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = L"sc.exe";
    sei.lpParameters = L"start clash_verge_service";
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (!ShellExecuteExW(&sei)) {
        if (out_error) *out_error = L"用户取消或 UAC 提权启动服务失败";
        return false;
    }

    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 5000);
        CloseHandle(sei.hProcess);
    }

    return QueryServiceState() == ServiceState::Ready;
}

bool ServiceManager::StopClashService(std::wstring* out_error) {
    if (IsProcessElevated()) {
        SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
        if (!hSCM) {
            if (out_error) *out_error = L"无法连接 SCM";
            return false;
        }

        SC_HANDLE hService = OpenServiceW(hSCM, kServiceName, SERVICE_STOP);
        if (!hService) {
            CloseServiceHandle(hSCM);
            if (out_error) *out_error = L"无法打开服务";
            return false;
        }

        SERVICE_STATUS status = {0};
        BOOL ok = ControlService(hService, SERVICE_CONTROL_STOP, &status);
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        return ok != FALSE;
    }

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = L"sc.exe";
    sei.lpParameters = L"stop clash_verge_service";
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (!ShellExecuteExW(&sei)) {
        if (out_error) *out_error = L"UAC 提权停止服务失败";
        return false;
    }

    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 5000);
        CloseHandle(sei.hProcess);
    }

    return true;
}

static std::filesystem::path FindServiceBinary(const std::wstring& filename) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    
    std::vector<std::filesystem::path> candidates = {
        exeDir / "sidecar" / filename,
        exeDir / filename,
        exeDir / ".." / "sidecar" / filename,
        std::filesystem::current_path() / "sidecar" / filename
    };
    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            return p;
        }
    }
    return {};
}

bool ServiceManager::InstallClashService(std::wstring* out_error) {
    std::filesystem::path installerPath = FindServiceBinary(L"clash-verge-service-install.exe");
    if (installerPath.empty()) {
        if (out_error) *out_error = L"未找到 clash-verge-service-install.exe 安装程序";
        return false;
    }

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = installerPath.c_str();
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (!ShellExecuteExW(&sei)) {
        if (out_error) *out_error = L"用户拒绝 UAC 提权或安装程序启动失败";
        return false;
    }

    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 15000);
        DWORD exitCode = 0;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        if (exitCode != 0) {
            if (out_error) *out_error = L"服务安装程序退出码异常: " + std::to_wstring(exitCode);
            return false;
        }
    }

    return QueryServiceState() == ServiceState::Ready || QueryServiceState() == ServiceState::StartRequired;
}

bool ServiceManager::UninstallClashService(std::wstring* out_error) {
    std::filesystem::path uninstallerPath = FindServiceBinary(L"clash-verge-service-uninstall.exe");
    if (uninstallerPath.empty()) {
        if (out_error) *out_error = L"未找到 clash-verge-service-uninstall.exe 卸载程序";
        return false;
    }

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = uninstallerPath.c_str();
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (!ShellExecuteExW(&sei)) {
        if (out_error) *out_error = L"用户拒绝 UAC 提权或卸载程序启动失败";
        return false;
    }

    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 15000);
        DWORD exitCode = 0;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
    }

    return QueryServiceState() == ServiceState::InstallRequired;
}

bool ServiceManager::EvaluateTunAndFallback(bool request_tun,
                                            const std::wstring& mixed_proxy_addr,
                                            std::wstring* out_fallback_msg) {
    if (!request_tun) {
        return true;
    }

    // 1. 若具有 Administrator 权限，直接允许 TUN (wintun 驱动直调免服务)
    if (IsProcessElevated()) {
        return true;
    }

    // 2. 普通权限：检查 SCM 服务状态
    ServiceState st = QueryServiceState();
    if (st == ServiceState::Ready) {
        return true;
    }

    if (st == ServiceState::StartRequired) {
        if (StartClashService()) {
            return true;
        }
    }

    // 3. 若未安装，尝试调用安装程序自动提权安装
    if (st == ServiceState::InstallRequired) {
        if (InstallClashService()) {
            st = QueryServiceState();
            if (st == ServiceState::Ready) {
                return true;
            }
            if (st == ServiceState::StartRequired && StartClashService()) {
                return true;
            }
        }
    }

    // 4. 服务不可用且普通权限：绝不静默失败，自动安全回退为系统代理 (BUG-298 防御)
    SystemProxyManager::Instance().EnableProxy(mixed_proxy_addr);
    if (out_fallback_msg) {
        *out_fallback_msg = L"系统服务未就绪且非管理员特权，TUN 模式无法生效，已自动安全回退为系统代理模式";
    }
    return false;
}

} // namespace clashwin::system
