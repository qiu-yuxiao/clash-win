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
// StartServiceW；ERROR_ACCESS_DENIED 时回退 runas 提权 sc.exe start（← service.rs:153-179 UAC 语义）
Result<void> start_service();

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

// ---------- 启动模式决策（← lifecycle.rs prepare_startup_mode + wait_for_service_ready） ----------
struct StartupDecision {
  bool use_service = false;      // true → Service 模式
  bool fallback_sysproxy = false;// true → 需回退系统代理（服务未就绪 + TUN 请求）
};
// 纯函数（可测）：needs_tun=true 且非管理员 → 必须走服务；服务未就绪 → 告警 + 回退
// （BUG-298 铁律：回退必须明确告警，禁止静默降级）
StartupDecision decide_startup_mode(bool needs_tun, bool is_admin, bool service_ready);

}  // namespace clashwin::core::service_mode
