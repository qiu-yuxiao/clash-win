#include "mihomo/pipe_ws.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <random>
#include <utility>
#include <vector>

#include "util/log.h"

namespace clashwin::mihomo {

namespace {

// ---------------------------------------------------------------------------
// SHA1（RFC 3174 紧凑实现，仅用于 Sec-WebSocket-Accept）
// ---------------------------------------------------------------------------

struct Sha1 {
  uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  uint64_t total = 0;
  uint8_t buf[64] = {};
  size_t buf_len = 0;

  static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

  void block(const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
      w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
             (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999u; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }

  void update(const uint8_t* p, size_t n) {
    total += n;
    while (n > 0) {
      size_t take = std::min(n, size_t(64) - buf_len);
      std::memcpy(buf + buf_len, p, take);
      buf_len += take;
      p += take;
      n -= take;
      if (buf_len == 64) { block(buf); buf_len = 0; }
    }
  }

  std::array<uint8_t, 20> digest() {
    uint64_t bits = total * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t z = 0;
    while (buf_len != 56) update(&z, 1);
    uint8_t lenb[8];
    for (int i = 0; i < 8; ++i) lenb[i] = uint8_t(bits >> (56 - i * 8));
    update(lenb, 8);
    std::array<uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i) {
      out[i * 4] = uint8_t(h[i] >> 24);
      out[i * 4 + 1] = uint8_t(h[i] >> 16);
      out[i * 4 + 2] = uint8_t(h[i] >> 8);
      out[i * 4 + 3] = uint8_t(h[i]);
    }
    return out;
  }
};

std::string base64_encode(const uint8_t* data, size_t n) {
  static const char* kTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((n + 2) / 3 * 4);
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = uint32_t(data[i]) << 16;
    if (i + 1 < n) v |= uint32_t(data[i + 1]) << 8;
    if (i + 2 < n) v |= uint32_t(data[i + 2]);
    out += kTable[(v >> 18) & 63];
    out += kTable[(v >> 12) & 63];
    out += (i + 1 < n) ? kTable[(v >> 6) & 63] : '=';
    out += (i + 2 < n) ? kTable[v & 63] : '=';
  }
  return out;
}

const char kWsGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

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

Error ws_err(const std::string& msg) {
  Error e;
  e.code = static_cast<int>(HttpErrc::Protocol);
  e.message = "Protocol: " + msg;
  return e;
}

Error io_err(const std::string& msg) {
  Error e;
  e.code = static_cast<int>(HttpErrc::Other);
  e.message = "Other: " + msg;
  return e;
}

// 带超时/停止事件的 overlapped 读；stop_event 触发时 CancelIoEx 返回 kError
enum class WsIo { kOk, kTimeout, kError, kStopped };

WsIo ws_read(HANDLE h, HANDLE stop_event, void* dst, DWORD want, DWORD* got, DWORD timeout_ms) {
  OVERLAPPED ov{};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ov.hEvent) return WsIo::kError;
  WsIo result = WsIo::kError;
  BOOL r = ReadFile(h, dst, want, nullptr, &ov);
  if (!r && GetLastError() != ERROR_IO_PENDING) {
    // 立即失败（如 ERROR_BROKEN_PIPE）
  } else {
    HANDLE waits[2] = {ov.hEvent, stop_event};
    DWORD w = WaitForMultipleObjects(2, waits, FALSE, timeout_ms);
    if (w == WAIT_OBJECT_0) {
      DWORD n = 0;
      if (GetOverlappedResult(h, &ov, &n, FALSE)) {
        *got = n;
        result = WsIo::kOk;
      }
    } else if (w == WAIT_OBJECT_0 + 1) {
      CancelIoEx(h, &ov);
      result = WsIo::kStopped;
    } else {
      CancelIoEx(h, &ov);  // 等待超时：取消后可重试（帧间隔可能 > timeout_ms）
      result = WsIo::kTimeout;
    }
  }
  CloseHandle(ov.hEvent);
  return result;
}

WsIo ws_write(HANDLE h, const void* src, DWORD want, DWORD* written, DWORD timeout_ms) {
  OVERLAPPED ov{};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ov.hEvent) return WsIo::kError;
  WsIo result = WsIo::kError;
  BOOL r = WriteFile(h, src, want, nullptr, &ov);
  if (!r && GetLastError() != ERROR_IO_PENDING) {
    // 立即失败
  } else {
    DWORD w = WaitForSingleObject(ov.hEvent, timeout_ms);
    if (w == WAIT_OBJECT_0) {
      DWORD n = 0;
      if (GetOverlappedResult(h, &ov, &n, FALSE)) {
        *written = n;
        result = WsIo::kOk;
      }
    } else {
      CancelIoEx(h, &ov);
      result = WsIo::kTimeout;
    }
  }
  CloseHandle(ov.hEvent);
  return result;
}

// WS 帧缓冲读取：保证 buf 可供 n 字节（含跨读拼接）
struct WsBuf {
  HANDLE h;
  HANDLE stop_event;
  std::string buf;
  size_t pos = 0;
  int timeout_ms = 5000;

  bool ensure(size_t n) {
    while (buf.size() - pos < n) {
      char tmp[16384];
      DWORD got = 0;
      WsIo r = ws_read(h, stop_event, tmp, sizeof(tmp), &got, static_cast<DWORD>(timeout_ms));
      if (r == WsIo::kTimeout) continue;  // 帧间隔可长于单次等待，继续读
      if (r != WsIo::kOk) return false;   // 断开/停止
      if (got == 0) return false;         // EOF/断开
      buf.append(tmp, got);
      if (pos > 0 && buf.size() - pos > (1u << 20)) {  // 防无限增长
        buf.erase(0, pos);
        pos = 0;
      }
    }
    return true;
  }
  uint8_t u8(size_t off) const { return static_cast<uint8_t>(buf[pos + off]); }
};

}  // namespace

std::string ws_compute_accept(const std::string& key) {
  Sha1 sha;
  std::string cat = key + kWsGuid;
  sha.update(reinterpret_cast<const uint8_t*>(cat.data()), cat.size());
  auto d = sha.digest();
  return base64_encode(d.data(), d.size());
}

// ===========================================================================

Result<void> PipeWebSocket::connect(const std::wstring& pipe_name, const std::string& path,
                                    const std::string& secret, PipeWsCallbacks cbs,
                                    int open_timeout_ms) {
  if (open_.load()) return Result<void>::err(io_err("already connected"));
  closing_.store(false, std::memory_order_release);
  cbs_ = std::move(cbs);

  // ---- 打开管道（WaitNamedPipeW 100ms + FALSE 时 80ms 退避，与 HTTP 层同语义，BUG-330/351） ----
  const std::wstring name =
      pipe_name.rfind(L"\\\\.\\pipe\\", 0) == 0 ? pipe_name
                                                : L"\\\\.\\pipe\\" + pipe_name;
  HANDLE h = INVALID_HANDLE_VALUE;
  int busy_left = 24;
  int other_left = 3;
  while (true) {
    h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED, nullptr);
    if (h != INVALID_HANDLE_VALUE) break;
    DWORD err = GetLastError();
    if (err == ERROR_PIPE_BUSY) {
      if (busy_left-- <= 0) {
        Error e;
        e.code = static_cast<int>(HttpErrc::Busy);
        e.message = "Busy: Named pipe busy timeout exceeded";
        return Result<void>::err(e);
      }
      BOOL waited = WaitNamedPipeW(name.c_str(), 100);
      if (!waited) {
        Sleep(80);
      }
    } else {
      if (other_left-- <= 0) {
        HttpErrc cat = (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
                           ? HttpErrc::PipeNotFound
                           : HttpErrc::Other;
        Error e;
        e.code = static_cast<int>(cat);
        e.message = "open named pipe failed (win32=" + std::to_string(err) + ")";
        return Result<void>::err(e);
      }
      Sleep(80);
    }
  }
  handle_ = h;
  stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!stop_event_) {
    CloseHandle(h);
    handle_ = nullptr;
    return Result<void>::err(io_err("create event failed"));
  }

  // ---- Upgrade 握手（← mihomo.rs LocalSocket WS 分支：Host clash-verge） ----
  std::random_device rd;
  uint8_t key_raw[16];
  for (auto& b : key_raw) b = static_cast<uint8_t>(rd());
  std::string ws_key = base64_encode(key_raw, 16);
  std::string req = "GET " + path + " HTTP/1.1\r\n";
  req += "Host: clash-verge\r\n";
  req += "Sec-WebSocket-Key: " + ws_key + "\r\n";
  req += "Connection: Upgrade\r\n";
  req += "Upgrade: websocket\r\n";
  req += "Sec-WebSocket-Version: 13\r\n";
  if (!secret.empty()) req += "Authorization: Bearer " + secret + "\r\n";
  req += "\r\n";

  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(open_timeout_ms);
  {
    const char* p = req.data();
    size_t left = req.size();
    while (left > 0) {
      long long remain = std::chrono::duration_cast<std::chrono::milliseconds>(
                             deadline - std::chrono::steady_clock::now())
                             .count();
      if (remain <= 0) {
        CloseHandle(stop_event_); stop_event_ = nullptr;
        CloseHandle(h); handle_ = nullptr;
        Error e; e.code = static_cast<int>(HttpErrc::Timeout);
        e.message = "Timeout: handshake send timeout";
        return Result<void>::err(e);
      }
      DWORD written = 0;
      WsIo wr = ws_write(h, p, static_cast<DWORD>(left), &written,
                         static_cast<DWORD>(std::min<long long>(remain, 500)));
      if (wr == WsIo::kTimeout) continue;  // 写等待超时：继续到 deadline
      if (wr != WsIo::kOk || written == 0) {
        CloseHandle(stop_event_); stop_event_ = nullptr;
        CloseHandle(h); handle_ = nullptr;
        return Result<void>::err(io_err("handshake write failed"));
      }
      p += written;
      left -= written;
    }
  }

  // 读握手响应头
  std::string head;
  {
    char tmp[4096];
    while (head.find("\r\n\r\n") == std::string::npos) {
      long long remain = std::chrono::duration_cast<std::chrono::milliseconds>(
                             deadline - std::chrono::steady_clock::now())
                             .count();
      if (remain <= 0) {
        CloseHandle(stop_event_); stop_event_ = nullptr;
        CloseHandle(h); handle_ = nullptr;
        Error e; e.code = static_cast<int>(HttpErrc::Timeout);
        e.message = "Timeout: handshake response timeout";
        return Result<void>::err(e);
      }
      DWORD got = 0;
      WsIo rr = ws_read(h, stop_event_, tmp, sizeof(tmp), &got,
                        static_cast<DWORD>(std::min<long long>(remain, 500)));
      if (rr == WsIo::kTimeout) continue;  // 继续等到 deadline
      if (rr != WsIo::kOk) {
        CloseHandle(stop_event_); stop_event_ = nullptr;
        CloseHandle(h); handle_ = nullptr;
        return Result<void>::err(io_err("handshake read failed"));
      }
      if (got == 0) {
        CloseHandle(stop_event_); stop_event_ = nullptr;
        CloseHandle(h); handle_ = nullptr;
        return Result<void>::err(io_err("handshake: connection closed"));
      }
      head.append(tmp, got);
    }
  }

  // 校验 101 与 Sec-WebSocket-Accept
  {
    size_t head_end = head.find("\r\n\r\n");
    std::string head_part = head.substr(0, head_end);
    size_t line_end = head_part.find("\r\n");
    std::string status_line = head_part.substr(0, line_end);
    if (status_line.find("101") == std::string::npos) {
      CloseHandle(stop_event_); stop_event_ = nullptr;
      CloseHandle(h); handle_ = nullptr;
      return Result<void>::err(ws_err("upgrade rejected: " + status_line));
    }
    std::string accept;
    size_t p = line_end + 2;
    while (p < head_part.size()) {
      size_t e = head_part.find("\r\n", p);
      std::string line;
      if (e == std::string::npos) {
        // 最后一行可能无结尾 \r\n（head_part 截去终结符）
        line = head_part.substr(p);
        p = head_part.size();
      } else if (e == p) {
        break;  // 空行
      } else {
        line = head_part.substr(p, e - p);
        p = e + 2;
      }
      size_t colon = line.find(':');
      if (colon == std::string::npos) continue;
      if (lower_ascii(trim(line.substr(0, colon))) == "sec-websocket-accept")
        accept = trim(line.substr(colon + 1));
    }
    std::string expect = ws_compute_accept(ws_key);
    if (accept != expect) {
      CloseHandle(stop_event_); stop_event_ = nullptr;
      CloseHandle(h); handle_ = nullptr;
      return Result<void>::err(ws_err("sec-websocket-accept mismatch"));
    }
    // 101 之后可能已带帧数据，留给读线程处理
    open_.store(true, std::memory_order_release);
    reader_ = std::thread(&PipeWebSocket::reader_loop, this, head.substr(head_end + 4));
    return Result<void>::ok();
  }
}

Result<void> PipeWebSocket::send_frame(int opcode, const std::string& payload,
                                       uint32_t write_timeout_ms) {
  if (!open_.load(std::memory_order_acquire) || !handle_) return Result<void>::ok();
  std::string frame;
  frame += static_cast<char>(0x80 | (opcode & 0x0F));  // FIN=1
  uint8_t mask_key[4];
  std::random_device rd;
  for (auto& b : mask_key) b = static_cast<uint8_t>(rd());
  size_t n = payload.size();
  if (n < 126) {
    frame += static_cast<char>(0x80 | n);  // mask 位=1
  } else if (n <= 0xFFFF) {
    frame += static_cast<char>(0x80 | 126);
    frame += static_cast<char>((n >> 8) & 0xFF);
    frame += static_cast<char>(n & 0xFF);
  } else {
    frame += static_cast<char>(0x80 | 127);
    for (int i = 7; i >= 0; --i) frame += static_cast<char>((static_cast<uint64_t>(n) >> (i * 8)) & 0xFF);
  }
  frame.append(reinterpret_cast<const char*>(mask_key), 4);
  frame.resize(frame.size() + n);
  char* dst = &frame[frame.size() - n];
  for (size_t i = 0; i < n; ++i)
    dst[i] = static_cast<char>(payload[i] ^ mask_key[i % 4]);
  // 写出（带重试窗口：等待超时可重试，管道级失败即报错）
  {
    const char* wp = frame.data();
    size_t wleft = frame.size();
    const uint32_t eff_ms = write_timeout_ms > 0 ? write_timeout_ms : 5000u;
    auto wdeadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(eff_ms);
    bool done = false;
    while (!done) {
      long long remain = std::chrono::duration_cast<std::chrono::milliseconds>(
                             wdeadline - std::chrono::steady_clock::now())
                             .count();
      if (remain <= 0) break;
      DWORD written = 0;
      WsIo wr = ws_write(static_cast<HANDLE>(handle_), wp, static_cast<DWORD>(wleft), &written,
                         static_cast<DWORD>(std::min<long long>(remain, 500)));
      if (wr == WsIo::kTimeout) continue;
      if (wr != WsIo::kOk || written == 0) return Result<void>::err(io_err("ws write failed"));
      wp += written;
      wleft -= written;
      done = wleft == 0;
    }
    if (!done) return Result<void>::err(io_err("ws write failed"));
  }
  return Result<void>::ok();
}

Result<void> PipeWebSocket::send_text(const std::string& payload) {
  return send_frame(0x1, payload);
}

void PipeWebSocket::reader_loop(std::string pending) {
  WsBuf buf{static_cast<HANDLE>(handle_), static_cast<HANDLE>(stop_event_), std::move(pending), 0, 5000};
  std::string msg;        // 分片重组缓冲
  bool in_fragment = false;
  int frag_opcode = 0;

  while (!closing_.load(std::memory_order_acquire)) {
    // ---- 帧头 2 字节 ----
    if (!buf.ensure(2)) break;
    uint8_t b0 = buf.u8(0), b1 = buf.u8(1);
    bool fin = (b0 & 0x80) != 0;
    int opcode = b0 & 0x0F;
    bool masked = (b1 & 0x80) != 0;
    uint64_t len = b1 & 0x7F;
    size_t header_len = 2;
    if (len == 126) header_len = 4;
    else if (len == 127) header_len = 10;
    if (masked) header_len += 4;
    if (!buf.ensure(header_len)) break;
    if (len == 126) len = (uint64_t(buf.u8(2)) << 8) | buf.u8(3);
    else if (len == 127) {
      len = 0;
      for (int i = 2; i < 10; ++i) len = (len << 8) | buf.u8(i);
    }
    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked) {
      mask[0] = buf.u8(header_len - 4);
      mask[1] = buf.u8(header_len - 3);
      mask[2] = buf.u8(header_len - 2);
      mask[3] = buf.u8(header_len - 1);
    }
    if (len > (64u << 20)) {  // 64MB 上限防异常
      if (cbs_.on_error) cbs_.on_error("frame too large");
      break;
    }
    if (!buf.ensure(header_len + static_cast<size_t>(len))) break;

    // ---- 提取载荷（去掩码） ----
    std::string payload;
    payload.reserve(static_cast<size_t>(len));
    size_t base = buf.pos + header_len;
    for (uint64_t i = 0; i < len; ++i) {
      uint8_t c = static_cast<uint8_t>(buf.buf[base + i]);
      if (masked) c ^= mask[i % 4];
      payload += static_cast<char>(c);
    }
    buf.pos = base + static_cast<size_t>(len);

    // ---- 分发 ----
    switch (opcode) {
      case 0x0:  // continuation
        if (!in_fragment) {
          if (cbs_.on_error) cbs_.on_error("unexpected continuation frame");
          goto done;
        }
        msg += payload;
        if (fin) {
          in_fragment = false;
          auto cb = cbs_.on_message;
          if (cb) cb(msg);
          msg.clear();
        }
        break;
      case 0x1:  // text
      case 0x2:  // binary（原版仅转发 Text：← websocket_message_to_channel_body）
        if (fin) {
          auto cb = cbs_.on_message;
          if (cb && opcode == 0x1) cb(payload);
        } else {
          in_fragment = true;
          frag_opcode = opcode;
          msg = payload;
        }
        break;
      case 0x8: {  // close
        uint16_t code = 1000;
        std::string reason;
        if (payload.size() >= 2) {
          code = static_cast<uint16_t>((static_cast<uint16_t>(payload[0]) << 8) | payload[1]);
          reason = payload.substr(2);
        }
        if (!closing_.load()) send_frame(0x8, std::string());  // 回显 close
        auto cb = cbs_.on_close;
        if (cb) cb(code, reason);
        goto done;
      }
      case 0x9:  // ping → 自动 pong（payload 原样返回，≤125B）
        send_frame(0xA, payload);
        break;
      case 0xA:  // pong：忽略
        break;
      default:
        if (cbs_.on_error) cbs_.on_error("unknown opcode " + std::to_string(opcode));
        goto done;
    }
    (void)frag_opcode;
  }
done:
  open_.store(false, std::memory_order_release);
  CancelIoEx(static_cast<HANDLE>(handle_), nullptr);
}

void PipeWebSocket::close(std::optional<uint32_t> force_timeout_ms) {
  close(1000, "Disconnected by client", force_timeout_ms);
}

void PipeWebSocket::close(uint16_t code, const std::string& reason,
                          std::optional<uint32_t> force_timeout_ms) {
  if (!handle_) return;
  bool already = closing_.exchange(true, std::memory_order_acq_rel);
  if (!already && open_.load(std::memory_order_acquire)) {
    // BUG-325: 严格限制 close 帧写超时上限（force_timeout.unwrap_or(1000).max(500)），
    // 且必须在置位 stop_event_ / CancelIoEx 之前发送，杜绝管道满缓冲时无限阻塞或自取消。
    const uint32_t close_timeout_ms =
        (std::max)(force_timeout_ms.value_or(1000u), 500u);
    std::string body;
    body += static_cast<char>((code >> 8) & 0xFF);
    body += static_cast<char>(code & 0xFF);
    body += reason;
    send_frame(0x8, body, close_timeout_ms);
  }
  if (stop_event_) SetEvent(static_cast<HANDLE>(stop_event_));
  // 取消未决 I/O 使读循环尽快退出，然后 join（禁止在回调内调用 close：会自 join 死锁）
  void* h = handle_;
  if (h) CancelIoEx(static_cast<HANDLE>(h), nullptr);
  if (reader_.joinable()) reader_.join();
  handle_ = nullptr;
  if (h) CloseHandle(static_cast<HANDLE>(h));
  if (stop_event_) {
    CloseHandle(static_cast<HANDLE>(stop_event_));
    stop_event_ = nullptr;
  }
  open_.store(false, std::memory_order_release);
}

}  // namespace clashwin::mihomo
