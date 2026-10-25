// T05 WS 四通道订阅管理实现
#include "mihomo/ws_manager.h"

#include "mihomo/pipe_ws.h"

#include <chrono>
#include <memory>
#include <thread>
#include <utility>

namespace clashwin::mihomo {

namespace {
constexpr int kReconnectDelayMs = 1000;  // ← use-mihomo-ws-subscription.ts RECONNECT_DELAY_MS
                                          // （原版为固定 1s 重连，非指数退避）
constexpr int kIdlePollMs = 200;          // 无订阅/空闲轮询步长
constexpr int kOpenTimeoutMs = 5000;
}  // namespace

struct WsManager::Channel {
  std::string ws_path;                 // /traffic /memory /connections /logs?level=
  std::mutex mu;
  std::map<uint64_t, WsHandler> subs;  // 订阅者（引用计数即 subs.size()）
  std::shared_ptr<PipeWebSocket> ws;   // 共享底层 WS
  std::atomic<bool> reopen{false};     // configure() 后要求断开重连
  std::atomic<bool> running{false};
  std::thread worker;
};

WsManager::WsManager() = default;

WsManager::~WsManager() {
  stopping_.store(true, std::memory_order_release);
  // 顺序铁律：先取走 worker/WS 句柄 → 锁外 close WS（join 读线程）→ join worker
  // → 最后才销毁 Channel（worker 循环体仍在引用 Channel，必须 join 后 clear，
  // 否则 use-after-free）
  std::vector<std::thread> workers;
  std::vector<std::shared_ptr<PipeWebSocket>> sockets;
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [key, ch] : channels_) {
      ch->running.store(false, std::memory_order_release);
      if (ch->worker.joinable()) workers.push_back(std::move(ch->worker));
      if (ch->ws) sockets.push_back(std::move(ch->ws));
    }
  }
  for (auto& s : sockets) s->close();
  for (auto& t : workers) t.join();
  {
    std::lock_guard<std::mutex> lk(mu_);
    channels_.clear();
  }
}

void WsManager::configure(const std::wstring& pipe_name, const std::string& secret) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    pipe_name_ = pipe_name;
    secret_ = secret;
    for (auto& [key, ch] : channels_) ch->reopen.store(true, std::memory_order_release);
  }
  disconnect_all();  // 立即断开，worker 以新参数重连
}

WsManager::Channel& WsManager::channel(const std::string& key, const std::string& ws_path) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = channels_.find(key);
  if (it == channels_.end()) {
    auto ch = std::make_unique<Channel>();
    ch->ws_path = ws_path;
    ch->running.store(true, std::memory_order_release);
    Channel* raw = ch.get();
    const std::string path = ws_path;
    ch->worker = std::thread([raw, this, path] { worker_loop(raw, this, path); });
    it = channels_.emplace(key, std::move(ch)).first;
  }
  return *it->second;
}

uint64_t WsManager::subscribe_generic(const std::string& key, const std::string& ws_path,
                                      WsHandler handler) {
  Channel& ch = channel(key, ws_path);
  uint64_t id = next_sub_id_.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lk(ch.mu);
    ch.subs.emplace(id, std::move(handler));
  }
  return id;
}

void WsManager::unsubscribe(uint64_t sub_id) {
  std::shared_ptr<PipeWebSocket> closing;
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [key, ch] : channels_) {
      std::lock_guard<std::mutex> lk2(ch->mu);
      if (ch->subs.erase(sub_id) > 0) {
        // 最后一个订阅者退订：关闭底层 WS（对齐原版 refs=0 → closeSharedSocket）
        if (ch->subs.empty() && ch->ws) {
          closing = std::move(ch->ws);
          break;
        }
      }
    }
  }
  if (closing) closing->close();  // 锁外 close：避免与读线程 on_message 抢锁死锁
}

void WsManager::disconnect_all() {
  std::vector<std::shared_ptr<PipeWebSocket>> sockets;
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [key, ch] : channels_) {
      std::lock_guard<std::mutex> lk2(ch->mu);
      if (ch->ws) {
        sockets.push_back(std::move(ch->ws));
      }
    }
  }
  for (auto& s : sockets) s->close();
}

bool WsManager::channel_connected(const std::string& channel_key) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = channels_.find(channel_key);
  if (it == channels_.end()) return false;
  std::lock_guard<std::mutex> lk2(it->second->mu);
  return it->second->ws && it->second->ws->is_open();
}

uint64_t WsManager::subscribe_traffic(WsHandler handler) {
  return subscribe_generic(kTraffic, "/traffic", std::move(handler));
}

uint64_t WsManager::subscribe_memory(WsHandler handler) {
  return subscribe_generic(kMemory, "/memory", std::move(handler));
}

uint64_t WsManager::subscribe_connections(WsHandler handler) {
  return subscribe_generic(kConnections, "/connections", std::move(handler));
}

uint64_t WsManager::subscribe_logs(LogLevel level, WsHandler handler) {
  return subscribe_generic(logs_key(level), std::string("/logs?level=") + log_level_to_str(level),
                           std::move(handler));
}

void WsManager::worker_loop(Channel* ch, WsManager* self, std::string ws_path) {
  while (ch->running.load(std::memory_order_acquire) &&
         !self->stopping_.load(std::memory_order_acquire)) {
    if (ch->reopen.exchange(false, std::memory_order_acq_rel)) {
      std::shared_ptr<PipeWebSocket> old;
      {
        std::lock_guard<std::mutex> lk(ch->mu);
        old = std::move(ch->ws);
      }
      if (old) old->close();
    }

    bool has_subs = false;
    std::shared_ptr<PipeWebSocket> idle;
    {
      std::lock_guard<std::mutex> lk(ch->mu);
      has_subs = !ch->subs.empty();
      if (!has_subs && ch->ws) {
        // 订阅归零兜底（unsubscribe 已处理常态）；close 会 join 读线程，
        // 而 on_message 需要 ch->mu —— 必须放到锁外执行防死锁
        idle = std::move(ch->ws);
      }
    }
    if (idle) idle->close();
    if (!has_subs) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kIdlePollMs));
      continue;
    }

    bool open = false;
    {
      std::lock_guard<std::mutex> lk(ch->mu);
      open = ch->ws && ch->ws->is_open();
    }
    if (open) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kIdlePollMs));
      continue;
    }

    // 建立新连接（当前参数）
    std::wstring pipe;
    std::string secret;
    {
      std::lock_guard<std::mutex> lk(self->mu_);
      pipe = self->pipe_name_;
      secret = self->secret_;
    }

    Channel* chp = ch;
    WsManager* selfp = self;
    auto ws = std::make_shared<PipeWebSocket>();
    PipeWsCallbacks cbs;
    cbs.on_message = [chp, selfp](const std::string& payload) {
      if (selfp->stopping_.load(std::memory_order_acquire)) return;
      // 锁内拷贝 handlers，锁外分发（回调中可安全 unsubscribe）
      std::vector<WsHandler> handlers;
      {
        std::lock_guard<std::mutex> lk(chp->mu);
        handlers.reserve(chp->subs.size());
        for (auto& [id, h] : chp->subs) handlers.push_back(h);
      }
      for (const auto& h : handlers) h(payload);
    };
    cbs.on_error = [](const std::string&) {};
    cbs.on_close = [](uint16_t, const std::string&) {
      // 断开由 worker 轮询 is_open() 发现；不在此拿锁/清引用
      // （on_close 期间 reader 线程仍持资源，任何 ch->mu 等待都可能死锁）
    };

    auto r = ws->connect(pipe, ws_path, secret, cbs, kOpenTimeoutMs);
    if (r.is_ok()) {
      std::lock_guard<std::mutex> lk(ch->mu);
      ch->ws = std::move(ws);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));
    }
  }
}

}  // namespace clashwin::mihomo
