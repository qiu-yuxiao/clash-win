#include "system/tray.h"
#include "util/log.h"

#include <shellapi.h>

namespace clashwin::system {

namespace {

struct GlobalTraySyncRegistry {
  std::mutex mu;
  Tray* target = nullptr;
  std::function<void(std::function<void()>)> dispatcher;
};

GlobalTraySyncRegistry& global_tray_registry() {
  static GlobalTraySyncRegistry reg;
  return reg;
}

}  // namespace

// ---------- Win32TrayBackend 实现 ----------

bool Win32TrayBackend::add_icon(HWND hwnd, UINT uID, UINT uCallbackMsg, HICON icon, const std::wstring& tip) {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hwnd;
  nid.uID = uID;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid.uCallbackMessage = uCallbackMsg;
  nid.hIcon = icon;
  if (!tip.empty()) {
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
  }
  return Shell_NotifyIconW(NIM_ADD, &nid) == TRUE;
}

bool Win32TrayBackend::modify_icon(HWND hwnd, UINT uID, HICON icon, const std::wstring& tip) {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hwnd;
  nid.uID = uID;
  nid.uFlags = NIF_ICON | NIF_TIP;
  nid.hIcon = icon;
  if (!tip.empty()) {
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
  }
  return Shell_NotifyIconW(NIM_MODIFY, &nid) == TRUE;
}

bool Win32TrayBackend::delete_icon(HWND hwnd, UINT uID) {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hwnd;
  nid.uID = uID;
  return Shell_NotifyIconW(NIM_DELETE, &nid) == TRUE;
}

bool Win32TrayBackend::show_balloon(HWND hwnd, UINT uID,
                                    const std::wstring& title,
                                    const std::wstring& body) {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hwnd;
  nid.uID = uID;
  nid.uFlags = NIF_INFO;
  nid.dwInfoFlags = NIIF_NONE | NIIF_NOSOUND;
  nid.uTimeout = 3000;
  wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
  wcsncpy_s(nid.szInfo, body.c_str(), _TRUNCATE);
  return Shell_NotifyIconW(NIM_MODIFY, &nid) == TRUE;
}

void Win32TrayBackend::show_menu(HWND hwnd, POINT pt, bool in_lite_mode, std::function<void(int cmd_id)> on_cmd) {
  HMENU hMenu = CreatePopupMenu();
  if (!hMenu) return;

  // src: src-tauri/src/core/tray/mod.rs:292 MenuItem::with_id 文本自身带 "✔ "，
  //   不叠乘 Win32 CheckMenuItem（否则会出现「✓ ✔」双重勾号）
  std::wstring lite_text = in_lite_mode ? L"✔ 轻量模式 / Lite mode" : L"轻量模式 / Lite mode";
  AppendMenuW(hMenu, MF_STRING, kMenuLiteModeId, lite_text.c_str());
  AppendMenuW(hMenu, MF_STRING, kMenuExitId, L"退出 (Exit)");

  SetForegroundWindow(hwnd);
  int cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                             pt.x, pt.y, hwnd, nullptr);
  DestroyMenu(hMenu);
  if (cmd != 0 && on_cmd) {
    on_cmd(cmd);
  }
}

// ---------- Tray 实现 ----------

Tray::Tray(std::unique_ptr<ITrayBackend> backend)
    : backend_(std::move(backend)) {
  if (!backend_) {
    backend_ = std::make_unique<Win32TrayBackend>();
  }
}

Tray::~Tray() {
  destroy();
  if (h_icon_default_ && h_icon_default_ != LoadIconW(nullptr, IDI_APPLICATION)) {
    DestroyIcon(h_icon_default_);
  }
  if (h_icon_sys_ && h_icon_sys_ != h_icon_default_ && h_icon_sys_ != LoadIconW(nullptr, IDI_APPLICATION)) {
    DestroyIcon(h_icon_sys_);
  }
  if (h_icon_tun_ && h_icon_tun_ != h_icon_default_ && h_icon_tun_ != LoadIconW(nullptr, IDI_APPLICATION)) {
    DestroyIcon(h_icon_tun_);
  }
}

void Tray::load_icons() {
  wchar_t exe_path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  std::wstring exe_dir(exe_path);
  size_t pos = exe_dir.find_last_of(L"\\/");
  if (pos != std::wstring::npos) exe_dir.resize(pos);

  HINSTANCE hInst = GetModuleHandleW(nullptr);
  const int cx_sm = GetSystemMetrics(SM_CXSMICON);
  const int cy_sm = GetSystemMetrics(SM_CYSMICON);
  const int w = cx_sm > 0 ? cx_sm : 16;
  const int h = cy_sm > 0 ? cy_sm : 16;
  auto load_one = [&](int res_id, const std::wstring& rel_path) -> HICON {
    // 1. 优先从编译期嵌入 .rc 的多尺寸 ico 中按 SM_CXSMICON 精确取帧（← Mini include_bytes!）
    HICON hi = static_cast<HICON>(
        LoadImageW(hInst, MAKEINTRESOURCEW(res_id), IMAGE_ICON, w, h, LR_DEFAULTCOLOR));
    if (hi) return hi;
    // 2. 兜底：无 .rc 的单测二进制回退到磁盘文件加载
    std::wstring full = exe_dir + L"\\" + rel_path;
    hi = static_cast<HICON>(
        LoadImageW(nullptr, full.c_str(), IMAGE_ICON, w, h, LR_LOADFROMFILE));
    if (!hi) {
      hi = static_cast<HICON>(
          LoadImageW(nullptr, full.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
    }
    return hi;
  };

  h_icon_default_ = load_one(kTrayIconResDefault, L"resources\\icons\\tray-icon.ico");
  h_icon_sys_     = load_one(kTrayIconResSys, L"resources\\icons\\tray-icon-sys.ico");
  h_icon_tun_     = load_one(kTrayIconResTun, L"resources\\icons\\tray-icon-tun.ico");

  if (!h_icon_default_) h_icon_default_ = LoadIconW(nullptr, IDI_APPLICATION);
  if (!h_icon_sys_) h_icon_sys_ = h_icon_default_;
  if (!h_icon_tun_) h_icon_tun_ = h_icon_default_;
}

HICON Tray::get_icon_for_state(TrayIconState state) {
  switch (state) {
    case TrayIconState::Tun:
      return h_icon_tun_ ? h_icon_tun_ : h_icon_default_;
    case TrayIconState::SysProxy:
      return h_icon_sys_ ? h_icon_sys_ : h_icon_default_;
    case TrayIconState::Default:
    default:
      return h_icon_default_;
  }
}

bool Tray::init(HWND hwnd, TrayIconState initial_state) {
  hwnd_ = hwnd;
  current_state_ = initial_state;
  load_icons();
  if (backend_) {
    HICON icon = get_icon_for_state(current_state_);
    icon_added_ = backend_->add_icon(hwnd_, kTrayIconId, kAppTrayMsg, icon, tooltip_);
    return icon_added_;
  }
  return false;
}

void Tray::destroy() {
  if (icon_added_ && backend_ && hwnd_) {
    backend_->delete_icon(hwnd_, kTrayIconId);
    icon_added_ = false;
  }
}

void Tray::update_icon(bool tun, bool sysproxy) {
  TrayIconState next = calculate_state(tun, sysproxy);
  update_icon_state(next);
}

void Tray::update_icon_state(TrayIconState state) {
  std::lock_guard<std::mutex> lk(mu_);
  current_state_ = state;
  if (icon_added_ && backend_ && hwnd_) {
    HICON icon = get_icon_for_state(current_state_);
    backend_->modify_icon(hwnd_, kTrayIconId, icon, tooltip_);
  }
}

void Tray::set_global_sync_target(
    Tray* tray, std::function<void(std::function<void()>)> dispatcher) {
  auto& reg = global_tray_registry();
  std::lock_guard<std::mutex> lk(reg.mu);
  reg.target = tray;
  reg.dispatcher = std::move(dispatcher);
}

void Tray::sync_from_verge(bool tun, bool sysproxy) {
  Tray* tray = nullptr;
  std::function<void(std::function<void()>)> dispatcher;
  auto& reg = global_tray_registry();
  {
    std::lock_guard<std::mutex> lk(reg.mu);
    tray = reg.target;
    dispatcher = reg.dispatcher;
  }
  if (!tray) return;
  if (dispatcher) {
    dispatcher([tray, tun, sysproxy] {
      auto& r = global_tray_registry();
      std::lock_guard<std::mutex> lk(r.mu);
      if (r.target == tray && r.target != nullptr) {
        r.target->update_icon(tun, sysproxy);
      }
    });
    return;
  }
  std::lock_guard<std::mutex> lk(reg.mu);
  if (reg.target == tray && reg.target != nullptr) {
    reg.target->update_icon(tun, sysproxy);
  }
}

void Tray::update_lite_mode_menu(bool in_lite_mode) {
  lite_mode_checked_ = in_lite_mode;
}

bool Tray::check_click_debounce(int64_t now_ms) {
  if (now_ms < 0) {
    now_ms = static_cast<int64_t>(GetTickCount64());
  }
  if (now_ms - last_click_ms_ < static_cast<int64_t>(kTRAY_CLICK_DEBOUNCE_MS)) {
    CWLOG_INFO("Tray click debounced (< 300ms)");
    return false;
  }
  last_click_ms_ = now_ms;
  return true;
}

bool Tray::notify_silent_running(const std::string& lang, int64_t now_ms) {
  if (now_ms < 0) {
    now_ms = static_cast<int64_t>(GetTickCount64());
  }
  if (last_silent_balloon_ms_ != 0 && now_ms - last_silent_balloon_ms_ < 2000) {
    return false;
  }
  last_silent_balloon_ms_ = now_ms;
  if (!icon_added_ || !backend_ || !hwnd_) return false;

  std::wstring title;
  std::wstring body;
  if (lang == "zhtw" || lang == "zh-TW" || lang == "zh-HK") {
    title = L"Clash Win \u5df2\u5728\u80cc\u666f\u975c\u9ed8\u57f7\u884c";
    body = L"\u70ba\u9632\u5b89\u5168\u8edf\u9ad4\u8aa4\u6bba\u672a\u5f48\u51fa\u4e3b\u8996\u7a97\uff1b\u5982\u9700\u958b\u555f\u4ecb\u9762\uff0c\u8acb\u5de6\u9375\u9ede\u64ca\u53f3\u4e0b\u89d2\u7cfb\u7d71\u5323\u5716\u793a\u3002";
  } else if (lang.empty() || lang == "zh" || lang == "zh-CN") {
    title = L"Clash Win \u5df2\u5728\u540e\u53f0\u9759\u9ed8\u8fd0\u884c";
    body = L"\u4e3a\u9632\u5b89\u5168\u8f6f\u4ef6\u8bef\u6740\u672a\u5f39\u51fa\u4e3b\u7a97\u53e3\uff1b\u5982\u9700\u6253\u5f00\u754c\u9762\uff0c\u8bf7\u5de6\u952e\u70b9\u51fb\u53f3\u4e0b\u89d2\u6258\u76d8\u56fe\u6807\u3002";
  } else {
    title = L"Clash Win is running silently in tray";
    body = L"Main window stays hidden to prevent false-positive cleanup. Left-click the tray icon to open.";
  }
  return backend_->show_balloon(hwnd_, kTrayIconId, title, body);
}

void Tray::handle_tray_message(WPARAM /*wParam*/, LPARAM lParam) {
  if (is_exiting_check_ && is_exiting_check_()) {
    return;
  }
  if (lParam == WM_LBUTTONDOWN) {
    if (check_click_debounce()) {
      if (on_left_click_) on_left_click_();
    }
  } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
    POINT pt;
    GetCursorPos(&pt);
    if (backend_) {
      backend_->show_menu(hwnd_, pt, lite_mode_checked_, [this](int cmd) {
        if (is_exiting_check_ && is_exiting_check_()) return;
        if (cmd == kMenuLiteModeId) {
          if (check_click_debounce()) {
            if (on_toggle_lite_mode_) on_toggle_lite_mode_();
          }
        } else if (cmd == kMenuExitId) {
          // 退出操作绕过防抖直接触发
          if (on_exit_) on_exit_();
        }
      });
    }
  }
}

}  // namespace clashwin::system
