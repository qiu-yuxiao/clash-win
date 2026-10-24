#include "mihomo/service_ipc.h"

#include <windows.h>

#include <utility>

namespace clashwin::mihomo {

constexpr wchar_t ServiceIpc::kDefaultPipeName[];
constexpr const char* ServiceIpc::kIpcMagic;

ServiceIpc::ServiceIpc(PipeHttpConfig cfg)
    : client_(std::make_shared<PipeHttpClient>(std::move(cfg))), owns_(true) {}

ServiceIpc::ServiceIpc(std::shared_ptr<PipeHttpClient> shared_client)
    : client_(std::move(shared_client)), owns_(false) {}

Result<void> ServiceIpc::connect(const std::wstring& pipe_name) {
  return client_->connect(pipe_name);
}

bool ServiceIpc::is_pipe_present(const std::wstring& pipe_name) {
  std::wstring full = pipe_name.rfind(L"\\\\.\\pipe\\", 0) == 0
                          ? pipe_name
                          : L"\\\\.\\pipe\\" + pipe_name;
  // NMPWAIT_USE_DEFAULT_WAIT=0：仅探测实例是否存在（未就绪也不长阻塞）
  if (WaitNamedPipeW(full.c_str(), 1)) return true;
  DWORD err = GetLastError();
  // ERROR_SEM_TIMEOUT=实例存在但忙；ERROR_FILE_NOT_FOUND=不存在
  return err == ERROR_SEM_TIMEOUT || err == ERROR_PIPE_BUSY;
}

Result<nlohmann::json> ServiceIpc::call(const char* method, const std::string& path,
                                        const std::string& body, int timeout_ms) {
  std::map<std::string, std::string> headers;
  headers["X-IPC-Magic"] = kIpcMagic;
  headers["Content-Type"] = "application/json";
  auto r = client_->send(method, path, headers, body, timeout_ms);
  if (!r.is_ok()) return Result<nlohmann::json>::err(r.error());

  const HttpResponse& resp = r.value();
  if (!resp.is_success()) {
    Error e;
    e.code = static_cast<int>(HttpErrc::Other);
    e.message = "service http status " + std::to_string(resp.status);
    return Result<nlohmann::json>::err(e);
  }

  nlohmann::json j = nlohmann::json::parse(resp.body, nullptr, false);
  if (j.is_discarded() || !j.is_object() || !j.contains("code") || !j["code"].is_number_integer()) {
    Error e;
    e.code = static_cast<int>(HttpErrc::Protocol);
    e.message = "service response not {code,message,data}";
    return Result<nlohmann::json>::err(e);
  }
  int code = j["code"].get<int>();
  if (code > 0) {
    Error e;
    e.code = 2000 + code;  // 业务失败（避免与 HttpErrc 1001~1006 冲突）
    e.message = j.value("message", "");
    return Result<nlohmann::json>::err(e);
  }
  // code==0 → data（可能缺省）
  return Result<nlohmann::json>::ok(j.contains("data") ? j["data"] : nlohmann::json(nullptr));
}

Result<nlohmann::json> ServiceIpc::get_version() { return call("GET", "/version", "", 3000); }
Result<nlohmann::json> ServiceIpc::get_status() { return call("GET", "/status", "", 3000); }
Result<nlohmann::json> ServiceIpc::get_clash_logs() { return call("GET", "/clash/logs", "", 3000); }

Result<std::string> ServiceIpc::magic_probe() {
  std::map<std::string, std::string> headers;
  headers["X-IPC-Magic"] = kIpcMagic;
  auto r = client_->send("GET", "/magic", headers, "", 3000);
  if (!r.is_ok()) return Result<std::string>::err(r.error());
  const HttpResponse& resp = r.value();
  if (!resp.is_success()) {
    Error e;
    e.code = static_cast<int>(HttpErrc::Other);
    e.message = "magic probe http status " + std::to_string(resp.status);
    return Result<std::string>::err(e);
  }
  return Result<std::string>::ok(resp.body);
}

Result<nlohmann::json> ServiceIpc::clash_start(const nlohmann::json& clash_config) {
  return call("POST", "/clash/start", clash_config.dump(), 10000);
}

Result<nlohmann::json> ServiceIpc::clash_stop() { return call("DELETE", "/clash/stop", "", 10000); }

Result<nlohmann::json> ServiceIpc::put_writer(const nlohmann::json& writer_config) {
  return call("PUT", "/writer", writer_config.dump(), 10000);
}

}  // namespace clashwin::mihomo
