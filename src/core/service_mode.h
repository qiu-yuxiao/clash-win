#pragma once
// T07 服务模式（← ClashVerge core/service.rs + 04§6 clash-verge-service-ipc 协议）
//
// - SCM 查询/启动替代 sc.exe 子进程（OpenSCManager/OpenService/QueryServiceStatus/StartService）
// - 就绪判定 = 管道存在 + magic_probe 成功（service.rs is_service_available 语义）
// - start_with_service：stop_clash 清残留 → 等 mixed-port 释放（50ms×最多 3s）→
//   clash_start(payload) → code>0 判失败
// - TUN 非管理员必须走服务；服务未就绪 → 明确告警（不静默）+ 回退系统代理（BUG-298 铁律）
#pragma once

#include "clashwin/core/constants.h"
#include "mihomo/service_ipc.h"
#include "util/result.h"

#include <nlohmann/json.hpp>

#include <string>

namespace clashwin::core::service_mode {

// ---------- 唯一服务名（共存铁律） ----------
// 与 Clash Mini 完全同名：clash_verge_service。禁止引入第二服务名；
// 所有 SCM 调用与安装器校验都必须引用这两个常量。
inline constexpr const wchar_t* kServiceNameW = L"clash_verge_service";
inline constexpr const char* kServiceName = "clash_verge_service";

// ---------- 上游服务二进制基线（总纲 §5.2.2「借用与归还」，2026-11-10 裁决） ----------
// 服务二进制继承自上游 Clash Verge，Mini/Win 是同一个组件。注册在位且 ImagePath
// 二进制哈希与该基线一致 → 共用不夺注册（零 UAC）；不一致（分叉/损坏）才重装接管。
// 与 installer/clashwin.nsi 的 SERVICE_EXE_SHA256、scripts/verify_sidecar.ps1 基线同源；
// sidecar 升级时三处同步。
inline constexpr const char* kServiceExeSha256 =
    "92ad792abb91bd84b89894e63a8251434a631a5803d9d043c18f7a5a7961e9d2";

// ---------- IPC 门面（mock 注入点；适配 T04 ServiceIpc） ----------
struct IServiceIpc {
  virtual ~IServiceIpc() = default;
  virtual Result<nlohmann::json> clash_start(const nlohmann::json& payload) = 0;
  virtual Result<nlohmann::json> clash_stop() = 0;
  virtual Result<std::string> magic_probe() = 0;
  virtual bool pipe_present() = 0;
  virtual Result<nlohmann::json> get_clash_logs() = 0;
};

class ServiceIpcAdapter : public IServiceIpc {
 public:
  explicit ServiceIpcAdapter(mihomo::ServiceIpc* ipc) : ipc_(ipc) {}
  Result<nlohmann::json> clash_start(const nlohmann::json& payload) override;
  Result<nlohmann::json> clash_stop() override;
  Result<std::string> magic_probe() override;
  bool pipe_present() override;
  Result<nlohmann::json> get_clash_logs() override;

 private:
  mihomo::ServiceIpc* ipc_;
};

// ---------- SCM（← service.rs:137-179，替代 sc.exe 子进程） ----------
// OpenService 探测（服务是否存在）
bool is_service_installed();
// QueryServiceStatus == SERVICE_RUNNING
bool is_service_running();
// 服务已注册但 ImagePath 指向的二进制不存在（Mini 卸载后残留）；读 QueryServiceConfigW
bool is_service_binary_valid();
// StartServiceW；ERROR_ACCESS_DENIED 时回退 runas 提权 sc.exe start（← service.rs:153-179 UAC 语义）
Result<void> start_service();
Result<void> stop_service();

// ---------- 就绪判定 / 等待（← service.rs is_service_available + wait_for_service_ipc） ----------
bool is_service_ready(IServiceIpc& ipc);  // 管道存在 && magic_probe 成功
// 30s 预算 / 200ms 间隔轮询（timing::SERVICE_WAIT_MAX / INTERVAL）
bool wait_for_service_ready(IServiceIpc& ipc, int budget_ms = consts::kServiceWaitMaxMs,
                            int interval_ms = consts::kServiceWaitIntervalMs);

// ---------- payload 构造（纯函数；← service.rs:231-239 + 04§6 逐字段） ----------
struct LogConfig {
  std::string directory;      // <home>\logs\service（UTF-8）
  uint64_t max_log_size = 0;  // 字节（app_log_max_size KB × 1024）
  size_t max_log_files = 0;
};
// 返回 {"core_config":{"core_path","core_ipc_path","config_path","config_dir"},
//        "log_config":{"directory","max_log_size","max_log_files"}}
nlohmann::json build_clash_config_payload(const std::string& core_path,
                                          const std::string& core_ipc_path,
                                          const std::string& config_path,
                                          const std::string& config_dir,
                                          const LogConfig& log_cfg);
// ← logger.rs service_writer_config：directory=service_log_dir，size/count 取 verge（缺省 128KB/8）
LogConfig default_log_config();

// ---------- 启停（← service.rs::start_with_existing_service / stop_core_by_service） ----------
// 先 stop_clash 清残留 + 等 mixed-port 释放（50ms×最多 3s，超时告警继续）→ clash_start
Result<void> start_with_service(IServiceIpc& ipc, const nlohmann::json& payload, uint16_t mixed_port,
                                int poll_ms = consts::kPortReleasePollMs,
                                int max_wait_ms = consts::kPortReleaseMaxWaitMs);
// DELETE /clash/stop；code>0 判失败
Result<void> stop_with_service(IServiceIpc& ipc);

// 端口是否可绑定（已释放）；供测试与 start_with_service 内部轮询
bool is_port_free(uint16_t port);

// ---------- 服务状态五态（可注入探测；纯函数便于单测） ----------
// ← 回归用例：管道在但 SCM 状态 STOPPED 时，不得判 ready（原缺陷）。
enum class ServiceState { Missing, BinaryInvalid, Stopped, NotReady, Ready };

struct IServiceProbe {
  virtual ~IServiceProbe() = default;
  virtual bool installed() = 0;     // OpenService 成功
  virtual bool binary_valid() = 0;  // ImagePath 指向的文件存在
  virtual bool running() = 0;       // QueryServiceStatus == RUNNING
  virtual bool pipe_ready() = 0;    // 管道存在 + magic 握手成功
};

ServiceState evaluate_state(IServiceProbe& probe);
inline bool state_requires_install(ServiceState s) {
  return s == ServiceState::Missing || s == ServiceState::BinaryInvalid;
}
inline bool state_requires_start(ServiceState s) {
  return s == ServiceState::Stopped || s == ServiceState::NotReady;
}

// ---------- 启动模式决策（← lifecycle.rs prepare_startup_mode + wait_for_service_ready） ----------
struct StartupDecision {
  bool use_service = false;      // true → Service 模式
  bool fallback_sysproxy = false;// true → 需回退系统代理（服务未就绪 + TUN 请求）
};
// 纯函数（可测）：needs_tun=true 且非管理员 → 必须走服务；服务未就绪 → 告警 + 回退
// （BUG-298 铁律：回退必须明确告警，禁止静默降级）
StartupDecision decide_startup_mode(bool needs_tun, bool is_admin, bool service_ready);

}  // namespace clashwin::core::service_mode
