#include "system/tray.h"
#include "util/log.h"

#include <shellapi.h>

namespace clashwin::system {

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

  const int cx_sm = GetSystemMetrics(SM_CXSMICON);
  const int cy_sm = GetSystemMetrics(SM_CYSMICON);
  auto load_one = [&](const std::wstring& rel_path) -> HICON {
    std::wstring full = exe_dir + L"\\" + rel_path;
    HICON h = static_cast<HICON>(
        LoadImageW(nullptr, full.c_str(), IMAGE_ICON,
                   cx_sm > 0 ? cx_sm : 16, cy_sm > 0 ? cy_sm : 16,
                   LR_LOADFROMFILE));
    if (!h) {
      h = static_cast<HICON>(
          LoadImageW(nullptr, full.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
    }
    return h;
  };

  h_icon_default_ = load_one(L"resources\\icons\\tray-icon.ico");
  h_icon_sys_     = load_one(L"resources\\icons\\tray-icon-sys.ico");
  h_icon_tun_     = load_one(L"resources\\icons\\tray-icon-tun.ico");

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

bool Tray::init(HWND hwnd) {
  hwnd_ = hwnd;
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
  current_state_ = state;
  if (icon_added_ && backend_ && hwnd_) {
    HICON icon = get_icon_for_state(current_state_);
    backend_->modify_icon(hwnd_, kTrayIconId, icon, tooltip_);
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

void Tray::handle_tray_message(WPARAM /*wParam*/, LPARAM lParam) {
  if (lParam == WM_LBUTTONUP) {
    if (check_click_debounce()) {
      if (on_left_click_) on_left_click_();
    }
  } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
    POINT pt;
    GetCursorPos(&pt);
    if (backend_) {
      backend_->show_menu(hwnd_, pt, lite_mode_checked_, [this](int cmd) {
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
