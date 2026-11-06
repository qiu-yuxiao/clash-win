// 主窗口实现（T11）：无框窗 + 钳制 + Widget 树宿主 + 几何持久化。
// 行为对照见任务报告「窗口行为对照表」。
#include "main_window.h"

#include <windowsx.h>
#include <shellapi.h>  // ShellExecuteW（T17 打开日志目录/帮助）

#include <atomic>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <thread>

#include "clashwin/config/constants.h"
#include "config/config.h"
#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "events/event_bus.h"
#include "events/event_names.h"
#include "theme/theme.h"
#include "ui/ui_postbox.h"
#include "mihomo/ws_manager.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"
#include "system/tray.h"
#include "system/signals.h"
#include "ui/help_menu.h"
#include "ui/textfield.h"
#include <cstring>
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
constexpr UINT_PTR kTimerNodeRetry = 3;  // 取数失败 2s 重试（内核配置就绪竞态）
constexpr UINT_PTR kTimerFirstRun = 4;   // 首次引导 500ms 延迟（_layout.tsx:351-353）
// 首启就绪轮询驱动 250ms（1.5s 轮询闸门 + 20s 超时由 dialog tick 判定）
constexpr UINT_PTR kTimerFirstReady = 5;  // src: first-run-guide-dialog.tsx:46-48
constexpr int kResizeBorder = 10;     // ← resize-handles.tsx HANDLE_SIZE
constexpr int kCornerSize = 20;       // ← HANDLE_SIZE*2（角落热区）
// 关闭键拖动判定（原版 mouseup 位移 <5 恢复标题栏）
constexpr float kStealthClickSlack = 5.0f;

int64_t now_ms() { return static_cast<int64_t>(GetTickCount64()); }

// 剪贴板：TextField 的生产实现（Ctrl+C/V/X 与右键编辑菜单共用）
std::wstring read_clipboard_text() {
  if (!OpenClipboard(nullptr)) return {};
  std::wstring out;
  if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
    if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
      out.assign(p);
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return out;
}

void write_clipboard_text(const std::wstring& s) {
  if (!OpenClipboard(nullptr)) return;
  EmptyClipboard();
  const size_t bytes = (s.size() + 1) * sizeof(wchar_t);
  if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
    if (void* p = GlobalLock(h)) {
      memcpy(p, s.c_str(), bytes);
      GlobalUnlock(h);
      if (SetClipboardData(CF_UNICODETEXT, h)) {
        CloseClipboard();  // 成功后所有权归系统
        return;
      }
    }
    GlobalFree(h);  // 未交出：自行释放
  }
  CloseClipboard();
}
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

  // 皮肤/亮暗/主色热切换：ThemeService 六皮肤表由此接上订阅者，切皮肤即重绘。
  // ← Mini: src/pages/_layout.tsx:1705-1749 选中格每次渲染按 get3DButtonStyle
  //   (theme, 'contained', 'primary') 取当前 skin；src: src/utils/button-styles.ts:22
  //   getActiveSkin(theme) —— 所有控件按当前皮肤即时换表，无需重启。
  theme_sub_ = theme::ThemeService::instance().subscribe([this] {
    if (settings_drawer_) settings_drawer_->restyle_cards();
    clock_.invalidate({0, 0, static_cast<float>(client_w_),
                       static_cast<float>(client_h_)});
  });

  // 几何恢复（window_state.json：钳制 + 脱屏居中守卫，← restore_window_state）
  apply_geometry_restore();

  // 断点 → 标题栏图标/场景挂卸
  // T14 修正：连接面板只在设置抽屉右列（SettingsDrawer::conn_panel，
  // Mini _layout.tsx L1519 {!isMinimalWidth && <ConnectionsPanel/>} 同构）。
  // 曾错误地把 ConnectionsView 常驻挂到 list_slot_——Stack 后挂覆盖，
  // NodeList 被盖住导致主页节点列表消失，且与 Mini 布局不符，已移除。
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
      traffic_pane_->set_narrow(bp_.is_narrow());  // src: _layout.tsx:1774 宽窄内距分支
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
  stealth_.subscribe([this](bool hidden) {
    title_bar_->set_visible(!hidden);  // 隐藏时整栏不挂载（内容顶满）
    clock_.invalidate({0, 0, static_cast<float>(client_w_),
                       static_cast<float>(client_h_)});
  });

  RECT rc;
  GetClientRect(hwnd_, &rc);
  on_client_resized(rc.right - rc.left, rc.bottom - rc.top);

  build_scene();
  // 【LAYOUTFIX】quickbar / 节点列表是在 build_scene 里才挂上树的，晚于上面
  // 首次 layout_tree → 这些后挂控件停留在 0×0（快捷条不绘制、齿轮/图钉无法
  // 命中，关闭钮也被挤到窗口外）。挂载完成后再按当前客户区重排一次。
  {
    RECT rc2;
    GetClientRect(hwnd_, &rc2);
    on_client_resized(rc2.right - rc2.left, rc2.bottom - rc2.top);
  }

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
  // 默认几何（restore 会覆盖）；【DPIARCH】← window.rs：内部=逻辑 px，物理=×dpi/96
  // 【RULERFIX】取整一律 round-half-up：271×1.5=406.5 → 407（与 Mini 落盘分数 DIP
  //   得到的外框同宽；截断会少 1px，属可见差异）。高度 680×1.5=1020 不受影响。
  const float s = static_cast<float>(GetDpiForSystem()) / 96.0f;
  int w = static_cast<int>(kDefaultWindowW * s + 0.5f);
  int h = static_cast<int>(kDefaultWindowH * s + 0.5f);
  int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
  int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
  // ← window.rs：加载完成才 show → 创建时不带 WS_VISIBLE，show_and_activate 再显
  // F-D07：窗口标题对齐 Mini（window.rs:176 → "Clash Mini Ver.{appVersion}"）；
  // 品牌名按豁免替换为 Clash Win，版本号取自单一事实源 kAppVersion
  // （与标题栏 TitleBar::set_version 同一出处）。
  std::wstring win_title =
      L"Clash Win Ver." +
      utf::utf8_to_utf16(clashwin::config::consts::kAppVersion);
  hwnd_ = CreateWindowExW(
      0, kWndClass, win_title.c_str(),
      WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_SYSMENU,
      x, y, w, h, nullptr, nullptr, GetModuleHandleW(nullptr), this);
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
  // 【RULERFIX】创建期 WS_THICKFRAME 的非客户框在首次显影前仍占 10px（实测 +0.9s
  //   客户区只有 386×1000，+2.5s 才变 406×1020）。这里强制重算外框，使显影前
  //   client 就与窗口矩形一致，避免截图/布局踩到瞬态小窗。
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

void MainWindow::build_widget_tree() {
  // 生产剪贴板接 Win32（TextField 的 Ctrl+C/V/X 与右键编辑命令共用）
  TextField::set_clipboard_hooks(read_clipboard_text, write_clipboard_text);

  // root = Stack{ flow = Column{ titlebar(30), content = Stack{ scene, drawer } } }
  // 【T17-fix 根治】此前 root_ 直接挂 titlebar/scene——Stack::on_arrange
  //   铺满语义使两者 bounds 均为全窗（30px 只是 titlebar 绘制假设，scene
  //   从 y=0 盖过标题栏），真窗仅靠 WM 层 NCHITTEST 硬撑。现流式布局收进
  //   flow Column（titlebar measure 固定 30 + content flex 撑满剩余），
  //   Stack 只承担 overlay（抽屉覆盖）。
  // 【P0-drawer】抽屉挂载范围与 Mini 对齐：源码 Settings Drawer 在
  //   .layout-content（标题栏下方内容区，_layout.tsx:1254-1424 的
  //   position:absolute top:0/left:0/100%×100%），标题栏始终在它上方、
  //   保持可见可点（最小化/关闭/齿轮）。此前挂在 root_ = 整窗覆盖，
  //   把标题栏吃掉。现改为 content Stack 的第二个子层（全内容区覆盖）。
  auto flow = std::make_unique<Column>();
  flow_ = flow.get();
  root_.add_child(std::move(flow));

  // 悬停提示层（src: tooltip_w.h:11-15 overlay z=1200，MUI Tooltip 500ms）
  auto tip = std::make_unique<TooltipHost>();
  tooltip_host_ = tip.get();
  root_.add_child(std::move(tip));

  auto tb = std::make_unique<TitleBar>(this);
  title_bar_ = tb.get();
  title_bar_->set_version(utf::utf8_to_utf16(config::consts::kAppVersion));  // 单一事实源
  flow_->add_child(std::move(tb));

  auto content = std::make_unique<ContentStack>();
  content->flex_weight = 1;  // 标题栏下方剩余高度
  content_ = content.get();  // 布局时按底部流量区高度给抽屉留边
  Stack* content_raw = content.get();  // 【CRASHFIX】move 后 content 为空，
                                       // use-after-move 曾致 add_child(this=0) AV
  flow_->add_child(std::move(content));

  auto scene = std::make_unique<MainScene>();
  scene_ = scene.get();
  content_raw->add_child(std::move(scene));

  auto drawer = std::make_unique<scene::SettingsDrawer>();
  settings_drawer_ = drawer.get();
  drawer_mounted_ = true;
  content_->overlay = settings_drawer_;  // 只铺上栏，不遮底部流量条
  content_raw->add_child(std::move(drawer));

  // /unlock 整页（Mini pages/unlock.tsx:1206-1219 为独立路由整页）：
  //   挂 content 全高，打开时覆盖主场景与底部流量条（入口见 Tray）
  auto unlock = std::make_unique<scene::UnlockDialog>();
  unlock_page_ = unlock.get();
  unlock_page_->set_page_mode(true);
  unlock_page_->set_visible(false);
  unlock_page_->on_dismiss = [this]() {
    if (unlock_page_) unlock_page_->set_visible(false);
  };
  content_raw->add_child(std::move(unlock));
}

void MainWindow::open_unlock_page() {
  if (!unlock_page_) return;
  unlock_page_->set_visible(true);
  // 整页首次可见时手动补一次布局（不可见子层不参与 ContentStack::on_arrange）
  if (content_) {
    unlock_page_->layout_tree({0, 0, content_->bounds().w, content_->bounds().h});
  }
  unlock_page_->open();
  unlock_page_->invalidate();
}

void ContentStack::on_arrange(const RectF& area) {
  set_bounds(area);
  const float h = area.h - (bottom_inset > 0.0f ? bottom_inset : 0.0f);
  for (size_t i = 0; i < child_count(); ++i) {
    Widget* c = child(i);
    if (!c->visible()) continue;
    if (c == overlay)
      c->layout_tree({0, 0, area.w, h});
    else
      c->layout_tree({0, 0, area.w, area.h});
  }
}

void MainWindow::reload_nodes() {
  if (!node_data_ || !quick_bar_) return;
  bool ok = node_data_->load();
  const std::string now = node_data_->now();
  quick_bar_->set_node_text(utf::utf8_to_utf16(now.empty() ? "暂无激活的代理节点" : now));
  quick_bar_->set_badge(node_data_->row_delay(now), !now.empty());
  // 协议胶囊 + 地址：type 取组成员记录，地址取 get_proxy_addr 的 "server:port"
  // src: active-node-card.tsx:278（type）、:89-96（getProxyAddr → `${server}:${port}`）
  {
    std::string type;
    for (const auto& r : node_data_->rows()) {
      if (r.name == now) {
        type = r.type;
        break;
      }
    }
    std::wstring addr;
    if (!now.empty()) {
      auto r = cmd::CommandService::instance().invoke(
          "get_proxy_addr", nlohmann::json::object({{"name", now}}));
      if (r.is_ok() && r.value().is_object()) {
        const std::string server = r.value().value("server", std::string());
        const int port = r.value().value("port", 0);
        if (!server.empty()) {
          addr = utf::utf8_to_utf16(server + ":" + std::to_string(port));
        }
      }
    }
    quick_bar_->set_node_meta(type, addr);
  }
  if (ok) {
    node_retry_left_ = 0;
    KillTimer(hwnd_, kTimerNodeRetry);
    // ← Mini 实测：进主页自动组测速（无需点 ⚡）。delay_group 为同步 HTTP
    //   （内核并行测全组，~5s），放后台线程防 UI 冻结；结果经
    //   backend-delay-results 事件回灌局部刷新。仅捕获组名，无 this 悬垂。
    static std::atomic<bool> group_probing{false};
    if (!group_probing.exchange(true)) {
      std::string grp = node_data_->group();
      std::thread([grp] {
        auto r = cmd::mihomo();
        if (r.is_ok()) {
          std::string url = "https://cp.cloudflare.com/generate_204";
          try {
            const auto& verge =
                *config::Config::instance().verge().latest_arc();
            url = verge.default_latency_test.value_or(url);
          } catch (...) {}
          auto dr = r.value()->delay_group(grp, url, 5000);
          if (dr.is_ok()) {
            CWLOG_INFO("auto-probe 组测速 完成 节点数=" +
                       std::to_string(dr.value().size()));
            // 内核 HTTP 响应即全量结果；直接发事件回灌（部分节点超时不返回，
            // 未返回者保持未测灰 '-'，← Mini 实测行为）
            std::vector<std::pair<std::string, uint32_t>> vec;
            for (const auto& [name, ms] : dr.value()) vec.emplace_back(name, ms);
            events::EventBus::backend_delay_results(grp, vec);
          } else {
            CWLOG_INFO("auto-probe 组测速 失败: " + dr.error().message);
          }
        }
        group_probing.store(false);
      }).detach();
    }
  } else if (node_retry_left_ > 0) {
    --node_retry_left_;
    SetTimer(hwnd_, kTimerNodeRetry, 2000, nullptr);
  }
}

// 首启就绪轮询：refreshProxy（getProxies 全量）+ activeDelay（src:
// first-run-guide-dialog.tsx:143-170/:236-245）；row_delay(-1/-2) → null(-1)
void MainWindow::sync_first_run_proxy() {
  if (!node_data_) return;
  node_data_->load();
  const std::string now = node_data_->now();
  const int delay = now.empty() ? -1 : node_data_->row_delay(now);
  if (quick_bar_) {
    quick_bar_->set_node_text(utf::utf8_to_utf16(now.empty() ? "暂无激活的代理节点" : now));
    quick_bar_->set_badge(delay, !now.empty());
  }
  if (first_run_) {
    first_run_->set_active_node_name(now);
    first_run_->set_active_delay_ms(delay >= 0 ? delay : -1);
  }
}

void MainWindow::build_scene() {
  // T13：场景装配——数据（production hooks）→ 快捷条/节点列表挂载 → 事件订阅
  node_data_ = std::make_unique<scene::NodeData>(scene::NodeData::production_hooks());

  auto qb = std::make_unique<scene::QuickBar>();
  quick_bar_ = qb.get();
  scene_->attach_quick_bar(std::move(qb));

  auto nl = std::make_unique<scene::NodeList>();
  node_list_ = nl.get();
  node_list_->bind(node_data_.get());
  // 点击节点 → select_node_for_group（失败回弹：NodeData::select 内处理）
  node_list_->select_node = [this](const std::string& node) {
    bool ok = node_data_->select(node);
    if (ok) {
      // 【P0-5】切节点后 quickbar 的节点名/延迟需立即跟着变（此前要等下一次
      // delay-results 或 reload 才刷新，表现为“点了没反应/顶栏还是旧节点”）
      quick_bar_->set_node_text(utf::utf8_to_utf16(
          node_data_->now().empty() ? "-" : node_data_->now()));
      quick_bar_->set_badge(node_data_->row_delay(node_data_->now()),
                            !node_data_->now().empty());
    }
    return ok;
  };
  // src: active-node-card.tsx:284「onClick={handleCycleNode}」→ 顺序切到组内下一节点
  quick_bar_->on_cycle_node = [this] {
    if (!node_data_) return;
    const auto& rows = node_data_->rows();
    if (rows.size() < 2) return;
    size_t cur = rows.size() - 1;  // 未匹配 → 从第一个开始
    for (size_t i = 0; i < rows.size(); ++i)
      if (rows[i].name == node_data_->now()) { cur = i; break; }
    const size_t next = (cur + 1) % rows.size();
    if (!node_data_->select(rows[next].name)) return;  // 失败回弹（NodeData 内处理）
    quick_bar_->set_node_text(utf::utf8_to_utf16(node_data_->now()));
    quick_bar_->set_badge(node_data_->row_delay(node_data_->now()),
                          !node_data_->now().empty());
  };
  scene_->attach_node_list(std::move(nl));

  // 数据初始加载（内核配置就绪竞态：失败 2s 重试，最多 8 次）
  node_retry_left_ = 8;
  reload_nodes();

  // 延迟结果事件：局部刷新（← backend-delay-results {group, results}）
  UiPostbox::instance().subscribe_ui(
      events::names::kBackendDelayResults, [this](const nlohmann::json& p) {
        node_data_->apply_delay_results(p);
        quick_bar_->set_node_text(utf::utf8_to_utf16(
            node_data_->now().empty() ? "-" : node_data_->now()));
        quick_bar_->set_badge(node_data_->row_delay(node_data_->now()),
                              !node_data_->now().empty());
      });
  // 配置/代理刷新事件：全量重载
  UiPostbox::instance().subscribe_ui(
      events::names::kRefreshClash, [this](const nlohmann::json&) {
        node_retry_left_ = 8;
        reload_nodes();
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
  conn_data_->on_resync = [this] {
    CWLOG_WARN("conn resync：等待下一个 snapshot 帧（WS 重连）");
    if (ws_sub_conn_ != 0 && ws_) {
      ws_->unsubscribe(ws_sub_conn_);
      ws_sub_conn_ = 0;
      update_conn_ws_state();
    }
  };

  build_ws_pipes();

  // QuickBar 齿轮/图钉接线（Mini：橙方块 gear=开抽屉，pin=置顶）
  if (quick_bar_) {
    quick_bar_->on_open_settings = [this] {
      CWLOG_INFO("[GEAR] on_click → toggle drawer");
      if (settings_drawer_) settings_drawer_->set_open(!settings_drawer_->is_open());
    };
    quick_bar_->on_toggle_pin = [this] {
      pinned_ = !pinned_;
      SetWindowPos(hwnd_, pinned_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      if (quick_bar_) quick_bar_->set_pinned(pinned_);
    };
  }

  // ---------- T17/T20：设置抽屉命令链接线 ----------
  if (settings_drawer_) {
    if (auto* cp = settings_drawer_->conn_panel()) {
      cp->bind(conn_data_.get());
      cp->on_close_one = [](const std::string& id) {
        cmd::CommandService::instance().invoke(
            "close_connection", nlohmann::json::object({{"id", id}}));
      };
      cp->on_close_all = [] {
        cmd::CommandService::instance().invoke(
            "close_all_connections", nlohmann::json::object());
      };
      cp->on_clear_history = [this] {
        if (conn_data_) conn_data_->clear_closed();
      };
    }
    settings_drawer_->on_open_changed = [this](bool open) {
      // src: _layout.tsx:1309「{!drawerOpen && (…)}」→ 抽屉打开时整条 quickbar 不渲染
      if (quick_bar_) quick_bar_->set_visible(!open);
      update_conn_ws_state();
    };
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
          auto r = svc.invoke("patch_verge_config", nlohmann::json::object({{k, v}}));
          CWLOG_INFO(std::string("[TAKEOVER] patch_verge ") + k + "=" + v.dump() +
                     (r.is_ok() ? " ok" : " FAIL " + r.error().message));
        };
    settings_drawer_->on_patch_clash_mode = [](const std::string& mode) {
      cmd::CommandService::instance().invoke(
          "patch_clash_mode", nlohmann::json::object({{"mode", mode}}));
    };
    settings_drawer_->on_import_profile =
        [](const std::string& url, const std::string& name) {
          nlohmann::json args = nlohmann::json::object({{"url", url}});
          if (!name.empty()) args["name"] = name;
          auto r = cmd::CommandService::instance().invoke("import_profile", args);
          return r.is_ok();
        };
    settings_drawer_->on_select_profile = [](const std::string& uid) {
      cmd::CommandService::instance().invoke(
          "patch_profiles_config", nlohmann::json::object({{"current", uid}}));
    };
    settings_drawer_->on_update_profile = [](const std::string& uid) {
      cmd::CommandService::instance().invoke(
          "update_profile", nlohmann::json::object({{"index", uid}}));
    };
    settings_drawer_->on_delete_profile = [](const std::string& uid) {
      cmd::CommandService::instance().invoke(
          "delete_profile", nlohmann::json::object({{"index", uid}}));
    };
    // 编辑订阅保存 → patch_profile（← _layout.tsx:1021-1028；payload 见
    // src/cmd/cmd_profile.cpp:339-359）
    settings_drawer_->on_patch_profile = [](const std::string& uid,
                                            const std::string& name,
                                            const std::string& url, int interval) {
      // patch_item 是 option 整体替换（src/config/profiles.cpp:311），
      // 必须带上原有 option 字段，否则会清掉 user_agent / with_proxy 等。
      nlohmann::json opt = nlohmann::json::object();
      if (const config::PrfItem* cur =
              config::Config::instance().profiles().data_arc()->get_item(uid)) {
        if (cur->option) {
          const auto& o = *cur->option;
          if (o.user_agent) opt["user_agent"] = *o.user_agent;
          if (o.with_proxy) opt["with_proxy"] = *o.with_proxy;
          if (o.self_proxy) opt["self_proxy"] = *o.self_proxy;
          if (o.timeout_seconds) opt["timeout_seconds"] = *o.timeout_seconds;
          if (o.danger_accept_invalid_certs)
            opt["danger_accept_invalid_certs"] = *o.danger_accept_invalid_certs;
          if (o.allow_auto_update) opt["allow_auto_update"] = *o.allow_auto_update;
          if (o.merge) opt["merge"] = *o.merge;
          if (o.script) opt["script"] = *o.script;
          if (o.rules) opt["rules"] = *o.rules;
          if (o.proxies) opt["proxies"] = *o.proxies;
          if (o.groups) opt["groups"] = *o.groups;
        }
      }
      opt["update_interval"] = interval;
      nlohmann::json patch = {{"name", name}, {"url", url}, {"option", opt}};
      cmd::CommandService::instance().invoke(
          "patch_profile",
          nlohmann::json::object({{"index", uid}, {"profile", patch}}));
      return true;
    };
    settings_drawer_->on_open_logs = [this] {
      // ← Mini 调试日志：应用内 LogViewerModal（pages/logs.tsx），
      //   不再 ShellExecute 打开目录。首开时回填 logs/clashwin.log 末尾。
      if (!log_viewer_) return;
      if (log_viewer_->log_count() == 0 && log_viewer_->incoming_count() == 0) {
        std::wstring path =
            clashwin::util::dirs::app_home_dir() + L"\\logs\\clashwin.log";
        std::ifstream in(utf::utf16_to_utf8(path));
        if (in) {
          std::deque<std::string> tail;
          std::string line;
          while (std::getline(in, line)) {
            tail.push_back(std::move(line));
            if (tail.size() > scene::LogViewerModal::kMaxLogLines) tail.pop_front();
          }
          for (const auto& l : tail) log_viewer_->push_log_line(l);
        }
      }
      log_viewer_->open();
    };
    // 日志弹窗装载（覆盖抽屉，与 Mini 的独立 logs modal 一致）
    {
      auto lv = std::make_unique<scene::LogViewerModal>();
      log_viewer_ = lv.get();
      lv->set_z_order(1001);
      root_.add_child(std::move(lv));
    }
    // 添加网址弹窗（Mini：AddUrlDialog 挂在抽屉右列，_layout.tsx:1485-1488）
    {
      auto au = std::make_unique<scene::AddUrlDialog>();
      add_url_ = au.get();
      au->set_z_order(960);  // ctor 已设；显式声明与 root_ 其它弹窗层序
      root_.add_child(std::move(au));
    }
    // 客户端/内核更新弹窗（Mini：_layout.tsx:1948/1956，帮助菜单点击才弹）
    {
      auto cu = std::make_unique<scene::ClientUpdateModal>();
      client_update_ = cu.get();
      cu->set_z_order(1001);
      root_.add_child(std::move(cu));
    }
    {
      auto co = std::make_unique<scene::CoreUpdateModal>();
      core_update_ = co.get();
      co->set_z_order(1001);
      root_.add_child(std::move(co));
    }
    // 检查软件更新（src: use-client-update.ts:32-54）：有新版才开弹窗。
    // F-D05：检查期间禁用「检查更新」菜单项（对齐 Mini use-client-update.ts 的 checking 状态）。
    settings_drawer_->on_check_app_update = [this] {
      if (!client_update_) return;
      settings_drawer_->set_app_update_checking(true);
      std::thread([this] {
        auto r = cmd::CommandService::instance().invoke(
            "check_app_update", nlohmann::json::object());
        if (!r.is_ok()) {
          UiPostbox::instance().post([this] { settings_drawer_->set_app_update_checking(false); });
          return;
        }
        const auto& v = r.value();
        if (!v.value("has_update", false)) {
          UiPostbox::instance().post([this] { settings_drawer_->set_app_update_checking(false); });
          return;
        }
        const std::string dl_url = v.value("download_url", std::string());
        const std::string dl_sha = v.value("sha256", std::string());
        const std::string dl_sig = v.value("signature", std::string());
        UiPostbox::instance().post([this, v, dl_url, dl_sha, dl_sig] {
          if (!client_update_) return;
          pending_client_sha256_ = dl_sha;
          pending_client_signature_ = dl_sig;
          settings_drawer_->set_app_update_checking(false);
          client_update_->open(v.value("current_version", std::string()),
                               v.value("latest_version", std::string()),
                               v.value("notes", std::string()), dl_url);
        });
      }).detach();
    };
    // 检查内核更新（src: use-core-update.ts:64-86）：版本不同才开弹窗。
    // F-D05：检查期间禁用「检查内核更新」菜单项（对齐 Mini use-core-update.ts 的 checking 状态）。
    settings_drawer_->on_check_core_update = [this] {
      if (!core_update_) return;
      settings_drawer_->set_core_update_checking(true);
      std::thread([this] {
        std::string cur;
        auto m = cmd::mihomo();
        if (m.is_ok()) {
          auto v = m.value()->get_version(300);
          if (v.is_ok()) cur = v.value().version;
        }
        auto r = cmd::CommandService::instance().invoke(
            "check_core_update", nlohmann::json::object());
        if (!r.is_ok()) {
          UiPostbox::instance().post([this] { settings_drawer_->set_core_update_checking(false); });
          return;
        }
        const std::string latest = r.value().value("tag_name", std::string());
        auto strip = [](std::string s) {
          if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
          return s;
        };
        if (latest.empty() || strip(cur) == strip(latest)) {
          UiPostbox::instance().post([this] { settings_drawer_->set_core_update_checking(false); });
          return;
        }
        const std::string rel_json = r.value().dump();
        UiPostbox::instance().post([this, cur, latest, rel_json] {
          if (!core_update_) return;
          pending_core_release_json_ = rel_json;
          settings_drawer_->set_core_update_checking(false);
          core_update_->open(cur, latest);
        });
      }).detach();
    };
    // 客户端更新包下载（src: use-client-update.ts:56-125 downloadAndInstall 的下载段）。
    //   安装器启动（NSIS + UAC）不在本轮接线，见 K 提权清单。
    if (client_update_) {
      client_update_->on_start_update = [this] {
        if (!client_update_ || client_update_->download_url().empty()) return;
        const std::string url = client_update_->download_url();
        const std::string sha = pending_client_sha256_;
        const std::string sig = pending_client_signature_;
        std::thread([this, url, sha, sig] {
          auto r = cmd::CommandService::instance().invoke(
              "download_app_update",
              nlohmann::json::object({{"url", url}, {"sha256", sha}, {"signature", sig}}));
          if (!r.is_ok()) {
            const std::string msg = "更新失败: " + r.error().message;
            UiPostbox::instance().post([this, msg] {
              if (client_update_) client_update_->handle_progress_event("error", 0, msg);
            });
          }
        }).detach();
      };
    }
    // 启动内核热升级（src: use-core-update.ts:88-120 start_core_upgrade）：
    //   把检查时拿到的 release 原样回传，避免二次请求 GitHub API。
    if (core_update_) {
      core_update_->on_start_upgrade = [this] {
        if (!core_update_) return;
        const std::string rel_json = pending_core_release_json_;
        std::thread([this, rel_json] {
          nlohmann::json args = nlohmann::json::object();
          try {
            if (!rel_json.empty()) args = nlohmann::json::parse(rel_json);
          } catch (...) {
          }
          auto r = cmd::CommandService::instance().invoke("start_core_upgrade", args);
          if (!r.is_ok()) {
            const std::string msg = "启动失败: " + r.error().message;
            UiPostbox::instance().post([this, msg] {
              if (core_update_) core_update_->handle_progress_event("error", 0, msg);
            });
          }
        }).detach();
      };
    }
    // 首次运行向导（Mini：无真实订阅时 500ms 后弹出，_layout.tsx:347-355）
    {
      auto fr = std::make_unique<scene::FirstRunGuideDialog>();
      first_run_ = fr.get();
      fr->set_z_order(980);
      fr->on_import = [](const std::string& url) {
        auto r = cmd::CommandService::instance().invoke(
            "import_profile", nlohmann::json::object({{"url", url}}));
        return r.is_ok();
      };
      // src: :236-245 就绪步轮询 refreshProxy（getProxies 全量重载）
      fr->on_refresh_proxy = [this] { sync_first_run_proxy(); };
      // src: :188-225 重测并优选：trigger_auto_select + forceFull 刷新（异步）
      fr->on_retest_nodes = [this] {
        std::thread([this] {
          std::string uid;
          if (auto p = config::Config::instance().profiles().data_arc();
              p && p->current) {
            uid = *p->current;
          }
          if (!uid.empty()) {
            cmd::CommandService::instance().invoke(
                "trigger_auto_select",
                nlohmann::json::object(
                    {{"profile_uid", uid}, {"sort_type", 0}, {"select", true}}));
          }
          UiPostbox::instance().post([this] {
            sync_first_run_proxy();
            if (first_run_) first_run_->on_retest_finished();
          });
        }).detach();
      };
      fr->on_dismiss = [] {
        scene::FirstRunGuideDialog::set_first_run_completed(true);
      };
      root_.add_child(std::move(fr));
    }
    // 帮助菜单已改自绘（SettingsDrawer::open_help_menu → ContextMenu），
    //   不再走原生 HMENU（TrackPopupMenu 无法截图/样式不一致）。
    // 代理方式第三段 → 添加网址弹窗（Mini：handleAddUrlClick 先 patch
    // rule_fallback=addurl，再 setAddUrlOpen(true)，_layout.tsx:427-430）
    settings_drawer_->on_add_rule = [this] {
      if (add_url_) add_url_->open();
    };
    // 齿轮入口已统一到 quickbar（Mini 同构）；标题栏仅三键
    settings_drawer_->wire_callbacks();
    // 恢复上次皮肤/因子（clashwin-skin.json）
    settings_drawer_->restore_prefs();
    // verdict: Mini 的 theme_mode 存在 verge.yaml，是单一事实源；
    //   本地 skin.json 只是缓存。启动期以 verge 为准回填（不触发回写）。
    try {
      const auto& v = *config::Config::instance().verge().latest_arc();
      if (v.theme_mode.has_value())
        settings_drawer_->sync_theme_mode(utf::utf8_to_utf16(*v.theme_mode));
      settings_drawer_->sync_takeover(v.enable_system_proxy.value_or(false),
                                      v.enable_tun_mode.value_or(false));
      settings_drawer_->sync_basic(v.enable_auto_launch.value_or(false),
                                   v.enable_silent_start.value_or(false),
                                   false, false,
                                   static_cast<int>(v.verge_mixed_port.value_or(0)));
    } catch (...) {
    }
  }

  // ← Mini _layout.tsx:340-355：未完成向导且无真实订阅（remote/local）→
  //   500ms 后自动弹出首次引导；Merge/Script 内置项不算订阅。
  if (first_run_ && !scene::FirstRunGuideDialog::is_first_run_completed() &&
      !scene::FirstRunGuideDialog::has_real_profile()) {
    SetTimer(hwnd_, kTimerFirstRun, 500, nullptr);
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
  ws_sub_conn_ = 0;
  update_conn_ws_state();
}

void MainWindow::update_conn_ws_state() {
  // Mini 同构：/connections 流仅抽屉打开且宽>285 时订阅（连接面板只在抽屉右列）
  bool needed = settings_drawer_ && settings_drawer_->is_open() &&
                !bp_.is_narrow() && !bp_.is_mini() &&
                bp_.logical_h() > win::kMiniHeightThreshold;
  if (needed && ws_sub_conn_ == 0 && ws_) {
    ws_sub_conn_ = ws_->subscribe_connections(
        [this](const std::string& payload) {
          UiPostbox::instance().post([this, payload] {
            if (conn_data_) conn_data_->consume(payload);
          });
        });
  } else if (!needed && ws_sub_conn_ != 0 && ws_) {
    ws_->unsubscribe(ws_sub_conn_);
    ws_sub_conn_ = 0;
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

void MainWindow::release_d2d_resources() {
  rt_.destroy();
  if (hwnd_) KillTimer(hwnd_, kTimerFrame);
}

void MainWindow::restore_d2d_resources() {
  if (hwnd_) {
    rt_.create_for_hwnd(hwnd_);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    on_client_resized(rc.right - rc.left, rc.bottom - rc.top);
    SetTimer(hwnd_, kTimerFrame, 33, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void MainWindow::disconnect_ws() {
  if (ws_) ws_->disconnect_all();
}

void MainWindow::reconnect_ws() {
  // WsManager 保持配置
}

void MainWindow::save_geometry(bool immediately) {
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
  stealth_.feed_activity(now_ms());
}

// ---------- 几何 ----------
void MainWindow::on_client_resized(int cw, int ch) {
  client_w_ = cw;
  client_h_ = ch;
  float s = dpi_scale();
  double lw = static_cast<double>(cw) / s;
  double lh = static_cast<double>(ch) / s;
  large_.on_resized(lw, lh);
  bp_.set_large(large_.is_large());
  bp_.set_logical_size(lw, lh);
  stealth_.set_mini(bp_.is_mini());
  // src: _layout.tsx:224-228「if (drawerOpen && isMiniStatus) setDrawerOpen(false)」
  if (bp_.is_mini() && settings_drawer_ && settings_drawer_->is_open())
    settings_drawer_->set_open(false);
  // 抽屉只覆盖上栏（Mini .layout-content：下栏流量条固定高，不随抽屉消失）
  if (content_)
    content_->bottom_inset = static_cast<float>(
        bp_.is_narrow() ? kTrafficPaneMinimal : kTrafficPaneWide);
  // 【LAYOUTFIX】widget 树为逻辑 DIP：传 lw/lh 而非物理 cw/ch（否则 150% 下
  // 布局区 = 物理像素，组件被当作更宽的逻辑画布 → 堆叠/溢出）
  root_.layout_tree({0, 0, static_cast<float>(lw), static_cast<float>(lh)});
  clock_.invalidate({0, 0, static_cast<float>(lw), static_cast<float>(lh)});
  persist_geometry();
}

void MainWindow::persist_geometry(bool immediately) {
  if (!restore_done_) return;  // 【DPIFIX】恢复完成前不落盘（防 dpi 未换挡污染）
  if (!store_) {
    store_ = std::make_unique<WindowStateStore>(
        clashwin::util::dirs::window_state_path());
  }
  if (client_w_ <= 0 || client_h_ <= 0) return;
  float s = dpi_scale();
  // 【DPIARCH】内部数值必须整数：÷s 后四舍五入取整（406/1.5=270.67→271），
  // restore ×s 截断闭环（271×1.5=406.5→406）。
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
  //   → --theme-bg（use-custom-theme.ts:421-423 original / :457-460 其余五皮肤）
  //   原生窗口底色（tauri backgroundColor #F5F5F5/#2E303D）另由窗口类设置，不走此层
  rt_.clear(theme::ThemeService::instance().vars().theme_bg);
  // src: assets/styles/layout.scss:12-28 .layout::before 极光三径向层
  //   （top -50% / left -50% / 200%×200%；circle at 30%30% 主色 .18、
  //     70%70% rgba(0,132,255,.15)、80%20% rgba(212,175,55,.12)）
  //   注意：::before 位于 .layout 内容层之下，仅从半透明/未覆盖处的缝隙透出。
  //   src: layout.scss:23,292-302 animation: aurora-move 25s ease infinite alternate
  //   0%→50% translate(0,0)→(-10%,10%)、rotate 0→180deg、scale 1→1.1；
  //   50%→100% 反向（alternate + 100% 关键帧 translate(10%,-10%) rotate(360) scale(1)
  //   与 0% 同态）→ 等效为 50s 往返。等价改写：smoothstep 逼近 ease 曲线，10fps 节流续帧。
  {
    const float w = rt_.size().w, h = rt_.size().h;
    const float vf = theme::ThemeService::instance().vibrancy_factor();
    const RectF big{-w * 0.5f, -h * 0.5f, w * 2.0f, h * 2.0f};
    // circle farthest-corner 半径 = 1.4 × 对角线
    const float rad = 1.4f * std::sqrt(w * w + h * h);
    const double u = std::fmod(static_cast<double>(now_ms()) / 25000.0, 2.0);
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
    const Color4f primary = theme::ThemeService::instance().primary();
    GradientStop g1[2] = {{0.0f, Color4f{primary.r, primary.g, primary.b, 0.18f * vf}},
                          {0.40f, Color4f{primary.r, primary.g, primary.b, 0.0f}}};
    float x1, y1;
    center(0.30f, 0.30f, &x1, &y1);
    rt_.fill_round_rect_radial(big, 0.0f, g1, 2, x1, y1, rr, rr);
    GradientStop g2[2] = {{0.0f, Color4f::hex(0x0084FF, 0.15f * vf)},
                          {0.40f, Color4f::hex(0x0084FF, 0.0f)}};
    float x2, y2;
    center(0.70f, 0.70f, &x2, &y2);
    rt_.fill_round_rect_radial(big, 0.0f, g2, 2, x2, y2, rr, rr);
    GradientStop g3[2] = {{0.0f, Color4f::hex(0xD4AF37, 0.12f * vf)},
                          {0.35f, Color4f::hex(0xD4AF37, 0.0f)}};
    float x3, y3;
    center(0.80f, 0.20f, &x3, &y3);
    rt_.fill_round_rect_radial(big, 0.0f, g3, 2, x3, y3, rr, rr);
    // 动画续帧（极慢速位移，10fps 足以视觉平滑；隐藏时停帧）
    if (hwnd_ && IsWindowVisible(hwnd_)) {
      const int64_t nm = now_ms();
      if (nm - aurora_tick_ms_ >= 100) {
        aurora_tick_ms_ = nm;
        root_.invalidate();
      }
    }
  }
  root_.paint_all(rt_);
  // src: assets/styles/layout.scss:168 标题栏 box-shadow 0 1px 4px
  //   var(--theme-border-outer)；需覆盖在内容层之上，故在 root 之后绘制。
  //   无模糊核 → 4 条递减 alpha 的 1px 线近似衰减。
  if (title_bar_) {
    const Color4f outer = theme::ThemeService::instance().vars().theme_border_outer;
    const RectF tb = title_bar_->bounds();
    for (int i = 1; i <= 4; ++i) {
      Color4f c = outer;
      c.a = outer.a * (1.0f - static_cast<float>(i - 1) / 4.0f) / 2.0f;
      rt_.fill_rect({tb.x, tb.bottom() + i - 1, tb.w, 1}, Brush{c});
    }
  }
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
  // src: resize-handles.tsx:500「if (isLargeMode) return null」→ 大尺寸态无侧边热区
  if (!bp_.is_large()) {
  const auto rb = static_cast<LONG>(kResizeBorder * s);
  const auto cs = static_cast<LONG>(kCornerSize * s);
  bool left = pt.x < rb;
  bool right = pt.x >= rc.right - rb;
  bool top = pt.y < rb;
  bool bottom = pt.y >= rc.bottom - rb;
  bool corner = false;
  // 角落 20px 热区（HANDLE_SIZE*2）
  if (pt.x < cs || pt.x >= rc.right - cs || pt.y < cs || pt.y >= rc.bottom - cs)
    corner = true;
  if (top && (left || (corner && pt.x < cs))) return HTTOPLEFT;
  if (top && (right || (corner && pt.x >= rc.right - cs))) return HTTOPRIGHT;
  if (bottom && (left || (corner && pt.x < cs))) return HTBOTTOMLEFT;
  if (bottom && (right || (corner && pt.x >= rc.right - cs)))
    return HTBOTTOMRIGHT;
  if (left) return HTLEFT;
  if (right) return HTRIGHT;
  if (top) return HTTOP;
  if (bottom) return HTBOTTOM;
  }

  // 标题栏拖动区：y < 30×s 且不在四键区（【DPIARCH】pt/rc 为物理像素，
  // 旧代码用 DIP 常量 30/96 直接比较 → 150% 下按钮区/栏高错位；
  // 现按 widget DIP bounds ×s 精确判定四键热区）
  if (pt.y < kTitleBarH * s) {
    if (title_bar_) {
      PointF p{static_cast<float>(pt.x) / s, static_cast<float>(pt.y) / s};
      for (Widget* b : {static_cast<Widget*>(title_bar_->btn_minimize()),
                        static_cast<Widget*>(title_bar_->btn_large()),
                        static_cast<Widget*>(title_bar_->btn_close())}) {
        if (b && b->visible() && b->global_bounds().contains(p.x, p.y))
          return HTCLIENT;
      }
    }
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
    if (self) {
      self->hwnd_ = hwnd;
      // 【DPIARCH】取窗口实际 DPI（150% 屏=144）：全链统一
      // 物理 = 逻辑 × dpi/96（persist ÷s / restore ×s / 输入 ÷s / D2D transform）。
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
      if (wp) return 0;
      break;

    case WM_NCHITTEST:
      return hit_test_nc({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});

    case WM_GETMINMAXINFO: {
      // ← min_inner_size(271,126) / max_inner_size(640,860)；outer==inner
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
      // 【DPIARCH】逻辑 min/max × 实际 DPI 四舍五入（与 create 的 407 同口径）
      float s = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
      mmi->ptMinTrackSize.x = static_cast<LONG>(kMinWindowW * s + 0.5f);
      mmi->ptMinTrackSize.y = static_cast<LONG>(kMinWindowH * s + 0.5f);
      mmi->ptMaxTrackSize.x = static_cast<LONG>(kMaxWindowW * s + 0.5f);
      mmi->ptMaxTrackSize.y = static_cast<LONG>(kMaxWindowH * s + 0.5f);
      return 0;
    }

    case WM_SIZING: {
      // ← lib.rs:411-441：上缘缩放最小高度防下移（top=bottom-minH）
      auto* rc = reinterpret_cast<RECT*>(lp);
      float s = dpi_scale();
      int min_h_px = static_cast<int>(kMinWindowH * s + 0.5);
      if (clamp_sizing_top(static_cast<uint32_t>(wp), *rc, min_h_px))
        return TRUE;
      break;  // 其余交给 DefWindowProc（系统按 MINMAX 钳制）
    }

    case WM_SIZE: {
      if (wp == SIZE_MINIMIZED) break;
      const int cw = LOWORD(lp), ch = HIWORD(lp);
      // 【DPIARCH】D2D HWND target 不会自行跟帧（本 WndProc 未走 DefWindowProc）：
      // backbuffer 停在创建期 386x1000 → 406x1020 客户区被拉伸 5.2% 呈现。
      // 显式 Resize 使像素尺寸恒等于客户区（DIP=px/scale 恒为 271x680）。
      rt_.resize(cw, ch);
      on_client_resized(cw, ch);
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
        if (tooltip_host_) tooltip_host_->tick_now(now_ms());
      } else if (wp == kTimerIdle) {
        stealth_.tick(now_ms());  // 到点隐藏（自续期 = 下一秒再 tick）
      } else if (wp == kTimerNodeRetry) {
        KillTimer(hwnd_, kTimerNodeRetry);
        reload_nodes();
      } else if (wp == kTimerFirstRun) {
        KillTimer(hwnd_, kTimerFirstRun);
        if (first_run_) {
          first_run_->open();
          // src: :228-245 就绪超时/轮询需要周期驱动
          SetTimer(hwnd_, kTimerFirstReady, 250, nullptr);
        }
      } else if (wp == kTimerFirstReady) {
        if (first_run_ && first_run_->is_open()) {
          first_run_->tick(now_ms());
        } else {
          KillTimer(hwnd_, kTimerFirstReady);
        }
      }
      return 0;

    case WM_ACTIVATE:
      // T14 卡⑤：失焦停采开关
      if (traffic_pane_) traffic_pane_->set_enabled(LOWORD(wp) != WA_INACTIVE);
      break;

    // 【INPUTFIX】鼠标/键盘路由给 widget 树（此前仅 feed_activity，
    // route_*_event 只在 tests/ 被调 → 实机开关点击无反应）
    case WM_NCMOUSEMOVE:
      if (tooltip_host_) tooltip_host_->leave();  // 移入非客户区 → 收起提示
      break;

    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP: {
      stealth_.feed_activity(now_ms());
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
      // 捕获：Down 后移出窗口仍能收到 Move/Up（开关拖动 thumb 跟随）
      if (down) SetCapture(hwnd_);
      else if (up && GetCapture() == hwnd_) ReleaseCapture();
      route_mouse_event(&root_, e);
      // 悬停提示：命中控件（或父链）→ 500ms 后显示（src: tooltip_w.h:20-23）
      if (tooltip_host_ && e.type == MouseEventType::Move) {
        tooltip_host_->tick_now(now_ms());
        tooltip_host_->hover(hovered_widget(), e.pos);
      }
      return 0;
    }

    case WM_MOUSEWHEEL: {
      stealth_.feed_activity(now_ms());
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
      stealth_.feed_activity(now_ms());
      KeyEvent e;
      e.type = (msg == WM_KEYUP) ? KeyEventType::Up : KeyEventType::Down;
      e.key = static_cast<uint32_t>(wp);
      e.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      e.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      e.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
      if (route_key_event(&root_, e)) return 0;
      break;  // 未处理 → DefWindowProc（助记符/系统键）
    }

    case WM_CHAR: {
      stealth_.feed_activity(now_ms());
      KeyEvent e;
      e.type = KeyEventType::Char;
      e.ch = static_cast<wchar_t>(wp);
      e.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      e.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      e.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
      if (route_key_event(&root_, e)) return 0;
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
      // 【DPIARCH】← Mini(tao) 行为：dpi_ 换挡 → 重建 target（transform ×scale）
      // → 应用建议矩形（= 保持逻辑尺寸、按新 DPI 换物理）→ 重排。
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

    case kAppWakeup:  // 单实例唤醒（← /commands/visible）
      app::Lifecycle::instance().exit_lightweight_mode();
      return 0;

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
      // 退出序列：立即落盘（跳过节流；← save_window_state_immediately）
      persist_geometry(true);
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
