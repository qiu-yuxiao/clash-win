#pragma once
// T07 内核生命周期管理（← ClashVerge core/manager/{mod,state,lifecycle,config}.rs）
//
// - 双运行模式 RunningMode{Service, Sidecar, NotRunning} + lifecycle 锁串行化
// - Sidecar：CreateProcessW 拉起 mini-mihomo（-d -f -ext-ctl-pipe + Go 三环境变量），
//   stdout/stderr 管道重定向写 logs/sidecar/sidecar_latest.log
// - 启停前 kill_all_mini_cores 扫杀残留（安全增强：只杀本应用作用域内的同名进程，
//   绝不触碰用户实机 Clash Mini 内核——见 select_stale_core_pids 注释）
// - restart_with_restore：snapshot PROXY.now → 停 → 启 → 等 API 30s → 等组员 20s →
//   select 90s 预算 → 失败回退 filterText 子集（lifecycle.rs::restart_core 逐段）
// - apply_config：snapshot → reload_config(force=true) → restore → 失败整核重启兜底
#pragma once

#include "clashwin/core/constants.h"
#include "mihomo/mihomo_client.h"
#include "util/result.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace clashwin::core {

enum class RunningMode { Service, Sidecar, NotRunning };
const char* to_string(RunningMode mode);

// ---------- 内核定位（← state.rs:36-37 + service.rs:215-229） ----------
// 优先 <home>\cores\mini-mihomo.exe（热升级内核），否则 <exe>\mini-mihomo.exe（sidecar）；
// 都不存在返回空串。
std::wstring locate_core_path();

// ---------- Sidecar 命令行构造（纯函数；← state.rs::start_core_by_sidecar） ----------
struct SidecarLaunch {
  std::wstring core_path;   // mini-mihomo.exe 全路径
  std::wstring config_dir;  // -d：app_home_dir
  std::wstring config_file; // -f：clash-win.yaml（运行时产物）
  std::wstring ipc_pipe;    // -ext-ctl-pipe：\\.\pipe\mini-mihomo-win
  // Go 运行时三变量（进程环境缺失时取默认，同 std::env::var unwrap_or）
  std::string go_memlimit = consts::kGoMemLimitDefault;
  std::string go_gc = consts::kGoGcDefault;
  std::string go_maxprocs = consts::kGoMaxProcsDefault;
};

// 可传 CreateProcessW lpCommandLine 的完整串（路径带引号；对照 state.rs args 顺序）
std::wstring build_sidecar_command_line(const SidecarLaunch& s);

// 构造 lpEnvironment 环境块（当前环境 + Go 三变量覆盖；按名不区分大小写字母序排列，
// UTF-16 双 NUL 结尾；调用方须配 CREATE_UNICODE_ENVIRONMENT）
std::wstring build_sidecar_env_block(const SidecarLaunch& s);

// 从当前进程环境解析 Go 三变量（缺失 → 默认值）并装配 launch（其余路径字段由调用方填）
SidecarLaunch make_sidecar_launch(const std::wstring& core_path, const std::wstring& config_dir,
                                  const std::wstring& config_file, const std::wstring& ipc_pipe);

// ---------- 残留进程扫描（← state.rs:156-183，安全增强版） ----------
struct ProcInfo {
  uint32_t pid = 0;
  std::wstring name;  // exe 文件名（如 mini-mihomo.exe）
  std::wstring path;  // 全路径（path_ok=false 时空）
  bool path_ok = false;
};
using ProcEnumerator = std::function<std::vector<ProcInfo>()>;

// 纯函数（可测）：从枚举结果筛出待杀 PID。
// 过滤规则（对照 state.rs:167-180 的 name.contains 语义 + 本项目铁律增强）：
//   1. 进程名必须含 "mini-mihomo"（同名才杀）；
//   2. 路径无法查询（OpenProcess/QueryFullProcessImageName 失败）→ 跳过不杀；
//   3. 路径必须位于 scope_dirs 之一（本应用 <home>\cores 或 <exe> 同级）之下——
//      用户实机 Clash Mini 的内核（其实例目录在本应用作用域外）绝不触碰；
//   4. pid ∈ exclude_pids（自身托管的子进程 / 自身 pid）→ 排除。
std::vector<uint32_t> select_stale_core_pids(const std::vector<ProcInfo>& procs,
                                             const std::set<uint32_t>& exclude_pids,
                                             const std::vector<std::wstring>& scope_dirs);

// 默认枚举器：Toolhelp32 全量进程快照（供注入替换）
std::vector<ProcInfo> enumerate_all_processes();

// kill_all_mini_cores：枚举 → 筛选（scope=home\cores + exe_dir；排除 exclude_pids）
// → TerminateProcess（失败回退 taskkill /F）。返回实际杀掉的 PID。
std::vector<uint32_t> kill_all_mini_cores(const std::set<uint32_t>& exclude_pids = {});

// ---------- 内核 API 门面（mock 注入点；适配 MihomoClient） ----------
struct ICoreApiClient {
  virtual ~ICoreApiClient() = default;
  virtual Result<mihomo::BaseConfig> get_base_config() = 0;
  virtual Result<mihomo::ProxyItem> get_group_by_name(const std::string& name) = 0;
  virtual Result<void> select_node_for_group(const std::string& group, const std::string& node) = 0;
  // PUT /configs?force=true（30s 梯度）
  virtual Result<void> reload_config(bool force, const std::string& config_path) = 0;
};

class CoreApiClientAdapter : public ICoreApiClient {
 public:
  explicit CoreApiClientAdapter(mihomo::MihomoClient* client) : client_(client) {}
  Result<mihomo::BaseConfig> get_base_config() override;
  Result<mihomo::ProxyItem> get_group_by_name(const std::string& name) override;
  Result<void> select_node_for_group(const std::string& group, const std::string& node) override;
  Result<void> reload_config(bool force, const std::string& config_path) override;

 private:
  mihomo::MihomoClient* client_;
};

// ---------- 恢复语义（lifecycle.rs 逐段对照） ----------
// 内置出站（← lifecycle.rs:19-21 is_builtin_outbound）
bool is_builtin_outbound(const std::string& name);

// ← lifecycle.rs:419-441 read_filter_text 的纯逻辑部分：
// state[uid]["PROXY"]["filterText"]，trim + lowercase
std::string extract_filter_text(const nlohmann::json& state, const std::string& uid);

// 纯函数（可测）：filterText 子集兜底选点（← lifecycle.rs:444-453）。
// 优先「含 filter 子串的首个非 dummy 节点」；filter 为空或无匹配 → 首个非 dummy；全 dummy → 空。
std::string pick_fallback_node(const std::vector<std::string>& all, const std::string& filter_lower);

// 子集兜底选点并 select（← lifecycle.rs select_fallback_in_subset）；
// 返回实际选中的节点名（未选中返回空串）
std::string select_fallback_in_subset(ICoreApiClient& api, const std::string& filter_lower);

// 从盘上读 filterText（proxy_head_state.json + profiles.current；读失败返回空串）
std::string read_filter_text_lower();

// 恢复时序参数（测试注入小值加速；生产用 constants 默认）
struct RestoreTimings {
  int api_ready_budget_ms = consts::kApiReadyBudgetMs;
  int api_ready_poll_ms = consts::kApiReadyPollMs;
  int group_fill_budget_ms = consts::kGroupFillBudgetMs;
  int group_fill_poll_ms = consts::kGroupFillPollMs;
  int select_budget_ms = consts::kSelectRestoreBudgetMs;
  int select_retry_ms = consts::kSelectRetryMs;
};

// ---------- CoreManager ----------
// 启停操作注入点：restart_with_restore / apply_config 经此调用停启，
// 生产装配为成员 start/stop 的绑定；测试注入录制桩（沙箱内无法 spawn）。
struct CoreOps {
  std::function<Result<void>()> start;
  std::function<Result<void>()> stop;
};

// 告警通知回调（T09 事件总线接入前降级为日志；BUG-298 铁律：回退必须留痕不静默）
using Notifier = std::function<void(const char* status, const std::string& message)>;

class CoreManager {
 public:
  CoreManager();
  ~CoreManager();
  CoreManager(const CoreManager&) = delete;
  CoreManager& operator=(const CoreManager&) = delete;

  static CoreManager& instance();

  RunningMode running_mode() const;

  // ---- Sidecar 模式（← state.rs::start_core_by_sidecar / stop_core_by_sidecar） ----
  // 启动：kill_all 清残留 → 定位内核 → CreateProcessW（含 Go 三环境变量 + 管道重定向）
  // → 日志消费线程写 logs/sidecar/。已在运行时幂等 no-op。
  // ipc_pipe：-ext-ctl-pipe 参数；空 = dirs::ipc_path()（\\.\pipe\mini-mihomo-win）。
  // 测试传独立管道名（绝不触碰用户实机内核管道）。
  Result<void> start_sidecar(const std::wstring& config_file,
                             const std::wstring& ipc_pipe = {});
  // 统一启动入口（← lifecycle.rs prepare_startup_mode 接线）：
  //   verge.enable_tun_mode && !is_admin && service 就绪 → Service 模式（SYSTEM 内核，
  //   可建 Wintun/TUN）；否则 Sidecar。服务缺失 → 安装流程（唯一名 clash_verge_service）。
  Result<void> start_auto(const std::wstring& config_file,
                          const std::wstring& ipc_pipe = {});
  // 停止：TerminateProcess → 等待 ≤2s → kill_all 兜底；Service 模式走 clash_stop
  Result<void> stop();

  uint32_t sidecar_pid() const { return pid_; }
  // 子进程是否仍在运行（测试用）
  bool sidecar_alive() const;
  // 模式感知的内核存活判定（CoreMonitor 用）：
  //   Sidecar → child_ 句柄；Service → 服务 IPC /status 的 core_pid。
  //   服务模式下内核归 SCM 服务持有，child_ 必为空，若仍用 sidecar_alive
  //   会被守护线程每轮判死并触发 16s 循环重启（2026-11-05 实机复现）。
  bool core_alive() const;

  // ---- 重启 + 节点恢复（← lifecycle.rs::restart_core） ----
  Result<void> restart_with_restore(ICoreApiClient& api, const CoreOps& ops,
                                    const RestoreTimings& t = {});

  // ---- 配置热应用（← manager/config.rs::apply_config） ----
  Result<void> apply_config(ICoreApiClient& api, const std::string& config_path,
                            const CoreOps& ops, const RestoreTimings& t = {});

  // ---- 快照/恢复（公开供 T24 core_updater 复用，← lifecycle.rs:282-416） ----
  std::optional<std::string> snapshot_proxy_group_now(ICoreApiClient& api);
  void restore_proxy_group_now(ICoreApiClient& api, const std::string& node,
                               const RestoreTimings& t = {});

  // 告警通知（默认 CWLOG_WARN；T09 接 notice-service）
  void set_notifier(Notifier n) { notifier_ = std::move(n); }
  void notify(const char* status, const std::string& message);

 private:
  Result<void> stop_sidecar_locked();
  // 调用方须已持有 lifecycle_（start_auto 内部先决策再分派）
  Result<void> start_sidecar_locked(const std::wstring& config_file,
                                    const std::wstring& ipc_pipe);
  // Service 模式启动：确保服务已安装/已运行 → 等 IPC 就绪 → clash_start
  Result<void> start_service_locked(const std::wstring& config_file);

  std::atomic<RunningMode> mode_{RunningMode::NotRunning};
  std::mutex lifecycle_;           // ← manager/mod.rs lifecycle_lock（串行化启停）
  void* child_ = nullptr;          // HANDLE（避免头文件引入 windows.h）
  uint32_t pid_ = 0;
  void* stdout_read_ = nullptr;    // 管道读端 HANDLE
  void* stderr_read_ = nullptr;
  std::atomic<bool> log_run_{false};
  std::vector<void*> log_threads_; // HANDLE 列表（避免 <thread> 泄漏到头文件）
  Notifier notifier_;
};

}  // namespace clashwin::core
