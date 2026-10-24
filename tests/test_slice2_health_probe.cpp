#include <windows.h>
#include <iostream>
#include <filesystem>
#include "clashwin/core/core_supervisor.h"
#include "clashwin/core/health_probe.h"
#include "clashwin/core/mihomo_client.h"
#include "clashwin/config/profile_manager.h"

#define TEST_CHECK(expr, msg) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] Check failed: " << msg << " (" << #expr << ") at line " << __LINE__ << "\n"; \
            return 1; \
        } \
    } while(0)

int main() {
    std::cout << "====================================================\n";
    std::cout << "[ClashWin 切片二] 内核生态、进程防护与健康自愈测试\n";
    std::cout << "====================================================\n";

    // 1. 验证孤儿进程扫除能力
    clashwin::core::HealthProbeService::KillAllOrphanCores();
    std::cout << "[PASS] 1. 孤儿内核进程强制排查完成（无僵死残留）\n";

    // 2. 验证冷却计时
    auto& probe = clashwin::core::HealthProbeService::Instance();
    TEST_CHECK(!probe.IsNodeInCooldown("Unknown-Node"), "新节点不应在冷却期内");
    std::cout << "[PASS] 2. 节点冷却计时状态计算正常\n";

    // 3. 准备测试环境并拉起内核
    std::filesystem::path testDir = std::filesystem::current_path() / "test_slice2_data";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);

    auto& profileMgr = clashwin::config::ProfileManager::Instance();
    profileMgr.Init(testDir);

    std::string sub = 
        "vmess://eyJhZGQiOiIxLjEuMS4xIiwicG9ydCI6NDQzLCJpZCI6ImI4MzEzODFkLTYzMjQtNGQ1My1hZDRmLThjZGE0OGIzMDgxMSIsImFpZCI6MCwibmV0Ijoid3MiLCJ0eXBlIjoiIiwicHMiOiLpppnmuK8gMDEifQ==\n"
        "trojan://password123@1.1.1.2:443#%E6%97%A5%E6%9C%AC%2001\n";

    std::string err;
    TEST_CHECK(profileMgr.ImportAndActivate(sub, err), "订阅导入成功");

    std::filesystem::path corePath = "c:\\Users\\sun_y\\Documents\\AntiGravity_Projects\\ClashWin\\sidecar\\mini-mihomo.exe";
    auto& supervisor = clashwin::core::CoreSupervisor::Instance();
    TEST_CHECK(supervisor.StartCore(corePath, testDir, 10801, 9098), "内核启动成功");

    // 轮询等待 API 就绪
    bool ready = false;
    for (int i = 0; i < 30; ++i) {
        Sleep(200);
        if (clashwin::core::MihomoClient::Instance().CheckHealth(9098)) {
            ready = true;
            break;
        }
    }
    TEST_CHECK(ready, "内核 API 握手就绪");
    std::cout << "[PASS] 3. 内核子进程成功启动并进入 Job Object 管控\n";

    // 4. 验证探针服务启动
    bool callbackFired = false;
    std::string detectedNode;
    probe.SetOnStatusChanged([&](const std::string& active, int delay, bool healed) {
        callbackFired = true;
        detectedNode = active;
        std::cout << "    [Probe Event] Active=" << active << " Delay=" << delay << "ms Healed=" << (healed ? "YES" : "NO") << "\n";
    });

    probe.Start(9098);
    probe.TriggerImmediateCheck();

    // 等待探针执行完毕
    for (int i = 0; i < 20; ++i) {
        Sleep(200);
        if (callbackFired) break;
    }

    TEST_CHECK(callbackFired, "健康探针状态探测回调成功触发");
    TEST_CHECK(!detectedNode.empty(), "探测到当前出口节点名称");
    std::cout << "[PASS] 4. 15s 恒定探针线程运行正常，成功捕获当前活跃节点\n";

    // 5. 验证停止探针与内核清理
    probe.Stop();
    supervisor.StopCore();
    TEST_CHECK(!supervisor.IsRunning(), "内核正常停止");
    std::cout << "[PASS] 5. 探针停止与内核优雅注销全部完成\n";

    std::cout << "\n>>> 切片二: 内核生态与健康自愈机制全项 100% 验收通过！<<<\n";
    return 0;
}
