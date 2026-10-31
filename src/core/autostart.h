#pragma once

#include <optional>
#include <string>
#include <vector>

#include "util/result.h"

namespace clashwin::core {

// 历史计划任务名（2025-12-26 起使用的 schtasks 方案）
inline constexpr const wchar_t* kLegacyTaskNames[] = {
    L"Clash Win",
    L"Clash Win (Admin)"
};

// 迁移完成标记文件（放在 app_home 下，与配置数据同生命周期）
inline constexpr const wchar_t* kAutostartMigrationMarker = L".autostart_task_migrated";

// 注册表 Run 键路径与值名称
inline constexpr const wchar_t* kRunKeySubPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr const wchar_t* kRunValueName = L"Clash Win";

// 抽象接口：隔离 Windows 注册表与 schtasks 子进程操作
class IAutostartAccess {
 public:
  virtual ~IAutostartAccess() = default;
  virtual Result<bool> is_enabled() = 0;
  virtual Result<void> enable(const std::wstring& cmd) = 0;
  virtual Result<void> disable() = 0;
  virtual Result<bool> delete_scheduled_task(const std::wstring& task_name) = 0;
  virtual std::wstring current_exe_path() = 0;
};

// 生产实现（真实操作注册表与 schtasks）
IAutostartAccess* windows_autostart_access();

class AutostartManager {
 public:
  static AutostartManager& instance();

  // 依赖注入（默认注入 windows_autostart_access()）
  void set_access(IAutostartAccess* access);
  IAutostartAccess* access();

  // 更新开机自启状态（若 target 为 nullopt 则读取 verge.enable_auto_launch）
  Result<void> update_launch(std::optional<bool> enable_auto_launch = std::nullopt);

  // 查询当前实际自启状态（读取 Run 注册表键是否存在）
  Result<bool> get_launch_status();

  // 启动时对账：清空历史 schtasks 并按配置重写 Run 键
  Result<void> reconcile_on_startup();

  // 清理历史计划任务（幂等，落 .autostart_task_migrated 标记）
  void cleanup_legacy_scheduled_tasks();

  // 测试夹具辅助方法
  void reset_state();

 private:
  AutostartManager();
  IAutostartAccess* access_ = nullptr;
};

}  // namespace clashwin::core
