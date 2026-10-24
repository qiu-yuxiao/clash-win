// T04 单测：WebSocket over named pipe（握手/帧编解码/分片/ping-pong/大帧/close）
#include "test_util.h"

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mihomo/pipe_ws.h"
#include "mock_pipe_server.h"  // mo_read/mo_write 帮助函数

using namespace clashwin;
using namespace clashwin::mihomo;

namespace {

std::string got_pong;   // 服务端捕获的客户端 pong 载荷
std::string echoed;     // 服务端捕获的客户端大帧回声

bool ws_send_raw(HANDLE h, const std::string& frame, DWORD timeout_ms) {
  return mockpipe::mo_write(h, frame.data(), static_cast<DWORD>(frame.size()), timeout_ms);
}

std::wstring ws_unique_pipe(const char* tag) {
  static std::atomic<int> seq{0};
  wchar_t buf[128];
  swprintf_s(buf, L"\\\\.\\pipe\\clashwin-t04ws-%hs-%d", tag, seq.fetch_add(1));
  return buf;
}

// ---- 服务端帧编解码（服务端发出的帧不掩码；收客户端帧须去掩码） ----
std::string ws_enc_frame(int opcode, const std::string& payload, bool fin = true) {
  std::string f;
  f += static_cast<char>((fin ? 0x80 : 0) | opcode);
  size_t n = payload.size();
  if (n < 126) f += static_cast<char>(n);
  else if (n <= 0xFFFF) {
    f += static_cast<char>(126);
    f += static_cast<char>((n >> 8) & 0xFF);
    f += static_cast<char>(n & 0xFF);
  } else {
    f += static_cast<char>(127);
    for (int i = 7; i >= 0; --i) f += static_cast<char>((uint64_t(n) >> (i * 8)) & 0xFF);
  }
  f += payload;
  return f;
}

struct WsFrame {
  int opcode;
  std::string payload;
  bool fin;
};

bool ws_read_frame(HANDLE h, WsFrame* out, DWORD timeout_ms = 10000) {
  std::string buf;
  char tmp[16384];
  auto need = [&](size_t n) {
    while (buf.size() < n) {
      DWORD got = 0;
      if (!mockpipe::mo_read(h, tmp, sizeof(tmp), &got, timeout_ms)) return false;
      if (got == 0) return false;
      buf.append(tmp, got);
    }
    return true;
  };
  if (!need(2)) return false;
  uint8_t b0 = uint8_t(buf[0]), b1 = uint8_t(buf[1]);
  bool masked = (b1 & 0x80) != 0;
  uint64_t len = b1 & 0x7F;
  size_t hl = 2;
  if (len == 126) hl = 4;
  else if (len == 127) hl = 10;
  if (masked) hl += 4;
  if (!need(hl)) return false;
  if (len == 126) len = (uint64_t(uint8_t(buf[2])) << 8) | uint8_t(buf[3]);
  else if (len == 127) {
    len = 0;
    for (int i = 2; i < 10; ++i) len = (len << 8) | uint8_t(buf[i]);
  }
  uint8_t mask[4] = {0, 0, 0, 0};
  if (masked) {
    for (int i = 0; i < 4; ++i) mask[i] = uint8_t(buf[hl - 4 + i]);
  }
  if (!need(hl + static_cast<size_t>(len))) return false;
  out->opcode = b0 & 0x0F;
  out->fin = (b0 & 0x80) != 0;
  out->payload.clear();
  for (uint64_t i = 0; i < len; ++i) {
    uint8_t c = uint8_t(buf[hl + i]);
    if (masked) c ^= mask[i % 4];
    out->payload += static_cast<char>(c);
  }
  return true;
}

// ---- mock WS 服务端：完成握手后执行 script(conn_handle) ----
struct WsServer {
  std::wstring name;
  HANDLE thread_handle_pipe = nullptr;  // 仅记录，生命周期由线程管理
  std::thread th;
  std::atomic<bool> running{false};
  std::atomic<bool> handshake_ok{false};
  std::string got_key;
  std::function<void(HANDLE)> script;
  HANDLE stop_ev = nullptr;

  void start() {
    running = true;
    stop_ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    th = std::thread([this] { run(); });
  }
  void stop() {
    if (!running.exchange(false)) return;
    SetEvent(stop_ev);
    if (th.joinable()) th.join();
    if (stop_ev) { CloseHandle(stop_ev); stop_ev = nullptr; }
  }

  void run() {
    HANDLE h = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 2, 1 << 20,
                                1 << 20, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL ok = ConnectNamedPipe(h, &ov);
    DWORD err = ok ? 0 : GetLastError();
    bool have = ok || err == ERROR_PIPE_CONNECTED;
    if (!have && err == ERROR_IO_PENDING) {
      HANDLE waits[2] = {ov.hEvent, stop_ev};
      have = WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0;
    }
    CloseHandle(ov.hEvent);
    if (have && running.load()) serve(h);
    FlushFileBuffers(h);
    DisconnectNamedPipe(h);
    CloseHandle(h);
  }

  void serve(HANDLE h) {
    // 读升级请求
    std::string buf;
    char tmp[8192];
    size_t head_end;
    for (;;) {
      head_end = buf.find("\r\n\r\n");
      if (head_end != std::string::npos) break;
      DWORD got = 0;
      if (!mockpipe::mo_read(h, tmp, sizeof(tmp), &got, 10000)) return;
      if (got == 0) return;
      buf.append(tmp, got);
    }
    std::string head = buf.substr(0, head_end);
    if (head.find("GET") != 0) return;
    if (head.find("Upgrade: websocket") == std::string::npos) return;
    if (head.find("Sec-WebSocket-Version: 13") == std::string::npos) return;
    if (head.find("Connection: Upgrade") == std::string::npos) return;
    // 提取 key 与 Authorization
    size_t kpos = head.find("Sec-WebSocket-Key: ");
    if (kpos == std::string::npos) return;
    size_t kstart = kpos + strlen("Sec-WebSocket-Key: ");
    got_key = head.substr(kstart, head.find("\r\n", kstart) - kstart);
    std::string auth;
    size_t apos = head.find("Authorization: Bearer ");
    if (apos != std::string::npos) {
      size_t astart = apos + strlen("Authorization: Bearer ");
      auth = head.substr(astart, head.find("\r\n", astart) - astart);
    }
    got_auth = auth;
    handshake_ok = true;
    // 101 + accept
    std::string resp = "HTTP/1.1 101 Switching Protocols\r\n"
                       "Upgrade: websocket\r\n"
                       "Connection: Upgrade\r\n"
                       "Sec-WebSocket-Accept: " + ws_compute_accept(got_key) + "\r\n\r\n";
    if (!mockpipe::mo_write(h, resp.data(), static_cast<DWORD>(resp.size()), 10000)) return;
    // 剩余未消费的帧数据（101 后粘连）需要先交给 script？简单起见：把残余留给读帧路径
    // 本 mock 的 script 通过 ws_read_frame / 直接写实现
    if (script) script(h);
  }

  std::string got_auth;
};

// ---- 回调收集器 ----
struct Collector {
  std::mutex mu;
  std::vector<std::string> messages;
  std::vector<uint16_t> close_codes;
  std::vector<std::string> errors;
  std::condition_variable cv;

  void push(const std::string& m) {
    std::lock_guard lk(mu);
    messages.push_back(m);
    cv.notify_all();
  }
  std::vector<std::string> snapshot() {
    std::lock_guard lk(mu);
    return messages;
  }
  bool wait_for_count(size_t n, int timeout_ms) {
    std::unique_lock lk(mu);
    return cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return messages.size() >= n; });
  }
};

}  // namespace

UT_TEST(ws_accept_rfc6455_vector) {
  // RFC 6455 §1.3 示例：固定向量验证 SHA1+base64 实现
  UT_CHECK_EQ(ws_compute_accept("dGhlIHNhbXBsZSBub25jZQ=="),
              std::string("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
}

UT_TEST(ws_handshake_text_and_close) {
  auto name = ws_unique_pipe("basic");
  WsServer srv;
  srv.name = name;
  Collector col;
  srv.script = [&](HANDLE h) {
    ws_send_raw(h, ws_enc_frame(0x1, "hello"), 10000);
    // 收到客户端 close 后回 close
    WsFrame f;
    // 不阻塞等待（客户端可能直接关）——这里短暂等待 close 帧
    if (ws_read_frame(h, &f, 5000)) {
      if (f.opcode == 0x8) ws_send_raw(h, ws_enc_frame(0x8, ""), 10000);
    }
  };
  srv.start();

  PipeWebSocket ws;
  PipeWsCallbacks cbs;
  cbs.on_message = [&](const std::string& m) { col.push(m); };
  cbs.on_close = [&](uint16_t code, const std::string&) { col.close_codes.push_back(code); };
  cbs.on_error = [&](const std::string& e) { col.errors.push_back(e); };
  auto r = ws.connect(name, "/traffic", "secret-123", cbs);
  if (!r.is_ok())
    std::printf("  connect err: %s\n", r.error().message.c_str());
  UT_CHECK(r.is_ok());
  UT_CHECK(srv.handshake_ok.load());
  UT_CHECK(srv.got_auth == "secret-123");
  UT_CHECK(ws.is_open());

  UT_CHECK(col.wait_for_count(1, 5000));
  auto msgs = col.snapshot();
  UT_CHECK(msgs.size() == 1 && msgs[0] == "hello");

  ws.close();  // 主动 close 握手
  UT_CHECK(!ws.is_open());
  srv.stop();
}

UT_TEST(ws_fragmented_message) {
  auto name = ws_unique_pipe("frag");
  WsServer srv;
  srv.name = name;
  Collector col;
  srv.script = [&](HANDLE h) {
    // 三片：text FIN=0 + cont FIN=0 + cont FIN=1
    ws_send_raw(h, ws_enc_frame(0x1, "foo", false), 10000);
    Sleep(30);
    ws_send_raw(h, ws_enc_frame(0x0, "bar", false), 10000);
    Sleep(30);
    ws_send_raw(h, ws_enc_frame(0x0, "baz", true), 10000);
    // ping 夹在中间
    ws_send_raw(h, ws_enc_frame(0x9, "xy"), 10000);
    // 等客户端 pong
    WsFrame f;
    if (ws_read_frame(h, &f, 5000)) {
      if (f.opcode == 0xA) got_pong = f.payload;
    }
    Sleep(50);
    ws_send_raw(h, ws_enc_frame(0x8, ""), 10000);
  };
  srv.start();

  PipeWebSocket ws;
  PipeWsCallbacks cbs;
  cbs.on_message = [&](const std::string& m) { col.push(m); };
  ws.connect(name, "/connections", "", cbs);
  UT_CHECK(col.wait_for_count(1, 5000));
  auto msgs = col.snapshot();
  UT_CHECK(msgs.size() >= 1 && msgs[0] == "foobarbaz");  // 分片重组为一条
  // 服务端异步读 pong：轮询等待
  int wait_ms = 0;
  while (got_pong.empty() && wait_ms < 5000) {
    Sleep(20);
    wait_ms += 20;
  }
  UT_CHECK(got_pong == "xy");  // 自动 pong
  Sleep(300);
  ws.close();
  srv.stop();
}

UT_TEST(ws_big_frame_64bit_length) {
  auto name = ws_unique_pipe("big");
  WsServer srv;
  srv.name = name;
  Collector col;
  std::string big(130000, 'Z');
  for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>('A' + (i % 26));
  srv.script = [&](HANDLE h) {
    // 130KB text 帧：len=130000 > 65535 → 64bit 长度路径
    ws_send_raw(h, ws_enc_frame(0x1, big), 30000);
    // 收客户端回发的大帧（验证客户端 64bit 掩码路径）
    WsFrame f;
    if (ws_read_frame(h, &f, 30000) && f.opcode == 0x1) echoed = f.payload;
    ws_send_raw(h, ws_enc_frame(0x8, ""), 10000);
  };
  srv.start();

  PipeWebSocket ws;
  PipeWsCallbacks cbs;
  cbs.on_message = [&](const std::string& m) { col.push(m); };
  ws.connect(name, "/memory", "", cbs);
  UT_CHECK(col.wait_for_count(1, 10000));
  auto msgs = col.snapshot();
  UT_CHECK(msgs.size() == 1 && msgs[0] == big);
  UT_CHECK_EQ(msgs[0].size(), size_t(130000));

  auto sr = ws.send_text(big);  // 客户端→服务端 64bit 长度 + mask
  UT_CHECK(sr.is_ok());
  int wait = 0;
  while (echoed.empty() && wait < 10000) {
    Sleep(50);
    wait += 50;
  }
  UT_CHECK(echoed == big);
  ws.close();
  srv.stop();
}

UT_TEST(ws_client_send_text) {
  auto name = ws_unique_pipe("send");
  WsServer srv;
  srv.name = name;
  Collector col;
  srv.script = [&](HANDLE h) {
    WsFrame f;
    if (ws_read_frame(h, &f, 10000) && f.opcode == 0x1) {
      // 回声（服务端发不掩码）
      ws_send_raw(h, ws_enc_frame(0x1, f.payload), 10000);
    }
    Sleep(100);
    ws_send_raw(h, ws_enc_frame(0x8, ""), 10000);
  };
  srv.start();

  PipeWebSocket ws;
  PipeWsCallbacks cbs;
  cbs.on_message = [&](const std::string& m) { col.push(m); };
  UT_CHECK(ws.connect(name, "/logs?level=info", "", cbs).is_ok());
  UT_CHECK(ws.send_text("echo-me-42").is_ok());
  UT_CHECK(col.wait_for_count(1, 5000));
  auto msgs = col.snapshot();
  UT_CHECK(msgs.size() >= 1 && msgs[0] == "echo-me-42");
  ws.close();
  srv.stop();
}

UT_TEST(ws_upgrade_rejected) {
  // 服务端拒绝 Upgrade → Protocol 错误分类
  auto name = ws_unique_pipe("reject");
  mockpipe::Server srv(name, 2);
  srv.start([](const mockpipe::Request&) {
    mockpipe::Response r;
    r.status = 400;
    r.reason = "Bad Request";
    r.body = "no websocket";
    return r;
  });

  PipeWebSocket ws;
  PipeWsCallbacks cbs;
  auto r = ws.connect(name, "/traffic", "", cbs);
  UT_CHECK(!r.is_ok());
  UT_CHECK_EQ(r.error().code, static_cast<int>(HttpErrc::Protocol));
  srv.stop();
}
