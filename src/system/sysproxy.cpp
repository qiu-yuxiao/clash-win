#include <windows.h>
#include <wininet.h>
#include "clashwin/system/sysproxy.h"
#include "clashwin/common/constants.h"

#pragma comment(lib, "wininet.lib")

namespace clashwin::system {

namespace {

const wchar_t* kInternetSettingsKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrl_type) {
    switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        SystemProxyManager::Instance().ResetAll();
        return FALSE; // 允许默认流程继续终止进程
    default:
        return FALSE;
    }
}

LONG WINAPI CrashRollbackHandler(EXCEPTION_POINTERS* pExceptionInfo) {
    (void)pExceptionInfo;
    SystemProxyManager::Instance().ResetAll();
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

SystemProxyManager& SystemProxyManager::Instance() {
    static SystemProxyManager instance;
    return instance;
}

SystemProxyManager::~SystemProxyManager() {
    // 进程析构时确保注册表代理复原
    ResetAll();
}

bool SystemProxyManager::EnableProxy(const std::wstring& server_address, const std::wstring& bypass_list) {
    std::lock_guard<std::mutex> lock(mutex_);
    HKEY hKey = NULL;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInternetSettingsKey, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    // 1. 先清空 AutoConfigURL (PAC 铁律顺序)
    RegDeleteValueW(hKey, L"AutoConfigURL");

    // 2. 设置 ProxyEnable = 1
    DWORD enable = 1;
    RegSetValueExW(hKey, L"ProxyEnable", 0, REG_DWORD, (const BYTE*)&enable, sizeof(enable));

    // 3. 设置 ProxyServer = "127.0.0.1:10801"
    RegSetValueExW(hKey, L"ProxyServer", 0, REG_SZ,
                   (const BYTE*)server_address.c_str(),
                   (DWORD)((server_address.length() + 1) * sizeof(wchar_t)));

    // 4. 设置 ProxyOverride
    const std::wstring& bypass = bypass_list.empty() ? std::wstring(network::kDefaultProxyBypass) : bypass_list;
    RegSetValueExW(hKey, L"ProxyOverride", 0, REG_SZ,
                   (const BYTE*)bypass.c_str(),
                   (DWORD)((bypass.length() + 1) * sizeof(wchar_t)));

    RegCloseKey(hKey);

    NotifySettingsChanged();
    return true;
}

bool SystemProxyManager::DisableProxy() {
    std::lock_guard<std::mutex> lock(mutex_);
    HKEY hKey = NULL;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInternetSettingsKey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    // 1. 设置 ProxyEnable = 0
    DWORD enable = 0;
    RegSetValueExW(hKey, L"ProxyEnable", 0, REG_DWORD, (const BYTE*)&enable, sizeof(enable));

    // 2. 清空 ProxyServer
    std::wstring empty = L"";
    RegSetValueExW(hKey, L"ProxyServer", 0, REG_SZ,
                   (const BYTE*)empty.c_str(),
                   (DWORD)((empty.length() + 1) * sizeof(wchar_t)));

    RegCloseKey(hKey);

    NotifySettingsChanged();
    return true;
}

bool SystemProxyManager::EnablePac(const std::wstring& pac_url) {
    std::lock_guard<std::mutex> lock(mutex_);
    HKEY hKey = NULL;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInternetSettingsKey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    // 1. 先关闭 Sysproxy 标志与地址
    DWORD enable = 0;
    RegSetValueExW(hKey, L"ProxyEnable", 0, REG_DWORD, (const BYTE*)&enable, sizeof(enable));
    std::wstring empty = L"";
    RegSetValueExW(hKey, L"ProxyServer", 0, REG_SZ,
                   (const BYTE*)empty.c_str(),
                   (DWORD)((empty.length() + 1) * sizeof(wchar_t)));

    // 2. 设置 AutoConfigURL
    RegSetValueExW(hKey, L"AutoConfigURL", 0, REG_SZ,
                   (const BYTE*)pac_url.c_str(),
                   (DWORD)((pac_url.length() + 1) * sizeof(wchar_t)));

    RegCloseKey(hKey);

    NotifySettingsChanged();
    return true;
}

bool SystemProxyManager::DisablePac() {
    std::lock_guard<std::mutex> lock(mutex_);
    HKEY hKey = NULL;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInternetSettingsKey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    RegDeleteValueW(hKey, L"AutoConfigURL");
    RegCloseKey(hKey);

    NotifySettingsChanged();
    return true;
}

bool SystemProxyManager::ResetAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    HKEY hKey = NULL;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInternetSettingsKey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    // 1. ProxyEnable = 0
    DWORD enable = 0;
    RegSetValueExW(hKey, L"ProxyEnable", 0, REG_DWORD, (const BYTE*)&enable, sizeof(enable));

    // 2. 清空 ProxyServer
    std::wstring empty = L"";
    RegSetValueExW(hKey, L"ProxyServer", 0, REG_SZ,
                   (const BYTE*)empty.c_str(),
                   (DWORD)((empty.length() + 1) * sizeof(wchar_t)));

    // 3. 删除 AutoConfigURL
    RegDeleteValueW(hKey, L"AutoConfigURL");

    RegCloseKey(hKey);

    NotifySettingsChanged();
    return true;
}

bool SystemProxyManager::IsProxyEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    HKEY hKey = NULL;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInternetSettingsKey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    DWORD enable = 0;
    DWORD size = sizeof(enable);
    DWORD type = REG_DWORD;
    bool result = false;

    if (RegQueryValueExW(hKey, L"ProxyEnable", NULL, &type, (LPBYTE)&enable, &size) == ERROR_SUCCESS) {
        result = (enable != 0);
    }

    RegCloseKey(hKey);
    return result;
}

bool SystemProxyManager::IsPacEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    HKEY hKey = NULL;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInternetSettingsKey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t buf[1024] = {0};
    DWORD size = sizeof(buf);
    DWORD type = REG_SZ;
    bool result = false;

    if (RegQueryValueExW(hKey, L"AutoConfigURL", NULL, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS) {
        result = (buf[0] != L'\0');
    }

    RegCloseKey(hKey);
    return result;
}

ProxyMode SystemProxyManager::GetCurrentMode() const {
    if (IsProxyEnabled()) {
        return ProxyMode::SystemProxy;
    }
    if (IsPacEnabled()) {
        return ProxyMode::Pac;
    }
    return ProxyMode::Direct;
}

std::wstring SystemProxyManager::GetCurrentProxyServer() const {
    std::lock_guard<std::mutex> lock(mutex_);
    HKEY hKey = NULL;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInternetSettingsKey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return L"";
    }

    wchar_t buffer[1024] = {0};
    DWORD buffer_size = sizeof(buffer);
    DWORD type = REG_SZ;
    std::wstring result = L"";

    if (RegQueryValueExW(hKey, L"ProxyServer", NULL, &type, (LPBYTE)buffer, &buffer_size) == ERROR_SUCCESS) {
        result = buffer;
    }

    RegCloseKey(hKey);
    return result;
}

std::wstring SystemProxyManager::GetCurrentPacUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    HKEY hKey = NULL;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInternetSettingsKey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return L"";
    }

    wchar_t buffer[1024] = {0};
    DWORD buffer_size = sizeof(buffer);
    DWORD type = REG_SZ;
    std::wstring result = L"";

    if (RegQueryValueExW(hKey, L"AutoConfigURL", NULL, &type, (LPBYTE)buffer, &buffer_size) == ERROR_SUCCESS) {
        result = buffer;
    }

    RegCloseKey(hKey);
    return result;
}

std::wstring SystemProxyManager::GetCurrentBypassList() const {
    std::lock_guard<std::mutex> lock(mutex_);
    HKEY hKey = NULL;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, kInternetSettingsKey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return L"";
    }

    wchar_t buffer[4096] = {0};
    DWORD buffer_size = sizeof(buffer);
    DWORD type = REG_SZ;
    std::wstring result = L"";

    if (RegQueryValueExW(hKey, L"ProxyOverride", NULL, &type, (LPBYTE)buffer, &buffer_size) == ERROR_SUCCESS) {
        result = buffer;
    }

    RegCloseKey(hKey);
    return result;
}

void SystemProxyManager::NotifySettingsChanged() {
    // 遵循 Windows 铁律：双步刷新系统 WinINET 缓存
    InternetSetOptionW(NULL, INTERNET_OPTION_SETTINGS_CHANGED, NULL, 0);
    InternetSetOptionW(NULL, INTERNET_OPTION_REFRESH, NULL, 0);
}

void SystemProxyManager::InstallCrashRollbackHandler() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (crash_handler_installed_) return;

    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    AddVectoredExceptionHandler(0, CrashRollbackHandler);
    crash_handler_installed_ = true;
}

} // namespace clashwin::system
