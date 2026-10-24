#include "app/app.h"
#include "util/log.h"
#include "util/utf.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <atomic>
#include <format>

namespace clashwin {

namespace {
std::atomic<bool> g_accept_running{false};
}  // namespace

App::~App() {
  g_accept_running = false;             // 让 accept 线程的 select 超时退出
  if (listen_sock_) {
    closesocket(static_cast<SOCKET>(listen_sock_));
    listen_sock_ = 0;
  }
  destroy_d2d();
  if (hwnd_) DestroyWindow(hwnd_);
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

  if (!init_window()) {
    CWLOG_ERROR("App::init：窗口创建失败");
    return false;
  }
  if (!init_d2d()) {
    CWLOG_ERROR("App::init：D2D 初始化失败");
    return false;
  }
  draw();
  start_accept_thread();
  CWLOG_INFO("App::init 完成：271x680 无框窗口 + D2D 就绪");
  return true;
}

int App::run() {
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

// ---------- 单实例（← utils/server.rs 行为桩） ----------

bool App::bind_singleton() {
  // ← 02附录：单例 33335（dev 33336）。开发/测试时可用环境变量覆盖端口，
  // 避免与本机真实 Clash Mini 实例冲突；正式运行固定 33335。
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
  if (!listen_sock_) return;
  g_accept_running = true;
  uintptr_t ls = listen_sock_;
  HWND hwnd = hwnd_;
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
      PostMessageW(hwnd, consts::kAppWakeup, 0, 0);
    }
  }).detach();
}

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

// ---------- 窗口 + D2D ----------

bool App::init_window() {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &App::wndproc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = L"ClashWinMainWindow";
  if (!RegisterClassExW(&wc)) return false;

  // 271×680 默认几何，屏幕居中（几何持久化/钳制属 T11，不做）
  int x = (GetSystemMetrics(SM_CXSCREEN) - consts::kWindowW) / 2;
  int y = (GetSystemMetrics(SM_CYSCREEN) - consts::kWindowH) / 2;
  hwnd_ = CreateWindowExW(
      0, wc.lpszClassName, L"Clash Mini",
      WS_POPUP | WS_VISIBLE | WS_THICKFRAME | WS_MINIMIZEBOX | WS_SYSMENU,
      x, y, consts::kWindowW, consts::kWindowH, nullptr, nullptr,
      wc.hInstance, this);  // GWLP_USERDATA ← this
  if (!hwnd_) return false;
  RECT wr;
  GetWindowRect(hwnd_, &wr);  // 启动诊断：记录实际窗口外接矩形
  CWLOG_INFO(std::format("窗口已创建：{}x{} @({},{})",
                         wr.right - wr.left, wr.bottom - wr.top, wr.left,
                         wr.top));
  return true;
}

bool App::init_d2d() {
  HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 &d2d_factory_);
  if (FAILED(hr)) return false;
  recreate_d2d();
  return rt_ != nullptr;
}

void App::recreate_d2d() {
  destroy_d2d();
  if (!d2d_factory_ || !hwnd_) return;
  RECT rc;
  GetClientRect(hwnd_, &rc);
  D2D1_SIZE_U size = D2D1::SizeU(
      static_cast<UINT32>(rc.right - rc.left),
      static_cast<UINT32>(rc.bottom - rc.top));
  d2d_factory_->CreateHwndRenderTarget(
      D2D1::RenderTargetProperties(),
      D2D1::HwndRenderTargetProperties(hwnd_, size),
      &rt_);
}

void App::destroy_d2d() {
  if (rt_) {
    rt_->Release();
    rt_ = nullptr;
  }
}

void App::draw() {
  if (!rt_) return;
  rt_->BeginDraw();
  rt_->Clear(D2D1::ColorF(0xF5F5F5));  // ← 02§7 亮色底 #F5F5F5
  HRESULT hr = rt_->EndDraw();
  if (hr == D2DERR_RECREATE_TARGET) recreate_d2d();
}

LRESULT CALLBACK App::wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  App* self = nullptr;
  if (msg == WM_NCCREATE) {
    self = static_cast<App*>(
        reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      if (self) self->draw();
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_SIZE: {
      if (self && self->rt_) {
        D2D1_SIZE_U size = D2D1::SizeU(LOWORD(lp), HIWORD(lp));
        if (FAILED(self->rt_->Resize(size))) self->recreate_d2d();
        self->draw();
      }
      return 0;
    }
    case WM_DISPLAYCHANGE:  // 分辨率/显示器变化：全量重建 target
      if (self) {
        self->recreate_d2d();
        self->draw();
      }
      return 0;
    case WM_ERASEBKGND:  // D2D 全屏 Clear，跳过背景擦除防闪烁
      return 1;
    case consts::kAppWakeup:  // 二次启动唤起（← /commands/visible）
      CWLOG_INFO("收到单实例唤醒请求：ShowWindow(SW_RESTORE)+前台化");
      ShowWindow(hwnd, SW_RESTORE);
      SetForegroundWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace clashwin
