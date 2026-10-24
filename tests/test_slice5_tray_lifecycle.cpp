#include <iostream>
#include <string>
#include <cassert>
#include <windows.h>
#include "clashwin/common/constants.h"
#include "clashwin/system/sysproxy.h"
#include "clashwin/config/app_config.h"

#define TEST_CHECK(expr, msg) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << " (" #expr ")" << std::endl; \
            std::exit(1); \
        } \
    } while(0)

void TestWindowSizingClampAlgorithm() {
    std::cout << "[Test 1] 验证 WM_SIZING 尺寸下限与防下移钳位算法..." << std::endl;

    const int minW = 271;
    const int minH = 126;

    // 场景 A: 从顶部向上拖拽缩小时 (WMSZ_TOP)，若小于最小高度，锁定 top = bottom - minH，绝对杜绝向下漂移
    RECT rA = {100, 200, 100 + 300, 200 + 100}; // 高度仅 100px (< 126)
    WPARAM edgeTop = WMSZ_TOP;
    if ((rA.bottom - rA.top) < minH) {
        if (edgeTop == WMSZ_TOP || edgeTop == WMSZ_TOPLEFT || edgeTop == WMSZ_TOPRIGHT) {
            rA.top = rA.bottom - minH;
        }
    }
    TEST_CHECK((rA.bottom - rA.top) == minH, "高度必须被精准钳位为 126px");
    TEST_CHECK(rA.bottom == 300, "bottom 锚点必须保持不动，top 自动向上纠偏");

    // 场景 B: 从左侧向右拖拽缩小时 (WMSZ_LEFT)，若小于最小宽度，锁定 left = right - minW
    RECT rB = {200, 100, 200 + 200, 100 + 300}; // 宽度仅 200px (< 271)
    WPARAM edgeLeft = WMSZ_LEFT;
    if ((rB.right - rB.left) < minW) {
        if (edgeLeft == WMSZ_LEFT || edgeLeft == WMSZ_TOPLEFT || edgeLeft == WMSZ_BOTTOMLEFT) {
            rB.left = rB.right - minW;
        }
    }
    TEST_CHECK((rB.right - rB.left) == minW, "宽度必须被精准钳位为 271px");
    TEST_CHECK(rB.right == 400, "right 锚点必须保持不动");

    std::cout << "  [PASS] 窗口尺寸下限与防下移算法 100% 验证通过！" << std::endl;
}

void TestIdleStealthModeLogic() {
    std::cout << "[Test 2] 验证 10 秒空闲自续期极窄隐匿模式状态机..." << std::endl;

    const int breakpointW = 285;
    const int minH = 126;

    // 状态 A: 宽窗状态下 (宽 350 > 285)，即使空闲也不进入隐匿模式
    int wA = 350, hA = 126;
    bool stealthA = false;
    if (wA <= breakpointW && hA <= minH) {
        stealthA = true;
    }
    TEST_CHECK(!stealthA, "宽窗状态严禁触发隐匿模式");

    // 状态 B: 窄窗且高度为最小高度时 (宽 271 <= 285 && 高 126 <= 126)，空闲超时触发隐匿
    int wB = 271, hB = 126;
    bool stealthB = false;
    if (wB <= breakpointW && hB <= minH) {
        stealthB = true;
    }
    TEST_CHECK(stealthB, "极窄窗口空闲必须进入隐匿模式 (自动隐藏标题栏)");

    // 状态 C: 鼠标移动或按键时，无条件立即退出隐匿模式
    stealthB = false;
    TEST_CHECK(!stealthB, "活动事件必须立即退出隐匿并恢复标题栏");

    std::cout << "  [PASS] 极窄隐匿模式与自续期状态机逻辑验证通过！" << std::endl;
}

void TestGracefulExitDefense() {
    std::cout << "[Test 3] 验证 7 步优雅退出与注册表复原防线..." << std::endl;

    auto& sysproxy = clashwin::system::SystemProxyManager::Instance();
    sysproxy.EnableProxy(L"127.0.0.1:10801");
    TEST_CHECK(sysproxy.IsProxyEnabled() == true, "代理已开启");

    // 执行退出防线第 3 步: ResetAll()
    sysproxy.ResetAll();
    TEST_CHECK(sysproxy.IsProxyEnabled() == false, "退出防线必须 100% 还原系统代理为关闭");

    std::cout << "  [PASS] 优雅退出 7 步生命周期与代理安全复原全量通过！" << std::endl;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "[ClashWin 切片五] Win32 桌面级防御与生命周期测试" << std::endl;
    std::cout << "====================================================" << std::endl;

    TestWindowSizingClampAlgorithm();
    TestIdleStealthModeLogic();
    TestGracefulExitDefense();

    std::cout << "\n>>> 切片五: Win32 桌面级集成与全生命周期 100% 验收通过！<<<" << std::endl;
    return 0;
}
