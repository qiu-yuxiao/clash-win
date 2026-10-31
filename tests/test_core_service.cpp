// T07 测试：service_mode payload 逐字段 + 等待轮询 + 失败路径 + TUN 回退告警（mock IServiceIpc）
#include "test_util.h"

#include "clashwin/core/constants.h"
#include "core/service_mode.h"
#include "mihomo/service_ipc.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace clashwin;
using namespace clashwin::core::service_mode;

namespace {

struct MockIpc : IServiceIpc {
  std::vector<Result<nlohmann::json>> start_script;
  std::vector<Result<nlohmann::json>> stop_script;
  std::vector<Result<std::string>> probe_script;
  bool pipe = false;
  int start_calls = 0, stop_calls = 0, probe_calls = 0;
  nlohmann::json last_payload;

  Result<nlohmann::json> clash_start(const nlohmann::json& payload) override {
    ++start_calls;
    last_payload = payload;
    if (!start_script.empty()) {
      auto r = start_script.front();
      start_script.erase(start_script.begin());
      return r;
    }
    return Result<nlohmann::json>::ok(nlohmann::json{{"code", 0}, {"message", ""}});
  }
  Result<nlohmann::json> clash_stop() override {
    ++stop_calls;
    if (!stop_script.empty()) {
      auto r = stop_script.front();
      stop_script.erase(stop_script.begin());
      return r;
    }
    return Result<nlohmann::json>::ok(nlohmann::json{{"code", 0}, {"message", ""}});
  }
  Result<std::string> magic_probe() override {
    ++probe_calls;
    if (!probe_script.empty()) {
      auto r = probe_script.front();
      probe_script.erase(probe_script.begin());
      return r;
    }
    return Result<std::string>::ok(std::string("Tunglies!"));
  }
  bool pipe_present() override { return pipe; }
  Result<nlohmann::json> get_clash_logs() override {
    return Result<nlohmann::json>::ok(nlohmann::json{{"code", 0}, {"data", nlohmann::json::array()}});
  }
};

}  // namespace

// ---------- payload 与 04§6 逐字段一致（← service.rs:231-239） ----------
UT_TEST(service_payload_fields) {
  LogConfig lc;
  lc.directory = "C:\\home\\logs\\service";
  lc.max_log_size = 131072;
  lc.max_log_files = 40;
  auto j = build_clash_config_payload("C:\\home\\cores\\mini-mihomo.exe",
                                      "\\\\.\\pipe\\mini-mihomo",
                                      "C:\\home\\clash-win.yaml", "C:\\home", lc);
  UT_CHECK(j.is_object());
  UT_CHECK(j.contains("core_config"));
  UT_CHECK(j.contains("log_config"));
  const auto& core = j["core_config"];
  UT_CHECK(core.is_object());
  UT_CHECK_EQ(core.value("core_path", std::string()),
              std::string("C:\\home\\cores\\mini-mihomo.exe"));
  UT_CHECK_EQ(core.value("core_ipc_path", std::string()), std::string("\\\\.\\pipe\\mini-mihomo"));
  UT_CHECK_EQ(core.value("config_path", std::string()), std::string("C:\\home\\clash-win.yaml"));
  UT_CHECK_EQ(core.value("config_dir", std::string()), std::string("C:\\home"));
  UT_CHECK(core.size() == 4);
  const auto& log = j["log_config"];
  UT_CHECK_EQ(log.value("directory", std::string()), std::string("C:\\home\\logs\\service"));
  UT_CHECK_EQ(log.value("max_log_size", uint64_t{0}), uint64_t{131072});
  UT_CHECK_EQ(log.value("max_log_files", size_t{0}), size_t{40});
  UT_CHECK(log.size() == 3);
}

// ---------- start_with_service：stop 清残留 → 端口等待 → start → code 判定 ----------
UT_TEST(service_start_flow) {
  MockIpc ipc;
  nlohmann::json payload = build_clash_config_payload("core", "pipe", "cfg", "dir", {});

  // 成功路径：stop 1 次 + start 1 次
  UT_CHECK(start_with_service(ipc, payload, 20801, 5, 50).is_ok());
  UT_CHECK_EQ(ipc.stop_calls, 1);
  UT_CHECK_EQ(ipc.start_calls, 1);
  UT_CHECK(ipc.last_payload == payload);

  // code>0 → 失败（← service.rs:265-269 bail）
  ipc.start_script = {Result<nlohmann::json>::ok(nlohmann::json{{"code", 3}, {"message", "spawn failed"}})};
  auto r = start_with_service(ipc, payload, 20801, 5, 50);
  UT_CHECK(r.is_err());
  UT_CHECK(r.error().message.find("code=3") != std::string::npos);
  UT_CHECK(r.error().message.find("spawn failed") != std::string::npos);

  // 传输失败（连不上服务）→ 失败
  ipc.start_script = {Result<nlohmann::json>::err(Error{1003, "pipe busy"})};
  UT_CHECK(start_with_service(ipc, payload, 20801, 5, 50).is_err());
}

UT_TEST(service_stop_flow) {
  MockIpc ipc;
  UT_CHECK(stop_with_service(ipc).is_ok());
  UT_CHECK_EQ(ipc.stop_calls, 1);
  ipc.stop_script = {Result<nlohmann::json>::ok(nlohmann::json{{"code", 2}, {"message", "no core"}})};
  auto r = stop_with_service(ipc);
  UT_CHECK(r.is_err());
  UT_CHECK(r.error().message.find("code=2") != std::string::npos);
  ipc.stop_script = {Result<nlohmann::json>::err(Error{1001, "closed"})};
  UT_CHECK(stop_with_service(ipc).is_err());
}

// ---------- 就绪判定与等待轮询（← wait_for_service_ipc 30s/200ms） ----------
UT_TEST(service_ready_wait) {
  MockIpc ipc;
  // 管道不存在 → 永不就绪
  UT_CHECK(!wait_for_service_ready(ipc, 60, 10));
  // 管道存在 + probe ok → 立即就绪
  ipc.pipe = true;
  UT_CHECK(wait_for_service_ready(ipc, 60, 10));
  UT_CHECK(ipc.probe_calls >= 1);
  // probe 失败 → 预算耗尽
  MockIpc ipc2;
  ipc2.pipe = true;
  ipc2.probe_script = {Result<std::string>::err(Error{1001, "no"}),  // 弹完后默认 ok
                       Result<std::string>::err(Error{1001, "no"}),
                       Result<std::string>::err(Error{1001, "no"})};
  // 注意：probe_script 弹完后默认成功 → 这里用恒失败子类
  struct AlwaysFailProbe : MockIpc {
    Result<std::string> magic_probe() override {
      ++probe_calls;
      return Result<std::string>::err(Error{1001, "down"});
    }
  } ipc3;
  ipc3.pipe = true;
  UT_CHECK(!wait_for_service_ready(ipc3, 80, 10));
  UT_CHECK(ipc3.probe_calls >= 3);  // 轮询多次
}

// ---------- is_service_ready 语义：pipe && magic_probe ----------
UT_TEST(service_ready_semantics) {
  MockIpc ipc;
  UT_CHECK(!is_service_ready(ipc));  // 无管道
  ipc.pipe = true;
  UT_CHECK(is_service_ready(ipc));
}

// ---------- 端口释放探测 ----------
UT_TEST(service_port_release) {
  // 绑定一个端口占住它 → is_port_free=false
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  UT_CHECK(s != INVALID_SOCKET);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(20999);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  UT_CHECK(::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
  UT_CHECK(!is_port_free(20999));
  ::closesocket(s);
  UT_CHECK(is_port_free(20999));
  // start_with_service 对被占端口等待超时后仍继续（warn 不失败）
  MockIpc ipc;
  s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  ::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  UT_CHECK(start_with_service(ipc, build_clash_config_payload("c", "p", "f", "d", {}), 20999, 10, 50)
               .is_ok());
  ::closesocket(s);
}

// ---------- 启动模式决策（BUG-298：TUN 非管理员必须走服务，未就绪告警回退） ----------
UT_TEST(service_startup_decision) {
  // 非 TUN → Sidecar，无回退
  auto d1 = decide_startup_mode(false, false, false);
  UT_CHECK(!d1.use_service && !d1.fallback_sysproxy);
  // TUN + 管理员 → Sidecar（自身可建 TUN）
  auto d2 = decide_startup_mode(true, true, false);
  UT_CHECK(!d2.use_service && !d2.fallback_sysproxy);
  // TUN + 非管理员 + 服务就绪 → Service
  auto d3 = decide_startup_mode(true, false, true);
  UT_CHECK(d3.use_service && !d3.fallback_sysproxy);
  // TUN + 非管理员 + 服务未就绪 → 告警 + 回退（禁止静默降级）
  auto d4 = decide_startup_mode(true, false, false);
  UT_CHECK(!d4.use_service && d4.fallback_sysproxy);
}

// ---------- SCM 查询（实机查询：不改动服务状态） ----------
UT_TEST(service_scm_query_live) {
  // 只读查询：clash_verge_service 装没装、跑没跑都合法；关键是不抛异常不崩溃
  bool installed = is_service_installed();
  bool running = is_service_running();
  std::printf("  [scm] installed=%d running=%d\n", installed ? 1 : 0, running ? 1 : 0);
  if (!installed) UT_CHECK(!running);  // 未安装必然未运行
}
