#pragma once
// T25 帮助菜单与操作分发（← help-menu-button.tsx）
// 包含 8 项核心功能：
//   1. ⭐ 去 GitHub 点亮 Star
//   2. 💡 Wiki
//   3. 🧭 重看向导
//   4. 📦 导出诊断信息
//   5. 🚀 检查客户端更新
//   6. ⚙️ 检查内核更新
//   7. 📂 打开配置目录
//   8. 📝 打开日志目录
#include <windows.h>
#include <functional>

namespace clashwin::ui {

enum class HelpAction {
  StarGithub = 1,        // 1. ⭐ 去 GitHub 点亮 Star
  OpenWiki = 2,          // 2. 💡 Wiki
  ReviewGuide = 3,       // 3. 🧭 重看向导
  ExportDiagnostics = 4, // 4. 📦 导出诊断信息
  CheckAppUpdate = 5,    // 5. 🚀 检查客户端更新
  CheckCoreUpdate = 6,   // 6. ⚙️ 检查内核更新
  // Mini 菜单无「打开配置/日志目录」入口（help-menu-button.tsx:114-205 6 项），
  // 已从菜单移除；枚举保留给命令链/测试直调
  OpenConfigDir = 7,     // 非菜单项
  OpenLogsDir = 8,       // 非菜单项
  AntivirusGuide = 9     // 9. 🛡️ 安全软件白名单说明
};

class HelpMenu {
 public:
  static void execute_action(HelpAction action, HWND hwnd = nullptr);
  static HMENU create_menu_handle();
  static void show_popup(HWND hwnd, POINT pt);

  using ActionHandler = std::function<void(HelpAction, HWND)>;
  static void set_action_handler_for_testing(ActionHandler handler);
};

}  // namespace clashwin::ui
