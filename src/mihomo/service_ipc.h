#pragma once
// clash-verge-service IPC 客户端封装（← docs/phase1/04_round2_clarifications.md §6
// 与 C:/Users/sun_y/.cargo/git/checkouts/clash-verge-service-ipc-.../src/lib.rs rev 21e661f）。
// - 管道 \\.\pipe\clash-verge-service，HTTP/1.1 body JSON
// - 每个请求自动附带 X-IPC-Magic（逐字符复刻 IPC_AUTH_EXPECT）
// - 端点：GET /version /status /magic /clash/logs、POST /clash/start、
//   DELETE /clash/stop、PUT /writer
// - 响应 {code:u16, message, data?}：code==0 → ok(data)；code>0 → 失败
//   （Error.code = 2000+业务码，与 HttpErrc 1001~1006 区分；Error.message = 服务端 message）
#include "mihomo/pipe_http.h"
#include "util/result.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>

namespace clashwin::mihomo {

class ServiceIpc {
 public:
  // ← lib.rs:28（不定义测试管道名；测试自建 mock 管道传入）
  static constexpr wchar_t kDefaultPipeName[] = L"clash-verge-service";
  // ← lib.rs:36 IPC_AUTH_EXPECT，逐字符复刻，禁止改动任何空白/标点
  static constexpr const char* kIpcMagic =
      "A thing of beauty is a joy for ever. Its loveliness increases; it will never pass into "
      "nothingness.";

  // 内建客户端（独立连接池）
  explicit ServiceIpc(PipeHttpConfig cfg = {});
  // 共享外部客户端（T07 可与 mihomo 层共享进程级池——注意各自 connect 管道名）
  explicit ServiceIpc(std::shared_ptr<PipeHttpClient> shared_client);
  ~ServiceIpc() = default;

  Result<void> connect(const std::wstring& pipe_name = kDefaultPipeName);

  Result<nlohmann::json> get_version();
  Result<nlohmann::json> get_status();
  Result<nlohmann::json> get_clash_logs();
  Result<nlohmann::json> clash_start(const nlohmann::json& clash_config);
  Result<nlohmann::json> clash_stop();
  Result<nlohmann::json> put_writer(const nlohmann::json& writer_config);

  // 探测端点 GET /magic：真实服务返回纯文本 "Tunglies!"（非 {code,message,data}）。
  // 成功 = HTTP 200 且 body 原样返回；服务端以 X-IPC-Magic 逐字符鉴权，
  // 200 即证明 magic 头逐字符一致（错则 401）。
  Result<std::string> magic_probe();

  // 服务管道是否就绪（WaitNamedPipeW 探测；不占用管道实例）
  static bool is_pipe_present(const std::wstring& pipe_name = kDefaultPipeName);

  PipeHttpClient& http() { return *client_; }

 private:
  // 失败分类：传输层 → Error.code=HttpErrc；服务业务失败(code>0) → Error.code=2000+code
  Result<nlohmann::json> call(const char* method, const std::string& path,
                              const std::string& body, int timeout_ms);

  std::shared_ptr<PipeHttpClient> client_;
  bool owns_ = false;
};

}  // namespace clashwin::mihomo
