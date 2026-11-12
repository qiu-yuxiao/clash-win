#pragma once
// 测试专用：mock mihomo 内核（REST over named pipe + WS 四通道），仅测试链接。
// 响应样例取自真实内核（v1.19.27）与 models.rs 序列化形态；WS 载荷严格按
// docs/phase1/04_round2_clarifications.md §2 协议。
#include "mock_pipe_server.h"

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "mihomo/pipe_ws.h"  // ws_compute_accept

using mockpipe::mo_read;
using mockpipe::mo_write;
using clashwin::mihomo::ws_compute_accept;

namespace mockmihomo {

constexpr const char* kSecret = "mihomo-test-secret";

// ---- WS 帧编解码（服务端不掩码；收客户端帧去掩码） ----
inline std::string ws_enc_frame(int opcode, const std::string& payload, bool fin = true) {
  std::string f;
  f += static_cast<char>((fin ? 0x80 : 0) | opcode);
  size_t n = payload.size();
  if (n < 126) {
    f += static_cast<char>(n);
  } else if (n <= 0xFFFF) {
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

inline bool ws_read_frame(HANDLE h, WsFrame* out, DWORD timeout_ms = 10000) {
  std::string buf;
  char tmp[16384];
  auto need = [&](size_t n) {
    while (buf.size() < n) {
      DWORD got = 0;
      if (!mo_read(h, tmp, sizeof(tmp), &got, timeout_ms)) return false;
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
  if (masked) for (int i = 0; i < 4; ++i) mask[i] = uint8_t(buf[hl - 4 + i]);
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

// ---- mock mihomo 服务端 ----
struct State {
  std::mutex mu;
  std::string last_method;
  std::string last_path;
  std::string last_body;
  int request_count = 0;

  void record(const char* m, const std::string& p, const std::string& b) {
    std::lock_guard lk(mu);
    last_method = m;
    last_path = p;
    last_body = b;
    ++request_count;
  }
  std::string last() {
    std::lock_guard lk(mu);
    return last_method + " " + last_path + " " + last_body;
  }
};

inline std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

class MockMihomo {
 public:
  explicit MockMihomo(const std::wstring& pipe_name, std::string secret = kSecret)
      : pipe_(pipe_name), secret_(std::move(secret)) {}
  ~MockMihomo() { stop(); }

  // 启动统一命名管道服务（单实例池同时按 Upgrade 头分发 REST 与 WebSocket，对齐真实 mihomo 内核）。
  void start(std::function<void(HANDLE, const std::string&)> ws_handler) {
    ws_handler_ = std::move(ws_handler);
    running_ = true;
    ready_count_ = 0;
    stop_ev_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    for (int i = 0; i < kPipeInstances; ++i) {
      threads_.emplace_back([this] { instance_loop(); });
    }
    for (int i = 0; i < 5000 && ready_count_.load() < 1; ++i) Sleep(1);
  }

  void stop() {
    if (!running_.exchange(false)) return;
    if (stop_ev_) SetEvent(stop_ev_);
    std::vector<HANDLE> hs;
    {
      std::lock_guard lk(hmu_);
      hs = live_handles_;
    }
    for (HANDLE h : hs) CancelIoEx(h, nullptr);
    for (auto& t : threads_) {
      if (t.joinable()) t.join();
    }
    threads_.clear();
    if (stop_ev_) {
      CloseHandle(stop_ev_);
      stop_ev_ = nullptr;
    }
  }

  State state;

 private:
  static constexpr int kPipeInstances = 16;
  static constexpr DWORD kMaxPipeInstances = 32;
  // 鉴权：secret 非空时校验 Bearer（← 任务验收 c 项；mihomo TCP 模式行为）
  bool check_auth(const mockpipe::Request& req, mockpipe::Response* resp) {
    if (secret_.empty()) return true;
    auto it = req.headers.find("authorization");
    if (it == req.headers.end() || it->second != "Bearer " + secret_) {
      resp->status = 401;
      resp->reason = "Unauthorized";
      resp->body = "{\"message\":\"Unauthorized\"}";
      return false;
    }
    return true;
  }

  mockpipe::Response rest_handler(const mockpipe::Request& req) {
    mockpipe::Response r;
    r.headers["Content-Type"] = "application/json";
    state.record(req.method.c_str(), req.path, req.body);
    if (!check_auth(req, &r)) return r;

    const std::string& p = req.path;
    const std::string m = req.method;
    auto ok204 = [&] { r.status = 204; r.body.clear(); return r; };

    if (m == "GET" && p == "/version") {
      r.body = R"({"meta":true,"version":"v1.19.27-mock"})";
    } else if (m == "GET" && p == "/configs") {
      r.body =
          R"({"port":0,"socks-port":0,"redir-port":0,"tproxy-port":0,"mixed-port":20801,)"
          R"("tun":{"enable":false,"device":"Mihomo","stack":"Mixed","dns-hijack":["any:53"],)"
          R"("auto-route":true,"auto-detect-interface":true,"file-descriptor":0},)"
          R"("allow-lan":false,"bind-address":"*","mode":"rule","unified-delay":false,)"
          R"("log-level":"info","ipv6":false,"interface-name":"","routing-mark":0,)"
          R"("tcp-concurrent":true,"find-process-mode":"off","sniffing":false,)"
          R"("global-ua":"clash.meta","etag-support":true})";
    } else if (m == "PATCH" && p == "/configs") {
      // 记录 patch 体；模拟内核校验
      if (req.body.find("log-level") == std::string::npos) {
        r.status = 400;
        r.body = R"({"message":"bad patch"})";
      } else {
        return ok204();
      }
    } else if (m == "PUT" && p.rfind("/configs?force=", 0) == 0) {
      return ok204();
    } else if (m == "POST" &&
               (p == "/configs/geo" || p == "/upgrade/geo" || p == "/upgrade/ui" ||
                p.rfind("/upgrade?", 0) == 0 || p.rfind("/upgrade?", 0) == 0 ||
                p == "/upgrade" || p == "/restart" || p == "/cache/fakeip/flush" ||
                p == "/cache/dns/flush")) {
      return ok204();
    } else if (m == "GET" && p == "/proxies") {
      r.body =
          R"({"proxies":{)"
          R"("DIRECT":{"alive":true,"history":[],"extra":{},"name":"DIRECT","udp":true,)"
          R"("uot":true,"type":"Direct","xudp":true,"tfo":false,"mptcp":false,"smux":false,)"
          R"("interface":"lo","dialer-proxy":"","routing-mark":0,"provider-name":""},)"
          R"("PROXY":{"alive":true,"all":["DIRECT"],"history":[{"time":"2026-05-25T00:00:00Z",)"
          R"("delay":123}],"extra":{},"name":"PROXY","now":"DIRECT","udp":true,)"
          R"("type":"Selector","xudp":false,"tfo":false,"mptcp":false,"smux":false,)"
          R"("interface":"","dialer-proxy":"","routing-mark":0,"provider-name":""}}})";
    } else if (m == "GET" && p.rfind("/proxies/", 0) == 0) {
      std::string rest = p.substr(strlen("/proxies/"));
      if (rest.find("/delay") != std::string::npos) {
        // GET /proxies/{name}/delay?timeout=&url=
        r.body = R"({"delay":123})";
      } else {
        r.body =
            R"({"alive":true,"history":[],"extra":{},"name":"DIRECT","udp":true,"uot":true,)"
            R"("type":"Direct","xudp":true,"tfo":false,"mptcp":false,"smux":false,)"
            R"("interface":"lo","dialer-proxy":"","routing-mark":0,"provider-name":""})";
      }
    } else if ((m == "PUT" || m == "DELETE") && p.rfind("/proxies/", 0) == 0) {
      return ok204();
    } else if (m == "GET" && p == "/group") {
      r.body =
          R"({"proxies":[{"alive":true,"all":["DIRECT"],"history":[],"extra":{},"name":"PROXY",)"
          R"("now":"DIRECT","udp":true,"type":"Selector","xudp":false,"tfo":false,"mptcp":false,)"
          R"("smux":false,"interface":"","dialer-proxy":"","routing-mark":0,"provider-name":""}]})";
    } else if (m == "GET" && p.rfind("/group/", 0) == 0) {
      std::string rest = p.substr(strlen("/group/"));
      if (rest.find("/delay") != std::string::npos) {
        r.body = R"({"DIRECT":123,"PROXY":456})";
      } else {
        r.body =
            R"({"alive":true,"all":["DIRECT"],"history":[],"extra":{},"name":"PROXY",)"
            R"("now":"DIRECT","udp":true,"type":"Selector","xudp":false,"tfo":false,)"
            R"("mptcp":false,"smux":false,"interface":"","dialer-proxy":"","routing-mark":0,)"
            R"("provider-name":""})";
      }
    } else if (m == "GET" && p == "/providers/proxies") {
      r.body =
          R"({"providers":{"p1":{"name":"p1","type":"Proxy","vehicleType":"HTTP","proxies":[],)"
          R"("testUrl":"http://t","expectedStatus":"200","updatedAt":"2026-05-25T00:00:00Z",)"
          R"("subscriptionInfo":{"Upload":1,"Download":2,"Total":3,"Expire":0}}}})";
    } else if (m == "PUT" && p.rfind("/providers/proxies/", 0) == 0) {
      return ok204();
    } else if (m == "GET" && p.rfind("/providers/proxies/", 0) == 0) {
      std::string rest = p.substr(strlen("/providers/proxies/"));
      if (rest.find("/healthcheck") != std::string::npos) {
        size_t slashes = static_cast<size_t>(std::count(rest.begin(), rest.end(), '/'));
        if (slashes >= 2) {
          // /providers/proxies/{p}/{n}/healthcheck → 返回延迟
          r.body = R"({"delay":88})";
        } else {
          r.status = 204;
          r.body.clear();
        }
      } else {
        r.body =
            R"({"name":"p1","type":"Proxy","vehicleType":"HTTP","proxies":[],)"
            R"("testUrl":"http://t","expectedStatus":"200","updatedAt":"2026-05-25T00:00:00Z"})";
      }
    } else if (m == "GET" && p == "/rules") {
      r.body =
          R"({"rules":[{"type":"Match","index":0,"payload":"","proxy":"DIRECT","size":0}]})";
    } else if (m == "GET" && p == "/providers/rules") {
      r.body =
          R"({"providers":{"r1":{"behavior":"Domain","format":"YamlRule","name":"r1",)"
          R"("ruleCount":10,"type":"Rule","updatedAt":"2026-05-25T00:00:00Z","vehicleType":"HTTP"}}})";
    } else if (m == "PUT" && p.rfind("/providers/rules/", 0) == 0) {
      return ok204();
    } else if (m == "GET" && p == "/connections") {
      r.body =
          R"({"downloadTotal":1,"uploadTotal":2,"connections":[{"id":"c1","metadata":{)"
          R"("network":"tcp","type":"HTTP","sourceIP":"198.18.0.1","destinationIP":)"
          R"("93.184.216.34","host":"example.com","dnsMode":"normal","sourcePort":"1",)"
          R"("destinationPort":"443"},"chains":["PROXY"],"rule":"MATCH","rulePayload":"",)"
          R"("upload":10,"download":20,"start":"2026-05-25T00:00:00Z"}],"memory":100})";
    } else if (m == "DELETE" && p == "/connections") {
      return ok204();
    } else if (m == "DELETE" && p.rfind("/connections/", 0) == 0) {
      return ok204();
    } else if (m == "GET" && p.rfind("/dns/query", 0) == 0) {
      r.body = R"({"Status":0,"TC":false,"RD":true,"RA":true,)"
               R"("Answer":[{"name":"www.baidu.com.","type":1,"TTL":60,"data":"1.2.3.4"}]})";
    } else {
      r.status = 404;
      r.body = R"({"message":"not found: )" + p + R"("})";
    }
    return r;
  }

  // ---- 统一 REST + WebSocket 命名管道监听循环 ----
  void instance_loop() {
    while (running_.load()) {
      HANDLE h = CreateNamedPipeW(pipe_.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                  kMaxPipeInstances, 1 << 20, 1 << 20, 0, nullptr);
      if (h == INVALID_HANDLE_VALUE) {
        if (!running_.load()) break;
        Sleep(20);
        continue;
      }
      {
        std::lock_guard lk(hmu_);
        live_handles_.push_back(h);
      }
      ready_count_.fetch_add(1);

      OVERLAPPED ov{};
      ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      BOOL ok = ConnectNamedPipe(h, &ov);
      DWORD err = ok ? 0 : GetLastError();
      bool have = ok || err == ERROR_PIPE_CONNECTED;
      if (!have && err == ERROR_IO_PENDING) {
        HANDLE waits[2] = {ov.hEvent, stop_ev_};
        have = WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0;
        if (!have) CancelIoEx(h, &ov);
      }
      CloseHandle(ov.hEvent);

      if (have && running_.load()) serve_connection(h);
      FlushFileBuffers(h);
      DisconnectNamedPipe(h);
      CloseHandle(h);
      {
        std::lock_guard lk(hmu_);
        for (auto it = live_handles_.begin(); it != live_handles_.end(); ++it) {
          if (*it == h) {
            live_handles_.erase(it);
            break;
          }
        }
      }
    }
  }

  void serve_connection(HANDLE h) {
    while (running_.load()) {
      mockpipe::Request req;
      if (!mockpipe::mo_read_request(h, &req)) break;

      auto up_it = req.headers.find("upgrade");
      if (up_it != req.headers.end() && lower(up_it->second) == "websocket") {
        auto key_it = req.headers.find("sec-websocket-key");
        if (key_it == req.headers.end()) break;
        std::string resp = "HTTP/1.1 101 Switching Protocols\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: " + ws_compute_accept(key_it->second) + "\r\n\r\n";
        if (!mo_write(h, resp.data(), static_cast<DWORD>(resp.size()), 10000)) break;
        if (ws_handler_) ws_handler_(h, req.path);

        // 之后读客户端帧直到 close（drains 关闭帧）或服务端停止
        WsFrame f;
        while (running_.load() && ws_read_frame(h, &f, 2000)) {
          if (f.opcode == 0x8) break;
        }
        break;
      }

      mockpipe::Response resp = rest_handler(req);
      mockpipe::mo_write_response(h, resp);
      auto conn_it = req.headers.find("connection");
      bool client_close = conn_it != req.headers.end() &&
                          lower(conn_it->second).find("close") != std::string::npos;
      if (resp.close_after || client_close) break;
    }
  }

  std::wstring pipe_;
  std::string secret_;
  std::function<void(HANDLE, const std::string&)> ws_handler_;
  std::mutex hmu_;
  std::vector<HANDLE> live_handles_;
  std::vector<std::thread> threads_;
  std::atomic<int> ready_count_{0};
  std::atomic<bool> running_{false};
  HANDLE stop_ev_ = nullptr;
};

}  // namespace mockmihomo
