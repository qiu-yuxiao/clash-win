#pragma once

#include <filesystem>
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

// BUG-414：两档迁移标记。全部清净写 done；存在普通权限删不掉的 Admin 任务写 pending_admin，
// 留待提权启动时补删。历史 "1" 视为未完成迁移（重新检查一次）。
inline constexpr const char* kMarkerDone = "done";
inline constexpr const char* kMarkerPendingAdmin = "pending_admin";

// BUG-414 纯函数：根据标记内容与当前是否管理员决定是否跳过清理
bool should_skip_cleanup_from_marker(const std::optional<std::string>& marker_content, bool is_admin);

// BUG-414 纯函数：仅当 Tasks 父目录可读存在且子条目确定 NotFound 时返回 true；
// 无法判断（目录不存在 / 权限拒绝）保守返回 false，交由 schtasks 处理。
bool is_task_file_definitely_absent(const std::optional<std::filesystem::path>& tasks_dir,
                                    const std::wstring& task_name);

// 注册表 Run 键路径与值名称
inline constexpr const wchar_t* kRunKeySubPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr const wchar_t* kRunValueName = L"Clash Win";

// ★ ❌6 StartupApproved\Run 键（任务管理器「禁用启动项」的真实状态位）：
//   启用自启时同步写「已批准启用」blob，使配置覆盖任务管理器的禁用（← Mini §2.10.1）。
inline constexpr const wchar_t* kStartupApprovedKeySubPath =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

// 抽象接口：隔离 Windows 注册表与 schtasks 子进程操作
class IAutostartAccess {
 public:
  virtual ~IAutostartAccess() = default;
  virtual Result<bool> is_enabled() = 0;
  virtual Result<void> enable(const std::wstring& cmd) = 0;
  virtual Result<void> disable() = 0;
  virtual Result<bool> delete_scheduled_task(const std::wstring& task_name) = 0;
  virtual std::wstring current_exe_path() = 0;
  // BUG-414：当前进程是否管理员（默认 false，测试替身可覆盖）
  virtual bool is_admin() { return false; }
  // BUG-414：%SystemRoot%\System32\Tasks（默认 nullopt = 无法判断，交由 schtasks）
  virtual std::optional<std::filesystem::path> system_tasks_dir() { return std::nullopt; }
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
