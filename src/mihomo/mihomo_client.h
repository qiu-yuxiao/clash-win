#pragma once
// T05 mihomo REST 客户端（← crates/tauri-plugin-mihomo/src/mihomo.rs Mihomo 逐方法对照；
// 端点全集 ← docs/phase1/02_backend_census.md §6）。
//
// - 主通道：Windows 命名管道上的 HTTP/1.1（组合 T04 PipeHttpClient，连接池
//   min3/max16/idle30s/BUSY 24×125ms 复刻原版）
// - 鉴权：secret 非空时统一附带 Authorization: Bearer {secret}（WS 通道同）
// - 仅支持管道模式（原版 LocalSocket 分支；Http host:port 模式归 T06 网络层）
// - 错误分类：传输层透传 HttpErrc(1001~1006)；HTTP 非 2xx → MihomoErrc：
//     AuthDenied(401/403) / ServerError(5xx) / HttpError(其余) / JsonParse
// - 超时梯度：方法默认值对齐 mihomo.rs（默认 3s、reload/geo/upgrade 60s、
//   delay = timeout+2s、group delay = max(timeout+13s, 15s)）；全部提供
//   timeout_ms 重载供 T09 按梯度（探针 10s/15s、配置 30s、测速 60s）覆盖
#include "clashwin/mihomo/models.h"
#include "mihomo/http_types.h"
#include "mihomo/pipe_http.h"
#include "util/result.h"

#include <memory>
#include <string>
#include <vector>

namespace clashwin::mihomo {

enum class MihomoErrc : int {
  AuthDenied = 1101,   // 401/403：secret 错误
  ServerError = 1102,  // 5xx
  HttpError = 1103,    // 其余非 2xx
  JsonParse = 1104,    // 响应体非合法 JSON / 结构不符
};

class MihomoClient {
 public:
  // pipe_name：接受 "\\.\pipe\{name}" 或 "{name}"
  explicit MihomoClient(const std::wstring& pipe_name, const std::string& secret = {},
                        PipeHttpConfig cfg = {});
  ~MihomoClient() = default;
  MihomoClient(const MihomoClient&) = delete;
  MihomoClient& operator=(const MihomoClient&) = delete;

  // ---- 目标切换（← mihomo.rs update_socket_path / update_secret） ----
  // 切换管道会 clear_pool（对齐原版语义：旧池连接全部作废）
  Result<void> update_controller(const std::wstring& pipe_name);
  void update_secret(const std::string& secret);

  const std::wstring& pipe_name() const { return pipe_name_; }
  const std::string& secret() const { return secret_; }

  // ---- 基础 ----
  Result<MihomoVersion> get_version(int timeout_ms = kDefaultTimeout);
  Result<BaseConfig> get_base_config(int timeout_ms = kDefaultTimeout);
  // patch 体：{"log-level":"info", ...}（键名 kebab-case，与内核一致）
  Result<void> patch_base_config(const nlohmann::json& patch, int timeout_ms = kDefaultTimeout);
  // PUT /configs?force={force}，body {"path": config_path}；配置重载 30s 梯度
  Result<void> reload_config(bool force, const std::string& config_path,
                             int timeout_ms = 30000);
  // POST /configs/geo（update_geo 别名端点，60s）
  Result<void> update_geo(int timeout_ms = 60000);
  // POST /upgrade/geo（60s）
  Result<void> upgrade_geo(int timeout_ms = 60000);
  // POST /upgrade（60s）
  Result<void> upgrade_core(const std::string& channel = "auto", bool force = false,
                            int timeout_ms = 60000);
  // POST /upgrade/ui（60s）
  Result<void> upgrade_ui(int timeout_ms = 60000);
  Result<void> restart(int timeout_ms = kDefaultTimeout);
  Result<void> flush_fakeip(int timeout_ms = kDefaultTimeout);
  Result<void> flush_dns(int timeout_ms = kDefaultTimeout);

  // ---- Proxies ----
  Result<Proxies> get_proxies(int timeout_ms = kDefaultTimeout);
  Result<ProxyItem> get_proxy_by_name(const std::string& name, int timeout_ms = kDefaultTimeout);
  // PUT /proxies/{group}，body {"name": node}
  Result<void> select_node_for_group(const std::string& group, const std::string& node,
                                     int timeout_ms = kDefaultTimeout);
  // DELETE /proxies/{group}（URLTest 组解除固定）
  Result<void> unfixed_proxy(const std::string& group, int timeout_ms = kDefaultTimeout);
  // GET /proxies/{name}/delay?url=&timeout=；非 2xx 一律返回 {delay:0}
  // （← mihomo.rs BUG-171/172 约定：测速失败不是 IPC 错误，禁止上报 Err）
  Result<ProxyDelay> delay_proxy_by_name(const std::string& name, const std::string& test_url,
                                         uint32_t timeout_ms, int timeout_override_ms = 0);
  Result<Groups> get_groups(int timeout_ms = kDefaultTimeout);
  Result<ProxyItem> get_group_by_name(const std::string& name, int timeout_ms = kDefaultTimeout);
  // GET /group/{name}/delay（PROXY__METRICS 批量测速）→ {节点名: 延迟ms}
  Result<std::map<std::string, uint32_t>> delay_group(const std::string& group,
                                                      const std::string& test_url,
                                                      uint32_t timeout_ms,
                                                      int timeout_override_ms = 0);

  // ---- Providers ----
  Result<ProxyProviders> get_proxy_providers(int timeout_ms = kDefaultTimeout);
  Result<ProxyProvider> get_proxy_provider_by_name(const std::string& name,
                                                   int timeout_ms = kDefaultTimeout);
  Result<void> update_proxy_provider(const std::string& name, int timeout_ms = 60000);
  Result<void> healthcheck_proxy_provider(const std::string& name,
                                          int timeout_ms = kDefaultTimeout);
  // 非代理节点健康检查；HTTP 失败返回 {delay:0}（← 原版 timeout 语义）
  Result<ProxyDelay> healthcheck_node_in_provider(const std::string& provider,
                                                  const std::string& node,
                                                  const std::string& test_url,
                                                  uint32_t timeout_ms,
                                                  int timeout_override_ms = 0);
  Result<Rules> get_rules(int timeout_ms = kDefaultTimeout);
  Result<RuleProviders> get_rule_providers(int timeout_ms = kDefaultTimeout);
  Result<void> update_rule_provider(const std::string& name, int timeout_ms = 60000);

  // ---- Connections ----
  Result<Connections> get_connections(int timeout_ms = kDefaultTimeout);
  Result<void> close_connection(const std::string& id, int timeout_ms = kDefaultTimeout);
  Result<void> close_all_connections(int timeout_ms = kDefaultTimeout);

  // ---- DNS ----
  // GET /dns/query?name=&type=A，返回内核原始 Message JSON（含 Status/Answer）
  Result<nlohmann::json> get_dns_query(const std::string& name, const std::string& type = "A",
                                       int timeout_ms = 3000);
  // ← mihomo.rs dns_query_ready：冷启动就绪探测。Ok(true)=就绪；Ok(false)=未就绪
  // （超时/连接类错误归入未就绪）；Err=端点不可用等硬错误（含 404）
  Result<bool> dns_query_ready(const std::string& name, int timeout_ms = 3000);

  PipeHttpClient& http() { return http_; }

  static constexpr int kDefaultTimeout = 5000;  // ← DEFAULT_REQUEST_TIMEOUT (BUG-324: 5s > pool 2s)

 private:
  // 统一请求入口：附带 Host/Content-Type/Bearer；非 2xx → MihomoErrc + {"message"}
  Result<HttpResponse> request(const char* method, const std::string& path,
                               const std::string& body, int timeout_ms);
  template <typename T>
  Result<T> request_json(const char* method, const std::string& path, const std::string& body,
                         int timeout_ms);

  std::wstring pipe_name_;
  std::string secret_;
  PipeHttpClient http_;
};

}  // namespace clashwin::mihomo
