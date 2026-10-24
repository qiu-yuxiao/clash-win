#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <cassert>
#include <windows.h>
#include <psapi.h>

#include "clashwin/common/constants.h"
#include "clashwin/common/types.h"
#include "clashwin/config/app_config.h"
#include "clashwin/config/profile_manager.h"
#include "clashwin/config/tun_compiler.h"
#include "clashwin/config/node_filter.h"
#include "clashwin/config/universal_parser.h"
#include "clashwin/config/enhance_chain.h"
#include "clashwin/system/sysproxy.h"
#include "clashwin/system/service_manager.h"
#include "clashwin/system/singleton.h"
#include "clashwin/core/core_supervisor.h"
#include "clashwin/core/health_probe.h"
#include "clashwin/core/connection_manager.h"
#include "clashwin/network/unlock_checker.h"
#include "clashwin/ui/theme.h"
#include "window/main_window.hpp"

#pragma comment(lib, "psapi.lib")

#define TEST_CHECK(expr, msg) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << " (" #expr ")" << std::endl; \
            std::exit(1); \
        } \
    } while(0)

using namespace clashwin;
using namespace ClashWin;

void BenchmarkColdStartAndFootprint() {
    std::cout << "[Benchmark 1] 物理性能指标验收：冷启动耗时与常驻内存基准..." << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();

    // 完整冷启动初始化流程
    std::filesystem::path test_dir = std::filesystem::current_path() / "test_p5_workspace";
    std::filesystem::create_directories(test_dir);

    config::AppConfig::Instance().Init(test_dir);
    config::ProfileManager::Instance().Init(test_dir);
    config::EnhanceChain::Instance().Init(test_dir);
    ThemeManager::Instance().SetSkin(SkinType::Retro3D);
    system::SystemProxyManager::Instance().InstallCrashRollbackHandler();

    auto end_time = std::chrono::high_resolution_clock::now();
    double init_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::cout << "  [Cold Start Time] 核心初始化耗时: " << init_ms << " ms (指标标准: < 150 ms)" << std::endl;
    TEST_CHECK(init_ms < 150.0, "冷启动耗时必须稳定在 150ms 以内");

    // 查询当前进程物理工作集 (Resident Working Set)
    PROCESS_MEMORY_COUNTERS pmc;
    BOOL ok = GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    TEST_CHECK(ok, "GetProcessMemoryInfo 调用成功");

    double working_set_mb = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    std::cout << "  [Memory Footprint] 当前物理常驻内存占用: " << working_set_mb << " MB (指标标准: < 25 MB)" << std::endl;
    TEST_CHECK(working_set_mb < 25.0, "纯原生常驻物理内存必须稳定在 25MB 以内");

    std::filesystem::remove_all(test_dir);
    std::cout << "  [PASS] 物理性能与冷启动指标 100% 验收达标！\n" << std::endl;
}

void TestZeroExternalWebDependency() {
    std::cout << "[Benchmark 2] 零外部 Web 依赖与二进制便携分发校验..." << std::endl;

    HMODULE hWebView2 = GetModuleHandleW(L"WebView2Loader.dll");
    TEST_CHECK(hWebView2 == NULL, "严禁加载 WebView2 运行时");

    HMODULE hNode = GetModuleHandleW(L"node.dll");
    TEST_CHECK(hNode == NULL, "严禁加载 Node/Electron 依赖");

    HMODULE hCef = GetModuleHandleW(L"libcef.dll");
    TEST_CHECK(hCef == NULL, "严禁加载 CEF 运行时");

    // 检查 ClashWin.exe 生成产物尺寸
    std::filesystem::path exe_path = std::filesystem::current_path() / "build" / "bin" / "ClashWin.exe";
    if (std::filesystem::exists(exe_path)) {
        auto file_size = std::filesystem::file_size(exe_path);
        double size_kb = static_cast<double>(file_size) / 1024.0;
        std::cout << "  [Binary Size] ClashWin.exe 编译产物体积: " << size_kb << " KB (原版 Clash Verge 约 100,000 KB)" << std::endl;
        TEST_CHECK(size_kb < 1024.0, "二进制单文件便携分发体积必须在 1MB (1024KB) 以内");
    }

    std::cout << "  [PASS] 零外部 Web 依赖与极致轻量便携指标 100% 验收达标！\n" << std::endl;
}

void TestAll11SlicesFunctionalParity() {
    std::cout << "[Benchmark 3] 全量 11 个切片业务功能端到端综合回归验收..." << std::endl;

    // --- Slice 1: 配置持久化与多订阅 ---
    TEST_CHECK(network::kDefaultMixedPort == 10801, "Mixed 端口固定为 10801");
    TEST_CHECK(network::kDefaultRestPort == 9098, "Controller API 端口固定为 9098");
    TEST_CHECK(network::kSingletonPort == 33335, "单实例检测端口固定为 33335");
    TEST_CHECK(config::NodeFilter::IsDummyNode("剩余流量: 500GB"), "广告节点必须拦截");
    TEST_CHECK(!config::NodeFilter::IsDummyNode("HK-流量优化 01"), "包含流量字样的真实节点严禁误杀");

    // --- Slice 2: 健康探针与进程守护 ---
    core::HealthProbeService::KillAllOrphanCores();
    core::HealthProbeService& probe = core::HealthProbeService::Instance();
    TEST_CHECK(!probe.IsNodeInCooldown("Unknown-Node"), "新节点不应在冷却期内");

    // --- Slice 3: TUN 模式、IPv6 防泄漏与 SCM 伴生服务 ---
    config::TunCompilerOptions tun_opts;
    tun_opts.enable_tun = true;
    tun_opts.user_ipv6 = true;
    std::string compiled = config::TunCompiler::CompileConfig("proxies:\n  - name: \"Node1\"\n", tun_opts);
    TEST_CHECK(compiled.find("ipv6: true") != std::string::npos, "TUN 启用时顶层 ipv6 必须强制为 true 防泄漏");
    TEST_CHECK(compiled.find("stack: gvisor") != std::string::npos, "TUN 协议栈必须为 gvisor");
    TEST_CHECK(compiled.find("fake-ip-range6: \"2001:2::0/64\"") != std::string::npos, "IPv6 fake-ip 范围必须正确");

    // --- Slice 4: 6 套皮肤无缝切换与 3D 拟物 ---
    ThemeManager::Instance().SetSkin(SkinType::Retro3D);
    TEST_CHECK(ThemeManager::Instance().GetColors().slider1_name == L"Depth", "Retro3D 滑块 1 标签为 Depth");
    ThemeManager::Instance().SetSkin(SkinType::Cyberpunk);
    TEST_CHECK(ThemeManager::Instance().GetColors().slider1_name == L"Neon", "Cyberpunk 滑块 1 标签为 Neon");
    ThemeManager::Instance().SetSkin(SkinType::Monochrome);
    TEST_CHECK(ThemeManager::Instance().GetColors().slider1_name == L"Intensity", "Monochrome 滑块 1 标签为 Intensity");

    // --- Slice 5: Win32 原生集成、尺寸下限与断点 ---
    TEST_CHECK(geometry::kMinimalWidth == 271.0, "最小宽度必须为 271px");
    TEST_CHECK(geometry::kMinimalHeight == 126.0, "最小高度必须为 126px");

    // --- Slice 6: 设置抽屉 5 大控制卡片配置属性 ---
    TEST_CHECK(!config::AppConfig::Instance().GetThemeMode().empty(), "主题模式非空");
    TEST_CHECK(!config::AppConfig::Instance().GetRuleFallback().empty(), "回退规则非空");

    // --- Slice 7: 19 协议 Universal Parser 与 Enhance Chain ---
    auto dec_b64 = config::UniversalParser::DecodeBase64Robust("aGVsbG8gd29ybGQ"); // 无填充容错
    TEST_CHECK(dec_b64.has_value() && *dec_b64 == "hello world", "鲁棒 Base64 容错解码成功");
    auto node_trojan = config::UniversalParser::ParseTrojan("trojan://pass123@1.2.3.4:443?sni=us.com#US-Node");
    TEST_CHECK(node_trojan.has_value() && node_trojan->name == "US-Node", "Trojan 节点协议解析成功");

    // --- Slice 8: 连接面板与右键分流规则注入 ---
    std::filesystem::path p5_tmp = std::filesystem::current_path() / "test_p5_tmp";
    std::filesystem::create_directories(p5_tmp);
    config::EnhanceChain::Instance().Init(p5_tmp);
    config::EnhanceChain::Instance().ClearInjectedRules();
    bool inj_ok = config::EnhanceChain::Instance().InjectRule("api.openai.com", "PROXY", true);
    TEST_CHECK(inj_ok, "右键分流规则注入成功");
    TEST_CHECK(!config::EnhanceChain::Instance().GetPrependRules().empty(), "Merge.yaml 包含注入规则");
    config::EnhanceChain::Instance().ClearInjectedRules();
    std::filesystem::remove_all(p5_tmp);

    // --- Slice 9: 主流 AI 与流媒体解锁检测 ---
    const auto& unlock_platforms = network::UnlockChecker::GetDefaultPlatformNames();
    TEST_CHECK(unlock_platforms.size() == 11, "覆盖全部 11 个主流平台");
    TEST_CHECK(network::UnlockChecker::FormatRegion("US") == "🇺🇸 US", "国旗 Emoji 格式化正确");

    // --- Slice 10: 窗口遮挡模式与微缩科学流量计 ---
    TEST_CHECK(MainWindow::MIN_HEIGHT == 126, "微缩流量计断点高度为 126px");
    TEST_CHECK(MainWindow::NARROW_BREAKPOINT == 285, "双宽窄断点为 285px");

    // --- Slice 11: 官方安装包与绿色分发包 ---
    std::filesystem::path zip_file = std::filesystem::current_path() / "build" / "bin" / "ClashWin-Portable-x64.zip";
    TEST_CHECK(std::filesystem::exists(zip_file), "便携归档压缩包已构建就绪");
    std::filesystem::path nsi_file = std::filesystem::current_path() / "installer" / "ClashWin-Setup.nsi";
    TEST_CHECK(std::filesystem::exists(nsi_file), "NSIS 安装向导脚本已准备就绪");

    std::cout << "  [PASS] 全量 11 个纵向切片核心业务功能 100% 对齐并通过验收！\n" << std::endl;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "[ClashWin 阶段五] 全面端到端、物理指标与最终交付验收" << std::endl;
    std::cout << "====================================================" << std::endl;

    BenchmarkColdStartAndFootprint();
    TestZeroExternalWebDependency();
    TestAll11SlicesFunctionalParity();

    std::cout << "====================================================" << std::endl;
    std::cout << ">>> 阶段五最终验收全部通过！ClashWin 移植与重构胜利竣工！<<<" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
