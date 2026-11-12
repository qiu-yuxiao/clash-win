#pragma once

// ★ Windows 轻量模式防强杀四重闭环守护（← Clash Mini src-tauri/src/process/guardian.rs，v3.2.5 BUG-316 + BUG-363）
//
// 1. 伴生看门狗异名异目录物理隔离（clashwin-guard.exe / clashwin-guard-alt.exe）：
//    - 按需经 .staging 原子同步至 %LOCALAPPDATA%\io.github.clash-win.clash-win\guard\，传入 --target-exe
//      并脱离父 Job Object 独立拉起，彻底免疫 360 加速球按 clashwin.exe 应用组连坐双杀，且不锁安装目录。
// 2. 主进程与看门狗双向互锁监督（0% CPU 内核阻塞）：
//    - 主进程通过独立监督线程（WaitForMultipleObjects([child, exit_event], INFINITE)）反向监控看门狗死活，
//      正常退出 0ms 唤醒；看门狗若遭外部误杀则记录退出码并按指数退避（1.5s~30s）自动重建。
// 3. 主进程与看门狗双端挂载工作区哨兵窗口对（owner 屏外隐藏 + sentinel 主屏工作区原点 1×1）：
//    - WS_POPUP | WS_VISIBLE + WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE，LWA_ALPHA = 1（1/255 肉眼不可见）、
//      非空标题、WM_NCHITTEST -> HTTRANSPARENT 完全穿透，既落入真实显示器工作区（MonitorFromWindow != NULL），
//      又绝不出现在任务栏与 Alt+Tab；看门狗侧以 MsgWaitForMultipleObjects(..., QS_ALLINPUT) 泵消息防 Hung 判死。
// 4. 120s 稳态代数复位与零断流自愈重挂（--guardian-respawn）：
//    - 稳定运行满 120s 后若遭外部强杀自动将 --gen 重置为 1，兼顾启动崩溃 3 次熔断与 7×24h 无限次静默托盘自愈。

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace clashwin::system::guardian {

/// 短时间内允许连续快速自愈拉起的最大代数（防止启动即崩导致无限拉起风暴）
inline constexpr uint32_t kMaxRapidRespawnGen = 3;
/// 主进程稳定存活超过该秒数（120s）后，自愈代数重置为 1（支持 7×24h 无限次外部清理自愈）
inline constexpr uint64_t kStableUptimeResetSecs = 120;
/// 检测到异常强杀后延迟拉起的缓冲毫秒数（避开卸载器/安装器/关机序列瞬态）
inline constexpr uint32_t kRespawnSafetyDelayMs = 1500;
/// 伴生看门狗稳定存活超过该秒数后被杀，快速重建退避计数归零
inline constexpr uint64_t kGuardRapidDeathWindowSecs = 60;
/// 伴生看门狗首次或稳态后意外终止的基础重建延迟毫秒数
inline constexpr uint64_t kGuardRebuildBaseDelayMs = 1500;
/// 1/255（约 0.39% 不透明度）：肉眼完全不可见，同时令 GetLayeredWindowAttributes 返回非零 bAlpha
inline constexpr uint8_t kSentinelVisibleAlpha = 1;

/// 物理隔离目录中的伴生看门狗主/备可执行文件名（与安装目录 clashwin.exe 异名异目录）
inline constexpr const wchar_t* kIsolatedGuardianBinaryName = L"clashwin-guard.exe";
inline constexpr const wchar_t* kIsolatedGuardianFallbackName = L"clashwin-guard-alt.exe";

struct GuardianArgs {
  uint32_t parent_pid = 0;
  std::filesystem::path target_exe;
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

/// 计算下一次自愈拉起代数（稳定运行 ≥120s 重置为 1；超过 kMaxRapidRespawnGen 返回 nullopt）
std::optional<uint32_t> compute_next_generation(uint32_t current_gen, uint64_t uptime_secs);

/// 根据伴生看门狗短时间连续异常死亡次数计算重建退避毫秒数（1500ms -> 2s -> 5s -> 15s -> 30s 封顶）
uint64_t compute_guard_rebuild_backoff_ms(uint32_t rapid_deaths);

/// 解析物理隔离看门狗驻留目录（优先 %LOCALAPPDATA%\io.github.clash-win.clash-win\guard）
std::filesystem::path resolve_isolated_guardian_dir();

/// 将主程序按需经 .staging 原子同步至隔离目录（clashwin-guard.exe，锁定时落入 clashwin-guard-alt.exe）
std::optional<std::filesystem::path> sync_guardian_binary_to_dir(
    const std::filesystem::path& main_exe,
    const std::filesystem::path& guard_dir);

/// 将 Windows 进程退出码格式化为人类可读的取证诊断文本（← guardian.rs:292-303）
std::string format_exit_code_diagnosis(uint32_t code);

/// 不依赖主进程日志器，直接以原子追加模式向 <log_dir>\clashwin.log 写入取证记录并刷盘
void append_forensic_log(const std::filesystem::path& log_dir,
                         const char* level,
                         const std::string& message);

/// 命名内核事件名（Local\ClashWinIntentionalExit_<pid> / Local\ClashWinInLightweight_<pid>）
std::wstring intentional_exit_event_name(uint32_t pid);
std::wstring lightweight_mode_event_name(uint32_t pid);

// ---------- 第一层：常驻零渲染防清理哨兵窗口对 ----------

/// 按给定窗口类名与标题创建一对工作区防清理哨兵窗口（测试可传独立类名校验 Win32 属性不变量）
std::optional<SentinelWindowPair> create_sentinel_window_pair(
    const wchar_t* class_name,
    const wchar_t* title = L"Clash Win Tray Host");
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
