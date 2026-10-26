#pragma once
// App：应用壳聚合（T01 Walking Skeleton；T11 起窗口壳移交 MainWindow）
// 装配顺序：单实例抢占 → 日志 → 主窗口（MainWindow） → 单实例监听线程 → 消息循环
#include <windows.h>
#include <string>

namespace clashwin::consts {
// 单实例端口：与实机 Clash Mini（33335）错开为 33336，允许两者共存（迁移期
// 长期并行对比是常态；原版共用 33335 曾导致实机无法启动，2026-10-26 用户报告）。
// 端口为内部实现细节，不影响外观/功能/行为/结果四同。测试可用
// CLASHWIN_SINGLETON_PORT 覆盖。
inline constexpr uint16_t kSingletonPort = 33336;
}  // namespace clashwin::consts

namespace clashwin::ui::win {
class MainWindow;
}

namespace clashwin {

class App {
 public:
  App() = default;
  ~App();

  // 返回 false = 已有 ClashWin 实例占用 33336（本进程应立即退出，不进消息循环）
  bool init();

  // 标准消息循环，返回 wParam
  int run();

 private:
  bool init_logger();
  bool bind_singleton();          // bind 33336：成功=true
  void notify_existing_and_exit();  // 作为客户端唤醒首实例后 exit(0)，不返回
  void start_accept_thread();     // 首实例：accept 线程，收到连接即唤醒窗口

  uintptr_t listen_sock_ = 0;     // SOCKET（避免头文件引入 winsock）
  uint16_t singleton_port_ = consts::kSingletonPort;  // 实际使用的单例端口
  bool wsa_inited_ = false;
  ui::win::MainWindow* main_window_ = nullptr;  // 拥有权在 .cpp（避免头循环）
};

}  // namespace clashwin
