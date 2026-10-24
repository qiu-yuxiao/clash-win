// T04 单测：管道 HTTP/1.1 客户端（mock 服务端全覆盖）
#include "test_util.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "mihomo/pipe_http.h"
#include "mock_pipe_server.h"

using namespace clashwin;
using namespace clashwin::mihomo;
using namespace mockpipe;

namespace {

std::wstring unique_pipe(const char* tag) {
  static std::atomic<int> seq{0};
  wchar_t buf[128];
  swprintf_s(buf, L"\\\\.\\pipe\\clashwin-t04-%hs-%d", tag, seq.fetch_add(1));
  return buf;
}

PipeHttpConfig fast_cfg() {
  PipeHttpConfig c;
  c.busy_retry_count = 24;
  c.busy_retry_delay_ms = 25;  // 加速测试注入
  c.open_retry_count = 3;
  c.health_check_interval_s = 1;
  return c;
}

// JSON 回声处理器：{method, path, body_len}
Response echo_handler(const Request& req) {
  Response r;
  r.headers["Content-Type"] = "application/json";
  r.body = "{\"method\":\"" + req.method + "\",\"path\":\"" + req.path +
           "\",\"body_len\":" + std::to_string(req.body.size()) + "}";
  return r;
}

}  // namespace

UT_TEST(http_get_roundtrip) {
  auto name = unique_pipe("get");
  Server srv(name, 4);
  srv.start(echo_handler);

  PipeHttpClient c(fast_cfg());
  UT_CHECK(c.connect(name).is_ok());
  auto r = c.send("GET", "/version", {}, "");
  UT_CHECK(r.is_ok());
  UT_CHECK_EQ(r.value().status, 200);
  UT_CHECK(r.value().body.find("\"method\":\"GET\"") != std::string::npos);
  UT_CHECK(r.value().body.find("\"path\":\"/version\"") != std::string::npos);
  srv.stop();
}

UT_TEST(http_post_put_delete_roundtrip) {
  auto name = unique_pipe("ppd");
  Server srv(name, 4);
  srv.start(echo_handler);

  PipeHttpClient c(fast_cfg());
  UT_CHECK(c.connect(name).is_ok());

  auto post = c.send("POST", "/configs", {{"Content-Type", "application/json"}}, "{\"hello\":1}");
  UT_CHECK(post.is_ok());
  UT_CHECK(post.value().body.find("\"method\":\"POST\"") != std::string::npos);
  UT_CHECK(post.value().body.find("\"body_len\":11") != std::string::npos);

  auto put = c.send("PUT", "/configs?force=true", {}, "payload-put");
  UT_CHECK(put.is_ok());
  UT_CHECK(put.value().body.find("\"method\":\"PUT\"") != std::string::npos);

  auto del = c.send("DELETE", "/connections/abc", {}, "");
  UT_CHECK(del.is_ok());
  UT_CHECK(del.value().body.find("\"method\":\"DELETE\"") != std::string::npos);
  srv.stop();
}

UT_TEST(http_chunked_response) {
  auto name = unique_pipe("chunked");
  Server srv(name, 2);
  srv.start([](const Request&) {
    Response r;
    r.chunked = true;
    r.write_chunk = 7;  // 强制多 chunk
    r.body = std::string("0123456789", 10) + std::string("abcdefghij", 10);
    return r;
  });

  PipeHttpClient c(fast_cfg());
  UT_CHECK(c.connect(name).is_ok());
  auto r = c.send("GET", "/chunked", {}, "");
  UT_CHECK(r.is_ok());
  UT_CHECK_EQ(r.value().body.size(), size_t(20));
  UT_CHECK(r.value().body == std::string("0123456789abcdefghij"));
  srv.stop();
}

UT_TEST(http_large_body_multiread) {
  auto name = unique_pipe("large");
  std::string big;
  big.reserve(200 * 1024);
  for (int i = 0; i < 200 * 1024; ++i) big += static_cast<char>('a' + (i % 26));
  Server srv(name, 2);
  srv.start([&big](const Request&) {
    Response r;
    r.body = big;
    r.write_chunk = 4096;     // 强制多次底层写/读
    r.write_delay_ms = 1;     // 撕裂读写时序
    return r;
  });

  PipeHttpClient c(fast_cfg());
  UT_CHECK(c.connect(name).is_ok());
  auto r = c.send("GET", "/big", {}, "");
  UT_CHECK(r.is_ok());
  if (r.is_ok()) {
    UT_CHECK_EQ(r.value().body.size(), size_t(200 * 1024));
    UT_CHECK(r.value().body == big);
  }
  srv.stop();
}

UT_TEST(http_connection_reuse) {
  auto name = unique_pipe("reuse");
  Server srv(name, 4);
  srv.start(echo_handler);

  PipeHttpClient c(fast_cfg());
  c.set_pool(3, 16, 30);
  UT_CHECK(c.connect(name).is_ok());
  for (int i = 0; i < 3; ++i) {
    auto r = c.send("GET", "/x", {}, "");
    UT_CHECK(r.is_ok());
  }
  // 顺序复用同一连接：只建 1 条，复用 ≥2 次
  UT_CHECK_EQ(c.connections_created(), uint64_t(1));
  UT_CHECK(c.connections_reused() >= 2);
  srv.stop();
}

UT_TEST(http_pool_grows_beyond_min) {
  auto name = unique_pipe("grow");
  Server srv(name, 16);
  srv.start([](const Request&) {
    Response r;
    r.body = "ok";
    Sleep(100);  // 拖慢响应迫使并发建新连接
    return r;
  });

  PipeHttpConfig cfg = fast_cfg();
  cfg.min_connections = 3;
  cfg.max_connections = 16;
  PipeHttpClient c(cfg);
  UT_CHECK(c.connect(name).is_ok());
  UT_CHECK_EQ(c.connections_created(), uint64_t(0));  // 懒创建

  std::vector<std::thread> ts;
  std::atomic<int> ok_count{0};
  for (int t = 0; t < 8; ++t)
    ts.emplace_back([&] {
      for (int i = 0; i < 5; ++i) {
        auto r = c.send("GET", "/g", {}, "");
        if (r.is_ok()) ok_count.fetch_add(1);
      }
    });
  for (auto& t : ts) t.join();
  UT_CHECK_EQ(ok_count.load(), 40);
  // 并发 8 > min 3：必然新建超过 3 条
  UT_CHECK(c.connections_created() > 3);
  srv.stop();
}

UT_TEST(http_busy_retry_then_success) {
  // 复现 ERROR_PIPE_BUSY：占住唯一实例 → 客户端重试 → 释放后成功
  auto name = unique_pipe("busy");

  // 1) 建一个单实例管道并让占用者连接它
  HANDLE h1 = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                               PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096,
                               0, nullptr);
  UT_CHECK(h1 != INVALID_HANDLE_VALUE);
  HANDLE occupier = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, 0, nullptr);
  UT_CHECK(occupier != INVALID_HANDLE_VALUE);  // 实例被占用

  // 2) 此时连接池客户端应吃 BUSY 并重试；300ms 后释放并起真服务
  PipeHttpConfig cfg = fast_cfg();
  cfg.busy_retry_count = 40;  // 40×25ms = 1s 窗口
  cfg.busy_retry_delay_ms = 25;
  PipeHttpClient c(cfg);
  UT_CHECK(c.connect(name).is_ok());

  std::unique_ptr<Server> srv;
  std::thread releaser([&] {
    Sleep(300);
    CloseHandle(occupier);
    CloseHandle(h1);
    // 释放后再起正式 mock 服务
    srv = std::make_unique<Server>(name, 2);
    srv->start(echo_handler);
  });

  auto r = c.send("GET", "/after-busy", {}, "");
  releaser.join();
  UT_CHECK(r.is_ok());
  UT_CHECK_EQ(r.value().status, 200);
  UT_CHECK(r.value().body.find("\"path\":\"/after-busy\"") != std::string::npos);
  if (srv) srv->stop();
}

UT_TEST(http_busy_retry_exhausted) {
  // BUSY 重试耗尽 → 错误分类 Busy（供 T05/T07 降级判定）
  auto name = unique_pipe("busyex");
  HANDLE h1 = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                               PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096,
                               0, nullptr);
  UT_CHECK(h1 != INVALID_HANDLE_VALUE);
  HANDLE occupier = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, 0, nullptr);
  UT_CHECK(occupier != INVALID_HANDLE_VALUE);

  PipeHttpConfig cfg = fast_cfg();
  cfg.busy_retry_count = 2;   // 注入：2 次即耗尽
  cfg.busy_retry_delay_ms = 25;
  PipeHttpClient c(cfg);
  UT_CHECK(c.connect(name).is_ok());
  auto r = c.send("GET", "/x", {}, "");
  UT_CHECK(!r.is_ok());
  UT_CHECK_EQ(r.error().code, static_cast<int>(HttpErrc::Busy));
  CloseHandle(occupier);
  CloseHandle(h1);
}

UT_TEST(http_pool_wait_timeout) {
  // 池耗尽 + 拒绝策略 Timeout：等待 pool_wait_timeout_ms 后报 PoolFull
  auto name = unique_pipe("poolfull");
  std::atomic<bool> handler_entered{false};
  Server srv(name, 16);
  srv.start([&handler_entered](const Request&) {
    handler_entered = true;
    Sleep(600);
    Response r;
    r.body = "slow";
    return r;
  });

  PipeHttpConfig cfg = fast_cfg();
  cfg.max_connections = 1;
  cfg.pool_wait_timeout_ms = 200;  // 注入加速
  cfg.min_connections = 0;
  PipeHttpClient c(cfg);
  UT_CHECK(c.connect(name).is_ok());

  bool worker_ok = false;
  std::string worker_err;
  std::thread worker([&] {
    auto r = c.send("GET", "/slow", {}, "");
    worker_ok = r.is_ok();
    if (!r.is_ok()) worker_err = r.error().message;
  });
  // 确定性同步：等服务端真正开始处理 worker 的请求（= worker 已持有唯一 permit）
  while (!handler_entered.load()) Sleep(5);

  auto r2 = c.send("GET", "/blocked", {}, "");
  UT_CHECK(!r2.is_ok());
  UT_CHECK_EQ(r2.error().code, static_cast<int>(HttpErrc::PoolFull));

  worker.join();
  if (!worker_ok) std::printf("  worker_err: %s\n", worker_err.c_str());
  UT_CHECK(worker_ok);
  srv.stop();
}

UT_TEST(http_idle_reclaim) {
  // 空闲回收：idle_timeout=1s、健康检查=1s → 2.5s 后空闲连接被清
  auto name = unique_pipe("idle");
  Server srv(name, 4);
  srv.start(echo_handler);

  PipeHttpConfig cfg = fast_cfg();
  cfg.min_connections = 0;
  cfg.idle_timeout_s = 1;
  cfg.health_check_interval_s = 1;
  PipeHttpClient c(cfg);
  UT_CHECK(c.connect(name).is_ok());

  auto r = c.send("GET", "/i", {}, "");
  UT_CHECK(r.is_ok());
  UT_CHECK_EQ(c.idle_count(), size_t(1));  // 归还进池

  Sleep(2500);
  UT_CHECK_EQ(c.idle_count(), size_t(0));  // 后台清理线程回收
  srv.stop();
}

UT_TEST(http_pipe_not_found) {
  PipeHttpConfig cfg = fast_cfg();
  cfg.open_retry_count = 1;  // 加速
  PipeHttpClient c(cfg);
  UT_CHECK(c.connect(L"\\\\.\\pipe\\clashwin-t04-definitely-not-exist").is_ok());
  auto r = c.send("GET", "/v", {}, "");
  UT_CHECK(!r.is_ok());
  UT_CHECK_EQ(r.error().code, static_cast<int>(HttpErrc::PipeNotFound));
}
