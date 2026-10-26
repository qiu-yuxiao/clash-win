// 主窗口实现（T11）：无框窗 + 钳制 + Widget 树宿主 + 几何持久化。
// 行为对照见任务报告「窗口行为对照表」。
#include "main_window.h"

#include <windowsx.h>
#include <shellapi.h>  // ShellExecuteW（T17 打开日志目录/帮助）

#include <cmath>
#include <filesystem>

#include "clashwin/config/constants.h"
#include "cmd/command_service.h"
#include "events/event_names.h"
#include "theme/theme.h"
#include "ui/ui_postbox.h"
#include "mihomo/ws_manager.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"
#include <format>

namespace clashwin::ui::win {

namespace fs = std::filesystem;
namespace utf = clashwin::utf;
using config::window_state::WindowStateStore;
using config::window_state::SavedWindowState;

namespace {
const wchar_t kWndClass[] = L"ClashWinMainWindow";
// 单实例唤醒消息（← T01 app.h consts::kAppWakeup；常量收编到本文件）
constexpr UINT kAppWakeup = WM_APP + 1;
// 定时器 id（WM_TIMER）
constexpr UINT_PTR kTimerFrame = 1;   // 33ms 帧泵（§5.4 30fps 合帧）
constexpr UINT_PTR kTimerIdle = 2;    // 1s stealth 空闲扫描（自续期）
constexpr int kResizeBorder = 10;     // ← resize-handles.tsx HANDLE_SIZE
constexpr int kCornerSize = 20;       // ← HANDLE_SIZE*2（角落热区）
// 关闭键拖动判定（原版 mouseup 位移 <5 恢复标题栏）
constexpr float kStealthClickSlack = 5.0f;

int64_t now_ms() { return static_cast<int64_t>(GetTickCount64()); }
}  // namespace

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
bool MainWindow::create(bool show_immediately) {
  // exe 目录（几何文件落此处；与 T03 app_home_dir 口径一致）
  wchar_t exe[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  app_dir_ = std::wstring(exe);
  size_t pos = app_dir_.find_last_of(L"\\/");
  if (pos != std::wstring::npos) app_dir_.resize(pos);

  if (!init_window_class()) return false;
  if (!create_hwnd()) return false;
  if (!init_render()) return false;
  build_widget_tree();

  // 几何恢复（window_state.json：钳制 + 脱屏居中守卫，← restore_window_state）
  apply_geometry_restore();

  // 断点 → 标题栏图标/场景挂卸
  bp_.subscribe([this](WinState prev, WinState cur) {
    title_bar_->set_large_mode(bp_.is_large());
    scene_->apply_state(prev, cur);
    scene_->set_narrow(bp_.is_narrow());
    // T14：Mini → 流量条占满上栏空间（census 极小窗布局）
    if (traffic_pane_) traffic_pane_->set_mini(bp_.state() == WinState::Mini);
    // T17：皮肤条高≤830 隐藏（census 口径；窗口客户区逻辑高）
    if (settings_drawer_ && hwnd_) {
      RECT rc{};
      GetClientRect(hwnd_, &rc);
      UINT dpi = GetDpiForWindow(hwnd_);
      double logical_h =
          dpi ? (rc.bottom - rc.top) * 96.0 / static_cast<double>(dpi) : 0;
      bool show_skin = logical_h > 830.0;
      for (auto* b : settings_drawer_->theme_card()->skin_buttons_)
        b->set_visible(show_skin);
    }
    // T14：连接面板仅 Large 挂载（census：宽≤285 不挂载）
    if (cur == WinState::Large && !conn_view_mounted_ && scene_->list_slot()) {
      auto cv = std::make_unique<scene::ConnectionsView>();
      conn_view_ = cv.get();
      conn_view_->bind(conn_data_.get());
      conn_view_->close_connection = [this](const std::string& id) {
        auto r = cmd::mihomo();
        return r.is_ok() &&
               static_cast<bool>(r.value()->close_connection(id).is_ok());
      };
      conn_view_->on_clear_closed = [this] { conn_data_->clear_closed(); };
      scene_->attach_node_list(std::move(cv));
      conn_view_mounted_ = true;
    }
  });
  stealth_.subscribe([this](bool hidden) {
    title_bar_->set_visible(!hidden);  // 隐藏时整栏不挂载（内容顶满）
    clock_.invalidate({0, 0, static_cast<float>(client_w_),
                       static_cast<float>(client_h_)});
  });

  RECT rc;
  GetClientRect(hwnd_, &rc);
  on_client_resized(rc.right - rc.left, rc.bottom - rc.top);

  build_scene();

  SetTimer(hwnd_, kTimerFrame, FrameClock::kFrameIntervalMs, nullptr);
  SetTimer(hwnd_, kTimerIdle, 1000, nullptr);
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
  wc.lpszClassName = kWndClass;
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
  // 默认几何（restore 会覆盖）
  int x = (GetSystemMetrics(SM_CXSCREEN) -
           static_cast<int>(kDefaultWindowW)) / 2;
  int y = (GetSystemMetrics(SM_CYSCREEN) -
           static_cast<int>(kDefaultWindowH)) / 2;
  int w = static_cast<int>(kDefaultWindowW);
  int h = static_cast<int>(kDefaultWindowH);
  // ← window.rs：加载完成才 show → 创建时不带 WS_VISIBLE，show_and_activate 再显
  hwnd_ = CreateWindowExW(
      0, kWndClass, L"Clash Mini",
      WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_SYSMENU,
      x, y, w, h, nullptr, nullptr, GetModuleHandleW(nullptr), this);
  if (!hwnd_) {
    CWLOG_ERROR(std::format("MainWindow: CreateWindowExW 失败 0x{:08X}",
                            static_cast<unsigned>(GetLastError())));
  }
  return hwnd_ != nullptr;
}

bool MainWindow::init_render() {
  if (!rt_.create_for_hwnd(hwnd_)) return false;
  dpi_ = rt_.dpi();
  clock_.bind([this](const RectF&) { draw(); });
  set_frame_clock(&clock_);
  return true;
}

void MainWindow::build_widget_tree() {
  // root = Stack{ titlebar(30), scene(撑满), drawer(全屏覆盖) }
  auto tb = std::make_unique<TitleBar>(this);
  title_bar_ = tb.get();
  title_bar_->set_version(L"3.0.10");  // ← tauri.conf.json version 3.0.10
  root_.add_child(std::move(tb));
  auto scene = std::make_unique<MainScene>();
  scene_ = scene.get();
  scene_->flex_weight = 1;
  root_.add_child(std::move(scene));
  // T17：设置抽屉（全屏覆盖层；Stack 铺满语义，最后挂=绘制/命中最上层）
  auto drawer = std::make_unique<scene::SettingsDrawer>();
  settings_drawer_ = drawer.get();
  drawer_mounted_ = true;
  root_.add_child(std::move(drawer));
}

void MainWindow::build_scene() {
  // T13：场景装配——数据（production hooks）→ 快捷条/节点列表挂载 → 事件订阅
  node_data_ = std::make_unique<scene::NodeData>(scene::NodeData::production_hooks());

  auto qb = std::make_unique<scene::QuickBar>();
  quick_bar_ = qb.get();
  qb->on_test_group = [this] { node_data_->test_group(); };
  scene_->attach_quick_bar(std::move(qb));

  auto nl = std::make_unique<scene::NodeList>();
  node_list_ = nl.get();
  node_list_->bind(node_data_.get());
  // 点击节点 → select_node_for_group（失败回弹：NodeData::select 内处理）
  node_list_->select_node = [this](const std::string& node) {
    return node_data_->select(node);
  };
  scene_->attach_node_list(std::move(nl));

  // 数据初始加载（内核未就绪静默；刷新事件再拉）
  node_data_->load();
  quick_bar_->set_node_text(
      utf::utf8_to_utf16(node_data_->now().empty() ? "-" : node_data_->now()));

  // 延迟结果事件：局部刷新（← backend-delay-results {group, results}）
  UiPostbox::instance().subscribe_ui(
      events::names::kBackendDelayResults, [this](const nlohmann::json& p) {
        node_data_->apply_delay_results(p);
        quick_bar_->set_node_text(utf::utf8_to_utf16(
            node_data_->now().empty() ? "-" : node_data_->now()));
      });
  // 配置/代理刷新事件：全量重载
  UiPostbox::instance().subscribe_ui(
      events::names::kRefreshClash, [this](const nlohmann::json&) {
        node_data_->load();
        quick_bar_->set_node_text(utf::utf8_to_utf16(
            node_data_->now().empty() ? "-" : node_data_->now()));
      });

  // T14：流量面板挂 traffic_slot；连接面板 Large 模式挂载（census：宽≤285
  // 时不挂载）
  auto tp = std::make_unique<scene::TrafficPane>();
  traffic_pane_ = tp.get();
  traffic_pane_->set_now_ms([] {
    return static_cast<int64_t>(GetTickCount64());
  });
  if (auto* slot = scene_->traffic_slot()) {
    slot->add_child(std::move(tp));  // 所有权移交场景树
  } else {
    traffic_pane_holder_ = std::move(tp);  // 无 slot 兜底持有
    traffic_pane_->set_visible(false);
  }

  conn_data_ = std::make_unique<scene::ConnectionData>(300);
  conn_data_->on_resync = [] {
    CWLOG_WARN("conn resync：等待下一个 snapshot 帧（WS 自动重连）");
  };

  build_ws_pipes();

  // ---------- T17：设置抽屉命令链接线 ----------
  if (settings_drawer_) {
    // patch_verge 键 → patch_verge_config；clash 键 → patch_clash_config
    settings_drawer_->on_patch_verge =
        [this](const std::string& k, const nlohmann::json& v) {
          auto& svc = cmd::CommandService::instance();
          if (k == "clash_allow_lan") {
            svc.invoke("patch_clash_config",
                       nlohmann::json::object({{"allow-lan", v}}));
            return;
          }
          if (k == "clash_ipv6") {
            svc.invoke("patch_clash_config", nlohmann::json::object({{"ipv6", v}}));
            return;
          }
          svc.invoke("patch_verge_config", nlohmann::json::object({{k, v}}));
        };
    settings_drawer_->on_patch_clash_mode = [](const std::string& mode) {
      cmd::CommandService::instance().invoke(
          "patch_clash_mode", nlohmann::json::object({{"mode", mode}}));
    };
    settings_drawer_->on_import_profile =
        [](const std::string& url, const std::string& name) {
          nlohmann::json args = nlohmann::json::object({{"url", url}});
          if (!name.empty()) args["name"] = name;
          return cmd::CommandService::instance()
              .invoke("import_profile", args)
              .is_ok();
        };
    settings_drawer_->on_open_logs = [this] {
      // ← 原版 open logs：打开日志目录（不存在则静默）
      std::wstring dir = app_dir_ + L"\\logs";
      if (fs::exists(dir))
        ShellExecuteW(hwnd_, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    };
    settings_drawer_->on_open_help = [] {
      // ← help-menu-button.tsx:131（帮助指南 Wiki）
      ShellExecuteW(nullptr, L"open", L"https://github.com/qiu-yuxiao/clash-mini/wiki",
                    nullptr, nullptr, SW_SHOWNORMAL);
    };
    // 齿轮 → 打开抽屉
    title_bar_->on_settings = [this] { settings_drawer_->set_open(true); };
    settings_drawer_->wire_callbacks();
    // 恢复上次皮肤/因子（clashwin-skin.json）
    settings_drawer_->restore_prefs();
  }
}

void MainWindow::build_ws_pipes() {
  // T14：WS 订阅 → UiPostbox 转投 UI 线程（复用 T13 范式）
  ws_ = std::make_unique<mihomo::WsManager>();
  ws_->configure(clashwin::util::dirs::ipc_path(),
                 clashwin::config::consts::kDefaultSecret);
  ws_sub_traffic_ = ws_->subscribe_traffic([this](const std::string& payload) {
    UiPostbox::instance().post([this, payload] {
      if (traffic_pane_) traffic_pane_->on_traffic_payload(payload);
    });
  });
  ws_sub_conn_ = ws_->subscribe_connections(
      [this](const std::string& payload) {
        UiPostbox::instance().post([this, payload] {
          if (conn_data_) conn_data_->consume(payload);
        });
      });
}

void MainWindow::apply_geometry_restore() {
  auto st = config::window_state::restore(app_dir_ + L"\\window_state.json");
  if (!st) return;  // 无记录 → 默认 271×680 居中
  int w = static_cast<int>(st->width + 0.5);
  int h = static_cast<int>(st->height + 0.5);
  int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
  int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
  if (st->x && st->y) {
    x = static_cast<int>(*st->x + 0.5);
    y = static_cast<int>(*st->y + 0.5);
  }
  SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
  // ← init：恢复到 640×860（大尺寸模式退出时关机）→ 标记 large + 读盘记忆尺寸
  double lw = static_cast<double>(w), lh = static_cast<double>(h);
  if (Breakpoints::is_large_size(lw, lh)) {
    large_.on_resized(lw, lh);  // 保持 large（isLargeSize 分支不动作）
    large_.enter();
    if (auto saved = LargeModeController::load(app_dir_ +
                                               L"\\last_non_large_size.json")) {
      // 经 on_resized 记忆路径写入
      last_non_large_restore_ = *saved;
    }
    bp_.set_large(true);
  }
}

// ---------- 行为入口 ----------
void MainWindow::show_and_activate() {
  ShowWindow(hwnd_, SW_SHOW);
  // ← activate trick（window_manager.rs activate_window）：
  //   set_always_on_top(true) → (false)，绕过前台锁定策略
  SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  SetForegroundWindow(hwnd_);
  stealth_.feed_activity(now_ms());
}

void MainWindow::hide() { ShowWindow(hwnd_, SW_HIDE); }

void MainWindow::close_to_lightweight() {
  // ← 02§7：关闭按钮 = 隐藏窗口 + 进入轻量模式（非退出）
  CWLOG_INFO("close -> hide + entry_lightweight_mode");
  hide();
  // T09 门面（线程池执行，非 UI 阻塞）：g_lightweight + 事件过滤
  cmd::CommandService::instance().invoke("entry_lightweight_mode");
}

void MainWindow::toggle_large() {
  RECT rc;
  GetWindowRect(hwnd_, &rc);  // outer==inner（NCCALCSIZE 去边框）
  double lw = static_cast<double>(rc.right - rc.left);
  double lh = static_cast<double>(rc.bottom - rc.top);
  if (!large_.is_large()) {
    // 进入：保存当前尺寸到盘（← localStorage last-non-large-size）
    LargeModeController::save(app_dir_ + L"\\last_non_large_size.json", lw, lh);
    large_.on_resized(lw, lh);
    large_.enter();
    SetWindowPos(hwnd_, nullptr, 0, 0, static_cast<int>(kMaxWindowW),
                 static_cast<int>(kMaxWindowH), SWP_NOMOVE | SWP_NOZORDER);
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
    SetWindowPos(hwnd_, nullptr, 0, 0, static_cast<int>(w),
                 static_cast<int>(h), SWP_NOMOVE | SWP_NOZORDER);
    bp_.set_large(false);
  }
  stealth_.feed_activity(now_ms());
}

// ---------- 几何 ----------
void MainWindow::on_client_resized(int cw, int ch) {
  client_w_ = cw;
  client_h_ = ch;
  float s = dpi_ / 96.0f;
  double lw = static_cast<double>(cw) / s;
  double lh = static_cast<double>(ch) / s;
  large_.on_resized(lw, lh);
  bp_.set_large(large_.is_large());
  bp_.set_logical_size(lw, lh);
  stealth_.set_mini(bp_.is_mini());
  root_.layout_tree({0, 0, static_cast<float>(cw), static_cast<float>(ch)});
  clock_.invalidate({0, 0, static_cast<float>(cw), static_cast<float>(ch)});
  persist_geometry();
}

void MainWindow::persist_geometry(bool immediately) {
  if (!store_) {
    store_ = std::make_unique<WindowStateStore>(app_dir_ + L"\\window_state.json");
  }
  if (client_w_ <= 0 || client_h_ <= 0) return;
  float s = dpi_ / 96.0f;
  double w = static_cast<double>(client_w_) / s;
  double h = static_cast<double>(client_h_) / s;
  RECT wr;
  GetWindowRect(hwnd_, &wr);
  double x = static_cast<double>(wr.left) / s;
  double y = static_cast<double>(wr.top) / s;
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
  // ← tauri backgroundColor 口径（dt.background_color）：light #F5F5F5/dark #2E303D
  rt_.clear(theme::ThemeService::instance().palette().background_color);
  root_.paint_all(rt_);
  rt_.end_frame();
}

// ---------- NCHITTEST（8 向 resize + 拖动区） ----------
int MainWindow::hit_test_nc(POINT screen_pt) {
  POINT pt = screen_pt;
  ScreenToClient(hwnd_, &pt);
  RECT rc;
  GetClientRect(hwnd_, &rc);
  bool left = pt.x < kResizeBorder;
  bool right = pt.x >= rc.right - kResizeBorder;
  bool top = pt.y < kResizeBorder;
  bool bottom = pt.y >= rc.bottom - kResizeBorder;
  bool corner = false;
  // 角落 20px 热区（HANDLE_SIZE*2）
  if (pt.x < kCornerSize || pt.x >= rc.right - kCornerSize ||
      pt.y < kCornerSize || pt.y >= rc.bottom - kCornerSize)
    corner = true;
  if (top && (left || (corner && pt.x < kCornerSize))) return HTTOPLEFT;
  if (top && (right || (corner && pt.x >= rc.right - kCornerSize)))
    return HTTOPRIGHT;
  if (bottom && (left || (corner && pt.x < kCornerSize))) return HTBOTTOMLEFT;
  if (bottom && (right || (corner && pt.x >= rc.right - kCornerSize)))
    return HTBOTTOMRIGHT;
  if (left) return HTLEFT;
  if (right) return HTRIGHT;
  if (top) return HTTOP;
  if (bottom) return HTBOTTOM;

  // 标题栏拖动区：y < 30 且不在三键区（右侧 28*3+2*2+8 ≈ 96px）
  if (pt.y < kTitleBarH) {
    if (pt.x >= rc.right - (28 * 3 + 2 * 2 + 8)) return HTCLIENT;
    return HTCAPTION;
  }
  // stealth 隐藏：整窗拖动（← isDecorationsHidden 时 layout 根 data-tauri-drag-region）
  if (stealth_.hidden()) return HTCAPTION;
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
    // 【坑】handle() 内 DefWindowProcW 用成员 hwnd_；创建序列中 NCCREATE
    // 先于构造完成，必须先落成员（否则 DefWindowProcW(nullptr,...) 返回 0
    // → CreateWindowExW 失败 ERROR_INVALID_WINDOW_HANDLE 1400）
    if (self) self->hwnd_ = hwnd;
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
      if (wp) return 0;
      break;

    case WM_NCHITTEST:
      return hit_test_nc({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});

    case WM_GETMINMAXINFO: {
      // ← min_inner_size(271,126) / max_inner_size(640,860)；outer==inner
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
      float s = dpi_ / 96.0f;
      mmi->ptMinTrackSize.x = static_cast<LONG>(kMinWindowW * s + 0.5);
      mmi->ptMinTrackSize.y = static_cast<LONG>(kMinWindowH * s + 0.5);
      mmi->ptMaxTrackSize.x = static_cast<LONG>(kMaxWindowW * s + 0.5);
      mmi->ptMaxTrackSize.y = static_cast<LONG>(kMaxWindowH * s + 0.5);
      return 0;
    }

    case WM_SIZING: {
      // ← lib.rs:411-441：上缘缩放最小高度防下移（top=bottom-minH）
      auto* rc = reinterpret_cast<RECT*>(lp);
      float s = dpi_ / 96.0f;
      int min_h_px = static_cast<int>(kMinWindowH * s + 0.5);
      if (clamp_sizing_top(static_cast<uint32_t>(wp), *rc, min_h_px))
        return TRUE;
      break;  // 其余交给 DefWindowProc（系统按 MINMAX 钳制）
    }

    case WM_SIZE: {
      if (wp == SIZE_MINIMIZED) break;
      on_client_resized(LOWORD(lp), HIWORD(lp));
      stealth_.feed_activity(now_ms());  // 几何变化 = 交互（续期）
      return 0;
    }

    case WM_MOVE:
      persist_geometry();
      stealth_.feed_activity(now_ms());
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
      if (wp == kTimerFrame) {
        pump_frame();
      } else if (wp == kTimerIdle) {
        stealth_.tick(now_ms());  // 到点隐藏（自续期 = 下一秒再 tick）
      }
      return 0;

    case WM_ACTIVATE:
      // T14 卡⑤：失焦停采开关
      if (traffic_pane_) traffic_pane_->set_enabled(LOWORD(wp) != WA_INACTIVE);
      break;

    case WM_MOUSEMOVE:
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
      stealth_.feed_activity(now_ms());
      break;

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
        if (dx * dx + dy * dy < kStealthClickSlack * kStealthClickSlack)
          stealth_.restore_chrome(now_ms());
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
      // ← GetDpiForWindow 变化：按建议矩形落位 + 目标重建（scale 换挡）
      dpi_ = static_cast<float>(HIWORD(wp));
      auto* sug = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd_, nullptr, sug->left, sug->top,
                   sug->right - sug->left, sug->bottom - sug->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      rt_.recreate();
      RECT rc;
      GetClientRect(hwnd_, &rc);
      on_client_resized(rc.right - rc.left, rc.bottom - rc.top);
      return 0;
    }

    case kAppWakeup:  // 单实例唤醒（← /commands/visible）
      show_and_activate();
      return 0;

    case WM_CLOSE:
      // 关闭 = 隐藏 + 轻量模式（非退出；← 02§7）
      close_to_lightweight();
      return 0;

    case WM_DESTROY: {
      // 退出序列：立即落盘（跳过节流；← save_window_state_immediately）
      persist_geometry(true);
      if (store_) store_->save_immediately(
          static_cast<double>(client_w_) / (dpi_ / 96.0f),
          static_cast<double>(client_h_) / (dpi_ / 96.0f), std::nullopt,
          std::nullopt);
      set_frame_clock(nullptr);
      PostQuitMessage(0);
      return 0;
    }
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

int MainWindow::run() {
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

}  // namespace clashwin::ui::win
