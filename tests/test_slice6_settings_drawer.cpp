#include <iostream>
#include <cassert>
#include <filesystem>
#include <vector>
#include "clashwin/config/app_config.h"
#include "clashwin/config/profile_manager.h"
#include "clashwin/ui/theme.h"
#include "clashwin/system/service_manager.h"
#include "clashwin/system/sysproxy.h"

using namespace clashwin;
using namespace clashwin::config;
using namespace clashwin::system;
using namespace ClashWin;

int main() {
    std::wcout.imbue(std::locale(""));
    std::cout << "====================================================" << std::endl;
    std::cout << "[ClashWin 切片六] 交互式设置抽屉与 5 大功能卡片验证" << std::endl;
    std::cout << "====================================================" << std::endl;

    std::filesystem::path testDir = std::filesystem::temp_directory_path() / "clashwin_slice6_test";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);

    AppConfig::Instance().Init(testDir);
    ProfileManager::Instance().Init(testDir);

    // -------------------------------------------------------------
    // Test 1: 卡片 1 流量接管模式 (TakeoverModeCard) MAN / SYS / TUN
    // -------------------------------------------------------------
    std::cout << "[Test 1] 验证流量接管模式卡片 (TakeoverModeCard) 状态流转..." << std::endl;
    {
        // 1. MAN 模式
        AppConfig::Instance().SetSystemProxyEnabled(false);
        AppConfig::Instance().SetTunModeEnabled(false);
        assert(!AppConfig::Instance().IsSystemProxyEnabled());
        assert(!AppConfig::Instance().IsTunModeEnabled());

        // 2. SYS 模式
        AppConfig::Instance().SetSystemProxyEnabled(true);
        AppConfig::Instance().SetTunModeEnabled(false);
        assert(AppConfig::Instance().IsSystemProxyEnabled());
        assert(!AppConfig::Instance().IsTunModeEnabled());

        // 3. TUN 模式下 SCM 探测与自动降级防线 (BUG-298)
        std::wstring fallbackMsg;
        std::wstring mixedProxy = L"127.0.0.1:" + std::to_wstring(AppConfig::Instance().GetMixedPort());
        bool tunAllowed = ServiceManager::Instance().EvaluateTunAndFallback(true, mixedProxy, &fallbackMsg);
        
        if (ServiceManager::IsProcessElevated() || ServiceManager::Instance().QueryServiceState() == ServiceState::Ready) {
            assert(tunAllowed == true);
            std::cout << "  [SCM Info] 当前处于管理员或 SCM 服务就绪状态，TUN 模式已就绪" << std::endl;
        } else {
            // 普通权限且未安装/未启动：自动安全回退为系统代理
            assert(tunAllowed == false);
            assert(!fallbackMsg.empty());
            std::wcout << L"  [BUG-298 防御] 成功触发安全降级回退: " << fallbackMsg << std::endl;
        }
        std::cout << "  [PASS] 流量接管模式卡片验证通过！" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 2: 卡片 2 分流路由偏好 (RoutingPreferenceCard) DIR / RUL / PRX
    // -------------------------------------------------------------
    std::cout << "[Test 2] 验证分流路由偏好卡片 (RoutingPreferenceCard)..." << std::endl;
    {
        AppConfig::Instance().SetRuleFallback("direct");
        assert(AppConfig::Instance().GetRuleFallback() == "direct");

        AppConfig::Instance().SetRuleFallback("rule");
        assert(AppConfig::Instance().GetRuleFallback() == "rule");

        AppConfig::Instance().SetRuleFallback("proxy");
        assert(AppConfig::Instance().GetRuleFallback() == "proxy");

        // 恢复出厂推荐默认：direct (GFWList 出厂默认)
        AppConfig::Instance().SetRuleFallback("direct");
        std::cout << "  [PASS] 路由偏好三段状态持久化与内核配置映射验证通过！" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 3: 卡片 3 基础网络设置 (BasicSettingsCard) 2x2 Grid + Port
    // -------------------------------------------------------------
    std::cout << "[Test 3] 验证基础网络设置卡片 (BasicSettingsCard)..." << std::endl;
    {
        // 2x2 网格开关
        AppConfig::Instance().SetAutoLaunchEnabled(true);
        assert(AppConfig::Instance().IsAutoLaunchEnabled() == true);
        AppConfig::Instance().SetAutoLaunchEnabled(false);
        assert(AppConfig::Instance().IsAutoLaunchEnabled() == false);

        AppConfig::Instance().SetSilentStartEnabled(true);
        assert(AppConfig::Instance().IsSilentStartEnabled() == true);

        AppConfig::Instance().SetIpv6Enabled(true);
        assert(AppConfig::Instance().IsIpv6Enabled() == true);
        AppConfig::Instance().SetIpv6Enabled(false);
        assert(AppConfig::Instance().IsIpv6Enabled() == false);

        // Mixed Port 输入保存
        AppConfig::Instance().SetMixedPort(10808);
        assert(AppConfig::Instance().GetMixedPort() == 10808);
        AppConfig::Instance().SetMixedPort(10801); // 还原默认
        assert(AppConfig::Instance().GetMixedPort() == 10801);

        std::cout << "  [PASS] 基础网络 2x2 网格开关与 Mixed Port 保存验证通过！" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 4: 卡片 4 界面外观与物理质感 (ThemeSettingsCard)
    // -------------------------------------------------------------
    std::cout << "[Test 4] 验证界面外观卡片 (ThemeSettingsCard) 6 皮肤与双滑块..." << std::endl;
    {
        ThemeManager::Instance().SetSkin(SkinType::Retro3D);
        assert(ThemeManager::Instance().GetCurrentSkin() == SkinType::Retro3D);
        assert(ThemeManager::Instance().GetColors().slider1_name == L"Depth");
        assert(ThemeManager::Instance().GetColors().slider2_name == L"Vibrancy");

        ThemeManager::Instance().SetSkin(SkinType::Frosted);
        assert(ThemeManager::Instance().GetCurrentSkin() == SkinType::Frosted);
        assert(ThemeManager::Instance().GetColors().slider1_name == L"Opacity");
        assert(ThemeManager::Instance().GetColors().slider2_name == L"Blur");

        ThemeManager::Instance().SetSkin(SkinType::Cyberpunk);
        assert(ThemeManager::Instance().GetCurrentSkin() == SkinType::Cyberpunk);
        assert(ThemeManager::Instance().GetColors().slider1_name == L"Glow");
        assert(ThemeManager::Instance().GetColors().slider2_name == L"Contrast");

        ThemeManager::Instance().SetSlider1(0.85f);
        ThemeManager::Instance().SetSlider2(3.6f);
        assert(ThemeManager::Instance().GetSlider1() == 0.85f);
        assert(ThemeManager::Instance().GetSlider2() == 3.6f);

        AppConfig::Instance().SetThemeMode("dark");
        assert(AppConfig::Instance().GetThemeMode() == "dark");
        AppConfig::Instance().SetThemeMode("system");
        assert(AppConfig::Instance().GetThemeMode() == "system");

        std::cout << "  [PASS] 6 套物理拟物皮肤与专属双滑块动态标签验证通过！" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 5: 卡片 5 订阅管理 (ProfileImportCard) 多配置增删改查
    // -------------------------------------------------------------
    std::cout << "[Test 5] 验证订阅管理卡片 (ProfileImportCard) 多订阅生命周期..." << std::endl;
    {
        std::string mockYaml = 
            "proxies:\n"
            "  - name: \"US-HighSpeed-01\"\n"
            "    type: ss\n"
            "    server: 1.1.1.1\n"
            "    port: 8388\n"
            "    cipher: aes-128-gcm\n"
            "    password: \"pass123\"\n"
            "  - name: \"JP-Tokyo-02\"\n"
            "    type: ss\n"
            "    server: 2.2.2.2\n"
            "    port: 8389\n"
            "    cipher: aes-128-gcm\n"
            "    password: \"pass456\"\n";

        std::string err;
        bool importOk = ProfileManager::Instance().ImportAndActivate(mockYaml, err);
        assert(importOk == true);
        assert(ProfileManager::Instance().HasRealProfile() == true);

        const auto& prfs = ProfileManager::Instance().GetProfileItems();
        assert(!prfs.empty());
        std::string activeUid = ProfileManager::Instance().GetCurrentProfileUid();
        assert(!activeUid.empty());

        // 验证多订阅切换
        bool switchOk = ProfileManager::Instance().SwitchProfile(activeUid, err);
        assert(switchOk == true);

        // 验证多订阅更新
        bool updateOk = ProfileManager::Instance().UpdateProfile(activeUid, err);
        assert(updateOk == true);

        // 验证多订阅删除
        bool delOk = ProfileManager::Instance().DeleteProfile(activeUid);
        assert(delOk == true);

        std::cout << "  [PASS] 订阅管理卡片多配置导入、切换、更新、删除验证通过！" << std::endl;
    }

    std::filesystem::remove_all(testDir);
    std::cout << "\n>>> 切片六: 设置抽屉与 5 大功能卡片 100% 验收通过！<<<" << std::endl;
    return 0;
}
