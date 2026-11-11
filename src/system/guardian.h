#pragma once

// ★ Windows 双层防误杀与零断流自愈守护（← Clash Mini src-tauri/src/utils/guardian.rs，v3.2.5 BUG-316）
//
// 第一层（事前免疫）：常驻零渲染防清理哨兵窗口对（owner 隐藏 + sentinel WS_POPUP|WS_VISIBLE alpha=0）
//   - 绕过 360 加速球（360perfoptm2.dll DoOptmGroupResidual / FastOptm）等第三方清理工具的
//     「无可见顶层窗口后台进程 → TerminateProcess(hProcess, 99)」误杀规则；
//   - 被隐藏 owner 持有，且不含 WS_EX_APPWINDOW / WS_EX_TOOLWINDOW / WS_EX_TOPMOST：
//     既满足 IsWindowVisible != 0 且不被判为悬浮工具窗，又绝不出现在任务栏与 Alt+Tab。
//
// 第二层（事后兜底）：超轻量级伴生看门狗进程（--guardian）+ 零断流自愈重连（--guardian-respawn）
//   - 伴生进程在 wWinMain 最早期拦截，不初始化 D2D/UI/网络栈，阻塞于内核对象 WaitForMultipleObjects，0% CPU；
//   - 主进程正常退出 / 系统关机 / 对方互斥接管前先置位 Local\ClashWinIntentionalExit_<pid>，伴生进程 0ms 退出；
//   - 主进程在后台轻量/托盘态被意外强杀时：同步落盘退出码取证日志（clashwin.log）→ 多重安全门禁 →
//     静默拉起 --guardian-respawn 恢复托盘与 CoreMonitor，系统服务内核存活时零断流无感挂载。

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace clashwin::system::guardian {

/// 短时间内允许连续快速自愈拉起的最大代数（防止启动即崩导致无限拉起风暴）
inline constexpr uint32_t kMaxRapidRespawnGen = 3;
/// 主进程稳定存活超过该秒数后，自愈代数清零重新计算
inline constexpr uint64_t kStableUptimeResetSecs = 60;
/// 检测到异常强杀后延迟拉起的缓冲毫秒数（避开卸载器/安装器/关机序列瞬态）
inline constexpr uint32_t kRespawnSafetyDelayMs = 1500;

struct GuardianArgs {
  uint32_t parent_pid = 0;
  std::filesystem::path log_dir;
  uint16_t singleton_port = 33335;
  uint32_t generation = 0;
};

struct RespawnInfo {
  uint32_t generation = 1;
  uint32_t prev_pid = 0;
  uint32_t prev_exit = 0;
};

struct SentinelWindowPair {
  void* owner_hwnd = nullptr;     // HWND
  void* sentinel_hwnd = nullptr;  // HWND
};

// ---------- 纯函数与命令行解析（供运行期与单元测试共用） ----------

std::optional<GuardianArgs> parse_guardian_args(const std::vector<std::wstring>& args);
std::optional<RespawnInfo> parse_respawn_info(const std::vector<std::wstring>& args);
bool is_guardian_respawn_args(const std::vector<std::wstring>& args);

/// 当前进程是否由伴生看门狗通过 `--guardian-respawn` 拉起
bool is_guardian_respawn();

/// 计算下一次自愈拉起代数（稳定运行 ≥60s 重置为 1；超过 kMaxRapidRespawnGen 返回 nullopt）
std::optional<uint32_t> compute_next_generation(uint32_t current_gen, uint64_t uptime_secs);

/// 将 Windows 进程退出码格式化为人类可读的取证诊断文本（← guardian.rs:509-520）
std::string format_exit_code_diagnosis(uint32_t code);

/// 不依赖主进程日志器，直接以原子追加模式向 <log_dir>\clashwin.log 写入取证记录并刷盘
void append_forensic_log(const std::filesystem::path& log_dir,
                         const char* level,
                         const std::string& message);

/// 命名内核事件名（Local\ClashWinIntentionalExit_<pid> / Local\ClashWinInLightweight_<pid>）
std::wstring intentional_exit_event_name(uint32_t pid);
std::wstring lightweight_mode_event_name(uint32_t pid);

// ---------- 第一层：常驻零渲染防清理哨兵窗口对 ----------

/// 按给定窗口类名创建一对防清理哨兵窗口（测试可传独立类名校验 Win32 属性不变量）
std::optional<SentinelWindowPair> create_sentinel_window_pair(const wchar_t* class_name);
void destroy_sentinel_window_pair(SentinelWindowPair& pair);

/// 在主线程（UI 线程）创建全生命周期常驻哨兵窗口（幂等）
void init_resident_sentinel_window();
/// 主进程退出前销毁常驻哨兵窗口
void destroy_resident_sentinel_window();

// ---------- 第二层：伴生看门狗进程与内核事件信令 ----------

/// 初始化当前进程的命名内核事件句柄（即使禁用子进程拉起也可供测试验证信令状态）
bool ensure_guardian_events(bool initial_lightweight);

/// 启动伴生看门狗子进程（可通过环境变量 CLASHWIN_DISABLE_GUARDIAN=1 显式禁用）
void start_guardian_companion(const std::filesystem::path& log_dir,
                              uint16_t singleton_port,
                              bool initial_lightweight);

/// 同步更新轻量/后台驻留状态信号（true = 允许意外强杀后自愈拉起；false = 仅记录取证日志）
void set_lightweight_mode_signal(bool in_lightweight);

/// 标记主进程即将主动退出（正常退出 / 系统关机 / 互斥让位），使伴生看门狗 0ms 同步退出
void notify_intentional_exit();

/// 互斥共存协作：在终止对方进程（如 Clash Mini v3.2.5+）前，礼貌置位其 IntentionalExit 事件，
/// 避免对方伴生看门狗误记崩溃日志或尝试反向拉起。返回是否成功置位至少一个事件。
bool signal_foreign_guardian_exit(uint32_t target_pid);

/// 在 wWinMain 最早期调用：若命令行含 `--guardian` 则直接进入超轻量守护阻塞态并返回退出码；
/// 正常主进程调用时立即返回 std::nullopt。
std::optional<int> maybe_run_guardian_mode();

}  // namespace clashwin::system::guardian
