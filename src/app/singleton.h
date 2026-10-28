#pragma once
// Singleton（T19）：TCP 33336 单实例服务与 HTTP 命令响应
// - SO_REUSEADDR 绑定，消除快速重启 TIME_WAIT 冲突
// - 端点：
//   - GET /commands/visible -> 200 OK "ok"（退出轻量模式，唤醒窗口）
//   - GET /commands/pac -> 200 OK Content-Type: application/x-ns-proxy-autoconfig（替换 %mixed-port%）
//   - GET /commands/scheme?param=<url> -> 200 OK "ok"（解码并触发深链导入）
//   - 其他路由 -> 404 Not Found
// - 二次启动客户端判定：--silent 静默退出 / clash:// 调 scheme / 缺省调 visible

#include <windows.h>
#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace clashwin::app {

class SingletonServer {
 public:
  SingletonServer();
  ~SingletonServer();

  // 绑定并启动单实例服务
  // on_visible: /commands/visible 时的回调
  // on_scheme: /commands/scheme?param=... 时的回调（入参为已 URL 解码的深链字符串）
  bool start(uint16_t port,
             std::function<void()> on_visible = nullptr,
             std::function<void(const std::string&)> on_scheme = nullptr);

  void stop();
  bool is_running() const { return running_.load(); }
  uint16_t port() const { return port_; }

  // 静态工具：生成 PAC 脚本内容（替换 %mixed-port%）
  static std::string generate_pac_script(uint16_t port_override = 0);

  // 纯函数处理 HTTP 请求并生成响应（用于单测和内联处理）
  std::string handle_http_request(const std::string& raw_request);

 private:
  void accept_loop();
  void handle_connection(uintptr_t client_sock);

  uintptr_t listen_sock_ = 0;
  uint16_t port_ = 0;
  std::atomic<bool> running_{false};
  std::thread worker_;

  std::function<void()> on_visible_;
  std::function<void(const std::string&)> on_scheme_;
};

class SingletonClient {
 public:
  // 检查是否已有实例占用端口；
  // 若未占用返回 false；
  // 若占用：根据命令行参数发送 HTTP 请求，然后调用 exit(0) 或返回 true
  static bool notify_or_exit(uint16_t port, int argc, wchar_t* argv[], bool do_exit = true);

  // 客户端轻量 HTTP 发送工具（供测试或内部调用）
  static int send_request(uint16_t port, const std::string& path_and_query, std::string* response_out = nullptr);
};

}  // namespace clashwin::app
