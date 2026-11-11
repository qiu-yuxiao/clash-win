// Win32 入口：窗口类注册/创建与 D2D 均在 App 内装配（T01 Walking Skeleton）
#include "app/app.h"
#include "app/lifecycle.h"
#include "system/guardian.h"

#include <shobjidl.h>

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
  // ★ 伴生看门狗极早期拦截（← Mini main.rs:4-8，v3.2.5 BUG-316）：
  //   在初始化 D2D/UI/网络栈之前直接进入纯内核对象阻塞等待态（0% CPU，~1MB 工作集）。
  if (const auto code = clashwin::system::guardian::maybe_run_guardian_mode()) {
    return *code;
  }

  SetCurrentProcessExplicitAppUserModelID(L"io.github.clashwin.clashwin");
  clashwin::App app;
  if (!app.init()) return 1;  // 单实例失败或装配失败即退出
  int ret = app.run();
  clashwin::app::Lifecycle::instance().quit();
  return ret;
}
