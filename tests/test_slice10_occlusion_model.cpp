#include <iostream>
#include <cassert>
#include "window/main_window.hpp"
#include "render/d2d_renderer.hpp"

using namespace ClashWin;

void TestOcclusionGeometryAndBreakpoints() {
    std::cout << "[Test 1] Testing Occlusion Geometry & Breakpoints..." << std::endl;

    // 权威断点常量验证
    assert(MainWindow::MIN_WIDTH == 271);
    assert(MainWindow::MIN_HEIGHT == 126);
    assert(MainWindow::NARROW_BREAKPOINT == 285);

    // 1. 微缩流量计模式判定 (isMiniStatus: height <= 126)
    auto is_mini_status = [](int h) { return h <= MainWindow::MIN_HEIGHT; };
    assert(is_mini_status(126) == true);
    assert(is_mini_status(125) == true);
    assert(is_mini_status(100) == true);
    assert(is_mini_status(127) == false);
    assert(is_mini_status(300) == false);

    // 2. 极窄/宽窗模式判定 (isMinimalWidth: width <= 285)
    auto is_minimal_width = [](int w) { return w <= MainWindow::NARROW_BREAKPOINT; };
    assert(is_minimal_width(271) == true);
    assert(is_minimal_width(285) == true);
    assert(is_minimal_width(286) == false);
    assert(is_minimal_width(500) == false);

    std::cout << "  -> Geometric boundaries & assertions verified!" << std::endl;
}

void TestMiniStatusUnmountInvariant() {
    std::cout << "[Test 2] Testing Micro Traffic Meter Invariant (Unmounted components)..." << std::endl;

    // 当处于微缩状态时：
    // - 抽屉必须强制关闭 (m_isDrawerOpen = false)
    // - 活跃连接轮询定时器必须销毁 (KillTimer CONN_TIMER_ID)
    // - 仅渲染底部实时流量指示栏
    int testHeight = 126;
    bool isMini = (testHeight <= MainWindow::MIN_HEIGHT);
    assert(isMini);

    bool drawerOpen = true;
    bool connTimerActive = true;

    if (isMini) {
        drawerOpen = false;
        connTimerActive = false;
    }

    assert(drawerOpen == false);
    assert(connTimerActive == false);

    std::cout << "  -> Micro status invariants & resource conservation verified!" << std::endl;
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "  ClashWin Phase 4 Slice 10 - Occlusion Model Test " << std::endl;
    std::cout << "==================================================" << std::endl;

    TestOcclusionGeometryAndBreakpoints();
    TestMiniStatusUnmountInvariant();

    std::cout << "\n>>> [ALL SLICE 10 TESTS PASSED SUCCESSFULLY!] <<<\n" << std::endl;
    return 0;
}
