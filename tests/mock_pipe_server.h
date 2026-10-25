#pragma once
// 测试专用：mock 命名管道 HTTP/1.1 服务端（多实例 + 手写响应，仅测试链接，不入库产物）
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mockpipe {

struct Request {
  std::string method;
  std::string path;
  std::map<std::string, std::string> headers;  // 键小写
  std::string body;
};

struct Response {
  int status = 200;
  std::string reason = "OK";
  std::map<std::string, std::string> headers;
  std::string body;
  bool close_after = false;   // 回复后断开连接
  bool chunked = false;       // Transfer-Encoding: chunked 发送
  size_t write_chunk = 8192;  // 写出分片大小（>1 触发多次底层写）
  int write_delay_ms = 0;     // 每片间延迟（迫使客户端多轮读取）
};

using Handler = std::function<Response(const Request&)>;

// ---- overlapped 帮助函数 ----
inline bool mo_read(HANDLE h, void* dst, DWORD want, DWORD* got, DWORD timeout_ms) {
  OVERLAPPED ov{};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ov.hEvent) return false;
  bool ok = false;
  BOOL r = ReadFile(h, dst, want, nullptr, &ov);
  if (!r && GetLastError() != ERROR_IO_PENDING) {
  } else {
    DWORD w = WaitForSingleObject(ov.hEvent, timeout_ms);
    if (w == WAIT_OBJECT_0) {
      DWORD n = 0;
      if (GetOverlappedResult(h, &ov, &n, FALSE)) { *got = n; ok = true; }
    } else {
      CancelIoEx(h, &ov);
    }
  }
  CloseHandle(ov.hEvent);
  return ok;
}

inline bool mo_write(HANDLE h, const void* src, DWORD want, DWORD timeout_ms) {
  const char* p = static_cast<const char*>(src);
  DWORD left = want;
  while (left > 0) {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;
    BOOL r = WriteFile(h, p, left, nullptr, &ov);
    bool ok = false;
    if (!r && GetLastError() != ERROR_IO_PENDING) {
      std::printf("  [srv-dbg] WriteFile immediate fail gle=%lu\n", GetLastError());
      std::fflush(stdout);
      CloseHandle(ov.hEvent);
      return false;
    }
    DWORD w = WaitForSingleObject(ov.hEvent, timeout_ms);
    if (w == WAIT_OBJECT_0) {
      DWORD n = 0;
      ok = GetOverlappedResult(h, &ov, &n, FALSE) != FALSE;
      if (ok) { p += n; left -= n; }
      else { std::printf("  [srv-dbg] GOR fail gle=%lu\n", GetLastError()); std::fflush(stdout); }
    } else {
      std::printf("  [srv-dbg] write wait timeout w=%lu\n", w);
      std::fflush(stdout);
      CancelIoEx(h, &ov);
    }
    CloseHandle(ov.hEvent);
    if (!ok) return false;
  }
  return true;
}

inline std::string mo_lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

// 解析一个请求（阻塞至头+body 完整或失败）；返回 false = 连接已断
inline bool mo_read_request(HANDLE h, Request* req) {
  std::string buf;
  size_t head_end;
  // 读头
  char tmp[8192];
  for (;;) {
    head_end = buf.find("\r\n\r\n");
    if (head_end != std::string::npos) break;
    DWORD got = 0;
    if (!mo_read(h, tmp, sizeof(tmp), &got, 10000)) return false;
    if (got == 0) return false;
    buf.append(tmp, got);
  }
  std::string head = buf.substr(0, head_end);
  size_t body_start = head_end + 4;
  // 状态行
  size_t line_end = head.find("\r\n");
  std::string req_line = head.substr(0, line_end);
  size_t sp1 = req_line.find(' ');
  size_t sp2 = req_line.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) return false;
  req->method = req_line.substr(0, sp1);
  req->path = req_line.substr(sp1 + 1, sp2 - sp1 - 1);
  // 头
  size_t p = line_end + 2;
  while (p < head.size()) {
    size_t e = head.find("\r\n", p);
    if (e == std::string::npos || e == p) break;
    std::string line = head.substr(p, e - p);
    p = e + 2;
    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string key = mo_lower(line.substr(0, colon));
    size_t vs = line.find_first_not_of(" \t", colon + 1);
    req->headers[key] = vs == std::string::npos ? "" : line.substr(vs);
  }
  // body
  size_t content_len = 0;
  auto it = req->headers.find("content-length");
  if (it != req->headers.end()) content_len = strtoul(it->second.c_str(), nullptr, 10);
  std::string body = buf.substr(body_start);
  while (body.size() < content_len) {
    DWORD got = 0;
    if (!mo_read(h, tmp, sizeof(tmp), &got, 10000)) return false;
    if (got == 0) return false;
    body.append(tmp, got);
  }
  req->body = body.substr(0, content_len);
  return true;
}

inline void mo_write_response(HANDLE h, const Response& r) {
  std::string head = "HTTP/1.1 " + std::to_string(r.status) + " " + r.reason + "\r\n";
  bool chunked = r.chunked;
  for (auto& [k, v] : r.headers) {
    head += k + ": " + v + "\r\n";
    if (mo_lower(k) == "transfer-encoding" && mo_lower(v).find("chunked") != std::string::npos)
      chunked = true;
  }
  if (chunked) head += "Transfer-Encoding: chunked\r\n";
  else head += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
  head += "Connection: keep-alive\r\n\r\n";
  if (!mo_write(h, head.data(), static_cast<DWORD>(head.size()), 10000)) {
    std::printf("  [srv-dbg] header write FAILED\n");
    std::fflush(stdout);
  }
  if (chunked) {
    size_t off = 0;
    if (r.body.empty()) {
      mo_write(h, "0\r\n\r\n", 5, 10000);
    } else {
      while (off < r.body.size()) {
        size_t n = std::min(r.write_chunk, r.body.size() - off);
        char sz[32];
        sprintf_s(sz, sizeof(sz), "%zX\r\n", n);
        mo_write(h, sz, static_cast<DWORD>(strlen(sz)), 10000);
        mo_write(h, r.body.data() + off, static_cast<DWORD>(n), 10000);
        mo_write(h, "\r\n", 2, 10000);
        off += n;
        if (r.write_delay_ms) Sleep(static_cast<DWORD>(r.write_delay_ms));
      }
      mo_write(h, "0\r\n\r\n", 5, 10000);
    }
  } else if (!r.body.empty()) {
    size_t off = 0;
    while (off < r.body.size()) {
      size_t n = std::min(r.write_chunk, r.body.size() - off);
      mo_write(h, r.body.data() + off, static_cast<DWORD>(n), 10000);
      off += n;
      if (r.write_delay_ms) Sleep(static_cast<DWORD>(r.write_delay_ms));
    }
  }
}

class Server {
 public:
  // threads：accept 线程数；max_instances：管道实例上限（REST+WS 共用同一管道名时
  // 需大于 threads 总和，否则后创建的 handle 因超出 nMaxInstance 失败）
  explicit Server(std::wstring name, int threads, int max_instances = -1)
      : name_(std::move(name)), instances_(threads), max_instances_(max_instances) {}
  ~Server() { stop(); }

  void start(Handler handler) {
    handler_ = std::move(handler);
    running_ = true;
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    for (int i = 0; i < instances_; ++i)
      threads_.emplace_back([this] { instance_loop(); });
    // 【flaky 根修】等待首个管道实例真正创建（instance_loop 线程内才
    // CreateNamedPipeW）。此前 start() 立即返回，客户端首请求可能撞上
    // FILE_NOT_FOUND 窗口（重试 3 次即耗尽）——CPU 负载下呈 1/8 概率 flaky。
    ready_count_ = 0;
    for (int i = 0; i < 5000 && ready_count_.load() < 1; ++i) Sleep(1);
  }

  void stop() {
    if (!running_.exchange(false)) return;
    SetEvent(stop_event_);
    std::vector<HANDLE> hs;
    {
      std::lock_guard lk(hmu_);
      hs = live_handles_;
    }
    for (HANDLE h : hs) CancelIoEx(h, nullptr);
    for (auto& t : threads_)
      if (t.joinable()) t.join();
    threads_.clear();
    if (stop_event_) { CloseHandle(stop_event_); stop_event_ = nullptr; }
  }

 private:
  void instance_loop() {
    for (;;) {
      if (!running_.load()) break;
      HANDLE h = CreateNamedPipeW(
          name_.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
          static_cast<DWORD>(max_instances_ < 0 ? instances_ : max_instances_), 1 << 20,
          1 << 20, 0, nullptr);
      if (h == INVALID_HANDLE_VALUE) break;
      {
        std::lock_guard lk(hmu_);
        live_handles_.push_back(h);
      }
      ready_count_.fetch_add(1);
      // ConnectNamedPipe（等待客户端或停止）
      OVERLAPPED ov{};
      ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      BOOL connected = ConnectNamedPipe(h, &ov);
      DWORD err = connected ? 0 : GetLastError();
      bool have_client = false;
      if (connected) have_client = true;
      else if (err == ERROR_IO_PENDING) {
        HANDLE waits[2] = {ov.hEvent, stop_event_};
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0) have_client = true;
      } else if (err == ERROR_PIPE_CONNECTED) {
        have_client = true;
      }
      CloseHandle(ov.hEvent);
      if (have_client && running_.load()) {
        // 连接级循环：keep-alive
        for (;;) {
          Request req;
          if (!mo_read_request(h, &req)) break;
          Response resp = handler_(req);
          mo_write_response(h, resp);
          auto it = req.headers.find("connection");
          bool client_close = it != req.headers.end() && mo_lower(it->second).find("close") != std::string::npos;
          if (resp.close_after || client_close) break;
        }
      }
      FlushFileBuffers(h);
      DisconnectNamedPipe(h);
      CloseHandle(h);
      {
        std::lock_guard lk(hmu_);
        for (auto it = live_handles_.begin(); it != live_handles_.end(); ++it)
          if (*it == h) { live_handles_.erase(it); break; }
      }
    }
  }

  std::wstring name_;
  int instances_;       // accept 线程数
  int max_instances_;   // 管道实例上限（<0 取 instances_）
  Handler handler_;
  std::atomic<bool> running_{false};
  HANDLE stop_event_ = nullptr;
  std::mutex hmu_;
  std::vector<HANDLE> live_handles_;
  std::vector<std::thread> threads_;
  std::atomic<int> ready_count_{0};
};

}  // namespace mockpipe
