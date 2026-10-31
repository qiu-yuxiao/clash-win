#pragma once
// Tray（T19）：系统托盘图标管理
// - 三态图标：TUN > SysProxy > Default
// - Tooltip："Clash Win"
// - 300ms 左键防抖（TRAY_CLICK_DEBOUNCE_MS）
// - 2 项右键菜单："轻量模式 / Lite mode" 与 "退出 (Exit)"（退出绕过防抖）
// - ITrayBackend 抽象接口：解耦 Win32 Shell API，支持全内存纯单测

#include <windows.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace clashwin::system {

inline constexpr uint64_t kTRAY_CLICK_DEBOUNCE_MS = 300;
inline constexpr UINT kAppTrayMsg = WM_APP + 2;
inline constexpr UINT kTrayIconId = 1001;

inline constexpr int kMenuLiteModeId = 1001;
inline constexpr int kMenuExitId = 1002;

enum class TrayIconState {
  Default = 0,
  SysProxy = 1,
  Tun = 2
};

// 纯函数：根据 tun 与 sysproxy 状态计算图标类型
// 优先级：TUN > SysProxy > Default
inline TrayIconState calculate_state(bool tun, bool sysproxy) {
  if (tun) return TrayIconState::Tun;
  if (sysproxy) return TrayIconState::SysProxy;
  return TrayIconState::Default;
}

class ITrayBackend {
 public:
  virtual ~ITrayBackend() = default;
  virtual bool add_icon(HWND hwnd, UINT uID, UINT uCallbackMsg, HICON icon, const std::wstring& tip) = 0;
  virtual bool modify_icon(HWND hwnd, UINT uID, HICON icon, const std::wstring& tip) = 0;
  virtual bool delete_icon(HWND hwnd, UINT uID) = 0;
  virtual void show_menu(HWND hwnd, POINT pt, bool in_lite_mode, std::function<void(int cmd_id)> on_cmd) = 0;
};

class Win32TrayBackend : public ITrayBackend {
 public:
  ~Win32TrayBackend() override = default;
  bool add_icon(HWND hwnd, UINT uID, UINT uCallbackMsg, HICON icon, const std::wstring& tip) override;
  bool modify_icon(HWND hwnd, UINT uID, HICON icon, const std::wstring& tip) override;
  bool delete_icon(HWND hwnd, UINT uID) override;
  void show_menu(HWND hwnd, POINT pt, bool in_lite_mode, std::function<void(int cmd_id)> on_cmd) override;
};

class Tray {
 public:
  explicit Tray(std::unique_ptr<ITrayBackend> backend = nullptr);
  ~Tray();

  bool init(HWND hwnd);
  void destroy();

  void update_icon(bool tun, bool sysproxy);
  void update_icon_state(TrayIconState state);
  TrayIconState current_state() const { return current_state_; }

  void update_lite_mode_menu(bool in_lite_mode);
  bool is_lite_mode_checked() const { return lite_mode_checked_; }

  // 300ms 防抖检查：返回 true 表示通过（更新时间戳），false 表示被抑制
  bool check_click_debounce(int64_t now_ms = -1);

  // 处理窗口消息循环收到的 kAppTrayMsg (WM_APP + 2)
  void handle_tray_message(WPARAM wParam, LPARAM lParam);

  // 回调设置
  void set_on_left_click(std::function<void()> cb) { on_left_click_ = std::move(cb); }
  void set_on_toggle_lite_mode(std::function<void()> cb) { on_toggle_lite_mode_ = std::move(cb); }
  void set_on_exit(std::function<void()> cb) { on_exit_ = std::move(cb); }

  void set_backend(std::unique_ptr<ITrayBackend> backend) { backend_ = std::move(backend); }
  ITrayBackend* backend() const { return backend_.get(); }

  const std::wstring& tooltip() const { return tooltip_; }
  HWND hwnd() const { return hwnd_; }

 private:
  HICON get_icon_for_state(TrayIconState state);
  void load_icons();

  HWND hwnd_ = nullptr;
  std::unique_ptr<ITrayBackend> backend_;
  TrayIconState current_state_ = TrayIconState::Default;
  bool lite_mode_checked_ = false;
  bool icon_added_ = false;
  int64_t last_click_ms_ = 0;
  std::wstring tooltip_ = L"Clash Win";

  std::function<void()> on_left_click_;
  std::function<void()> on_toggle_lite_mode_;
  std::function<void()> on_exit_;

  HICON h_icon_default_ = nullptr;
  HICON h_icon_sys_ = nullptr;
  HICON h_icon_tun_ = nullptr;
};

}  // namespace clashwin::system
