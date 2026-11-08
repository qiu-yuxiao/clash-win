// Win32 入口：窗口类注册/创建与 D2D 均在 App 内装配（T01 Walking Skeleton）
#include "app/app.h"
#include "app/lifecycle.h"

#include <shobjidl.h>

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
  SetCurrentProcessExplicitAppUserModelID(L"io.github.clashwin.clashwin");
  clashwin::App app;
  if (!app.init()) return 1;  // 单实例失败或装配失败即退出
  int ret = app.run();
  clashwin::app::Lifecycle::instance().quit();
  return ret;
}
