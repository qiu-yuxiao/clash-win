#include "app/app.h"

#include "cmd/command_service.h"
#include "config/config.h"
#include "config/prfitem.h"
#include "core/timer.h"
#include "events/event_bus.h"
#include "events/event_names.h"
#include "ui/theme/theme.h"
#include "ui/main_window.h"
#include "util/log.h"

#include <map>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <atomic>
#include <format>

namespace clashwin {

namespace {
std::atomic<bool> g_accept_running{false};
// 单实例唤醒（与 ui/win/main_window.cpp 的 kAppWakeup 同值 WM_APP+1）
constexpr UINT kAppWakeupMsg = WM_APP + 1;
}  // namespace

App::~App() {
  g_accept_running = false;             // 让 accept 线程的 select 超时退出
  if (listen_sock_) {
    closesocket(static_cast<SOCKET>(listen_sock_));
    listen_sock_ = 0;
  }
  delete main_window_;  // WM_DESTROY 内完成几何落盘 + PostQuitMessage
  main_window_ = nullptr;
  if (wsa_inited_) WSACleanup();
  CWLOG_INFO("App 析构：资源已释放");
  Logger::instance().flush();
}

bool App::init() {
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
  wsa_inited_ = true;

  if (!bind_singleton()) {
    notify_existing_and_exit();  // 内部 exit(0)，不会返回
  }

  if (!init_logger()) return false;
  CWLOG_INFO(std::format("App::init：单实例 bind {} 成功，首实例启动",
                         singleton_port_));

  // T12：主题服务初始化（缺省 retro-3d/light；皮肤/亮暗切换 T17 接 verge）
  ui::theme::ThemeService::instance().init();

  // T15：订阅定时更新（默认 1440min/过期补跑/失败 2min 重试/切换互斥）
  {
    auto& timer = core::ProfileTimer::instance();
    timer.set_intervals_provider([] {
      std::map<std::string, std::pair<uint64_t, int64_t>> m;
      auto latest = config::Config::instance().profiles().latest_arc();
      if (latest && latest->items) {
        for (const auto& it : *latest->items) {
          if (!it.uid) continue;
          uint64_t interval = 1440;
          if (it.option && it.option->update_interval)
            interval = *it.option->update_interval;
          // next = updated(Unix 秒)×1000 + interval×60k；无 updated → 0 补跑
          int64_t next =
              it.updated ? *it.updated * 1000 +
                                static_cast<int64_t>(interval) * 60000
                         : 0;
          m[*it.uid] = {interval, next};
        }
      }
      return m;
    });
    timer.set_update_fn([](const std::string& uid) {
      auto r = cmd::CommandService::instance().invoke(
          "update_profile", {{"index", uid}});
      return r.is_ok();
    });
    timer.set_event_emitter([](const std::string& uid, bool is_start) {
      events::EventBus::instance().publish(
          is_start ? events::names::kProfileUpdateStarted
                   : events::names::kProfileUpdateCompleted,
          uid);
    });
    timer.start();
  }

  // T11：窗口壳移交 MainWindow（无框窗 + 标题栏 + 钳制 + Widget 树）
  main_window_ = new ui::win::MainWindow();
  if (!main_window_->create(/*show_immediately=*/true)) {
    CWLOG_ERROR("App::init：主窗口创建失败");
    return false;
  }
  start_accept_thread();
  CWLOG_INFO("App::init 完成：主窗口就绪");
  return true;
}

int App::run() { return ui::win::MainWindow::run(); }

// ---------- 日志 ----------

bool App::init_logger() {
  wchar_t exe_path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  std::wstring exe_dir(exe_path);
  size_t pos = exe_dir.find_last_of(L"\\/");
  if (pos != std::wstring::npos) exe_dir.resize(pos);
  Logger::instance().init(exe_dir);
  return true;
}

// ---------- 单实例（← utils/server.rs 行为桩） ----------

bool App::bind_singleton() {
  // 单实例端口 33336（与实机 Clash Mini 的 33335 错开，允许共存——迁移期并行
  // 对比是常态，共用端口曾导致实机无法启动）。开发/测试时可用环境变量覆盖。
  uint16_t port = consts::kSingletonPort;
  if (const char* env = getenv("CLASHWIN_SINGLETON_PORT")) {
    int p = atoi(env);
    if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
  }
  singleton_port_ = port;
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    closesocket(s);
    return false;
  }
  listen(s, 4);
  listen_sock_ = static_cast<uintptr_t>(s);
  return true;
}

void App::notify_existing_and_exit() {
  // 作为客户端连单例端口，发 /commands/visible 请求（桩：内容不解析）唤醒首实例
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s != INVALID_SOCKET) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(singleton_port_);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
      const char req[] = "GET /commands/visible HTTP/1.1\r\n"
                         "Host: 127.0.0.1\r\n"
                         "Connection: close\r\n\r\n";
      send(s, req, static_cast<int>(sizeof(req) - 1), 0);
      char buf[256];
      recv(s, buf, sizeof(buf), 0);  // 尽力读一次响应，失败不影响
    }
    closesocket(s);
  }
  exit(0);  // ← server.rs：二次启动唤醒后自身退出
}

void App::start_accept_thread() {
  if (!listen_sock_ || !main_window_) return;
  g_accept_running = true;
  uintptr_t ls = listen_sock_;
  HWND hwnd = main_window_->hwnd();
  std::thread([ls, hwnd] {
    SOCKET s = static_cast<SOCKET>(ls);
    while (g_accept_running) {
      fd_set rset;
      FD_ZERO(&rset);
      FD_SET(s, &rset);
      timeval tv{0, 500 * 1000};  // 500ms 轮询，便于退出
      int r = select(0, &rset, nullptr, nullptr, &tv);
      if (r <= 0) continue;
      SOCKET c = accept(s, nullptr, nullptr);
      if (c == INVALID_SOCKET) continue;
      // 桩：不解析请求，直接唤醒窗口；响应留给后续任务的 embed server
      const char resp[] = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n"
                          "Connection: close\r\n\r\n";
      send(c, resp, static_cast<int>(sizeof(resp) - 1), 0);
      shutdown(c, SD_BOTH);
      closesocket(c);
      // 唤起动作必须在 UI 线程做（ShowWindow/SetForegroundWindow）
      PostMessageW(hwnd, kAppWakeupMsg, 0, 0);
    }
  }).detach();
}

}  // namespace clashwin
