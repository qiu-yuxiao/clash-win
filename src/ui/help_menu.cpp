#include "ui/help_menu.h"

#include <shellapi.h>
#include <mutex>

#include "cmd/command_service.h"
#include "events/event_bus.h"
#include "util/utf.h"

namespace clashwin::ui {

namespace {

std::mutex g_help_mu;
HelpMenu::ActionHandler g_action_handler;

}  // namespace

void HelpMenu::set_action_handler_for_testing(ActionHandler handler) {
  std::lock_guard<std::mutex> lock(g_help_mu);
  g_action_handler = std::move(handler);
}

void HelpMenu::execute_action(HelpAction action, HWND hwnd) {
  {
    std::lock_guard<std::mutex> lock(g_help_mu);
    if (g_action_handler) {
      g_action_handler(action, hwnd);
      return;
    }
  }

  switch (action) {
    case HelpAction::StarGithub:
      ShellExecuteW(hwnd, L"open", L"https://github.com/qiu-yuxiao/clash-mini", nullptr, nullptr, SW_SHOWNORMAL);
      break;
    case HelpAction::OpenWiki:
      ShellExecuteW(hwnd, L"open", L"https://github.com/qiu-yuxiao/clash-mini/wiki", nullptr, nullptr, SW_SHOWNORMAL);
      break;
    case HelpAction::ReviewGuide:
      events::EventBus::instance().publish("ui:open_guide", "");
      break;
    case HelpAction::ExportDiagnostics: {
      auto res = cmd::CommandService::instance().invoke("export_diagnostic_bundle", {});
      if (res.is_ok() && res.value().is_string()) {
        std::string p = res.value().get<std::string>();
        (void)cmd::CommandService::instance().invoke("show_in_folder", {{"path", p}});
      }
      break;
    }
    case HelpAction::CheckAppUpdate:
      (void)cmd::CommandService::instance().invoke("check_app_update", {});
      break;
    case HelpAction::CheckCoreUpdate:
      (void)cmd::CommandService::instance().invoke("check_core_update", {});
      break;
    case HelpAction::OpenConfigDir:
      (void)cmd::CommandService::instance().invoke("open_app_dir", {});
      break;
    case HelpAction::OpenLogsDir:
      (void)cmd::CommandService::instance().invoke("open_logs_dir", {});
      break;
  }
}

HMENU HelpMenu::create_menu_handle() {
  HMENU menu = CreatePopupMenu();
  if (!menu) return nullptr;

  AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HelpAction::StarGithub), L"⭐ 去 GitHub 点亮 Star");
  AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HelpAction::OpenWiki), L"💡 Wiki");
  AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HelpAction::ReviewGuide), L"🧭 重看向导");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HelpAction::ExportDiagnostics), L"📦 导出诊断信息");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HelpAction::CheckAppUpdate), L"🚀 检查客户端更新");
  AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HelpAction::CheckCoreUpdate), L"⚙️ 检查内核更新");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HelpAction::OpenConfigDir), L"📂 打开配置目录");
  AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HelpAction::OpenLogsDir), L"📝 打开日志目录");

  return menu;
}

void HelpMenu::show_popup(HWND hwnd, POINT pt) {
  HMENU menu = create_menu_handle();
  if (!menu) return;

  int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
  DestroyMenu(menu);

  if (cmd > 0) {
    execute_action(static_cast<HelpAction>(cmd), hwnd);
  }
}

}  // namespace clashwin::ui
