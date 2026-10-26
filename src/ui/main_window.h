#pragma once
// 主窗口框架（T11）：接管 T01 窗口壳——无框窗 + 钳制 + 8 向 resize +
// Widget 树宿主 + 几何持久化 + 断点/stealth/大尺寸模式。
//
// 分层：纯逻辑（可测）与 HWND 薄壳分离——
// - clamp_sizing_top / minmax 数值：纯函数（← lib.rs:411-441 sizing_wndproc）
// - StealthController：FEAT-003 标题栏 10s 空闲隐藏（← window-provider.tsx）
// - LargeModeController：大尺寸 toggle + last-non-large-size 持久化
// - MainWindow：HWND/WndProc/渲染泵/几何事件接线
#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "breakpoints.h"
#include "clashwin/config/constants.h"
#include "config/window_state.h"
#include "d2d_context.h"
#include "render_target.h"
#include "title_bar.h"
#include "widget.h"
#include "container.h"
#include "main_scene.h"
#include "scenes/connection_data.h"
#include "scenes/connections_view.h"
#include "scenes/node_data.h"
#include "scenes/settings_drawer.h"
#include "scenes/node_list.h"
#include "scenes/quick_bar.h"
#include "scenes/traffic_pane.h"
#include "mihomo/ws_manager.h"

namespace clashwin::ui::win {

// ---------- 纯逻辑：WM_SIZING 上缘钳制（← lib.rs:411-441 逐行） ----------
// 系统只钳尺寸不钳位置：上边缘缩放到最小高度后 Windows 继续增加 Y，
// 窗口整体下移。WM_SIZING 在系统应用矩形之前给提议矩形，此处同时钳制。
// side：WMSZ_*；rc：提议矩形（外框口径；本项目 outer==inner）；min_h_px：
// 最小高度物理像素（MINIMAL_HEIGHT × dpi/96，取整）。返回 TRUE 表示已修正。
inline bool clamp_sizing_top(uint32_t side, RECT& rc, int min_h_px) {
  if (side != WMSZ_TOP && side != WMSZ_TOPLEFT && side != WMSZ_TOPRIGHT)
    return false;
  int height = static_cast<int>(rc.bottom - rc.top);
  if (height < min_h_px) {
    rc.top = rc.bottom - min_h_px;
    return true;
  }
  return false;
}

// ---------- 纯逻辑：FEAT-003 标题栏 stealth（10s 空闲隐藏） ----------
// ← window-provider.tsx resetIdleTimer/restoreChrome/applyGeometry：
// - 交互（mouse/key/resize）续期；计时器到点且处于极小窗 → 隐藏
// - 隐藏必须自续期（tick 后无条件重排，不得自杀式退出）
// - 离开极小窗尺寸 → 立即恢复（applyGeometry 语义）
// - 隐藏态下点击（位移<5）恢复；拖动（>5）不恢复 → 由 MainWindow NCHITTEST 配合
class StealthController {
 public:
  void set_mini(bool mini) {
    mini_ = mini;
    // 离开极小窗：立即退出隐匿、恢复标题栏
    if (!mini && hidden_) {
      hidden_ = false;
      notify();
    }
  }
  bool mini() const { return mini_; }
  bool hidden() const { return hidden_; }

  // 任意用户输入/窗口几何变化：续期（onResized 每帧续 + 输入事件续）
  void feed_activity(int64_t now_ms) { last_activity_ = now_ms; }

  // 周期驱动（MainWindow 1s timer；调用方无条件再排 = 自续期语义）
  void tick(int64_t now_ms) {
    if (!hidden_ && mini_ &&
        now_ms - last_activity_ >= kIdleHideDelayMs) {
      hidden_ = true;
      notify();
    }
  }

  // 点击恢复（restoreChrome；恢复本身也是一次交互 → 续期）
  void restore_chrome(int64_t now_ms) {
    if (hidden_) {
      hidden_ = false;
      notify();
    }
    last_activity_ = now_ms;
  }

  uint64_t subscribe(std::function<void(bool hidden)> cb) {
    uint64_t id = next_id_++;
    subs_.push_back({id, std::move(cb)});
    return id;
  }
  void unsubscribe(uint64_t id) {
    for (size_t i = 0; i < subs_.size(); ++i)
      if (subs_[i].first == id) { subs_.erase(subs_.begin() + i); break; }
  }

 private:
  void notify() {
    for (auto& [id, cb] : subs_) cb(hidden_);
  }
  int64_t last_activity_ = 0;
  bool mini_ = false;
  bool hidden_ = false;
  std::vector<std::pair<uint64_t, std::function<void(bool)>>> subs_;
  uint64_t next_id_ = 1;
};

// ---------- 纯逻辑：大尺寸模式 + last-non-large-size ----------
// ← toggleMaximize/init：进入 640×860 前保存当前尺寸；从大尺寸拖小自动退出；
// 启动时若恢复到 640×860 → 标记 large + 从盘读 last-non-large。
// 原版存 localStorage 'last-non-large-size'（webview 侧）；C++ 无 webview，
// 等价落盘到 app 目录 last_non_large_size.json（独立小文件，不混入
// window_state.json 以保持与原版该文件逐字段 diff 兼容）。
class LargeModeController {
 public:
  bool is_large() const { return large_; }
  double last_w() const { return last_w_; }
  double last_h() const { return last_h_; }

  // ← onResized checkMaximized（防抖后）：
  void on_resized(double logical_w, double logical_h) {
    bool is_large_size = Breakpoints::is_large_size(logical_w, logical_h);
    if (large_ && !is_large_size) {
      // 大尺寸模式下被拖小 → 自动退出并记录当前
      large_ = false;
      last_w_ = logical_w;
      last_h_ = logical_h;
    } else if (!large_ && !is_large_size) {
      last_w_ = logical_w;
      last_h_ = logical_h;
    }
    // isLargeSize 且已 large：toggleMaximize 触发的 setSize，无需处理
  }

  // toggleMaximize 进入：返回 true（调用方 SetWindowPos 640×860）
  bool enter() {
    large_ = true;
    return true;
  }
  // toggleMaximize 退出：恢复尺寸（调用方 SetWindowPos）
  void exit() { large_ = false; }

  // 持久化（进入大尺寸前调用；path 为 last_non_large_size.json 全路径）
  static void save(const std::wstring& path, double w, double h);
  // 启动恢复：窗口恰为 640×860 时读盘；无记录/损坏 → 默认 271×680
  static std::optional<std::pair<double, double>> load(const std::wstring& path);

 private:
  bool large_ = false;
  double last_w_ = kDefaultWindowW, last_h_ = kDefaultWindowH;
};

// ---------- 主窗口 ----------
class MainWindow {
 public:
  // 创建窗口（restore 几何 → 注册类 → CreateWindow → D2D → Widget 树 → 定时器）
  bool create(bool show_immediately = true);
  // 消息循环（App 转发）
  static int run();

  // 行为入口（TitleBar 回调 / 托盘 T19 复用）
  void show_and_activate();   // ShowWindow + 前台化 + 激活 trick(TOPMOST→NOTOPMOST)
  void hide();                // SW_HIDE（→ 轻量模式语义）
  void close_to_lightweight();// 关闭按钮：隐藏 + entry_lightweight_mode
  void toggle_large();        // 大尺寸 toggle（640×860 ↔ last-non-large）
  void minimize() { if (hwnd_) ShowWindow(hwnd_, SW_MINIMIZE); }

  HWND hwnd() const { return hwnd_; }
  Breakpoints& breakpoints() { return bp_; }
  StealthController& stealth() { return stealth_; }
  Widget* root_widget() { return &root_; }

 private:
  bool init_window_class();
  bool create_hwnd();
  bool init_render();
  void build_widget_tree();
  void build_scene();     // T13/T14：场景装配 + 数据事件订阅
  void build_ws_pipes();  // T14：WS 订阅 → UiPostbox 转投
  void apply_geometry_restore();  // window_state.json 恢复（含脱屏居中）

  void on_client_resized(int cw, int ch);          // WM_SIZE
  void persist_geometry(bool immediately = false); // WM_SIZE/WM_MOVE
  void pump_frame();                               // 33ms 帧泵
  void draw();
  int hit_test_nc(POINT screen_pt);                // WM_NCHITTEST

  static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handle(UINT msg, WPARAM, LPARAM);

  HWND hwnd_ = nullptr;
  D2dContext ctx_;
  D2dRenderTarget rt_{ctx_};
  FrameClock clock_;

  Stack root_;            // Widget 树根（客户区）
  TitleBar* title_bar_ = nullptr;
  MainScene* scene_ = nullptr;
  // T13 场景
  std::unique_ptr<scene::NodeData> node_data_;
  scene::QuickBar* quick_bar_ = nullptr;
  scene::NodeList* node_list_ = nullptr;
  // T14 场景
  std::unique_ptr<scene::TrafficPane> traffic_pane_holder_;
  scene::TrafficPane* traffic_pane_ = nullptr;
  std::unique_ptr<scene::ConnectionData> conn_data_;
  scene::ConnectionsView* conn_view_ = nullptr;
  scene::SettingsDrawer* settings_drawer_ = nullptr;
  bool drawer_mounted_ = false;
  bool conn_view_mounted_ = false;
  std::unique_ptr<mihomo::WsManager> ws_;  // T14：traffic/connections WS
  uint64_t ws_sub_traffic_ = 0, ws_sub_conn_ = 0;

  Breakpoints bp_;
  StealthController stealth_;
  LargeModeController large_;

  std::wstring app_dir_;            // exe 目录（几何文件落这里，T03 dirs 同口径）
  std::unique_ptr<config::window_state::WindowStateStore> store_;
  int client_w_ = 0, client_h_ = 0;
  float dpi_ = 96.0f;
  bool in_destroy_ = false;
  std::optional<std::pair<double, double>> last_non_large_restore_;  // 盘读
  std::optional<POINT> nc_down_;  // stealth 点击恢复判定
};

}  // namespace clashwin::ui::win
