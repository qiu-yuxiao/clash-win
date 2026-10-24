#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <windows.h>
#include "clashwin/system/sysproxy.h"
#include "clashwin/system/service_manager.h"
#include "clashwin/config/tun_compiler.h"
#include "clashwin/config/app_config.h"

#define TEST_CHECK(expr, msg) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << " (" #expr ")" << std::endl; \
            std::exit(1); \
        } \
    } while(0)

using namespace clashwin::system;
using namespace clashwin::config;

void TestSystemProxyRegistry() {
    std::cout << "[Test 1] 验证 Windows 系统代理注册表与 WinINET 状态机..." << std::endl;

    auto& sysproxy = SystemProxyManager::Instance();
    sysproxy.InstallCrashRollbackHandler();

    // 记录测试前状态，用于测试完毕后还原
    bool orig_proxy_enabled = sysproxy.IsProxyEnabled();
    std::wstring orig_proxy_server = sysproxy.GetCurrentProxyServer();
    bool orig_pac_enabled = sysproxy.IsPacEnabled();
    std::wstring orig_pac_url = sysproxy.GetCurrentPacUrl();

    // 1. 启用系统代理
    std::wstring test_server = L"127.0.0.1:10801";
    std::wstring test_bypass = L"localhost;127.*;<local>";
    bool ok = sysproxy.EnableProxy(test_server, test_bypass);
    TEST_CHECK(ok, "EnableProxy 必须成功写入注册表");
    TEST_CHECK(sysproxy.IsProxyEnabled() == true, "ProxyEnable 必须为 1");
    TEST_CHECK(sysproxy.GetCurrentProxyServer() == test_server, "ProxyServer 必须为 127.0.0.1:10801");
    TEST_CHECK(sysproxy.IsPacEnabled() == false, "启用系统代理时 AutoConfigURL 必须被原子清空");
    TEST_CHECK(sysproxy.GetCurrentMode() == ProxyMode::SystemProxy, "模式必须识别为 SystemProxy");

    // 2. 启用 PAC 模式
    std::wstring test_pac = L"http://127.0.0.1:10801/commands/pac";
    ok = sysproxy.EnablePac(test_pac);
    TEST_CHECK(ok, "EnablePac 必须成功写入注册表");
    TEST_CHECK(sysproxy.IsPacEnabled() == true, "AutoConfigURL 必须有效写入");
    TEST_CHECK(sysproxy.GetCurrentPacUrl() == test_pac, "PAC URL 必须匹配");
    TEST_CHECK(sysproxy.IsProxyEnabled() == false, "启用 PAC 时 ProxyEnable 必须置 0");
    TEST_CHECK(sysproxy.GetCurrentMode() == ProxyMode::Pac, "模式必须识别为 Pac");

    // 3. ResetAll 清空所有
    ok = sysproxy.ResetAll();
    TEST_CHECK(ok, "ResetAll 必须成功执行");
    TEST_CHECK(sysproxy.IsProxyEnabled() == false, "ResetAll 后 ProxyEnable 必须为 0");
    TEST_CHECK(sysproxy.IsPacEnabled() == false, "ResetAll 后 AutoConfigURL 必须已删除");
    TEST_CHECK(sysproxy.GetCurrentMode() == ProxyMode::Direct, "模式必须识别为 Direct");

    // 4. RAII Guard 验证
    {
        SystemProxyGuard guard;
        sysproxy.EnableProxy(test_server, test_bypass);
        TEST_CHECK(sysproxy.IsProxyEnabled() == true, "Guard 内部代理有效开启");
    }
    // 出作用域后 guard 析构
    TEST_CHECK(sysproxy.IsProxyEnabled() == false, "SystemProxyGuard 析构后代理必须 100% 自动复原");

    // 恢复测试前初始状态
    if (orig_proxy_enabled) {
        sysproxy.EnableProxy(orig_proxy_server);
    } else if (orig_pac_enabled) {
        sysproxy.EnablePac(orig_pac_url);
    } else {
        sysproxy.ResetAll();
    }

    std::cout << "  [PASS] 系统代理注册表操作与铁律顺序全部验证通过！" << std::endl;
}

void TestServiceManagerAndFallback() {
    std::cout << "[Test 2] 验证 Windows SCM 服务状态与 BUG-298 自动降级自愈..." << std::endl;

    auto& sm = ServiceManager::Instance();

    // 1. 验证特权检测 API
    bool is_elevated = sm.IsProcessElevated();
    std::cout << "  [SCM Info] 当前进程提权状态 (IsProcessElevated): " << (is_elevated ? "YES" : "NO") << std::endl;

    // 2. 查询 clash_verge_service 状态
    std::wstring detail;
    ServiceState st = sm.QueryServiceState(&detail);
    std::wcout << L"  [SCM Info] clash_verge_service 状态: " << detail << std::endl;
    TEST_CHECK(st == ServiceState::Ready || st == ServiceState::StartRequired ||
               st == ServiceState::InstallRequired || st == ServiceState::Unavailable,
               "服务查询必须返回合法枚举状态");

    // 3. 验证非 TUN 请求返回 true
    TEST_CHECK(sm.EvaluateTunAndFallback(false, L"127.0.0.1:10801"), "非 TUN 模式无需任何服务门控，直接就绪");

    // 4. 验证 TUN 降级自愈逻辑
    std::wstring fallback_msg;
    bool tun_ready = sm.EvaluateTunAndFallback(true, L"127.0.0.1:10801", &fallback_msg);
    if (!is_elevated && st != ServiceState::Ready && st != ServiceState::StartRequired) {
        // 未安装服务的普通权限环境，必须触发优雅降级
        TEST_CHECK(!tun_ready, "服务未就绪且非管理员特权时必须返回 false 降级");
        TEST_CHECK(!fallback_msg.empty(), "必须输出明确的降级提示通知");
        TEST_CHECK(SystemProxyManager::Instance().IsProxyEnabled() == true, "降级必须自动切换并开启系统代理");
        SystemProxyManager::Instance().ResetAll(); // 清理
    } else {
        TEST_CHECK(tun_ready, "管理员特权或服务 Ready 环境可直接放行 TUN");
    }

    std::cout << "  [PASS] SCM 状态查询与降级自愈防御验证通过！" << std::endl;
}

void TestTunCompiler() {
    std::cout << "[Test 3] 验证 TUN 模式配置编译器、IPv6 防泄漏与协议树注入..." << std::endl;

    std::string mock_base = R"(
proxies:
  - name: "香港 01"
    type: vmess
    server: 1.1.1.1
    port: 443
  - name: "日本 02"
    type: ss
    server: 2.2.2.2
    port: 8388
)";

    // 1. 测试 TUN 开启 + user_ipv6 开启
    TunCompilerOptions opts_tun_ipv6;
    opts_tun_ipv6.enable_tun = true;
    opts_tun_ipv6.user_ipv6 = true;
    opts_tun_ipv6.rule_fallback = "direct";

    std::string compiled_tun = TunCompiler::CompileConfig(mock_base, opts_tun_ipv6);

    // 核心安全红线检验：顶层 ipv6 必须强制为 true
    TEST_CHECK(compiled_tun.find("ipv6: true") != std::string::npos, "开启 TUN 时顶层 ipv6 必须恒为 true (防泄漏最高铁律)");
    // 验证 gvisor 协议栈与 inet6-address
    TEST_CHECK(compiled_tun.find("stack: gvisor") != std::string::npos, "TUN 协议栈必须为 gvisor");
    TEST_CHECK(compiled_tun.find("inet6-address:") != std::string::npos, "必须配置 inet6-address 数组");
    TEST_CHECK(compiled_tun.find("fdfe:dcba:9876::1/126") != std::string::npos, "必须包含规范 IPv6 网段");
    // 验证 fake-ip 与 fake-ip-range6
    TEST_CHECK(compiled_tun.find("enhanced-mode: fake-ip") != std::string::npos, "必须启用 fake-ip 增强模式");
    TEST_CHECK(compiled_tun.find("fake-ip-range: \"198.18.0.1/16\"") != std::string::npos, "必须配置 fake-ip IPv4 范围");
    TEST_CHECK(compiled_tun.find("fake-ip-range6: \"2001:2::0/64\"") != std::string::npos, "IPv6 开启时必须配置 fc00::/7 之外的 fake-ip-range6");
    // 验证 fake-ip 过滤名单 (NCSI、NTP 等)
    TEST_CHECK(compiled_tun.find("dns.msftncsi.com") != std::string::npos, "必须注入 Windows NCSI 假 IP 豁免");
    TEST_CHECK(compiled_tun.find("*.lan") != std::string::npos, "必须注入局域网假 IP 豁免");

    // 验证双控制/测量组
    TEST_CHECK(compiled_tun.find("- name: PROXY\n    type: select") != std::string::npos, "必须生成单一 PROXY 控制组");
    TEST_CHECK(compiled_tun.find("- name: PROXY__METRICS\n    type: url-test") != std::string::npos, "必须生成 PROXY__METRICS 测量组");
    TEST_CHECK(compiled_tun.find("\"香港 01\"") != std::string::npos, "代理组必须保留原节点名");
    TEST_CHECK(compiled_tun.find("\"日本 02\"") != std::string::npos, "代理组必须保留原节点名");

    // 验证强制直连与 IPv6 兜底规则
    TEST_CHECK(compiled_tun.find("RULE-SET,gfwlist,PROXY") != std::string::npos, "必须包含 GFWList 规则");
    TEST_CHECK(compiled_tun.find("IP-CIDR,8.8.8.8/32,PROXY,no-resolve") != std::string::npos, "必须包含 DNS fallback 防投毒代理规则");
    TEST_CHECK(compiled_tun.find("IP-CIDR,127.0.0.0/8,DIRECT,no-resolve") != std::string::npos, "必须包含 IPv4 环回直连规则");
    TEST_CHECK(compiled_tun.find("IP-CIDR6,fc00::/7,DIRECT,no-resolve") != std::string::npos, "必须包含 IPv6 ULA 直连规则");
    TEST_CHECK(compiled_tun.find("IP-CIDR6,::ffff:192.168.0.0/112,DIRECT,no-resolve") != std::string::npos, "必须包含 IPv4-mapped IPv6 私网段直连规则");
    TEST_CHECK(compiled_tun.find("GEOIP,CN,DIRECT") != std::string::npos, "必须包含境内 IPv6 GEOIP 直连");
    TEST_CHECK(compiled_tun.find("IP-CIDR6,::/0,PROXY") != std::string::npos, "必须包含境外 IPv6 兜底代理 (且刻意不带 no-resolve)");
    TEST_CHECK(compiled_tun.find("MATCH,DIRECT") != std::string::npos, "GFWList 模式 MATCH 必须为 DIRECT");

    // 2. 测试全局代理模式 (rule_fallback == "proxy")
    opts_tun_ipv6.rule_fallback = "proxy";
    std::string compiled_proxy = TunCompiler::CompileConfig(mock_base, opts_tun_ipv6);
    TEST_CHECK(compiled_proxy.find("MATCH,PROXY") != std::string::npos, "全局代理模式 MATCH 必须为 PROXY");

    // 3. 测试 TUN 关闭场景
    TunCompilerOptions opts_no_tun;
    opts_no_tun.enable_tun = false;
    opts_no_tun.user_ipv6 = false;
    std::string compiled_notun = TunCompiler::CompileConfig(mock_base, opts_no_tun);
    TEST_CHECK(compiled_notun.find("enable: false") != std::string::npos, "关闭 TUN 时 tun.enable 必须为 false");
    TEST_CHECK(compiled_notun.find("ipv6: false") != std::string::npos, "关闭 TUN 时顶层 ipv6 应跟随 user_ipv6");

    std::cout << "  [PASS] TUN 虚拟网卡配置与 Agreement 全协议规则注入全部验证通过！" << std::endl;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "[ClashWin 切片三] 系统代理注册表、TUN 与 SCM 验证测试" << std::endl;
    std::cout << "====================================================" << std::endl;

    TestSystemProxyRegistry();
    TestServiceManagerAndFallback();
    TestTunCompiler();

    std::cout << "\n>>> 切片三: 系统代理注册表、TUN 与 SCM 服务模式全项 100% 验收通过！<<<" << std::endl;
    return 0;
}
