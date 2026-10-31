#pragma once
// 内核生命周期域常量（← phase1 02 附录常量速查 + 04§6 + 源码逐条标注）
// T07 唯一事实来源；T15/T16/T18/T22/T23/T24 直接引用，禁止各自再定义。
#include <cstdint>

namespace clashwin::core::consts {

// ---------- Sidecar 启动（← manager/state.rs:39-41 环境变量缺省值） ----------
inline constexpr const char* kGoMemLimitDefault = "128MiB";
inline constexpr const char* kGoGcDefault = "100";
inline constexpr const char* kGoMaxProcsDefault = "2";
// ← state.rs:56-63 启动参数（-d <home> -f <clash-win.yaml> -ext-ctl-pipe \\.\pipe\mini-mihomo）

// ---------- 服务等待（← constants.rs timing + service.rs wait_for_service_ipc） ----------
inline constexpr int kServiceWaitMaxMs = 30000;       // SERVICE_WAIT_MAX：就绪预算
inline constexpr int kServiceWaitIntervalMs = 200;    // 轮询间隔
// ← service.rs:372-384 IpcConfig（每请求重试，语义独立于就绪预算，勿混用）
inline constexpr int kServiceIpcTimeoutMs = 150;
inline constexpr int kServiceIpcRetryDelayMs = 250;
inline constexpr int kServiceIpcMaxRetries = 20;

// ---------- 校验（← validate.rs + constants.rs） ----------
inline constexpr int kValidateControlTimeoutMs = 10000;  // mini-mihomo -t 超时

// ---------- 内核合法性（← verge.rs VALID_CLASH_CORES / get_valid_clash_core） ----------
inline constexpr const char* kValidClashCores[] = {"mini-mihomo"};
inline constexpr const char* kDefaultClashCore = "mini-mihomo";

// ---------- 重启恢复预算（← lifecycle.rs::restore_proxy_group_now） ----------
inline constexpr int kApiReadyBudgetMs = 30000;     // 等 API 就绪（200ms 轮询）
inline constexpr int kApiReadyPollMs = 200;
inline constexpr int kGroupFillBudgetMs = 20000;    // 等组员填充（500ms 轮询）
inline constexpr int kGroupFillPollMs = 500;
inline constexpr int kSelectRestoreBudgetMs = 90000;  // select 重试总预算（BUG-301）
inline constexpr int kSelectRetryMs = 3000;           // 失败后 3s 重试

// ---------- Sidecar 停止（← state.rs::stop_core_by_sidecar） ----------
inline constexpr int kSidecarStopWaitMs = 2000;  // 杀后等待端口释放上限

// ---------- 服务模式端口释放（← service.rs L2-02：50ms × 最多 3s） ----------
inline constexpr int kPortReleasePollMs = 50;
inline constexpr int kPortReleaseMaxWaitMs = 3000;

// ---------- 残留扫描（← state.rs:170：进程名 contains "mini-mihomo"） ----------
inline constexpr wchar_t kCoreProcessToken[] = L"mini-mihomo";

// ---------- 服务模式日志（← logger.rs service_writer_config） ----------
// max_log_size = app_log_max_size(KB, 默认 128) × 1024；max_log_files = app_log_max_count（默认 8，verge 模板 40）
inline constexpr uint64_t kServiceLogSizeBytes = 128u * 1024;
inline constexpr size_t kServiceLogMaxFiles = 8;  // unwrap_or 口径；运行时以 verge.yaml 为准

}  // namespace clashwin::core::consts
