#include "ui/help_menu.h"

#include <shellapi.h>
#include <mutex>
#include <thread>

#include "cmd/command_service.h"
#include "events/event_bus.h"
#include "events/notice.h"
#include "util/log.h"
#include "util/utf.h"
#include "ui/i18n_text.h"  // F-D01

namespace clashwin::ui {

namespace {

std::mutex g_help_mu;
HelpMenu::ActionHandler g_action_handler;

// 命令型菜单项一律在后台执行：这些动作会走网络/重 IO（check_app_update 的
// manifest 拉取、export_diagnostic_bundle 的 zip 打包），在 UI 线程同步等待
// 会把消息循环卡死（WER AppHangB1）。此处独立线程执行并记录结果。
void run_command_async(const char* tag, std::function<void()> fn) {
  const std::string name = tag;
  std::thread([name, fn = std::move(fn)]() {
    try {
      fn();
      CWLOG_INFO("help action done: " + name);
    } catch (const std::exception& ex) {
      CWLOG_WARN("help action exception: " + name + ": " + ex.what());
    } catch (...) {
      CWLOG_WARN("help action exception: " + name);
    }
  }).detach();
}

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
      ShellExecuteW(hwnd, L"open", L"https://github.com/qiu-yuxiao/clash-win", nullptr, nullptr, SW_SHOWNORMAL);
      break;
    case HelpAction::OpenWiki:
      ShellExecuteW(hwnd, L"open", L"https://github.com/qiu-yuxiao/clash-win/wiki", nullptr, nullptr, SW_SHOWNORMAL);
      break;
    case HelpAction::ReviewGuide:
      events::EventBus::instance().publish("ui:open_guide", "");
      break;
    case HelpAction::AntivirusGuide:
      events::EventBus::instance().publish("ui:open_antivirus_guide", "");
      break;
    case HelpAction::ExportDiagnostics: {
      // ← help-menu-button.tsx:156-180：常驻 info「正在生成诊断包」→ success「已导出：{{path}}」
      //   （隐藏 info 在 finally 里，与源码 hideNotice(waitId) 同序）
      const uint64_t wait_id = events::NoticeService::instance().push(
          "info", tr8("layout.components.helpMenu.generatingDiagnostics"), 0);
      run_command_async("export_diagnostic_bundle", [wait_id] {
        auto res = cmd::CommandService::instance().invoke("export_diagnostic_bundle", {});
        if (!res.is_ok()) {
          CWLOG_WARN("export_diagnostic_bundle failed: " + res.error().message);
          events::NoticeService::instance().hide(wait_id);
          events::NoticeService::instance().push("error", res.error().message);
          return;
        }
        if (res.value().is_string()) {
          std::string p = res.value().get<std::string>();
          events::NoticeService::instance().hide(wait_id);
          events::NoticeService::instance().push(
              "success",
              tr8("layout.components.helpMenu.diagnosticsExported", {{"path", p}}));
          (void)cmd::CommandService::instance().invoke("show_in_folder", {{"path", p}});
          return;
        }
        events::NoticeService::instance().hide(wait_id);
      });
      break;
    }
    case HelpAction::CheckAppUpdate:
      run_command_async("check_app_update", [] {
        auto res = cmd::CommandService::instance().invoke("check_app_update", {});
        if (!res.is_ok()) CWLOG_WARN("check_app_update failed: " + res.error().message);
      });
      break;
    case HelpAction::CheckCoreUpdate:
      run_command_async("check_core_update", [] {
        auto res = cmd::CommandService::instance().invoke("check_core_update", {});
        if (!res.is_ok()) CWLOG_WARN("check_core_update failed: " + res.error().message);
      });
      break;
    case HelpAction::OpenConfigDir:
      run_command_async("open_app_dir", [] {
        (void)cmd::CommandService::instance().invoke("open_app_dir", {});
      });
      break;
    case HelpAction::OpenLogsDir:
      run_command_async("open_logs_dir", [] {
        (void)cmd::CommandService::instance().invoke("open_logs_dir", {});
      });
      break;
  }
}

HMENU HelpMenu::create_menu_handle() {
  HMENU menu = CreatePopupMenu();
  if (!menu) return nullptr;

  // src: src/pages/_layout/components/help-menu-button.tsx:114-205 7 项 + 2 分隔线；文案 locales/zh/layout.json:32-39
  AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HelpAction::StarGithub),
              (L"⭐ " + tr("layout.components.helpMenu.github")).c_str());
  AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HelpAction::OpenWiki),
              (L"💡 " + tr("layout.components.helpMenu.wiki")).c_str());
  AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HelpAction::ReviewGuide),
              (L"🧭 " + tr("layout.components.helpMenu.firstRunGuide")).c_str());
  AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HelpAction::AntivirusGuide),
              (L"🛡️ " + tr("layout.components.antivirus.menuLabel")).c_str());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HelpAction::ExportDiagnostics),
              (L"📦 " + tr("layout.components.helpMenu.exportDiagnostics")).c_str());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HelpAction::CheckAppUpdate),
              (L"🚀 " + tr("layout.components.helpMenu.checkAppUpdate")).c_str());
  AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HelpAction::CheckCoreUpdate),
              (L"⚙️ " + tr("layout.components.helpMenu.checkCoreUpdate")).c_str());

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
