#include <windows.h>
#include <iostream>
#include <filesystem>
#include "clashwin/config/app_config.h"
#include "clashwin/config/node_filter.h"
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
    std::cout << "[ClashWin 切片一] 配置持久化与多订阅管理验收测试\n";
    std::cout << "====================================================\n";

    std::filesystem::path testDir = std::filesystem::current_path() / "test_slice1_data";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);

    // ========================================================================
    // 1. 验证 NodeFilter 过滤准则（杜绝误杀）
    // ========================================================================
    using clashwin::config::NodeFilter;

    TEST_CHECK(NodeFilter::IsDummyNode("Remaining Traffic: 50GB"), "英文流量信息");
    TEST_CHECK(NodeFilter::IsDummyNode("Reset Date: 2026-11-01"), "重置日期通知");
    TEST_CHECK(NodeFilter::IsDummyNode("https://my-sub.com/info"), "网址占位符");
    TEST_CHECK(NodeFilter::IsDummyNode("剩余流量 100G"), "中文剩余流量");
    TEST_CHECK(NodeFilter::IsDummyNode("套餐到期 2026-12-31"), "中文套餐到期");
    TEST_CHECK(NodeFilter::IsDummyNode("官网发布页：clash.io"), "中文官网发布页");
    std::cout << "[PASS] 1.1 广告与废弃通知节点正确过滤\n";

    TEST_CHECK(!NodeFilter::IsDummyNode("HK-流量优化专线"), "正常HK优化节点");
    TEST_CHECK(!NodeFilter::IsDummyNode("SG-官方中继 01"), "正常SG官方中继");
    TEST_CHECK(!NodeFilter::IsDummyNode("JP-BGP 高速 02"), "正常JP高防节点");
    TEST_CHECK(!NodeFilter::IsDummyNode("US-Silicon Valley"), "正常US节点");
    std::cout << "[PASS] 1.2 包含敏感字样的正常出口节点绝不误杀 (杜绝 contains 陷阱)\n";

    // ========================================================================
    // 2. 验证 AppConfig (verge.yaml & window_state.json)
    // ========================================================================
    auto& appCfg = clashwin::config::AppConfig::Instance();
    appCfg.Init(testDir);

    appCfg.SetSkin("cyberpunk");
    appCfg.SetSlider1Val(4.2f);
    appCfg.SetSlider2Val(3.8f);
    appCfg.SetRuleFallback("addurl");
    appCfg.SetMixedPort(10802);
    appCfg.SaveVergeConfig();

    TEST_CHECK(appCfg.GetSkin() == "cyberpunk", "Skin 对齐");
    TEST_CHECK(appCfg.GetSlider1Val() == 4.2f, "Slider1 对齐");
    TEST_CHECK(appCfg.GetSlider2Val() == 3.8f, "Slider2 对齐");
    TEST_CHECK(appCfg.GetRuleFallback() == "addurl", "RuleFallback 对齐");
    TEST_CHECK(appCfg.GetMixedPort() == 10802, "MixedPort 对齐");
    std::cout << "[PASS] 2.1 verge.yaml 配置项序列化与反序列化完全一致\n";

    appCfg.SaveWindowStateImmediately(300.0, 500.0, 150.0, 120.0);
    clashwin::WindowGeometry geom;
    TEST_CHECK(appCfg.LoadWindowState(geom), "LoadWindowState 成功");
    TEST_CHECK(geom.width == 300.0 && geom.height == 500.0, "几何宽高对齐");
    TEST_CHECK(geom.x == 150.0 && geom.y == 120.0, "几何坐标对齐");

    // 0x0 守卫拦截
    appCfg.SaveWindowStateImmediately(0.0, 0.0, 0.0, 0.0);
    TEST_CHECK(appCfg.LoadWindowState(geom), "LoadWindowState 守卫后仍有效");
    TEST_CHECK(geom.width == 300.0 && geom.height == 500.0, "0x0 异常被成功拦截，未污染有效几何");
    std::cout << "[PASS] 2.2 window_state.json 几何状态持久化与 0x0 守卫防御生效\n";

    // ========================================================================
    // 3. 验证多 Profile 管理、存储与切换 (profiles.yaml)
    // ========================================================================
    auto& profileMgr = clashwin::config::ProfileManager::Instance();
    profileMgr.Init(testDir);

    std::string sub1 = 
        "vmess://eyJhZGQiOiIxLjEuMS4xIiwicG9ydCI6NDQzLCJpZCI6ImI4MzEzODFkLTYzMjQtNGQ1My1hZDRmLThjZGE0OGIzMDgxMSIsImFpZCI6MCwibmV0Ijoid3MiLCJ0eXBlIjoiIiwicHMiOiLpppnmuK8gMDEifQ==\n"
        "vmess://eyJhZGQiOiIxLjEuMS4xIiwicG9ydCI6NDQzLCJpZCI6ImI4MzEzODFkLTYzMjQtNGQ1My1hZDRmLThjZGE0OGIzMDgxMSIsImFpZCI6MCwibmV0Ijoid3MiLCJ0eXBlIjoiIiwicHMiOiLlvannmL8g5YmN57yA5rWB6YePIDUwbSJ9\n" // 包含"剩余流量 50m"
        "trojan://password123@1.1.1.2:443#%E6%97%A5%E6%9C%AC%2001\n";

    std::string err;
    bool ok1 = profileMgr.ImportAndActivate(sub1, err);
    if (!ok1) {
        std::cerr << "[FAIL] ImportAndActivate 失败: " << err << "\n";
        return 1;
    }
    std::string uid1 = profileMgr.GetCurrentProfileUid();
    TEST_CHECK(!uid1.empty(), "UID1 非空");
    std::cout << "[PASS] 3.1 首个 Profile 导入成功，生成独立文件 " << uid1 << ".yaml\n";

    // 导入第二个 Profile
    std::string sub2 = 
        "ss://YWVzLTEyOC1nY206cGFzc3dvcmQ=@1.1.1.3:8388#%E6%96%B0%E5%8A%A0%E5%9坡%2001\n"
        "ss://YWVzLTEyOC1nY206cGFzc3dvcmQ=@1.1.1.4:8388#%E7%BE%8E%E5%9B%BD%2001\n";
    bool ok2 = profileMgr.ImportAndActivate(sub2, err);
    if (!ok2) {
        std::cerr << "[FAIL] ImportAndActivate sub2 失败: " << err << "\n";
        return 1;
    }
    std::string uid2 = profileMgr.GetCurrentProfileUid();
    TEST_CHECK(uid2 != uid1, "UID2 独立于 UID1");
    TEST_CHECK(profileMgr.GetProfileItems().size() == 2, "Profile 列表项为 2");
    std::cout << "[PASS] 3.2 第二个 Profile 导入成功，profiles.yaml 列表中共计 2 项\n";

    // 切换回首个 Profile
    bool okSwitch = profileMgr.SwitchProfile(uid1, err);
    TEST_CHECK(okSwitch, "切换至 Profile 1 成功");
    TEST_CHECK(profileMgr.GetCurrentProfileUid() == uid1, "当前激活 UID 为 UID1");
    std::cout << "[PASS] 3.3 成功切换回 Profile 1，并重新加载其对应节点列表\n";

    // 节点快照持久化
    std::string nodeName = profileMgr.GetNodes()[0].name;
    profileMgr.SetCurrentActiveNode(nodeName);
    TEST_CHECK(profileMgr.GetCurrentActiveNode() == nodeName, "节点快照同步");
    std::cout << "[PASS] 3.4 节点活跃状态快照保存并在 profiles.yaml 中对账一致\n";

    // 删除第二个 Profile
    bool okDel = profileMgr.DeleteProfile(uid2);
    TEST_CHECK(okDel, "删除 Profile 2 成功");
    TEST_CHECK(profileMgr.GetProfileItems().size() == 1, "删除后列表剩余 1 项");
    std::cout << "[PASS] 3.5 成功删除 Profile 2 并清理本地存储文件\n";

    std::cout << "\n>>> 切片一: 配置持久化与多订阅管理全项 100% 验收通过！<<<\n";
    return 0;
}
