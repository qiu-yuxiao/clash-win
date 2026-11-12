#pragma once
// HTTP/1.1 over Windows named pipe 客户端（mihomo REST 与 clash-verge-service 共用）
// ← crates/tauri-plugin-mihomo/src/ipc.rs（连接池语义、ERROR_PIPE_BUSY 重试 24×125ms）
//   与 docs/phase1/02_backend_census.md §6（min3/max16/idle30s/健康检查60s/拒绝策略 Timeout 5s）。
//
// 线程安全：多线程并发 send()。连接池用 mutex+cv 复刻 hyper 池语义：
//   - 空闲连接复用（校验活性，失效即弃）
//   - 池耗尽 → 按拒绝策略 Timeout 等待 pool_wait_timeout_ms（默认 5s）后报 PoolFull
//   - 后台清理线程按 health_check_interval_s 回收超空闲连接（保留 min_connections）
// 错误码：Result::error().code = static_cast<int>(HttpErrc)。
#include "mihomo/http_types.h"
#include "util/result.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace clashwin::mihomo {

struct PipeHttpConfig {
  int min_connections = 3;              // 池最小保活连接数
  int max_connections = 16;             // 并发上限
  int idle_timeout_s = 30;              // 空闲回收阈值
  int health_check_interval_s = 60;     // 后台清理周期
  int pool_wait_timeout_ms = 2000;      // 拒绝策略 Timeout(2s < 5s 请求超时，BUG-324)
  int busy_retry_count = 24;            // ERROR_PIPE_BUSY 重试次数（BUG-171/172，勿减）
  int pipe_wait_ms = 100;               // WaitNamedPipeW 单步等待上限（BUG-330/351）
  int busy_retry_delay_ms = 80;         // WaitNamedPipeW 返回 FALSE 时的退避间隔（BUG-330/351）
  int open_retry_count = 3;             // 非 BUSY 打开失败重试次数
  int default_timeout_ms = 5000;        // ← mihomo DEFAULT_REQUEST_TIMEOUT (BUG-324: 5s)
  // clash-verge-service 的 kode_bridge 服务端不接受空 body 的 Content-Length 与
  // keep-alive（会直接 EOF）；ServiceIpc 构造时打开这两项以改用 close 语义。
  bool omit_content_length_when_empty = false;
  bool force_connection_close = false;
};

class PipeHttpClient {
 public:
  explicit PipeHttpClient(PipeHttpConfig cfg = {});
  ~PipeHttpClient();
  PipeHttpClient(const PipeHttpClient&) = delete;
  PipeHttpClient& operator=(const PipeHttpClient&) = delete;

  // 目标管道名：接受 "\\\\..\\pipe\\{name}" 或 "{name}"（自动补前缀）。
  // 启动后台清理线程；连接懒创建（对齐原版池语义）。
  Result<void> connect(const std::wstring& pipe_name);
  void set_pool(int min, int max, int idle_s);
  // 关闭全部空闲连接并停止清理线程（重建线程在下次 connect）
  void clear_pool();

  Result<HttpResponse> send(const std::string& method, const std::string& path,
                            const std::map<std::string, std::string>& headers,
                            const std::string& body, int timeout_ms);
  Result<HttpResponse> send(const std::string& method, const std::string& path,
                            const std::map<std::string, std::string>& headers,
                            const std::string& body) {
    return send(method, path, headers, body, cfg_.default_timeout_ms);
  }

  // ---- 观测接口（测试/诊断用） ----
  size_t idle_count();
  bool idle_empty() { return idle_count() == 0; }
  uint64_t connections_created() const { return created_.load(std::memory_order_relaxed); }
  uint64_t connections_reused() const { return reused_.load(std::memory_order_relaxed); }
  const PipeHttpConfig& config() const { return cfg_; }

 private:
  struct Conn {
    void* handle = nullptr;  // HANDLE，避免头文件拖 windows.h
    std::chrono::steady_clock::time_point last_used{};
  };

  static Result<void*> open_pipe(const std::wstring& raw_pipe_name,
                                 const PipeHttpConfig& cfg);  // 锁外 BUSY/NotFound 重试（BUG-330/351）
  static void close_handle(void* h);
  static bool pipe_peek_valid(void* h);             // PeekNamedPipe 活性
  Result<HttpResponse> do_send(Conn& conn, const std::string& method,
                               const std::string& path,
                               const std::map<std::string, std::string>& headers,
                               const std::string& body, int timeout_ms);
  void cleanup_idle();                              // 清理线程体
  std::string pipe_full_path() const;

  PipeHttpConfig cfg_;
  std::wstring pipe_name_;
  bool connected_ = false;

  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Conn> idle_;          // 空闲连接
  int in_use_ = 0;                 // 持有中的连接数（permit 计数）
  bool stopping_ = false;

  std::atomic<uint64_t> created_{0};
  std::atomic<uint64_t> reused_{0};
  std::unique_ptr<std::thread> janitor_;
};

}  // namespace clashwin::mihomo
