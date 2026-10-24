#pragma once
// WebSocket over Windows named pipe（mihomo WS 四通道 /traffic /memory /connections /logs?level=
// 共用同一管道，← 02§6 + tauri-plugin-mihomo/mihomo.rs LocalSocket 分支）。
// - Upgrade 握手：Sec-WebSocket-Key(Accept=SHA1+base64)、Sec-WebSocket-Version 13、
//   Host: clash-verge（逐字对齐原版请求构造）
// - 帧编解码：FIN/opcode(text/binary/ping/pong/close)/mask（客户端必掩码）/
//   7bit+16bit+64bit 长度/分片消息重组
// - 自动回 pong；close 帧处理（回显 close 并触发 on_close）
// - 回调式投递：on_message(text) / on_error / on_close，读循环可被 close() 停止
#include "mihomo/http_types.h"
#include "util/result.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace clashwin::mihomo {

struct PipeWsCallbacks {
  std::function<void(const std::string& payload)> on_message;  // 文本帧（重组后）
  std::function<void(const std::string& reason)> on_error;
  std::function<void(uint16_t code, const std::string& reason)> on_close;
};

// RFC 6455 Sec-WebSocket-Accept 计算：base64(sha1(key + GUID))。测试用。
std::string ws_compute_accept(const std::string& sec_websocket_key);

class PipeWebSocket {
 public:
  PipeWebSocket() = default;
  ~PipeWebSocket() { close(); }
  PipeWebSocket(const PipeWebSocket&) = delete;
  PipeWebSocket& operator=(const PipeWebSocket&) = delete;

  // secret 非空时附带 Authorization: Bearer {secret}
  Result<void> connect(const std::wstring& pipe_name, const std::string& path,
                       const std::string& secret, PipeWsCallbacks cbs,
                       int open_timeout_ms = 5000);
  // 外部停止：发 close 帧（尽力）→ 取消未决 I/O → join 读线程
  void close(uint16_t code = 1000, const std::string& reason = {});
  bool is_open() const { return open_.load(std::memory_order_acquire); }
  // 主动发送文本帧（供 T05 扩展使用）
  Result<void> send_text(const std::string& payload);

 private:
  Result<void> send_frame(int opcode, const std::string& payload);
  void reader_loop(std::string pending);

  void* handle_ = nullptr;      // HANDLE
  void* stop_event_ = nullptr;  // HANDLE
  std::thread reader_;
  std::atomic<bool> open_{false};
  std::atomic<bool> closing_{false};
  PipeWsCallbacks cbs_;
};

}  // namespace clashwin::mihomo
