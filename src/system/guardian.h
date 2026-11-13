#pragma once

// ★ Windows 防误杀与自愈守护闭环（← Clash Mini src-tauri/src/process/guardian.rs，v3.3.0）
//
// 1. 伴生看门狗异名异目录物理隔离（clashwin-guard.exe / clashwin-guard-alt.exe）：
//    - 按需基于字节长度 + SHA-256 校验，经 .staging.<pid>.<seq>.exe 原子同步至
//      %LOCALAPPDATA%\io.github.clash-win.clash-win\guard\，脱离父 Job Object 仅走 Breakaway 独立拉起。
// 2. 主进程与看门狗双向互锁监督 + 独立日志与心跳（A1/A2/A6）：
//    - logs/guardian.log 结构化审计（失败回退 %TEMP%\clashwin-guard-fallback.log + OutputDebugStringW）
//    - main_heartbeat.json / guard_heartbeat.json（15s 刷新）
//    - 监督线程退场清零运行标记，CoreMonitor 周期调用 ensure_guardian_supervisor() 自愈重武装。
// 3. 双阶段主动退出凭证 intentional_exit.json + 内核事件带回读校验（A7/C1/C2）：
//    - prepared (30s TTL) -> confirmed (120s TTL)；cancel_intentional_exit 撤销并 ResetEvent。
//    - t=0 及 3500ms 安全延时后双重校验退出凭证 + 内核事件。
// 4. 无时间回退的 Boot/Epoch 累计熔断 + Fail-Closed 状态读写（A8/C3/C4/P0-6）：
//    - 移除 120s 稳态代数归零！同一开机/会话纪元累计上限 kMaxRapidRespawnGen = 3。
//    - 非干净重启状态下状态文件缺失或 JSON 损坏一律 Fail-Closed 拒绝拉起。
// 5. A5 形态保真：非轻量态崩溃按 GUI 形态恢复，轻量态崩溃携带 `--silent` 恢复至托盘。

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"
#include "util/result.h"

namespace clashwin::system::guardian {

/// 同一开机与会话纪元内允许自愈拉起的最大累计次数（严格熔断，无 120s/3600s 自动归零）
inline constexpr uint32_t kMaxRapidRespawnGen = 3;
/// 检测到异常强杀后延迟拉起的安全缓冲毫秒数（v3.3.0：3500ms 覆盖卸载器/安装器/关机瞬态）
inline constexpr uint32_t kRespawnSafetyDelayMs = 3500;
/// 伴生看门狗与宿主独立诊断心跳刷新间隔毫秒数
inline constexpr uint32_t kHeartbeatIntervalMs = 15000;
/// 主动退出凭证有效期：`prepared` 30s / `confirmed` 120s
inline constexpr uint64_t kIntentionalExitProofTtlMs = 30000;
inline constexpr uint64_t kIntentionalExitConfirmedTtlMs = 120000;
/// 伴生看门狗稳定存活超过该秒数后被杀，快速重建退避计数归零
inline constexpr uint64_t kGuardRapidDeathWindowSecs = 60;
/// 伴生看门狗首次或稳态后意外终止的基础重建延迟毫秒数
inline constexpr uint64_t kGuardRebuildBaseDelayMs = 1500;
/// 1/255（约 0.39% 不透明度）：肉眼完全不可见，同时令 GetLayeredWindowAttributes 返回非零 bAlpha
inline constexpr uint8_t kSentinelVisibleAlpha = 1;

/// 物理隔离目录与持久化文件名
inline constexpr const wchar_t* kIsolatedGuardianBinaryName = L"clashwin-guard.exe";
inline constexpr const wchar_t* kIsolatedGuardianFallbackName = L"clashwin-guard-alt.exe";
inline constexpr const wchar_t* kGuardianLogFilename = L"guardian.log";
inline constexpr const wchar_t* kMainHeartbeatFilename = L"main_heartbeat.json";
inline constexpr const wchar_t* kGuardHeartbeatFilename = L"guard_heartbeat.json";
inline constexpr const wchar_t* kIntentionalExitFilename = L"intentional_exit.json";
inline constexpr const wchar_t* kRespawnStateFilename = L"respawn_state.json";
inline constexpr const char* kBrokerBreakawayJob = "breakaway_from_job";

struct GuardianArgs {
  uint32_t parent_pid = 0;
  std::filesystem::path target_exe;
  std::filesystem::path log_dir;
  std::filesystem::path guard_dir;
  uint16_t singleton_port = 33335;
  uint32_t generation = 0;
  std::string session_id;
  std::string session_epoch;
  std::string broker;
};

struct RespawnInfo {
  uint32_t generation = 1;
  uint32_t prev_pid = 0;
  uint32_t prev_exit = 0;
  bool silent = false;
  std::string session_epoch;
};

struct SentinelWindowPair {
  void* owner_hwnd = nullptr;     // HWND
  void* sentinel_hwnd = nullptr;  // HWND
};

enum class IntentionalProofStatus {
  NotPresent,
  ValidPrepared,
  ValidConfirmed,
  Rejected,
};

struct IntentionalProofCheck {
  IntentionalProofStatus status = IntentionalProofStatus::NotPresent;
  std::string reason;
  std::string detail;
};

struct RespawnSessionState {
  std::string session_epoch_id;
  std::string boot_fingerprint;
  uint32_t respawn_count = 0;
  uint64_t last_respawn_unix_ms = 0;
  uint32_t last_parent_pid = 0;
};

// ---------- 纯函数与命令行解析（供运行期与单元测试共用） ----------

std::optional<GuardianArgs> parse_guardian_args(const std::vector<std::wstring>& args);
std::optional<RespawnInfo> parse_respawn_info(const std::vector<std::wstring>& args);
std::optional<std::string> parse_respawn_session_epoch(const std::vector<std::wstring>& args);
bool is_guardian_respawn_args(const std::vector<std::wstring>& args);
bool is_guardian_silent_respawn_args(const std::vector<std::wstring>& args);

/// 当前进程是否由伴生看门狗通过 `--guardian-respawn` 拉起（含前台 GUI 与静默托盘形态）
bool is_guardian_respawn();
/// 当前进程是否为伴生看门狗的轻量/静默托盘复活启动（`--guardian-respawn` 且携带 `--silent`）
bool is_guardian_silent_respawn();

/// 在同一开机与会话纪元内计算下一代自愈计数（累计至 3 次熔断，无时间回退清零）
std::optional<uint32_t> compute_next_generation(uint32_t current_gen);

/// 基于持久会话状态、session_epoch_id 与 OS 启动指纹评估自愈预算
std::optional<uint32_t> evaluate_respawn_budget(const RespawnSessionState& state,
                                                uint32_t cli_gen,
                                                std::string_view expected_epoch_id,
                                                std::string_view boot_fingerprint);

/// 当前系统开机指纹（`boot:<unix_secs>`）及 ±30s 容差比较
std::string current_boot_fingerprint();
bool boot_fingerprints_match(std::string_view saved, std::string_view current);

/// 根据伴生看门狗短时间连续异常死亡次数计算重建退避毫秒数（1500ms -> 2s -> 5s -> 15s -> 30s 封顶）
uint64_t compute_guard_rebuild_backoff_ms(uint32_t rapid_deaths);

/// 解析物理隔离看门狗驻留目录（优先 %LOCALAPPDATA%\io.github.clash-win.clash-win\guard）
std::filesystem::path resolve_isolated_guardian_dir();

/// 将主程序按需经 .staging 原子同步至隔离目录（字节长度 + SHA-256 一致则复用；锁定时落入 clashwin-guard-alt.exe）
std::optional<std::filesystem::path> sync_guardian_binary_to_dir(
    const std::filesystem::path& main_exe,
    const std::filesystem::path& guard_dir);

/// 原子写入 JSON 文件（<target>.tmp.<pid>.<seq> + FlushFileBuffers + MoveFileExW 重试）
bool write_json_atomic(const std::filesystem::path& target, const nlohmann::json& payload);

/// 将 Windows 进程退出码格式化为人类可读的取证诊断文本
std::string format_exit_code_diagnosis(uint32_t code);

/// 向 <log_dir>\clashwin.log 写入取证记录
void append_forensic_log(const std::filesystem::path& log_dir,
                         const char* level,
                         const std::string& message);

/// 向 <log_dir>\guardian.log 写入结构化审计日志（失败回退 %TEMP%\clashwin-guard-fallback.log + OutputDebugStringW）
void append_structured_guardian_log(const std::filesystem::path& log_dir,
                                    const char* level,
                                    std::string_view session_id,
                                    uint32_t parent_pid,
                                    std::optional<uint32_t> guard_pid,
                                    uint32_t gen,
                                    std::string_view event,
                                    std::string_view message);

/// 双阶段主动退出凭证读写与校验（供生产与单元测试共用）
bool write_intentional_exit_proof_in(const std::filesystem::path& guard_dir,
                                     uint32_t pid,
                                     std::string_view session_id,
                                     std::string_view phase,
                                     std::string_view reason,
                                     uint64_t now_unix_ms);
IntentionalProofCheck evaluate_intentional_exit_proof_in(const std::filesystem::path& guard_dir,
                                                        uint32_t parent_pid,
                                                        std::string_view session_id,
                                                        uint64_t now_unix_ms);
void clear_intentional_exit_proof_in(const std::filesystem::path& guard_dir);

/// Fail-Closed 复活预算状态读写与干净冷启动复位（供生产与单元测试共用）
void reset_respawn_state_for_clean_start_in(const std::filesystem::path& guard_dir,
                                            const std::filesystem::path& log_dir,
                                            std::string_view session_epoch_id,
                                            std::string_view boot_fingerprint);
Result<std::optional<uint32_t>> evaluate_and_record_respawn_in(
    const std::filesystem::path& guard_dir,
    std::string_view epoch_id,
    uint32_t cli_gen,
    uint32_t parent_pid,
    uint64_t now_unix_ms,
    std::string_view boot_fingerprint,
    bool commit);

/// 命名内核事件名（Local\ClashWinIntentionalExit_<pid> / Local\ClashWinInLightweight_<pid>）
std::wstring intentional_exit_event_name(uint32_t pid);
std::wstring lightweight_mode_event_name(uint32_t pid);

// ---------- 第一层：常驻零渲染防清理哨兵窗口对 ----------

std::optional<SentinelWindowPair> create_sentinel_window_pair(
    const wchar_t* class_name,
    const wchar_t* title = L"Clash Win Tray Host");
void destroy_sentinel_window_pair(SentinelWindowPair& pair);

void init_resident_sentinel_window();
void destroy_resident_sentinel_window();

// ---------- 第二层：伴生看门狗进程与双向/带外信令 ----------

bool ensure_guardian_events(bool initial_lightweight);

void start_guardian_companion(const std::filesystem::path& log_dir,
                              uint16_t singleton_port,
                              bool initial_lightweight);

/// 幂等确保伴生看门狗监督线程处于运行态（由 CoreMonitor 周期巡检调用）
void ensure_guardian_supervisor();

/// 若距上次刷新已达 15 秒，落盘宿主诊断心跳 main_heartbeat.json
void record_main_heartbeat_if_due();

/// 同步更新轻量/后台静默驻留信号（true = 复活时带 `--silent`，false = 复活时恢复 GUI 窗口）
void set_lightweight_mode_signal(bool in_lightweight);

/// 双阶段主动退出信号：prepare (prepared 30s TTL) / confirm (confirmed 120s TTL) / cancel
void notify_intentional_exit();
void notify_intentional_exit_with_reason(std::string_view reason);
void confirm_intentional_exit_with_reason(std::string_view reason);
void cancel_intentional_exit(std::string_view reason);

/// 返回看门狗与 Breakaway/隔离状态简报（供 A4 诊断包与 system-info.txt 展示）
std::string get_diagnostic_summary();

/// 互斥共存协作：终止对端进程前置位其 IntentionalExit 事件
bool signal_foreign_guardian_exit(uint32_t target_pid);

/// wWinMain 入口早退：`--guardian` 模式执行守护阻塞循环并返回退出码；否则返回 nullopt
std::optional<int> maybe_run_guardian_mode();

}  // namespace clashwin::system::guardian
