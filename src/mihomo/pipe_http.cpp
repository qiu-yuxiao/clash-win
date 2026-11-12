#include "mihomo/pipe_http.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "util/log.h"

namespace clashwin::mihomo {

namespace {

constexpr wchar_t kPipePrefix[] = L"\\\\.\\pipe\\";

std::string lower_ascii(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

Error make_err(HttpErrc c, std::string msg) {
  Error e;
  e.code = static_cast<int>(c);
  e.message = std::string(errc_name(c)) + ": " + std::move(msg);
  return e;
}

// ---------------------------------------------------------------------------
// 带超时的 overlapped 管道读写
// ---------------------------------------------------------------------------

enum class IoResult { kOk, kTimeout, kError };

// 带超时读：kOk（got 为实际字节数）/ kTimeout（未决 I/O 已取消，可重试）/ kError（管道级失败）
IoResult timed_read(HANDLE h, void* dst, DWORD want, DWORD* got, DWORD timeout_ms) {
  OVERLAPPED ov{};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ov.hEvent) return IoResult::kError;
  IoResult result = IoResult::kError;
  BOOL r = ReadFile(h, dst, want, nullptr, &ov);
  if (!r && GetLastError() != ERROR_IO_PENDING) {
    // 立即失败（如 ERROR_BROKEN_PIPE）
  } else {
    // 同步完成（r==TRUE）时事件同样已被触发，统一走等待路径
    DWORD w = WaitForSingleObject(ov.hEvent, timeout_ms);
    if (w == WAIT_OBJECT_0) {
      DWORD n = 0;
      if (GetOverlappedResult(h, &ov, &n, FALSE)) {
        *got = n;
        result = IoResult::kOk;
      }
    } else {
      CancelIoEx(h, &ov);  // 超时：取消未决 I/O，管道仍可用，上层可继续等待
      result = IoResult::kTimeout;
    }
  }
  CloseHandle(ov.hEvent);
  return result;
}

IoResult timed_write(HANDLE h, const void* src, DWORD want, DWORD* written, DWORD timeout_ms) {
  OVERLAPPED ov{};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ov.hEvent) return IoResult::kError;
  IoResult result = IoResult::kError;
  BOOL r = WriteFile(h, src, want, nullptr, &ov);
  if (!r && GetLastError() != ERROR_IO_PENDING) {
    // 立即失败
  } else {
    DWORD w = WaitForSingleObject(ov.hEvent, timeout_ms);
    if (w == WAIT_OBJECT_0) {
      DWORD n = 0;
      if (GetOverlappedResult(h, &ov, &n, FALSE)) {
        *written = n;
        result = IoResult::kOk;
      }
    } else {
      CancelIoEx(h, &ov);
      result = IoResult::kTimeout;
    }
  }
  CloseHandle(ov.hEvent);
  return result;
}

// ---------------------------------------------------------------------------
// 流式缓冲读取器：HTTP/1.1 响应解析
// ---------------------------------------------------------------------------

struct PipeReader {
  HANDLE h;
  std::string buf;
  size_t pos = 0;              // 已消费偏移
  int timeout_ms = 3000;
  bool timed_out = false;      // 区分真实等待超时与对端断管/EOF（BUG-329/351）

  // 单次补读（deadline 收口）；返回 false = deadline 用尽或管道级失败
  bool read_more(std::chrono::steady_clock::time_point deadline) {
    for (;;) {
      long long remain_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                                std::chrono::steady_clock::now())
              .count();
      if (remain_ms <= 0) {
        timed_out = true;
        return false;
      }
      char tmp[16384];
      DWORD got = 0;
      IoResult r = timed_read(h, tmp, sizeof(tmp), &got,
                              static_cast<DWORD>(std::min<long long>(remain_ms, 500)));
      if (r == IoResult::kTimeout) continue;  // 500ms 内无数据：继续等到 deadline
      if (r == IoResult::kError) return false;
      if (got == 0) {
        // 0 字节"成功"：查管道实际状态，无数据则继续等到 deadline
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false;
        if (avail == 0) continue;
      }
      buf.append(tmp, got);
      return true;
    }
  }

  // 保证缓冲中未消费数据 ≥ n 字节
  bool ensure(size_t n, std::chrono::steady_clock::time_point deadline) {
    while (buf.size() - pos < n) {
      if (!read_more(deadline)) return false;
    }
    return true;
  }

  // 读一行（到 "\r\n"，不含），行内容 trim
  bool read_line(std::string* line, std::chrono::steady_clock::time_point deadline) {
    for (;;) {
      size_t e = buf.find("\r\n", pos);
      if (e != std::string::npos) {
        *line = trim(buf.substr(pos, e - pos));
        pos = e + 2;
        return true;
      }
      if (!read_more(deadline)) return false;
    }
  }

  // 读到 "\r\n\r\n"（含）为止，返回 false = 数据不足/超时
  bool read_until_headers_end(std::chrono::steady_clock::time_point deadline) {
    static const char kEnd[] = "\r\n\r\n";
    while (true) {
      if (buf.size() >= pos + 4) {
        auto it = std::search(buf.begin() + static_cast<long>(pos), buf.end(), kEnd, kEnd + 4);
        if (it != buf.end()) {
          pos = static_cast<size_t>(it - buf.begin()) + 4;
          return true;
        }
      }
      if (!read_more(deadline)) return false;
    }
  }

  // 收缩缓冲，丢弃 pos 之前的部分
  void compact() {
    if (pos == 0) return;
    buf.erase(0, pos);
    pos = 0;
  }
};

// 解析状态行+头部到 line_out/headers（键小写）；head 为含终结符的完整头块
bool parse_head(const std::string& head, int* status,
                std::map<std::string, std::string>* headers) {
  size_t line_end = head.find("\r\n");
  if (line_end == std::string::npos) return false;
  // HTTP/1.1 200 OK
  const std::string sl = head.substr(0, line_end);
  if (sl.rfind("HTTP/", 0) != 0) return false;
  int sp1 = static_cast<int>(sl.find(' '));
  if (sp1 < 0) return false;
  *status = std::atoi(sl.c_str() + sp1 + 1);
  size_t p = line_end + 2;
  while (p < head.size()) {
    size_t e = head.find("\r\n", p);
    if (e == std::string::npos || e == p) break;
    std::string line = head.substr(p, e - p);
    p = e + 2;
    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    (*headers)[lower_ascii(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
  }
  return true;
}

bool str_ieq(const std::string& a, const char* b) {
  return lower_ascii(a) == lower_ascii(b);
}

}  // namespace

const char* errc_name(HttpErrc e) {
  switch (e) {
    case HttpErrc::PipeNotFound: return "PipeNotFound";
    case HttpErrc::Busy: return "Busy";
    case HttpErrc::Timeout: return "Timeout";
    case HttpErrc::Protocol: return "Protocol";
    case HttpErrc::PoolFull: return "PoolFull";
    case HttpErrc::Other: return "Other";
  }
  return "Unknown";
}

// ===========================================================================

PipeHttpClient::PipeHttpClient(PipeHttpConfig cfg) : cfg_(cfg) {}

PipeHttpClient::~PipeHttpClient() { clear_pool(); }

std::string PipeHttpClient::pipe_full_path() const {
  auto narrow = [](const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (wchar_t c : w) s += static_cast<char>(c);  // 管道名为 ASCII
    return s;
  };
  if (pipe_name_.rfind(kPipePrefix, 0) == 0) return narrow(pipe_name_);
  return "\\\\.\\pipe\\" + narrow(pipe_name_);
}

Result<void> PipeHttpClient::connect(const std::wstring& pipe_name) {
  if (pipe_name.empty()) return Result<void>::err(make_err(HttpErrc::Other, "empty pipe name"));
  clear_pool();  // 换管道必须重建
  pipe_name_ = pipe_name;
  {
    std::lock_guard lk(mu_);
    connected_ = true;
    stopping_ = false;
  }
  // 后台清理线程（对齐原版 health_check_interval 周期任务）
  janitor_ = std::make_unique<std::thread>([this] { cleanup_idle(); });
  return Result<void>::ok();
}

void PipeHttpClient::set_pool(int min, int max, int idle_s) {
  std::lock_guard lk(mu_);
  cfg_.min_connections = min;
  cfg_.max_connections = max;
  cfg_.idle_timeout_s = idle_s;
  cv_.notify_all();
}

void PipeHttpClient::close_handle(void* h) {
  if (h) CloseHandle(static_cast<HANDLE>(h));
}

bool PipeHttpClient::pipe_peek_valid(void* h) {
  DWORD avail = 0;
  return PeekNamedPipe(static_cast<HANDLE>(h), nullptr, 0, nullptr, &avail, nullptr) != FALSE;
}

// 打开管道：ERROR_PIPE_BUSY 时优先通过 WaitNamedPipeW(<=100ms) 事件唤醒立即重试，
// 仅在 WaitNamedPipeW 返回 FALSE（如实例暂未创建）时退避 busy_retry_delay_ms（默认 80ms），
// 且全程在连接池互斥锁 mu_ 之外执行，杜绝单条管道慢连接阻塞全池归还/借出（BUG-330 / BUG-351）。
Result<void*> PipeHttpClient::open_pipe(const std::wstring& raw_pipe_name,
                                        const PipeHttpConfig& cfg) {
  // 允许调用方传裸服务名（如 clash-verge-service），此处统一补 \\.\pipe\ 前缀
  const std::wstring name =
      raw_pipe_name.rfind(L"\\\\.\\pipe\\", 0) == 0 ? raw_pipe_name
                                                    : L"\\\\.\\pipe\\" + raw_pipe_name;
  int busy_left = cfg.busy_retry_count;
  int other_left = cfg.open_retry_count;
  const DWORD wait_ms = static_cast<DWORD>(cfg.pipe_wait_ms > 0 ? cfg.pipe_wait_ms : 100);
  const DWORD fallback_ms =
      static_cast<DWORD>(cfg.busy_retry_delay_ms >= 0 ? cfg.busy_retry_delay_ms : 80);
  while (true) {
    HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h != INVALID_HANDLE_VALUE) return Result<void*>::ok(h);
    DWORD err = GetLastError();
    if (err == ERROR_PIPE_BUSY) {
      if (busy_left-- <= 0)
        return Result<void*>::err(
            make_err(HttpErrc::Busy, "Named pipe busy timeout exceeded"));
      BOOL waited = WaitNamedPipeW(name.c_str(), wait_ms);
      if (!waited) {
        Sleep(fallback_ms);
      }
    } else {
      if (other_left-- <= 0) {
        HttpErrc cat = (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
                           ? HttpErrc::PipeNotFound
                           : HttpErrc::Other;
        char buf[128];
        snprintf(buf, sizeof(buf), "Failed to connect to named pipe (win32=%lu)", err);
        return Result<void*>::err(make_err(cat, buf));
      }
      Sleep(fallback_ms);
    }
  }
}

Result<HttpResponse> PipeHttpClient::send(const std::string& method,
                                          const std::string& path,
                                          const std::map<std::string, std::string>& headers,
                                          const std::string& body, int timeout_ms) {
  std::wstring pipe_snapshot;
  PipeHttpConfig cfg_snapshot;

  // ---- permit 获取（拒绝策略 Timeout）----
  {
    std::unique_lock lk(mu_);
    if (!connected_ || pipe_name_.empty())
      return Result<HttpResponse>::err(make_err(HttpErrc::Other, "connect() not called"));
    int max = cfg_.max_connections;
    int wait_ms = cfg_.pool_wait_timeout_ms;
    bool got = cv_.wait_for(lk, std::chrono::milliseconds(wait_ms), [&] {
      return stopping_ || in_use_ < max;
    });
    if (!got || stopping_)
      return Result<HttpResponse>::err(make_err(HttpErrc::PoolFull, "connection pool full"));
    ++in_use_;
    pipe_snapshot = pipe_name_;
    cfg_snapshot = cfg_;
  }
  struct PermitGuard {
    PipeHttpClient* self;
    ~PermitGuard() {
      {
        std::lock_guard lk(self->mu_);
        --self->in_use_;
      }
      self->cv_.notify_one();
    }
  } guard{this};

  // ---- 取连接：锁内仅弹出空闲连接，新建物理管道严格在锁外执行（BUG-330/351） ----
  Conn conn;
  bool fresh = false;
  std::vector<void*> stale_handles;
  {
    std::lock_guard lk(mu_);
    while (!idle_.empty()) {
      Conn c = idle_.back();
      idle_.pop_back();
      auto idle_for = std::chrono::steady_clock::now() - c.last_used;
      if (idle_for > std::chrono::seconds(cfg_.idle_timeout_s) || !pipe_peek_valid(c.handle)) {
        stale_handles.push_back(c.handle);  // 延后到锁外关闭
        continue;
      }
      conn = c;
      reused_.fetch_add(1, std::memory_order_relaxed);
      break;
    }
  }
  for (void* h : stale_handles) close_handle(h);

  if (!conn.handle) {
    auto r = open_pipe(pipe_snapshot, cfg_snapshot);
    if (!r.is_ok()) return Result<HttpResponse>::err(r.error());
    conn.handle = r.value();
    fresh = true;
    created_.fetch_add(1, std::memory_order_relaxed);
  }

  const int eff_timeout = timeout_ms > 0 ? timeout_ms : cfg_snapshot.default_timeout_ms;
  const auto send_start = std::chrono::steady_clock::now();
  auto resp = do_send(conn, method, path, headers, body, eff_timeout);

  // ---- 半关闭空闲管道透明单次重发（BUG-329 / BUG-351） ----
  // 池中复用的空闲命名管道在对端热重载或空闲回收时可能已半关闭（PeekNamedPipe 尚未感知）；
  // 仅对 GET / HEAD 幂等只读请求且非外层超时错误自动丢弃旧连接并新建物理管道重发 1 次，
  // 非幂等写请求（PUT/POST/PATCH/DELETE）严禁盲目重发以免内核重复执行。
  const bool is_idempotent_read = str_ieq(method, "GET") || str_ieq(method, "HEAD");
  if (!resp.is_ok() && !fresh && is_idempotent_read &&
      resp.error().code != static_cast<int>(HttpErrc::Timeout)) {
    close_handle(conn.handle);
    conn.handle = nullptr;
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - send_start)
                                .count();
    const int remain_ms = static_cast<int>(eff_timeout - elapsed_ms);
    if (remain_ms > 0) {
      auto r2 = open_pipe(pipe_snapshot, cfg_snapshot);
      if (!r2.is_ok()) return Result<HttpResponse>::err(r2.error());
      conn.handle = r2.value();
      fresh = true;
      created_.fetch_add(1, std::memory_order_relaxed);
      resp = do_send(conn, method, path, headers, body, remain_ms);
    }
  }

  // ---- 归还或丢弃连接 ----
  bool keep = resp.is_ok() && conn.handle != nullptr;
  if (keep) {
    // 服务端要求关闭则不复用
    if (auto* cc = resp.value().header("connection"); cc && str_ieq(*cc, "close")) keep = false;
    if (cfg_snapshot.force_connection_close) keep = false;
  }
  if (keep && !pipe_peek_valid(conn.handle)) keep = false;
  if (keep) {
    conn.last_used = std::chrono::steady_clock::now();
    std::lock_guard lk(mu_);
    if (stopping_) {
      keep = false;
    } else {
      idle_.push_back(conn);
    }
  }
  if (!keep && conn.handle) close_handle(conn.handle);
  return resp;
}

// 构造请求并解析响应（单次往返，不含池逻辑）
Result<HttpResponse> PipeHttpClient::do_send(
    Conn& conn, const std::string& method, const std::string& path,
    const std::map<std::string, std::string>& headers, const std::string& body,
    int timeout_ms) {
  if (timeout_ms <= 0) timeout_ms = cfg_.default_timeout_ms;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  // ---- 构造请求 ----
  std::string req = method + " " + path + " HTTP/1.1\r\n";
  bool has_host = false;
  for (auto& [k, v] : headers) {
    if (lower_ascii(k) == "host") has_host = true;
    req += k + ": " + v + "\r\n";
  }
  if (!has_host) req += "Host: localhost\r\n";
  if (!(cfg_.omit_content_length_when_empty && body.empty())) {
    req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  }
  req += cfg_.force_connection_close ? "Connection: close\r\n\r\n"
                                     : "Connection: keep-alive\r\n\r\n";
  req += body;

  // ---- 写出（累计超时） ----
  {
    const char* p = req.data();
    size_t left = req.size();
    while (left > 0) {
      long long remain_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                                std::chrono::steady_clock::now())
              .count();
      if (remain_ms <= 0)
        return Result<HttpResponse>::err(make_err(HttpErrc::Timeout, "request send timeout"));
      DWORD written = 0;
      IoResult wr = timed_write(conn.handle, p, static_cast<DWORD>(left), &written,
                                static_cast<DWORD>(std::min<long long>(remain_ms, 500)));
      if (wr == IoResult::kTimeout) continue;  // 写等待超时：继续到 deadline
      if (wr != IoResult::kOk || written == 0)
        return Result<HttpResponse>::err(make_err(HttpErrc::Other, "pipe write failed"));
      p += written;
      left -= written;
    }
  }

  // ---- 读响应 ----
  PipeReader rd{static_cast<HANDLE>(conn.handle), {}, 0, timeout_ms};
  if (!rd.read_until_headers_end(deadline)) {
    if (rd.timed_out) {
      return Result<HttpResponse>::err(make_err(HttpErrc::Timeout, "response headers timeout"));
    }
    return Result<HttpResponse>::err(
        make_err(HttpErrc::Other, "pipe closed before response headers"));
  }

  HttpResponse resp;
  if (!parse_head(rd.buf.substr(0, rd.pos), &resp.status, &resp.headers))
    return Result<HttpResponse>::err(make_err(HttpErrc::Protocol, "bad response head"));

  bool chunked = false;
  if (auto* te = resp.header("transfer-encoding"); te && lower_ascii(*te).find("chunked") != std::string::npos)
    chunked = true;
  bool no_body = resp.status == 204 || resp.status == 304 ||
                 str_ieq(method, "HEAD");
  bool close_delimited = false;
  if (auto* cc = resp.header("connection"); cc && str_ieq(*cc, "close")) close_delimited = true;

  auto body_err = [&](const char* what) {
    return Result<HttpResponse>::err(make_err(HttpErrc::Protocol, what));
  };

  if (no_body) {
    resp.body.clear();
  } else if (chunked) {
    // chunked：size 行 hex\r\n、数据\r\n、……、0\r\n、[trailers、]空行
    std::string size_line;
    for (;;) {
      if (!rd.read_line(&size_line, deadline))
        return body_err("chunked: premature end");
      size_t chunk_size = 0;
      try {
        chunk_size = static_cast<size_t>(std::stoull(size_line, nullptr, 16));
      } catch (...) {
        return body_err("chunked: bad size line");
      }
      if (chunk_size == 0) {
        // 0\r\n 之后是 trailer 段（每行 \r\n 结尾）+ 终结空行
        std::string trailer;
        for (;;) {
          if (!rd.read_line(&trailer, deadline))
            return body_err("chunked: trailer end missing");
          if (trailer.empty()) break;  // 空行 = 结束
          // 非 trailer 声明一律跳过
        }
        break;
      }
      if (!rd.ensure(chunk_size + 2, deadline)) return body_err("chunked: body short");
      resp.body.append(rd.buf, rd.pos, chunk_size);
      rd.pos += chunk_size + 2;  // 跳过数据后的 \r\n
    }
  } else if (auto* cl = resp.header("content-length")) {
    unsigned long long n = 0;
    try {
      n = std::stoull(*cl);
    } catch (...) {
      return body_err("bad content-length");
    }
    size_t have = rd.buf.size() - rd.pos;
    if (have > n) return body_err("buffered beyond content-length");
    if (!rd.ensure(static_cast<size_t>(n), deadline))
      return body_err("body shorter than content-length");
    resp.body.assign(rd.buf, rd.pos, static_cast<size_t>(n));
    rd.pos += static_cast<size_t>(n);
  } else if (close_delimited) {
    // 读到连接关闭为止（管道级失败 = EOF = 正文结束；kTimeout 继续等 deadline）
    for (;;) {
      char tmp[16384];
      DWORD got = 0;
      long long remain_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                deadline - std::chrono::steady_clock::now())
                                .count();
      if (remain_ms <= 0) return body_err("close-delimited body timeout");
      IoResult rr = timed_read(conn.handle, tmp, sizeof(tmp), &got,
                               static_cast<DWORD>(std::min<long long>(remain_ms, 500)));
      if (rr == IoResult::kTimeout) continue;
      if (rr == IoResult::kError) break;  // 服务端已关闭 = 正文结束
      if (got == 0) {
        DWORD avail = 0;
        if (!PeekNamedPipe(conn.handle, nullptr, 0, nullptr, &avail, nullptr)) break;
        if (avail == 0) {
          Sleep(5);
          continue;
        }
        continue;
      }
      resp.body.append(tmp, got);
    }
  } else {
    // 无长度信息且 keep-alive：按 HTTP/1.1 语义视为空 body
    resp.body.clear();
  }
  rd.compact();

  return Result<HttpResponse>::ok(std::move(resp));
}

void PipeHttpClient::cleanup_idle() {
  std::unique_lock lk(mu_);
  while (!cv_.wait_for(lk, std::chrono::seconds(cfg_.health_check_interval_s < 1 ? 1
                                                                                 : cfg_.health_check_interval_s),
                       [&] { return stopping_; })) {
    // 周期到期：回收超时与失效连接，保活 min_connections
    int kept = 0;
    std::deque<Conn> keep;
    auto now = std::chrono::steady_clock::now();
    while (!idle_.empty()) {
      Conn c = idle_.back();
      idle_.pop_back();
      bool expired =
          std::chrono::duration_cast<std::chrono::seconds>(now - c.last_used).count() >=
          cfg_.idle_timeout_s;
      if (!pipe_peek_valid(c.handle) || (expired && kept >= cfg_.min_connections)) {
        close_handle(c.handle);
      } else {
        ++kept;
        keep.push_back(c);
      }
    }
    idle_.swap(keep);
  }
}

void PipeHttpClient::clear_pool() {
  std::deque<Conn> drop;
  {
    std::lock_guard lk(mu_);
    stopping_ = true;
    drop.swap(idle_);
    connected_ = false;
    cv_.notify_all();
  }
  if (janitor_ && janitor_->joinable()) {
    janitor_->join();
    janitor_.reset();
  }
  for (auto& c : drop) close_handle(c.handle);
}

size_t PipeHttpClient::idle_count() {
  std::lock_guard lk(mu_);
  return idle_.size();
}

}  // namespace clashwin::mihomo
