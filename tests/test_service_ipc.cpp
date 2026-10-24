// T04 单测：clash-verge-service IPC 封装（X-IPC-Magic 逐字符 + {code,message,data} 语义）
// + 真实管道探测（\\.\pipe\clash-verge-service 存在时 HIT，否则 SKIP）
#include "test_util.h"

#include <cstring>
#include <memory>
#include <string>

#include "mihomo/service_ipc.h"
#include "mock_pipe_server.h"

using namespace clashwin;
using namespace clashwin::mihomo;
using namespace mockpipe;

namespace {

std::wstring ipc_unique_pipe(const char* tag) {
  static std::atomic<int> seq{0};
  wchar_t buf[128];
  swprintf_s(buf, L"\\\\.\\pipe\\clashwin-t04svc-%hs-%d", tag, seq.fetch_add(1));
  return buf;
}

// mock clash-verge-service：校验 magic 头逐字符一致，按端点回 {code,message,data}
struct IpcState {
  bool magic_header_present = false;
  bool magic_exact = false;
  std::string magic_received;
};

Response service_handler(IpcState* st, const Request& req) {
  Response r;
  r.headers["Content-Type"] = "application/json";
  auto it = req.headers.find("x-ipc-magic");
  st->magic_header_present = it != req.headers.end();
  if (st->magic_header_present) {
    st->magic_received = it->second;
    const char* expect = ServiceIpc::kIpcMagic;
    // 逐字符比对（要求长度与内容完全一致）
    st->magic_exact = it->second.size() == strlen(expect) &&
                      std::memcmp(it->second.data(), expect, strlen(expect)) == 0;
  }
  if (!st->magic_header_present || !st->magic_exact) {
    r.status = 401;
    r.reason = "Unauthorized";
    r.body = "{\"code\":1,\"message\":\"unauthorized\"}";
    r.close_after = false;
    return r;
  }

  if (req.path == "/version") {
    r.body = "{\"code\":0,\"message\":\"Success\",\"data\":\"2.3.0\"}";
  } else if (req.path == "/status") {
    r.body = "{\"code\":0,\"message\":\"Success\",\"data\":{\"core_running\":false}}";
  } else if (req.path == "/magic") {
    // 真实服务该端点返回纯文本
    r.body = "Tunglies!";
    r.headers["Content-Type"] = "text/plain";
  } else if (req.path == "/clash/start" && req.method == "POST") {
    r.body = "{\"code\":0,\"message\":\"Success\",\"data\":null}";
  } else if (req.path == "/clash/stop" && req.method == "DELETE") {
    r.body = "{\"code\":0,\"message\":\"Success\",\"data\":null}";
  } else if (req.path == "/writer" && req.method == "PUT") {
    r.body = "{\"code\":0,\"message\":\"Success\",\"data\":null}";
  } else if (req.path == "/clash/logs") {
    r.body = "{\"code\":5,\"message\":\"service not running\"}";
  } else {
    r.status = 404;
    r.body = "{\"code\":9,\"message\":\"not found\"}";
  }
  return r;
}

}  // namespace

UT_TEST(service_ipc_magic_header_exact) {
  auto name = ipc_unique_pipe("magic");
  IpcState st;
  Server srv(name, 2);
  srv.start([&st](const Request& req) { return service_handler(&st, req); });

  PipeHttpConfig cfg;
  cfg.busy_retry_delay_ms = 25;
  ServiceIpc ipc(cfg);
  UT_CHECK(ipc.connect(name).is_ok());

  auto v = ipc.get_version();
  UT_CHECK(v.is_ok());
  UT_CHECK(v.value() == nlohmann::json("2.3.0"));
  UT_CHECK(st.magic_header_present);
  UT_CHECK(st.magic_exact);
  // 逐字符再断言一次
  const char* expect = ServiceIpc::kIpcMagic;
  UT_CHECK_EQ(st.magic_received.size(), strlen(expect));
  UT_CHECK(std::memcmp(st.magic_received.data(), expect, strlen(expect)) == 0);
  srv.stop();
}

UT_TEST(service_ipc_endpoints_roundtrip) {
  auto name = ipc_unique_pipe("ep");
  IpcState st;
  Server srv(name, 2);
  srv.start([&st](const Request& req) { return service_handler(&st, req); });

  ServiceIpc ipc;
  UT_CHECK(ipc.connect(name).is_ok());

  auto status = ipc.get_status();
  UT_CHECK(status.is_ok());
  UT_CHECK(status.value()["core_running"] == false);

  nlohmann::json start_payload = {
      {"core_config",
       {{"core_path", "C:/x/mini-mihomo.exe"},
        {"core_ipc_path", "\\\\.\\pipe\\mini-mihomo"},
        {"config_path", "C:/x/config.yaml"},
        {"config_dir", "C:/x"}}},
      {"log_config", {{"directory", "C:/logs"}, {"max_log_size", 1}, {"max_log_files", 3}}}};
  auto s = ipc.clash_start(start_payload);
  UT_CHECK(s.is_ok());
  auto stop = ipc.clash_stop();
  UT_CHECK(stop.is_ok());
  auto w = ipc.put_writer({{"directory", "C:/logs"}});
  UT_CHECK(w.is_ok());

  auto magic = ipc.magic_probe();
  UT_CHECK(magic.is_ok());
  UT_CHECK(magic.value() == "Tunglies!");
  srv.stop();
}

UT_TEST(service_ipc_business_failure) {
  // code>0 判失败：Error.code = 2000+code，message 透传
  auto name = ipc_unique_pipe("fail");
  IpcState st;
  Server srv(name, 2);
  srv.start([&st](const Request& req) { return service_handler(&st, req); });

  ServiceIpc ipc;
  UT_CHECK(ipc.connect(name).is_ok());
  auto logs = ipc.get_clash_logs();
  UT_CHECK(!logs.is_ok());
  UT_CHECK_EQ(logs.error().code, 2000 + 5);
  UT_CHECK(logs.error().message == "service not running");
  srv.stop();
}

UT_TEST(service_ipc_real_pipe_probe) {
  // 真实管道探测：服务已装 → HIT（version code==0 + magic 文本一致）；未装 → SKIP
  const std::wstring full = L"\\\\.\\pipe\\" + std::wstring(ServiceIpc::kDefaultPipeName);
  if (!ServiceIpc::is_pipe_present(ServiceIpc::kDefaultPipeName)) {
    std::printf("  SKIP: \\\\.\\pipe\\clash-verge-service not present (service not installed)\n");
    return;
  }
  std::printf("  HIT: real \\\\.\\pipe\\clash-verge-service found\n");

  PipeHttpConfig cfg;
  cfg.min_connections = 1;
  cfg.max_connections = 2;
  cfg.default_timeout_ms = 3000;
  ServiceIpc ipc(cfg);
  auto conn = ipc.connect(full);
  UT_CHECK(conn.is_ok());

  auto v = ipc.get_version();
  UT_CHECK(v.is_ok());  // {code:0,...} 才 ok
  std::printf("  real /version -> %s\n", v.value().dump().c_str());

  auto m = ipc.magic_probe();
  UT_CHECK(m.is_ok());  // 200 即证明 X-IPC-Magic 逐字符一致（错则 401）
  UT_CHECK(m.value() == "Tunglies!");
}
