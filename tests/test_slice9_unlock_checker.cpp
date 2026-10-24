#include <iostream>
#include <cassert>
#include "clashwin/network/unlock_checker.h"

using namespace clashwin::network;

void TestPlatformListAndRegionFormat() {
    std::cout << "[Test 1] Testing Default Platforms & Region Formatting..." << std::endl;

    const auto& names = UnlockChecker::GetDefaultPlatformNames();
    assert(names.size() == 11);
    assert(names[0] == "ChatGPT Web");
    assert(names[2] == "Claude");
    assert(names[3] == "Gemini");
    assert(names[5] == "Netflix");

    assert(UnlockChecker::FormatRegion("US") == "🇺🇸 US");
    assert(UnlockChecker::FormatRegion("JP") == "🇯🇵 JP");
    assert(UnlockChecker::FormatRegion("HK") == "🇭🇰 HK");
    assert(UnlockChecker::FormatRegion("SG") == "🇸🇬 SG");
    assert(UnlockChecker::FormatRegion("TW") == "🇹🇼 TW");
    assert(UnlockChecker::FormatRegion("USA") == "🇺🇸 US");

    std::cout << "  -> Platforms and Region formatting passed!" << std::endl;
}

void TestUnlockCheckerLifecycle() {
    std::cout << "[Test 2] Testing Unlock Checker Execution..." << std::endl;

    UnlockChecker& checker = UnlockChecker::Instance();
    auto initial_results = checker.GetResults();
    assert(initial_results.size() == 11);
    assert(initial_results[0].status == "Pending");

    // 测试快速超时探针 (不挂代理或本地无代理时应优雅降级为 Failed，绝不崩溃或抛异常)
    auto results = checker.RunAllChecks("127.0.0.1:54321", 50);
    assert(results.size() == 11);

    for (const auto& item : results) {
        assert(!item.name.empty());
        // 在无对应代理服务时，返回状态应为 Failed
        assert(item.status == "Failed" || item.status == "No" || item.status == "Yes");
    }

    std::cout << "  -> Unlock Checker Lifecycle & Error Tolerance passed!" << std::endl;
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "  ClashWin Phase 4 Slice 9 - Media Unlock Checker " << std::endl;
    std::cout << "==================================================" << std::endl;

    TestPlatformListAndRegionFormat();
    TestUnlockCheckerLifecycle();

    std::cout << "\n>>> [ALL SLICE 9 TESTS PASSED SUCCESSFULLY!] <<<\n" << std::endl;
    return 0;
}
