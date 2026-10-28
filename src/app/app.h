#pragma once
// App：应用壳聚合（T01 Walking Skeleton；T11 起窗口壳移交 MainWindow）
// 装配顺序：单实例抢占 → 日志 → 主窗口（MainWindow） → 托盘（Tray） → 消息循环
#include <windows.h>
#include <memory>
#include <string>

namespace clashwin::consts {
inline constexpr uint16_t kSingletonPort = 33336;
}  // namespace clashwin::consts

namespace clashwin::ui::win {
class MainWindow;
}

namespace clashwin::system {
class Tray;
}

namespace clashwin::app {
class SingletonServer;
}

namespace clashwin {

class App {
 public:
  App();
  ~App();

  // 返回 false = 已有 ClashWin 实例占用 33336（本进程已唤醒已有实例并退出）
  bool init();

  // 标准消息循环，返回 wParam
  int run();

 private:
  bool init_logger();

  uint16_t singleton_port_ = consts::kSingletonPort;
  bool wsa_inited_ = false;
  ui::win::MainWindow* main_window_ = nullptr;
  std::unique_ptr<system::Tray> tray_;
  std::unique_ptr<app::SingletonServer> singleton_server_;
};

}  // namespace clashwin
