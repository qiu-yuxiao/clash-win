// 主窗口实现（T11）：无框窗 + 钳制 + Widget 树宿主 + 几何持久化。
// 行为对照见任务报告「窗口行为对照表」。
#include "main_window.h"
#include "main_window_internal.h"

#include <windowsx.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include <imm.h>
#pragma comment(lib, "imm32.lib")

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>

#include "clashwin/config/constants.h"
#include "config/config.h"
#include "core/core_manager.h"
#include "events/event_bus.h"
#include "mihomo/ws_manager.h"
#include "system/signals.h"
#include "system/tray.h"
#include "theme/theme.h"
#include "ui/textfield.h"
#include "ui/ui_postbox.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/node.h"
#include "util/utf.h"

namespace clashwin::ui::win {

namespace fs = std::filesystem;
namespace utf = clashwin::utf;
using config::window_state::WindowStateStore;

// ---------- LargeModeController 持久化 ----------
void LargeModeController::save(const std::wstring& path, double w, double h) {
  // {"height":..,"width":..}（localStorage last-non-large-size 等价落盘）
  std::wstring tmp = path + L".tmp";
  FILE* f = nullptr;
  if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || !f) return;
  std::string json = "{\"height\":" + std::to_string(h) + ",\"width\":" +
                     std::to_string(w) + "}";
  fwrite(json.data(), 1, json.size(), f);
  fclose(f);
  std::error_code ec;
  fs::rename(tmp, path, ec);
  if (ec) fs::copy_file(tmp, path, fs::copy_options::overwrite_existing, ec);
}

std::optional<std::pair<double, double>> LargeModeController::load(
    const std::wstring& path) {
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return std::nullopt;
  std::string buf;
  char ch;
  while (fread(&ch, 1, 1, f) == 1) buf.push_back(ch);
  fclose(f);
  // 极简解析 {"height":H,"width":W}
  auto find_num = [&](const char* key) -> std::optional<double> {
    size_t p = buf.find(key);
    if (p == std::string::npos) return std::nullopt;
    p = buf.find(':', p);
    if (p == std::string::npos) return std::nullopt;
    try {
      return std::stod(buf.substr(p + 1));
    } catch (...) {
      return std::nullopt;
    }
  };
  auto h = find_num("\"height\"");
  auto w = find_num("\"width\"");
  if (h && w) return std::make_pair(*w, *h);
  return std::nullopt;
}

// ---------- 创建 ----------
MainWindow::MainWindow() = default;

MainWindow::~MainWindow() {
  core::CoreManager::instance().set_pre_stop_hook(nullptr);
  if (alive_) {
    alive_->store(false, std::memory_order_release);
  }
  if (uia_) {
    uia_->shutdown();
  }
  if (node_data_ && node_data_sub_) {
    node_data_->unsubscribe(node_data_sub_);
    node_data_sub_ = 0;
  }
  if (theme_sub_) {
    theme::ThemeService::instance().unsubscribe(theme_sub_);
    theme_sub_ = 0;
  }
  if (bus_sub_core_upgrade_) {
    events::EventBus::instance().unsubscribe(bus_sub_core_upgrade_);
    bus_sub_core_upgrade_ = 0;
  }
  if (bus_sub_open_guide_) {
    events::EventBus::instance().unsubscribe(bus_sub_open_guide_);
    bus_sub_open_guide_ = 0;
  }
  if (postbox_sub_delay_) {
    UiPostbox::instance().unsubscribe(postbox_sub_delay_);
    postbox_sub_delay_ = 0;
  }
  if (postbox_sub_refresh_) {
    UiPostbox::instance().unsubscribe(postbox_sub_refresh_);
    postbox_sub_refresh_ = 0;
  }
  if (net_health_sub_) {
    unsubscribe_network_health(net_health_sub_);
    net_health_sub_ = 0;
  }
  if (frame_clock() == &clock_) {
    set_frame_clock(nullptr);
  }
}

bool MainWindow::create(bool show_immediately, bool defer_render) {
  // UI 线程标记：通知可能由后台线程 push，SnackbarW 据此决定是否转投 UI 线程
  UiPostbox::instance().mark_ui_thread();
  // exe 目录（几何文件落此处；与 T03 app_home_dir 口径一致）
  wchar_t exe[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  app_dir_ = std::wstring(exe);
  size_t pos = app_dir_.find_last_of(L"\\/");
  if (pos != std::wstring::npos) app_dir_.resize(pos);

  if (!init_window_class()) return false;
  // 在 CreateWindowExW 之前预建 UIA 桥接：若读屏软件（讲述人/NVDA）已提前运行，
  // 系统在窗口创建期即会同步投递 WM_GETOBJECT，预建可确保首条查询即命中自绘 Provider
  uia_ = std::make_unique<UiaWindowBridge>(nullptr, &root_,
                                           [this] { return dpi_scale(); });
  if (!create_hwnd()) {
    uia_->shutdown();
    uia_.reset();
    return false;
  }
  uia_->set_hwnd(hwnd_);
  // 静默启动直入轻量：跳过 D2D 设备创建（渲染层进轻量后立即释放，纯属浪费）
  if (!defer_render && !init_render()) return false;
  build_widget_tree();

  // 皮肤/亮暗/主色热切换：ThemeService 六皮肤表由此接上订阅者，切皮肤即重绘。
  // ← Mini: src/pages/_layout/components/settings-drawer.tsx:425-487 选中格每次渲染按 get3DButtonStyle
  //   (theme, 'contained', 'primary') 取当前 skin；src: src/utils/button-styles/button.ts:11
  //   getActiveSkin(theme) —— 所有控件按当前皮肤即时换表，无需重启。
  theme_sub_ = theme::ThemeService::instance().subscribe([this] {
    if (settings_drawer_) settings_drawer_->restyle_cards();
    // 主页节点列表的输入框也走 get3DInputStyle 查表，缓存需重刷
    if (node_list_) node_list_->restyle_theme();
    clock_.invalidate({0, 0, static_cast<float>(client_w_),
                       static_cast<float>(client_h_)});
  });

  // 几何恢复（钳制 + 脱屏居中守卫，对接 restore_window_state）
  apply_geometry_restore();

  // 断点 → 标题栏图标/场景挂卸
  // 连接面板只在设置抽屉右列（src: src/pages/_layout/components/settings-drawer.tsx:306-310 {!isMinimalWidth && <ConnectionsPanel/>}）。
  bp_.subscribe([this](WinState prev, WinState cur) {
    CWLOG_INFO(std::format(
        "[DPIARCH] bp {}->{} narrow={} large={} logical={:.2f}x{:.2f}",
        static_cast<int>(prev), static_cast<int>(cur), bp_.is_narrow(),
        bp_.is_large(), bp_.logical_w(), bp_.logical_h()));
    title_bar_->set_large_mode(bp_.is_large());
    scene_->apply_state(prev, cur);
    scene_->set_narrow(bp_.is_narrow());
    // T14：Mini → 流量条占满上栏空间（census 极小窗布局）
    if (traffic_pane_) {
      traffic_pane_->set_mini(bp_.state() == WinState::Mini);
      traffic_pane_->set_narrow(bp_.is_narrow());  // src: src/pages/_layout.tsx:684 宽窄内距与曲线模式分支
    }
    // T17：皮肤条高≤830 隐藏（census 口径；窗口客户区逻辑高）
    if (settings_drawer_ && hwnd_) {
      RECT rc{};
      GetClientRect(hwnd_, &rc);
      // 【DPIARCH】逻辑高 = 物理高 ÷ (dpi/96)（阈值 830 为逻辑口径）
      double logical_h = static_cast<double>(rc.bottom - rc.top) /
                         static_cast<double>(dpi_scale());
      bool show_skin = logical_h > 830.0;
      for (auto* b : settings_drawer_->theme_card()->skin_buttons_)
        b->set_visible(show_skin);
    }
  });
  // 初始同步：WM_SIZE 首帧判定早于 subscribe（Breakpoints 同值不重发事件），
  // 不补一次则 scene 永远停在 wide 态（traffic_h 135 而非 narrow 107）
  title_bar_->set_large_mode(bp_.is_large());
  scene_->set_narrow(bp_.is_narrow());
  if (traffic_pane_) {
    traffic_pane_->set_mini(bp_.state() == WinState::Mini);
    traffic_pane_->set_narrow(bp_.is_narrow());
  }
  // 初始同步：WM_SIZE 首帧判定早于 subscribe（Breakpoints 同值不重发事件）
  scene_->apply_state(WinState::Normal, bp_.state());
  stealth_.subscribe([this](bool hidden) {
    title_bar_->set_visible(!hidden);  // 隐藏时整栏不挂载（内容顶满）
    if (snackbar_) {
      snackbar_->set_top_offset(hidden ? 0.0f : static_cast<float>(kTitleBarH));
    }
    float s = dpi_scale();
    float lw = static_cast<float>(client_w_) / s;
    float lh = static_cast<float>(client_h_) / s;
    root_.layout_tree({0, 0, lw, lh});
    clock_.invalidate({0, 0, static_cast<float>(client_w_),
                       static_cast<float>(client_h_)});
  });

  RECT rc;
  GetClientRect(hwnd_, &rc);
  // 启动期显式重排：跳过 persist_geometry()，避免把 dpi 未换挡的中间几何写回盘
  on_client_resized(rc.right - rc.left, rc.bottom - rc.top, false);

  build_scene();
  // 【LAYOUTFIX】quickbar / 节点列表是在 build_scene 里才挂上树的，晚于上面
  // 首次 layout_tree → 挂载完成后再按当前客户区重排一次。
  {
    RECT rc2;
    GetClientRect(hwnd_, &rc2);
    // 同上：启动期重排不落盘
    on_client_resized(rc2.right - rc2.left, rc2.bottom - rc2.top, false);
  }

  // 静默启动直入轻量：跳过定时器
  if (!defer_render) {
    SetTimer(hwnd_, detail::kTimerFrame, FrameClock::kFrameIntervalMs, nullptr);
    SetTimer(hwnd_, detail::kTimerIdle, 1000, nullptr);
    SetTimer(hwnd_, detail::kTimerTunGuard, 5000, nullptr);
    SetTimer(hwnd_, detail::kTimerDelayCachePrune, 300000, nullptr);
  }
  start_ms_ = detail::now_ms();
  if (show_immediately) show_and_activate();
  CWLOG_INFO("MainWindow: 创建完成（无框 271x680 + 标题栏三键 + 钳制就绪）");
  return true;
}

bool MainWindow::init_window_class() {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &MainWindow::wndproc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  const std::wstring ico_path = clashwin::util::dirs::app_resources_dir() + L"\\icons\\icon.ico";
  wc.hIcon = static_cast<HICON>(LoadImageW(
      wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
  if (!wc.hIcon) {
    wc.hIcon = static_cast<HICON>(LoadImageW(
        nullptr, ico_path.c_str(), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE));
  }
  wc.hIconSm = static_cast<HICON>(LoadImageW(
      wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
  if (!wc.hIconSm) {
    wc.hIconSm = static_cast<HICON>(LoadImageW(
        nullptr, ico_path.c_str(), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE));
  }
  wc.lpszClassName = detail::kWndClass;
  if (!RegisterClassExW(&wc)) {
    DWORD err = GetLastError();
    if (err != ERROR_CLASS_ALREADY_EXISTS) {
      CWLOG_ERROR(std::format("MainWindow: RegisterClassExW 失败 0x{:08X}",
                              static_cast<unsigned>(err)));
      return false;
    }
  }
  return true;
}

bool MainWindow::create_hwnd() {
  // 默认几何（restore 会覆盖）；【DPIARCH】← window.rs：内部=逻辑 px，物理=×dpi/96
  const float s = static_cast<float>(GetDpiForSystem()) / 96.0f;
  int w = static_cast<int>(kDefaultWindowW * s + 0.5f);
  int h = static_cast<int>(kDefaultWindowH * s + 0.5f);
  int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
  int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
  // ← window.rs:176 加载完成才 show → 创建时不带 WS_VISIBLE，show_and_activate 再显
  // F-D07：窗口标题对齐 Mini（src: src-tauri/src/utils/resolve/window.rs:176 "Clash Mini Ver.{appVersion}"）
  // 豁免: Mini 无对应物 — 按品牌规范替换为 Clash Win，版本号取 kAppVersion（出处: src-tauri/src/utils/resolve/window.rs:176）
  std::wstring win_title =
      L"Clash Win Ver." +
      utf::utf8_to_utf16(clashwin::config::consts::kAppVersion);
  hwnd_ = CreateWindowExW(
      WS_EX_APPWINDOW, detail::kWndClass, win_title.c_str(),
      WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_SYSMENU,
      x, y, w, h, nullptr, nullptr, GetModuleHandleW(nullptr), this);
  {
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    const std::wstring ico_path = clashwin::util::dirs::app_resources_dir() + L"\\icons\\icon.ico";
    HICON hbig = static_cast<HICON>(LoadImageW(
        hinst, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    if (!hbig) {
      hbig = static_cast<HICON>(LoadImageW(
          nullptr, ico_path.c_str(), IMAGE_ICON,
          GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE));
    }
    HICON hsm = static_cast<HICON>(LoadImageW(
        hinst, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (!hsm) {
      hsm = static_cast<HICON>(LoadImageW(
          nullptr, ico_path.c_str(), IMAGE_ICON,
          GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE));
    }
    h_icon_big_ = hbig;
    h_icon_sm_ = hsm;
    if (hwnd_ && hbig) SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hbig));
    if (hwnd_ && hsm) SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hsm));
  }
  if (!hwnd_) {
    CWLOG_ERROR(std::format("MainWindow: CreateWindowExW 失败 0x{:08X}",
                            static_cast<unsigned>(GetLastError())));
    return false;
  }
  // src: window.rs:83「.inner_size(win_w, win_h)」按**窗口所在显示器** DPI 换算；
  //   创建前只能取系统 DPI，创建后用 GetDpiForWindow 校正一次（含居中位置）。
  {
    const float ws = dpi_ / 96.0f;
    if (ws != s) {
      int w2 = static_cast<int>(kDefaultWindowW * ws + 0.5f);
      int h2 = static_cast<int>(kDefaultWindowH * ws + 0.5f);
      int x2 = (GetSystemMetrics(SM_CXSCREEN) - w2) / 2;
      int y2 = (GetSystemMetrics(SM_CYSCREEN) - h2) / 2;
      SetWindowPos(hwnd_, nullptr, x2, y2, w2, h2,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }
  // 【RULERFIX】强制重算外框，使显影前 client 就与窗口矩形一致
  // D11: 消除非客户区厚白边框（DWM 延伸客户区到边缘）
  MARGINS m{1, 1, 1, 1};
  DwmExtendFrameIntoClientArea(hwnd_, &m);
  DWORD border_none = 0xFFFFFFFE;  // 豁免: Mini 无对应物 — DWMWA_COLOR_NONE 抑制 Win11 非客户区边框（出处: 无）
  DwmSetWindowAttribute(hwnd_, 34, &border_none, sizeof(border_none));  // 豁免: Mini 无对应物 — DWMWA_BORDER_COLOR=34（出处: 无）
  SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                   SWP_FRAMECHANGED);
  RECT cr{};
  GetClientRect(hwnd_, &cr);
  CWLOG_INFO(std::format("[RULERFIX] create_hwnd want={}x{} got={}x{} dpi={}",
                         w, h, cr.right - cr.left, cr.bottom - cr.top,
                         GetDpiForWindow(hwnd_)));
  return true;
}

bool MainWindow::init_render() {
  if (!rt_.create_for_hwnd(hwnd_)) return false;
  dpi_ = rt_.dpi();
  clock_.bind([this](const RectF&) { draw(); });
  set_frame_clock(&clock_);
  return true;
}

void ContentStack::on_arrange(const RectF& area) {
  set_bounds(area);
  const float h = (std::max)(0.0f, area.h - (bottom_inset > 0.0f ? bottom_inset : 0.0f));
  for (size_t i = 0; i < child_count(); ++i) {
    Widget* c = child(i);
    if (!c->visible()) continue;
    if (c == overlay)
      c->layout_tree({0, 0, area.w, h});
    else
      c->layout_tree({0, 0, area.w, area.h});
  }
}

bool MainWindow::should_tray_click_hide(bool in_lightweight_mode) const {
  if (!hwnd_) return false;
  const bool visible = IsWindowVisible(hwnd_) != FALSE;
  const bool minimized = IsIconic(hwnd_) != FALSE;
  const bool foreground = GetForegroundWindow() == hwnd_;
  const uint64_t blur_ms = last_blur_ms_.load(std::memory_order_acquire);
  const uint64_t cur_ms = GetTickCount64();
  return compute_should_tray_click_hide(in_lightweight_mode, visible, minimized,
                                        foreground, blur_ms, cur_ms);
}

bool MainWindow::is_any_overlay_open() const {
  // src: src/pages/_layout.tsx:365
  return (settings_drawer_ && settings_drawer_->is_open()) ||
         (add_url_ && add_url_->is_open()) ||
         (log_viewer_ && log_viewer_->is_open()) ||
         (first_run_ && first_run_->is_open()) ||
         (client_update_ && client_update_->is_open()) ||
         (core_update_ && core_update_->is_open());
}

bool MainWindow::is_editable_focused() const {
  // src: src/utils/disable-webview-shortcuts.ts:9
  return dynamic_cast<TextField*>(focused_widget()) != nullptr;
}

void MainWindow::update_ime_position() {
  Widget* fw = focused_widget();
  auto* tf = dynamic_cast<TextField*>(fw);
  if (!tf || !hwnd_) {
    DestroyCaret();
    return;
  }

  float scale = dpi_scale();
  PointF cp = tf->caret_point();
  float ch = tf->caret_height();
  int px = static_cast<int>(std::round(cp.x * scale));
  int py = static_cast<int>(std::round(cp.y * scale));
  int ph = static_cast<int>(std::round(ch * scale));

  // 1. 设置系统 Caret，确保通过 GetCaretPos 感知的现代输入法候选框精确跟随
  CreateCaret(hwnd_, NULL, 2, ph);
  SetCaretPos(px, py);

  // 2. 设置 IMM32 组合窗与候选窗位置，确保所有标准输入法候选框精准定位在光标下方
  HIMC himc = ImmGetContext(hwnd_);
  if (himc) {
    COMPOSITIONFORM cf{};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = px;
    cf.ptCurrentPos.y = py;
    ImmSetCompositionWindow(himc, &cf);

    CANDIDATEFORM cdf{};
    cdf.dwIndex = 0;
    cdf.dwStyle = CFS_CANDIDATEPOS;
    cdf.ptCurrentPos.x = px;
    cdf.ptCurrentPos.y = py + ph;
    ImmSetCandidateWindow(himc, &cdf);

    ImmReleaseContext(hwnd_, himc);
  }
}

float MainWindow::dpi_scale() const {
  return static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
}

void MainWindow::apply_geometry_restore() {
  auto st = config::window_state::restore(clashwin::util::dirs::window_state_path());
  if (!st) {
    restore_done_ = true;
    return;  // 无记录 → 默认 271×680 居中
  }
  // 【DPI-fix】window_state.json 存逻辑像素（persist_geometry ÷s 落盘），
  //   SetWindowPos 需要物理像素 → ×(dpi/96)；x/y 同理。
  const float s = dpi_scale();
  // 【DPIARCH】四舍五入（与 create/MINMAX 同口径；persist 存逻辑 → ×s 回物理闭环）
  int w = static_cast<int>(st->width * s + 0.5f);
  int h = static_cast<int>(st->height * s + 0.5f);
  int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
  int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
  if (st->x && st->y) {
    x = static_cast<int>(*st->x * s + 0.5f);
    y = static_cast<int>(*st->y * s + 0.5f);
  }
  SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
  // ← init：恢复到 640×860（大尺寸模式退出时关机）→ 标记 large + 读盘记忆尺寸
  double lw = st->width, lh = st->height;  // 断点口径 = 逻辑像素
  if (Breakpoints::is_large_size(lw, lh)) {
    large_.on_resized(lw, lh);  // 保持 large（isLargeSize 分支不动作）
    large_.enter();
    if (auto saved = LargeModeController::load(
            clashwin::util::dirs::app_home_dir() +
            L"\\last_non_large_size.json")) {
      // 经 on_resized 记忆路径写入
      last_non_large_restore_ = *saved;
    }
    bp_.set_large(true);
  }
  restore_done_ = true;  // 【DPIFIX】此后 WM_SIZE/WM_MOVE 才允许落盘
}

// ---------- 行为入口 ----------
void MainWindow::show_and_activate() {
  if (!hwnd_) return;
  // src: src-tauri/src/utils/window_manager.rs:503-555 show_main_window / activate_window
  const bool was_minimized = IsIconic(hwnd_) != FALSE;
  if (was_minimized) {
    ShowWindow(hwnd_, SW_RESTORE);
  } else {
    ShowWindow(hwnd_, SW_SHOW);
    RECT wr{};
    if (GetWindowRect(hwnd_, &wr)) {
      const int ww = wr.right - wr.left;
      const int wh = wr.bottom - wr.top;
      HMONITOR mon = MonitorFromRect(&wr, MONITOR_DEFAULTTONULL);
      bool on_screen = false;
      if (mon) {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, &mi)) {
          RECT inter{};
          if (IntersectRect(&inter, &wr, &mi.rcWork)) {
            on_screen = (inter.right - inter.left >= 48) &&
                        (inter.bottom - inter.top >= 30);
          }
        }
      }
      if (!on_screen && ww > 0 && wh > 0) {
        MONITORINFO pmi{};
        pmi.cbSize = sizeof(pmi);
        HMONITOR prim = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        RECT work{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        if (prim && GetMonitorInfoW(prim, &pmi)) work = pmi.rcWork;
        const int nx = work.left + ((work.right - work.left) - ww) / 2;
        const int ny = work.top + ((work.bottom - work.top) - wh) / 2;
        SetWindowPos(hwnd_, nullptr, nx, ny, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
    }
  }
  SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  if (!pinned_) {
    SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
  SetForegroundWindow(hwnd_);
  const int64_t now = detail::now_ms();
  stealth_.feed_activity(now);

  // ← src/pages/_layout.tsx:474-479 退出轻量唤醒走 skipProbeIfFresh 刷新；普通恢复可见仅 3s 节流同步展示状态（BUG-404）
  if (lightweight_wakeup_pending_) {
    lightweight_wakeup_pending_ = false;
    last_visibility_refresh_ms_ = now;
    reload_nodes();
    auto cur = config::Config::instance().profiles().latest_arc()->current;
    if (cur && !cur->empty()) {
      trigger_auto_select_and_refresh(*cur, false, true);
    }
    return;
  }

  // ← src/pages/_layout.tsx:548-551 窗口恢复可见仅同步前端状态展示，不触发活跃节点探测雪崩（BUG-404）
  if (now - last_visibility_refresh_ms_ >= 3000) {
    last_visibility_refresh_ms_ = now;
    reload_nodes();
  }
}

void MainWindow::hide() {
  last_blur_ms_.store(0, std::memory_order_release);
  if (settings_drawer_) settings_drawer_->flush_pending_skin_prefs();
  if (node_list_) node_list_->cancel_pending_filter_auto_test();
  mouse_tracking_ = false;
  clear_mouse_hover();
  if (tooltip_host_) tooltip_host_->leave();
  ShowWindow(hwnd_, SW_HIDE);
}

void MainWindow::release_d2d_resources() {
  if (settings_drawer_) settings_drawer_->flush_pending_skin_prefs();
  if (node_list_) node_list_->cancel_pending_filter_auto_test();
  if (tooltip_host_) tooltip_host_->leave();
  rt_.destroy();
  if (!hwnd_) return;
  KillTimer(hwnd_, detail::kTimerFrame);
  KillTimer(hwnd_, detail::kTimerIdle);
  KillTimer(hwnd_, detail::kTimerTunGuard);
  KillTimer(hwnd_, detail::kTimerSkinPersist);
  KillTimer(hwnd_, detail::kTimerFilterAutoTest);
  KillTimer(hwnd_, detail::kTimerDelayCachePrune);
}

void MainWindow::restore_d2d_resources() {
  if (!hwnd_) return;
  lightweight_wakeup_pending_ = true;
  rt_.create_for_hwnd(hwnd_);
  // 静默启动 defer_render=true 时跳过了 init_render()，此处必须补齐 clock_ 与 frame_clock
  dpi_ = rt_.dpi();
  clock_.bind([this](const RectF&) { draw(); });
  set_frame_clock(&clock_);
  RECT rc;
  GetClientRect(hwnd_, &rc);
  on_client_resized(rc.right - rc.left, rc.bottom - rc.top);
  SetTimer(hwnd_, detail::kTimerFrame, 33, nullptr);  // src: src/pages/_layout.tsx:1
  SetTimer(hwnd_, detail::kTimerIdle, 1000, nullptr);  // src: src/providers/window/window-provider.tsx:1
  SetTimer(hwnd_, detail::kTimerTunGuard, 5000, nullptr);  // src: src/hooks/use-system-state.ts:65
  SetTimer(hwnd_, detail::kTimerDelayCachePrune, 300000, nullptr);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::disconnect_ws() {
  // BUG-325 / 轻量模式静默：断开全部 WS 并暂停后台自动重连（500ms 有界超时）
  if (ws_) ws_->pause_all(500);
}

void MainWindow::reconnect_ws() {
  // 退出轻量模式：解除暂停并即时唤醒全通道重连
  if (ws_) ws_->resume_all();
}

void MainWindow::save_geometry(bool immediately) {
  if (immediately && settings_drawer_) settings_drawer_->flush_pending_skin_prefs();
  persist_geometry(immediately);
}

void MainWindow::close_to_lightweight() {
  CWLOG_INFO("close -> Lifecycle::entry_lightweight_mode(false)");
  app::Lifecycle::instance().entry_lightweight_mode(false);
}

void MainWindow::toggle_large() {
  RECT rc;
  GetWindowRect(hwnd_, &rc);  // outer==inner（NCCALCSIZE 去边框）
  // 【DPI-fix】GetWindowRect 为物理像素；large_/落盘统一逻辑像素口径
  const float s = dpi_scale();
  double lw = static_cast<double>(rc.right - rc.left) / s;
  double lh = static_cast<double>(rc.bottom - rc.top) / s;
  if (!large_.is_large()) {
    // 进入：保存当前尺寸到盘（← localStorage last-non-large-size）
    LargeModeController::save(
        clashwin::util::dirs::app_home_dir() + L"\\last_non_large_size.json",
        lw, lh);
    last_non_large_restore_.reset();
    large_.on_resized(lw, lh);
    large_.enter();
    SetWindowPos(hwnd_, nullptr, 0, 0, static_cast<int>(kMaxWindowW * s + 0.5),
                 static_cast<int>(kMaxWindowH * s + 0.5),
                 SWP_NOMOVE | SWP_NOZORDER);
    // ← ensureWindowInScreen：右下角超界则收回到工作区
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    RECT now_rc;
    GetWindowRect(hwnd_, &now_rc);
    int nx = now_rc.left, ny = now_rc.top;
    if (now_rc.right > wa.right) nx = wa.right - (now_rc.right - now_rc.left);
    if (now_rc.bottom > wa.bottom) ny = wa.bottom - (now_rc.bottom - now_rc.top);
    if (nx < wa.left) nx = wa.left;
    if (ny < wa.top) ny = wa.top;
    if (nx != now_rc.left || ny != now_rc.top)
      SetWindowPos(hwnd_, nullptr, nx, ny, 0, 0,
                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    bp_.set_large(true);
  } else {
    // 退出：恢复记忆尺寸
    double w = last_non_large_restore_ ? last_non_large_restore_->first
                                       : large_.last_w();
    double h = last_non_large_restore_ ? last_non_large_restore_->second
                                       : large_.last_h();
    large_.exit();
    SetWindowPos(hwnd_, nullptr, 0, 0, static_cast<int>(w * s + 0.5),
                 static_cast<int>(h * s + 0.5), SWP_NOMOVE | SWP_NOZORDER);
    bp_.set_large(false);
  }
  update_conn_ws_state();
  stealth_.feed_activity(detail::now_ms());
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------- 几何 ----------
void MainWindow::arrange_modal_overlays(float lw, float lh) {
  // ← src/pages/_layout.tsx: 全客户区覆盖弹窗几何同步
  const RectF r{0.0f, 0.0f, lw, lh};
  if (log_viewer_) log_viewer_->layout_tree(r);
  if (add_url_) add_url_->layout_tree(r);
  if (client_update_) client_update_->layout_tree(r);
  if (core_update_) core_update_->layout_tree(r);
  if (first_run_) first_run_->layout_tree(r);
}

void MainWindow::on_client_resized(int cw, int ch, bool persist) {
  client_w_ = cw;
  client_h_ = ch;
  float s = dpi_scale();
  double lw = static_cast<double>(cw) / s;
  double lh = static_cast<double>(ch) / s;
  large_.on_resized(lw, lh);
  bp_.set_physical_size(cw, ch, s);
  bp_.set_large(large_.is_large());
  stealth_.set_mini(bp_.is_mini());
  if (title_bar_) title_bar_->set_large_mode(bp_.is_large());
  if (scene_) {
    scene_->apply_state(bp_.state(), bp_.state());
    scene_->set_narrow(bp_.is_narrow());
  }
  if (traffic_pane_) {
    traffic_pane_->set_mini(bp_.is_mini());
    traffic_pane_->set_narrow(bp_.is_narrow());
  }
  if (settings_drawer_ && settings_drawer_->theme_card()) {
    // src: src/pages/_layout/components/settings-drawer.tsx:420-422 @media (max-height: 830px)
    const bool show_skin = lh > 830.0;
    for (auto* b : settings_drawer_->theme_card()->skin_buttons_)
      b->set_visible(show_skin);
  }
  // src: _layout.tsx:224-228「if (drawerOpen && isMiniStatus) setDrawerOpen(false)」
  if (bp_.is_mini() && settings_drawer_ && settings_drawer_->is_open())
    settings_drawer_->set_open(false);
  // 抽屉只覆盖上栏（Mini .layout-content：下栏流量条固定高，不随抽屉消失）
  if (content_)
    content_->bottom_inset = static_cast<float>(
        bp_.is_narrow() ? kTrafficPaneMinimal : kTrafficPaneWide);
  // 【LAYOUTFIX】widget 树为逻辑 DIP：传 lw/lh 而非物理 cw/ch
  root_.layout_tree({0, 0, static_cast<float>(lw), static_cast<float>(lh)});
  arrange_modal_overlays(static_cast<float>(lw), static_cast<float>(lh));
  clock_.invalidate({0, 0, static_cast<float>(lw), static_cast<float>(lh)});
  update_conn_ws_state();
  if (persist) persist_geometry();
}

void MainWindow::persist_geometry(bool immediately) {
  if (!restore_done_) return;  // 【DPIFIX】恢复完成前不落盘（防 dpi 未换挡污染）
  if (!immediately && in_sizemove_) {
    pending_persist_geometry_ = true;
    return;
  }
  if (!store_) {
    store_ = std::make_unique<WindowStateStore>(
        clashwin::util::dirs::window_state_path());
  }
  if (client_w_ <= 0 || client_h_ <= 0) return;
  float s = dpi_scale();
  // 【DPIARCH】内部数值必须整数：÷s 后四舍五入取整
  double w = std::round(static_cast<double>(client_w_) / s);
  double h = std::round(static_cast<double>(client_h_) / s);
  RECT wr;
  GetWindowRect(hwnd_, &wr);
  double x = std::round(static_cast<double>(wr.left) / s);
  double y = std::round(static_cast<double>(wr.top) / s);
  if (immediately)
    store_->save_immediately(w, h, x, y);
  else
    store_->on_geometry_change(w, h, x, y);  // 1s 节流+尾随补写+版本防乱序
}

// ---------- 渲染 ----------
void MainWindow::pump_frame() { clock_.flush(); }

void MainWindow::draw() {
  if (rt_.is_lost()) {
    if (!rt_.recreate()) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    on_client_resized(rc.right - rc.left, rc.bottom - rc.top);
  }
  if (rt_.pixel_width() == 0) return;
  rt_.begin_frame();
  // src: assets/styles/layout.scss:8 「background: var(--theme-bg) !important」
  rt_.clear(theme::ThemeService::instance().vars().theme_bg);
  // src: assets/styles/layout.scss:12-28 .layout::before 极光三径向层
  {
    // F-D03：极光按皮肤分支（src: layout.scss .layout::before 与各皮肤覆盖块）
    const theme::SkinId skin = theme::ThemeService::instance().skin();
    if (skin == theme::SkinId::ModernFlat || skin == theme::SkinId::Monochrome ||
        skin == theme::SkinId::Original) {
      // src: layout.scss:331,420,445 modern-flat/monochrome/original 关闭极光
    } else {
      const float w = rt_.size().w, h = rt_.size().h;
      const float vf = theme::ThemeService::instance().vibrancy_factor();
      const float df = theme::ThemeService::instance().depth_factor();
      const RectF big{-w * 0.5f, -h * 0.5f, w * 2.0f, h * 2.0f};
      // circle farthest-corner 半径 = 1.4 × 对角线
      const float rad = 1.4f * std::sqrt(w * w + h * h);
      const float period = (skin == theme::SkinId::Cyberpunk)
                               ? 25000.0f / (vf + 0.001f)  // 25s/(vibrancy) 单向
                               : 25000.0f;                   // 25s 单向（50s 往返）
      const double u = std::fmod(static_cast<double>(detail::now_ms()) / period, 2.0);
      const double s = u < 1.0 ? u : 2.0 - u;
      const float k = static_cast<float>(s * s * (3.0 - 2.0 * s));  // 逼近 ease 曲线
      const float ca = std::cos(k * 3.14159265f), sa = std::sin(k * 3.14159265f);
      const float scale = 1.0f + 0.1f * k;
      const float shift = 0.1f * k;  // translate 相对 200% 盒
      // 盒中心变换：scale 不改变比例（仅半径乘以 scale）→ rotate → translate
      auto center = [&](float cx, float cy, float* ox, float* oy) {
        const float dx = cx - 0.5f, dy = cy - 0.5f;
        *ox = 0.5f + dx * ca - dy * sa - shift;
        *oy = 0.5f + dx * sa + dy * ca + shift;
      };
      const float rr = rad * scale;
      GradientStop g1[2], g2[2], g3[2];
      float x1, y1, x2, y2, x3, y3;
      if (skin == theme::SkinId::Cyberpunk) {
        // 霓虹三色（src: layout.scss:375-377）
        g1[0] = {0.0f, Color4f::hex(0x00F0FF, 0.15f * df)};
        g1[1] = {0.40f, Color4f::hex(0x00F0FF, 0.0f)};
        g2[0] = {0.0f, Color4f::hex(0xFF0055, 0.12f * df)};
        g2[1] = {0.40f, Color4f::hex(0xFF0055, 0.0f)};
        g3[0] = {0.0f, Color4f::hex(0x39FF14, 0.10f * df)};
        g3[1] = {0.35f, Color4f::hex(0x39FF14, 0.0f)};
        center(0.30f, 0.30f, &x1, &y1);
        center(0.70f, 0.70f, &x2, &y2);
        center(0.80f, 0.20f, &x3, &y3);
      } else {
        // 默认三色极光（src: layout.scss:20-22；α×vibrancy）
        const Color4f primary = theme::ThemeService::instance().primary();
        g1[0] = {0.0f, Color4f{primary.r, primary.g, primary.b, 0.18f * vf}};
        g1[1] = {0.40f, Color4f{primary.r, primary.g, primary.b, 0.0f}};
        g2[0] = {0.0f, Color4f::hex(0x0084FF, 0.15f * vf)};
        g2[1] = {0.40f, Color4f::hex(0x0084FF, 0.0f)};
        g3[0] = {0.0f, Color4f::hex(0xD4AF37, 0.12f * vf)};
        g3[1] = {0.35f, Color4f::hex(0xD4AF37, 0.0f)};
        center(0.30f, 0.30f, &x1, &y1);
        center(0.70f, 0.70f, &x2, &y2);
        center(0.80f, 0.20f, &x3, &y3);
      }
      rt_.fill_round_rect_radial(big, 0.0f, g1, 2, x1, y1, rr, rr);
      rt_.fill_round_rect_radial(big, 0.0f, g2, 2, x2, y2, rr, rr);
      rt_.fill_round_rect_radial(big, 0.0f, g3, 2, x3, y3, rr, rr);
      // 动画续帧（极慢速位移，10fps 足以视觉平滑；隐藏时停帧）
      if (hwnd_ && IsWindowVisible(hwnd_)) {
        const int64_t nm = detail::now_ms();
        if (nm - aurora_tick_ms_ >= 100) {
          aurora_tick_ms_ = nm;
          root_.invalidate();
        }
      }
    }
  }
  root_.paint_all(rt_);
  // src: assets/styles/layout.scss:9  .layout { box-shadow: inset 0 0 0 1px
  //      var(--theme-border); } —— 整窗 1px 内描边（CSS 1px = 1 DIP）
  const float cw = rt_.size().w, ch = rt_.size().h;
  if (cw > 0 && ch > 0)
    rt_.round_rect(RectF{0.5f, 0.5f, cw - 1.0f, ch - 1.0f}, 0,
                   {theme::ThemeService::instance().vars().theme_border, 1.0f},
                   nullptr);
  rt_.end_frame();
}

// ---------- NCHITTEST（8 向 resize + 拖动区） ----------
int MainWindow::hit_test_nc(POINT screen_pt) {
  POINT pt = screen_pt;
  ScreenToClient(hwnd_, &pt);
  RECT rc;
  GetClientRect(hwnd_, &rc);
  // 【DPIARCH】pt/rc 为物理像素，热区常量是 DIP → 边界处 ×s 换算一次
  const float s = dpi_scale();
  // src: resize-handles.tsx:329「if (isLargeMode) return null」→ 大尺寸态无侧边热区
  if (!bp_.is_large()) {
    const auto rb = static_cast<LONG>(detail::kResizeBorder * s);
    const auto cs = static_cast<LONG>(detail::kCornerSize * s);
    bool left = pt.x < rb;
    bool right = pt.x >= rc.right - rb;
    bool top = pt.y < rb;
    bool bottom = pt.y >= rc.bottom - rb;
    // 角落 20px 热区（src: resize-handles.tsx:58-96 HANDLE_SIZE*2 = 20x20 正方形）
    if (pt.x < cs && pt.y < cs) return HTTOPLEFT;
    if (pt.x >= rc.right - cs && pt.y < cs) return HTTOPRIGHT;
    if (pt.x < cs && pt.y >= rc.bottom - cs) return HTBOTTOMLEFT;
    if (pt.x >= rc.right - cs && pt.y >= rc.bottom - cs) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
  }

  // 标题栏拖动区：y < 30×s 且不在三键区
  if (title_bar_ && title_bar_->visible() && pt.y < kTitleBarH * s) {
    PointF p{static_cast<float>(pt.x) / s, static_cast<float>(pt.y) / s};
    for (Widget* b : {static_cast<Widget*>(title_bar_->btn_minimize()),
                      static_cast<Widget*>(title_bar_->btn_large()),
                      static_cast<Widget*>(title_bar_->btn_close())}) {
      if (b && b->visible() && b->global_bounds().contains(p.x, p.y))
        return HTCLIENT;
    }
    return HTCAPTION;
  }
  // stealth 隐藏：可交互控件走 HTCLIENT，背景走 HTCAPTION 整窗拖动
  if (stealth_.hidden()) {
    PointF p{static_cast<float>(pt.x) / s, static_cast<float>(pt.y) / s};
    Widget* hit = root_.hit_test(p);
    if (hit && hit != &root_ && hit != scene_) {
      return HTCLIENT;
    }
    return HTCAPTION;
  }
  return HTCLIENT;
}

// ---------- 消息 ----------
LRESULT CALLBACK MainWindow::wndproc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp) {
  MainWindow* self = nullptr;
  if (msg == WM_NCCREATE) {
    self = static_cast<MainWindow*>(
        reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    if (self) {
      self->hwnd_ = hwnd;
      self->dpi_ = static_cast<float>(GetDpiForWindow(hwnd));
    }
  } else {
    self =
        reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  LRESULT r = self ? self->handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
  if (msg == WM_NCCREATE && !self) return DefWindowProcW(hwnd, msg, wp, lp);
  return r;
}

LRESULT MainWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NCCALCSIZE:
      // 去非客户区：客户区 = 窗口矩形（无框；保留 THICKFRAME 的系统 resize）
      return 0;

    case WM_NCPAINT:
    case 0x00AE:  // 豁免: Mini 无对应物 — WM_NCUAHDRAWCAPTION 抑制非客户区边框（出处: 无）
    case 0x00AF:  // 豁免: Mini 无对应物 — WM_NCUAHDRAWFRAME 抑制非客户区边框（出处: 无）
      // D11: 阻止 DefWindowProcW 绘制 GDI 非客户边框
      return 0;

    case WM_NCACTIVATE:
      // D11: 阻止激活/非激活状态切换时 DefWindowProcW 绘制白色边框
      return TRUE;

    case WM_NCHITTEST:
      return hit_test_nc({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});

    case WM_GETMINMAXINFO: {
      // ← min_inner_size(271,126) / max_inner_size(640,860)；outer==inner
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
      float s = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
      mmi->ptMinTrackSize.x = static_cast<LONG>(kMinWindowW * s + 0.5f);
      mmi->ptMinTrackSize.y = static_cast<LONG>(kMinWindowH * s + 0.5f);
      mmi->ptMaxTrackSize.x = static_cast<LONG>(kMaxWindowW * s + 0.5f);
      mmi->ptMaxTrackSize.y = static_cast<LONG>(kMaxWindowH * s + 0.5f);
      return 0;
    }

    case WM_ENTERSIZEMOVE:
      in_sizemove_ = true;
      pending_persist_geometry_ = false;
      break;

    case WM_EXITSIZEMOVE:
      in_sizemove_ = false;
      if (pending_persist_geometry_) {
        pending_persist_geometry_ = false;
        persist_geometry(false);
      }
      break;

    case WM_SIZING: {
      // src: src-tauri/src/utils/resolve/window.rs:351 上缘缩放最小高度防下移（top=bottom-minH）
      auto* rc = reinterpret_cast<RECT*>(lp);
      float s = dpi_scale();
      int min_h_px = static_cast<int>(kMinWindowH * s + 0.5);
      if (clamp_sizing_top(static_cast<uint32_t>(wp), *rc, min_h_px))
        return TRUE;
      break;
    }

    case WM_SIZE: {
      if (wp == SIZE_MINIMIZED) {
        if (settings_drawer_) settings_drawer_->flush_pending_skin_prefs();
        if (tooltip_host_) tooltip_host_->leave();
        clear_mouse_hover();
        if (traffic_pane_) traffic_pane_->set_enabled(false);
        update_conn_ws_state();
        break;
      }
      if (traffic_pane_) traffic_pane_->set_enabled(true);
      const int cw = LOWORD(lp), ch = HIWORD(lp);
      rt_.resize(cw, ch);
      on_client_resized(cw, ch);
      InvalidateRect(hwnd_, nullptr, FALSE);
      stealth_.feed_activity(detail::now_ms());  // 几何变化 = 交互（续期）
      return 0;
    }

    case WM_MOVE:
      if (tooltip_host_) tooltip_host_->leave();
      persist_geometry();
      stealth_.feed_activity(detail::now_ms());
      break;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd_, &ps);
      draw();
      EndPaint(hwnd_, &ps);
      return 0;
    }

    case WM_ERASEBKGND:
      return 1;  // D2D 全域 Clear，跳过背景擦除防闪烁

    case WM_TIMER:
      if (wp == detail::kTimerFrame) {
        pump_frame();
        if (tooltip_host_) tooltip_host_->tick_now(detail::now_ms());
      } else if (wp == detail::kTimerIdle) {
        const int64_t now = detail::now_ms();
        stealth_.tick(now);  // 到点隐藏（自续期 = 下一秒再 tick）
        if (conn_gc_deadline_ms_ != 0 && now >= conn_gc_deadline_ms_) {
          conn_gc_deadline_ms_ = 0;
          if (conn_data_) conn_data_->clear_all();
        }
      } else if (wp == detail::kTimerNodeRetry) {
        KillTimer(hwnd_, detail::kTimerNodeRetry);
        reload_nodes();
      } else if (wp == detail::kTimerTunGuard) {
        tun_guard_tick();
      } else if (wp == detail::kTimerFirstRun) {
        KillTimer(hwnd_, detail::kTimerFirstRun);
        if (first_run_) {
          first_run_->open();
          // src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:218-226
          SetTimer(hwnd_, detail::kTimerFirstReady, 250, nullptr);  // src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:34
        }
      } else if (wp == detail::kTimerFirstReady) {
        if (first_run_ && first_run_->is_open()) {
          first_run_->tick(detail::now_ms());
        } else {
          KillTimer(hwnd_, detail::kTimerFirstReady);
        }
      } else if (wp == detail::kTimerSkinPersist) {
        KillTimer(hwnd_, detail::kTimerSkinPersist);
        if (settings_drawer_) settings_drawer_->flush_pending_skin_prefs();
      } else if (wp == detail::kTimerFilterAutoTest) {
        KillTimer(hwnd_, detail::kTimerFilterAutoTest);
        if (node_list_) node_list_->fire_filter_auto_test_timer();
      } else if (wp == detail::kTimerDelayCachePrune) {
        if (node_data_) node_data_->clean_expired_delays();
      }
      return 0;

    case WM_ACTIVATE:
      // BUG-361：记录窗口失焦时刻供托盘左键点击 450ms 前台收起判定（src: src-tauri/src/core/tray/mod.rs:253）
      if (LOWORD(wp) == WA_INACTIVE) {
        last_blur_ms_.store(GetTickCount64(), std::memory_order_release);
        if (settings_drawer_) settings_drawer_->flush_pending_skin_prefs();
      } else {
        last_blur_ms_.store(0, std::memory_order_release);
      }
      break;

    case WM_SETFOCUS:
      stealth_.feed_activity(detail::now_ms());
      update_ime_position();
      return 0;

    case WM_KILLFOCUS:
      stealth_.feed_activity(detail::now_ms());
      if (tooltip_host_) tooltip_host_->leave();
      DestroyCaret();
      return 0;

    case WM_GETOBJECT: {
      if (uia_) {
        LRESULT r = uia_->handle_wm_getobject(hwnd_, wp, lp);
        if (r != 0) return r;
      }
      break;
    }

    case WM_GETICON: {
      if (wp == ICON_BIG || wp == 2 /* ICON_BIG */) return reinterpret_cast<LRESULT>(h_icon_big_);
      return reinterpret_cast<LRESULT>(h_icon_sm_);
    }

    case WM_SETCURSOR: {
      if (LOWORD(lp) == HTCLIENT) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd_, &pt);
        float s = dpi_scale();
        PointF local_pt{static_cast<float>(pt.x) / s, static_cast<float>(pt.y) / s};
        Widget* hit = root_.hit_test(local_pt);
        while (hit) {
          PointF widget_pt = hit->to_local(local_pt);
          HCURSOR hc = hit->cursor(widget_pt);
          if (hc) {
            SetCursor(hc);
            return TRUE;
          }
          hit = hit->parent();
        }
      }
      break;
    }

    case WM_NCMOUSEMOVE:
      clear_mouse_hover();
      if (tooltip_host_) tooltip_host_->leave();
      break;

    case WM_MOUSELEAVE:
      mouse_tracking_ = false;
      clear_mouse_hover();
      if (tooltip_host_) tooltip_host_->leave();
      return 0;

    case WM_IME_SETCONTEXT:
    case WM_IME_COMPOSITION:
      update_ime_position();
      break;
    case WM_IME_STARTCOMPOSITION:
      ime_composing_ = true;  // G3: 进入组词
      update_ime_position();
      break;
    case WM_IME_ENDCOMPOSITION:
      ime_composing_ = false;  // G3: 组词结束
      break;

    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP: {
      stealth_.feed_activity(detail::now_ms());
      float s = dpi_scale();
      MouseEvent e;
      e.pos = {static_cast<float>(GET_X_LPARAM(lp)) / s,
               static_cast<float>(GET_Y_LPARAM(lp)) / s};  // 物理→逻辑
      e.buttons = ((wp & MK_LBUTTON) ? kMouseLeft : 0) |
                  ((wp & MK_RBUTTON) ? kMouseRight : 0) |
                  ((wp & MK_MBUTTON) ? kMouseMiddle : 0);
      bool down = false, up = false;
      switch (msg) {
        case WM_MOUSEMOVE: e.type = MouseEventType::Move; break;
        case WM_LBUTTONDOWN:
          e.type = MouseEventType::Down; e.buttons |= kMouseLeft; down = true; break;
        case WM_RBUTTONDOWN:
          e.type = MouseEventType::Down; e.buttons |= kMouseRight; down = true; break;
        case WM_MBUTTONDOWN:
          e.type = MouseEventType::Down; e.buttons |= kMouseMiddle; down = true; break;
        default: e.type = MouseEventType::Up; up = true; break;  // *BUTTONUP
      }
      if (msg == WM_MOUSEMOVE && !mouse_tracking_) {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd_;
        if (TrackMouseEvent(&tme)) mouse_tracking_ = true;
      }
      if (down) {
        if (tooltip_host_) tooltip_host_->leave();
        SetCapture(hwnd_);
      } else if (up && GetCapture() == hwnd_) {
        ReleaseCapture();
      }
      Widget* hit_w = route_mouse_event(&root_, e);
      if (down) update_ime_position();
      if (e.type == MouseEventType::Down || e.type == MouseEventType::Up) {
        std::string d = std::string("[MOUSEDBG] ") +
                        (e.type == MouseEventType::Down ? "down" : "up") +
                        " pos=(" + std::to_string(e.pos.x) + "," +
                        std::to_string(e.pos.y) + ")";
        if (hit_w) {
          const RectF gb = hit_w->global_bounds();
          d += " hit=(" + std::to_string(gb.x) + "," + std::to_string(gb.y) +
               "," + std::to_string(gb.w) + "," + std::to_string(gb.h) + ")";
        } else {
          d += " hit=NONE";
        }
        CWLOG_DEBUG(d);
      }
      // 悬停提示：命中控件（或父链）→ 100ms 后显示（src: node_modules/@mui/material/Tooltip/Tooltip.js:331）
      if (tooltip_host_ && e.type == MouseEventType::Move) {
        tooltip_host_->tick_now(detail::now_ms());
        tooltip_host_->hover(hovered_widget(), e.pos);
      }
      return 0;
    }

    case WM_MOUSEWHEEL: {
      stealth_.feed_activity(detail::now_ms());
      if (tooltip_host_) tooltip_host_->leave();
      float s = dpi_scale();
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};  // wheel lp = 屏幕坐标
      ScreenToClient(hwnd_, &pt);
      MouseEvent e;
      e.type = MouseEventType::Wheel;
      e.pos = {static_cast<float>(pt.x) / s, static_cast<float>(pt.y) / s};
      e.wheel_delta = static_cast<int>(GET_WHEEL_DELTA_WPARAM(wp));
      route_mouse_event(&root_, e);
      return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP: {
      stealth_.feed_activity(detail::now_ms());
      KeyEvent e;
      e.type = (msg == WM_KEYUP) ? KeyEventType::Up : KeyEventType::Down;
      e.key = static_cast<uint32_t>(wp);
      e.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      e.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      e.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
      if (ime_composing_ && wp == VK_RETURN) {
        update_ime_position();
        return 0;
      }
      if (first_run_ && first_run_->is_open() && msg == WM_KEYDOWN) {
        if (wp == VK_ESCAPE) {
          if (!first_run_->is_importing()) first_run_->finish();
          return 0;
        }
        if (wp == VK_RETURN && first_run_->is_ready_step() &&
            !first_run_->is_importing()) {
          first_run_->finish();
          return 0;
        }
      }
      if (msg == WM_KEYDOWN) {
        // BUG-355：统一全局快捷键收口（Ctrl+F / F4 / F1）
        // src: src/utils/disable-webview-shortcuts.ts:35-77
        const bool is_repeat = (lp & (1LL << 30)) != 0;  // src: src/utils/disable-webview-shortcuts.ts:38
        switch (classify_global_shortcut(static_cast<uint32_t>(wp), e.ctrl, e.alt,
                                         e.shift, ime_composing_, is_repeat,
                                         is_any_overlay_open(),
                                         is_editable_focused())) {
          case GlobalShortcutAction::FocusFilter:
            if (node_list_) {
              node_list_->focus_filter_input();
              update_ime_position();
              return 0;
            }
            break;
          case GlobalShortcutAction::TestVisibleSubset:
            if (node_list_) {
              const auto intent = util::resolve_batch_check_intent(
                  util::BatchCheckTrigger::F4);
              node_list_->trigger_check_visible(intent.select);
              return 0;
            }
            break;
          case GlobalShortcutAction::TestFullGroup: {
            auto cur = config::Config::instance().profiles().latest_arc()->current;
            if (cur && !cur->empty()) {
              trigger_auto_select_and_refresh(*cur, true);
              return 0;
            }
            break;
          }
          case GlobalShortcutAction::None:
            break;
        }
      }
      bool handled = route_key_event(&root_, e);
      update_ime_position();
      if (handled) return 0;
      break;
    }

    case WM_CHAR: {
      stealth_.feed_activity(detail::now_ms());
      if (ime_composing_ && wp == L'\r') return 0;
      KeyEvent e;
      e.type = KeyEventType::Char;
      e.ch = static_cast<wchar_t>(wp);
      e.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      e.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      e.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
      bool handled = route_key_event(&root_, e);
      update_ime_position();
      if (handled) return 0;
      break;
    }

    case WM_NCLBUTTONDOWN:
      // stealth 隐藏态：记录按下位置（UP 位移<5 → 恢复标题栏）
      if (wp == HTCAPTION && stealth_.hidden()) {
        nc_down_ = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      }
      break;

    case WM_NCLBUTTONUP:
      if (wp == HTCAPTION && stealth_.hidden() && nc_down_) {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        float dx = static_cast<float>(pt.x - nc_down_->x);
        float dy = static_cast<float>(pt.y - nc_down_->y);
        if (dx * dx + dy * dy < detail::kStealthClickSlack * detail::kStealthClickSlack)
          stealth_.restore_chrome(detail::now_ms());
        nc_down_.reset();
      }
      break;

    case WM_DISPLAYCHANGE:
      if (rt_.is_lost() || true) {
        rt_.recreate();
        RECT rc;
        GetClientRect(hwnd_, &rc);
        on_client_resized(rc.right - rc.left, rc.bottom - rc.top);
        draw();
      }
      return 0;

    case WM_DPICHANGED: {
      CWLOG_INFO(std::format(
          "[DPIARCH] DPICHANGED new={} sug={}x{}@{},{}",
          HIWORD(wp),
          reinterpret_cast<const RECT*>(lp)->right -
              reinterpret_cast<const RECT*>(lp)->left,
          reinterpret_cast<const RECT*>(lp)->bottom -
              reinterpret_cast<const RECT*>(lp)->top,
          reinterpret_cast<const RECT*>(lp)->left,
          reinterpret_cast<const RECT*>(lp)->top));
      dpi_ = static_cast<float>(HIWORD(wp));
      rt_.recreate();
      const RECT* sug = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd_, nullptr, sug->left, sug->top,
                   sug->right - sug->left, sug->bottom - sug->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      RECT rc;
      GetClientRect(hwnd_, &rc);
      on_client_resized(rc.right - rc.left, rc.bottom - rc.top);
      return 0;
    }

    case detail::kAppWakeup: {  // 单实例唤醒（← /commands/visible & tray/mod.rs:216）
      bool stay_silent = false;
      std::string lang = "zh";
      try {
        auto v = config::Config::instance().verge().latest_arc();
        auto p = config::Config::instance().profiles().latest_arc();
        if (v && v->language && !v->language->empty()) lang = *v->language;
        if (app::Lifecycle::instance().is_in_lightweight_mode() && v &&
            v->enable_silent_start.value_or(false) && p && p->items) {
          for (const auto& it : *p->items) {
            if (it.itype && (*it.itype == "remote" || *it.itype == "local")) {
              stay_silent = true;
              break;
            }
          }
        }
      } catch (...) {
      }
      if (stay_silent) {
        if (tray_) tray_->notify_silent_running(lang);
      } else {
        app::Lifecycle::instance().exit_lightweight_mode();
      }
      return 0;
    }

    case system::kAppTrayMsg:
      if (tray_) tray_->handle_tray_message(wp, lp);
      return 0;

    case WM_CLOSE:
      // 关闭 = 隐藏 + 轻量模式（非退出；← 02§7）
      close_to_lightweight();
      return 0;

    case WM_QUERYENDSESSION:
      CWLOG_INFO("MainWindow: received WM_QUERYENDSESSION");
      return system::SignalHandler::instance().handle_session_query_end(lp) ? TRUE : FALSE;

    case WM_ENDSESSION:
      CWLOG_INFO(std::format("MainWindow: received WM_ENDSESSION wp={}", wp));
      if (wp != 0) {
        system::SignalHandler::instance().handle_session_end(true, lp);
      }
      return 0;

    case WM_DESTROY: {
      if (settings_drawer_) settings_drawer_->flush_pending_skin_prefs();
      if (node_list_) node_list_->cancel_pending_filter_auto_test();
      if (uia_) uia_->shutdown();
      if (tooltip_host_) tooltip_host_->bind_owner_hwnd(nullptr);
      // 退出序列：立即落盘（跳过节流；← save_window_state_immediately）
      persist_geometry(true);
      set_frame_clock(nullptr);
      // src: src-tauri/src/lib.rs:377-383 RunEvent::ExitRequested 防意外退出铁律（BUG-315）
      if (app::Lifecycle::instance().is_exiting()) {
        PostQuitMessage(0);
      } else {
        CWLOG_WARN("MainWindow: intercepted WM_DESTROY while !is_exiting(), preventing exit");
      }
      return 0;
    }
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

int MainWindow::run() {
  MSG msg;
  for (;;) {
    const BOOL ret = GetMessageW(&msg, nullptr, 0, 0);
    if (ret <= 0) {
      if (ret == 0 && !app::Lifecycle::instance().is_exiting()) {
        // src: src-tauri/src/lib.rs:377-383 RunEvent::ExitRequested 防意外退出铁律（BUG-315）
        CWLOG_WARN("MainWindow::run: intercepted WM_QUIT while !is_exiting(), keeping tray alive");
        continue;
      }
      break;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

}  // namespace clashwin::ui::win
