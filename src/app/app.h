#pragma once
// App：应用壳聚合（T01 Walking Skeleton）
// 装配顺序：单实例抢占 → 日志 → 窗口 → D2D → 单实例监听线程 → 消息循环
#include <windows.h>
#include <d2d1.h>
#include <string>

namespace clashwin::consts {
inline constexpr uint16_t kSingletonPort = 33335;      // ← 02附录 单例端口（dev 33336）
inline constexpr int kWindowW = 271, kWindowH = 680;   // ← 02§7 默认窗口 271×680
inline constexpr UINT kAppWakeup = WM_APP + 1;         // 单实例监听线程→UI 线程唤起
}  // namespace clashwin::consts

namespace clashwin {

class App {
 public:
  App() = default;
  ~App();

  // 返回 false = 已有实例占用 33335（本进程应立即退出，不进消息循环）
  bool init();

  // 标准消息循环，返回 wParam
  int run();

 private:
  bool init_logger();
  bool bind_singleton();          // bind 33335：成功=true
  void notify_existing_and_exit();  // 作为客户端唤醒首实例后 exit(0)，不返回
  bool init_window();             // 注册窗口类 + 创建 271×680 无框窗
  bool init_d2d();
  void destroy_d2d();
  void recreate_d2d();            // WM_DISPLAYCHANGE/设备丢失时全量重建
  void draw();                    // BeginDraw/Clear(#F5F5F5)/EndDraw
  void start_accept_thread();     // 首实例：accept 线程，收到连接即唤醒窗口

  static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);

  HWND hwnd_ = nullptr;
  ID2D1Factory* d2d_factory_ = nullptr;
  ID2D1HwndRenderTarget* rt_ = nullptr;
  uintptr_t listen_sock_ = 0;     // SOCKET（避免头文件引入 winsock）
  uint16_t singleton_port_ = consts::kSingletonPort;  // 实际使用的单例端口
  bool wsa_inited_ = false;
};

}  // namespace clashwin
