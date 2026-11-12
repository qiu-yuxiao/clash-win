// T05 WS 四通道订阅管理实现
#include "mihomo/ws_manager.h"

#include <nlohmann/json.hpp>

#include "events/event_bus.h"
#include "events/event_names.h"
#include "mihomo/pipe_ws.h"
#include "util/log.h"
#include "util/utf.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <memory>
#include <random>
#include <set>
#include <thread>
#include <utility>

namespace clashwin::mihomo {

namespace {
constexpr uint32_t kBaseReconnectDelayMs = 1000;
constexpr uint32_t kMaxReconnectDelayMs = 15000;
constexpr int kIdlePollMs = 200;          // 无订阅/空闲轮询步长
constexpr int kOpenTimeoutMs = 5000;
constexpr size_t kConnViewportMax = 100;  // ← mihomo.rs ws_connections viewport 分页
constexpr uint64_t kSnapshotEvery = 100;  // 每 100 帧全量 snapshot 防漂移

// ---------- connections 通道裸流 → snapshot/delta 转换器 ----------
// ← mihomo.rs ws_connections_checked：内核 /connections WS 推的是全量 Connections
// JSON，snapshot/delta 信封（04§2）由插件层生成：
//   - epoch_id = 16 位 hex 随机；sequence_id 从 0 递增
//   - viewport：活跃连接 >100 时按 upload+download 降序截取前 100
//   - sequence_id==0 或 %100==0 → 全量 snapshot；其余 → delta（added/removed/
//     updated 扁平步长3，仅 upload/download 变化的连接计入 updated）
class ConnectionsTransformer {
 public:
  ConnectionsTransformer() {
    std::random_device rd;
    epoch_id_ = fmt_hex(rd()) + fmt_hex(rd());  // 64bit 随机 → 16 hex
  }

  // 返回待分发的 JSON 字符串；解析失败返回空串（丢弃，对齐原版 log+continue）
  std::string process(const std::string& raw) {
    nlohmann::json j = nlohmann::json::parse(raw, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return {};
    Connections frame = Connections::from_json(j);

    // viewport 截断（← mihomo.rs：按活跃度 upload+download 降序取前 100）
    std::vector<ConnectionItem> list = std::move(frame.connections);
    if (list.size() > kConnViewportMax) {
      std::stable_sort(list.begin(), list.end(), [](const ConnectionItem& a, const ConnectionItem& b) {
        return (a.upload + a.download) > (b.upload + b.download);
      });
      list.resize(kConnViewportMax);
    }

    bool send_full = sequence_id_ == 0 || sequence_id_ % kSnapshotEvery == 0;
    nlohmann::json data;
    data["epochId"] = epoch_id_;
    data["sequenceId"] = sequence_id_;
    data["downloadTotal"] = frame.download_total;
    data["uploadTotal"] = frame.upload_total;
    data["memory"] = frame.memory;

    nlohmann::json msg;
    if (send_full) {
      last_.clear();
      nlohmann::json arr = nlohmann::json::array();
      for (const auto& c : list) {
        last_[c.id] = c;
        arr.push_back(conn_to_json(c));
      }
      data["connections"] = std::move(arr);
      msg["type"] = "snapshot";
    } else {
      nlohmann::json added = nlohmann::json::array();
      nlohmann::json updated = nlohmann::json::array();
      std::set<std::string> current_ids;
      for (const auto& c : list) {
        current_ids.insert(c.id);
        auto it = last_.find(c.id);
        if (it != last_.end()) {
          if (it->second.upload != c.upload || it->second.download != c.download) {
            updated.push_back(c.id);
            updated.push_back(c.upload);
            updated.push_back(c.download);
            it->second.upload = c.upload;
            it->second.download = c.download;
          }
        } else {
          added.push_back(conn_to_json(c));
          last_[c.id] = c;
        }
      }
      nlohmann::json removed = nlohmann::json::array();
      for (auto it = last_.begin(); it != last_.end();) {
        if (!current_ids.count(it->first)) {
          removed.push_back(it->first);
          it = last_.erase(it);
        } else {
          ++it;
        }
      }
      data["added"] = std::move(added);
      data["removed"] = std::move(removed);
      data["updated"] = std::move(updated);
      msg["type"] = "delta";
    }
    msg["data"] = std::move(data);
    ++sequence_id_;
    return msg.dump();
  }

 private:
  static std::string fmt_hex(uint64_t v) {
    static const char* hex = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i) {
      s[i] = hex[v & 0xF];
      v >>= 4;
    }
    return s;
  }

  static nlohmann::json conn_to_json(const ConnectionItem& c) {
    nlohmann::json j;
    j["id"] = c.id;
    const auto& m = c.metadata;
    j["metadata"] = {{"network", m.network},
                     {"type", m.type},
                     {"sourceIP", m.source_ip},
                     {"destinationIP", m.destination_ip},
                     {"sourcePort", m.source_port},
                     {"destinationPort", m.destination_port},
                     {"host", m.host},
                     {"dnsMode", m.dns_mode},
                     {"process", m.process},
                     {"processPath", m.process_path}};
    j["upload"] = c.upload;
    j["download"] = c.download;
    j["start"] = c.start;
    j["chains"] = c.chains;
    j["rule"] = c.rule;
    j["rulePayload"] = c.rule_payload;
    return j;
  }

  std::string epoch_id_;
  uint64_t sequence_id_ = 0;
  std::map<std::string, ConnectionItem> last_;
};

}  // namespace

uint32_t compute_ws_reconnect_delay_ms(uint32_t attempt, double rand01) {
  const uint32_t capped_exp = (std::min)(attempt, 4u);
  const uint32_t base = (std::min)(kBaseReconnectDelayMs << capped_exp, kMaxReconnectDelayMs);
  const double min_delay = std::round(static_cast<double>(base) * 0.8);
  const double max_delay =
      (std::min)(std::round(static_cast<double>(base) * 1.2),
                 static_cast<double>(kMaxReconnectDelayMs));
  const double clamped = std::clamp(rand01, 0.0, 1.0);
  return static_cast<uint32_t>(std::lround(min_delay + clamped * (max_delay - min_delay)));
}

struct WsManager::Channel {
  std::string ws_path;                 // /traffic /memory /connections /logs?level=
  std::mutex mu;
  std::condition_variable cv;          // 可中断退避与即时唤醒（BUG-347）
  std::map<uint64_t, WsHandler> subs;  // 订阅者（引用计数即 subs.size()）
  std::shared_ptr<PipeWebSocket> ws;   // 共享底层 WS
  std::atomic<bool> reopen{false};     // configure() / kRefreshClash 后要求断开重连
  uint32_t reconnect_attempt = 0;      // 连续重连退避阶数（连接成功或 refresh_clash 时清零）
  uint64_t wake_epoch = 0;             // 中断当前退避等待的单调纪元
  bool logged_err = false;             // 仅 worker 线程读写：失败只报一次，成功清零
  std::atomic<bool> running{false};
  std::thread worker;
  // connections 通道专用：内核裸流 → snapshot/delta 信封（原版插件层职责）；
  // 仅被该通道的 WS 读线程调用，无需加锁
  std::unique_ptr<ConnectionsTransformer> transformer;
};

WsManager::WsManager() {
  refresh_clash_sub_id_ = events::EventBus::instance().subscribe(
      events::names::kRefreshClash,
      [this](const events::Payload&) { flush_reconnect_backoff(); });
}

WsManager::~WsManager() {
  if (refresh_clash_sub_id_ != 0) {
    events::EventBus::instance().unsubscribe(refresh_clash_sub_id_);
    refresh_clash_sub_id_ = 0;
  }
  stopping_.store(true, std::memory_order_release);
  // 顺序铁律：先取走 worker/WS 句柄并唤醒 cv → 锁外 close WS（join 读线程）→ join worker
  // → 最后才销毁 Channel（worker 循环体仍在引用 Channel，必须 join 后 clear，
  // 否则 use-after-free）
  std::vector<std::thread> workers;
  std::vector<std::shared_ptr<PipeWebSocket>> sockets;
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [key, ch] : channels_) {
      {
        std::lock_guard<std::mutex> lk2(ch->mu);
        ch->running.store(false, std::memory_order_release);
        ++ch->wake_epoch;
        if (ch->ws) sockets.push_back(std::move(ch->ws));
      }
      ch->cv.notify_all();
      if (ch->worker.joinable()) workers.push_back(std::move(ch->worker));
    }
  }
  for (auto& s : sockets) s->close(500u);
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
    for (auto& [key, ch] : channels_) {
      {
        std::lock_guard<std::mutex> lk2(ch->mu);
        ch->reconnect_attempt = 0;
        ++ch->wake_epoch;
        ch->reopen.store(true, std::memory_order_release);
      }
      ch->cv.notify_all();
    }
  }
  disconnect_all(500u);  // 立即断开，worker 以新参数第 0ms 重连
}

WsManager::Channel& WsManager::channel(const std::string& key, const std::string& ws_path) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = channels_.find(key);
  if (it == channels_.end()) {
    auto ch = std::make_unique<Channel>();
    ch->ws_path = ws_path;
    if (ws_path == "/connections") {
      // 插件层职责：内核裸流 → snapshot/delta 信封（04§2）
      ch->transformer = std::make_unique<ConnectionsTransformer>();
    }
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
    const bool was_empty = ch.subs.empty();
    ch.subs.emplace(id, std::move(handler));
    if (was_empty) {
      ch.reconnect_attempt = 0;
      ++ch.wake_epoch;
    }
  }
  ch.cv.notify_all();
  return id;
}

void WsManager::unsubscribe(uint64_t sub_id) {
  std::shared_ptr<PipeWebSocket> closing;
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [key, ch] : channels_) {
      bool erased = false;
      {
        std::lock_guard<std::mutex> lk2(ch->mu);
        if (ch->subs.erase(sub_id) > 0) {
          erased = true;
          // 最后一个订阅者退订：关闭底层 WS（对齐原版 refs=0 → closeSharedSocket）
          if (ch->subs.empty()) {
            ch->reconnect_attempt = 0;
            ++ch->wake_epoch;
            if (ch->ws) closing = std::move(ch->ws);
          }
        }
      }
      if (erased) {
        ch->cv.notify_all();
        break;
      }
    }
  }
  if (closing) closing->close(500u);  // 锁外 close：避免与读线程 on_message 抢锁死锁
}

void WsManager::disconnect_all(std::optional<uint32_t> force_timeout_ms) {
  std::vector<std::shared_ptr<PipeWebSocket>> sockets;
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [key, ch] : channels_) {
      {
        std::lock_guard<std::mutex> lk2(ch->mu);
        if (ch->ws) {
          sockets.push_back(std::move(ch->ws));
        }
      }
      ch->cv.notify_all();
    }
  }
  for (auto& s : sockets) s->close(force_timeout_ms);
}

void WsManager::flush_reconnect_backoff() {
  if (stopping_.load(std::memory_order_acquire)) return;
  std::vector<std::shared_ptr<PipeWebSocket>> stale_sockets;
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [key, ch] : channels_) {
      {
        std::lock_guard<std::mutex> lk2(ch->mu);
        ch->reconnect_attempt = 0;
        ++ch->wake_epoch;
        if (!ch->subs.empty() && ch->ws) {
          ch->reopen.store(true, std::memory_order_release);
          stale_sockets.push_back(std::move(ch->ws));
        }
      }
      ch->cv.notify_all();
    }
  }
  for (auto& s : stale_sockets) s->close(500u);
}

bool WsManager::channel_connected(const std::string& channel_key) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = channels_.find(channel_key);
  if (it == channels_.end()) return false;
  std::lock_guard<std::mutex> lk2(it->second->mu);
  return it->second->ws && it->second->ws->is_open();
}

uint32_t WsManager::channel_reconnect_attempt_for_test(const std::string& channel_key) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = channels_.find(channel_key);
  if (it == channels_.end()) return 0;
  std::lock_guard<std::mutex> lk2(it->second->mu);
  return it->second->reconnect_attempt;
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
  std::mt19937 rng{std::random_device{}()};
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  bool was_connected = false;

  while (ch->running.load(std::memory_order_acquire) &&
         !self->stopping_.load(std::memory_order_acquire)) {
    if (ch->reopen.exchange(false, std::memory_order_acq_rel)) {
      was_connected = false;
      std::shared_ptr<PipeWebSocket> old;
      {
        std::lock_guard<std::mutex> lk(ch->mu);
        old = std::move(ch->ws);
      }
      if (old) old->close(500u);
    }

    std::shared_ptr<PipeWebSocket> idle;
    bool should_connect = false;
    bool need_disconnect_backoff = false;
    uint32_t disconnect_attempt = 0;
    uint64_t epoch_before_wait = 0;
    {
      std::unique_lock<std::mutex> lk(ch->mu);
      if (ch->subs.empty()) {
        was_connected = false;
        if (ch->ws) {
          // 订阅归零兜底；close 会 join 读线程，必须放到锁外执行防死锁
          idle = std::move(ch->ws);
        } else {
          ch->cv.wait_for(lk, std::chrono::milliseconds(kIdlePollMs), [&] {
            return !ch->running.load(std::memory_order_acquire) ||
                   self->stopping_.load(std::memory_order_acquire) ||
                   !ch->subs.empty();
          });
        }
      } else if (ch->ws && ch->ws->is_open()) {
        was_connected = true;
        ch->cv.wait_for(lk, std::chrono::milliseconds(kIdlePollMs), [&] {
          return !ch->running.load(std::memory_order_acquire) ||
                 self->stopping_.load(std::memory_order_acquire) ||
                 ch->reopen.load(std::memory_order_acquire) ||
                 ch->subs.empty() || !ch->ws || !ch->ws->is_open();
        });
      } else {
        if (ch->ws) {
          idle = std::move(ch->ws);
        }
        if (was_connected && !ch->reopen.load(std::memory_order_acquire)) {
          // 已建立的 WS 连接被对端断开或主动 disconnect_all：进入可中断指数退避（首轮 ~1s，对齐 scheduleReconnect）
          was_connected = false;
          need_disconnect_backoff = true;
          disconnect_attempt = ch->reconnect_attempt++;
          epoch_before_wait = ch->wake_epoch;
        } else {
          was_connected = false;
          should_connect = true;
        }
      }
    }
    if (idle) idle->close(500u);
    if (need_disconnect_backoff) {
      const uint32_t delay_ms = compute_ws_reconnect_delay_ms(disconnect_attempt, dist(rng));
      std::unique_lock<std::mutex> lk(ch->mu);
      ch->cv.wait_for(lk, std::chrono::milliseconds(delay_ms), [&] {
        return !ch->running.load(std::memory_order_acquire) ||
               self->stopping_.load(std::memory_order_acquire) ||
               ch->reopen.load(std::memory_order_acquire) ||
               ch->subs.empty() || ch->wake_epoch != epoch_before_wait;
      });
      continue;
    }
    if (!should_connect) continue;

    // 建立新连接（当前参数）
    std::wstring pipe;
    std::string secret;
    {
      std::lock_guard<std::mutex> lk(self->mu_);
      pipe = self->pipe_name_;
      secret = self->secret_;
    }
    uint64_t connect_epoch = 0;
    {
      std::lock_guard<std::mutex> lk(ch->mu);
      connect_epoch = ch->wake_epoch;
    }

    Channel* chp = ch;
    WsManager* selfp = self;
    auto ws = std::make_shared<PipeWebSocket>();
    PipeWsCallbacks cbs;
    cbs.on_message = [chp, selfp](const std::string& payload) {
      if (selfp->stopping_.load(std::memory_order_acquire)) return;
      // connections 通道：内核裸流 → snapshot/delta 信封（← mihomo.rs ws_connections）
      std::string payload2 = payload;
      if (chp->transformer) {
        payload2 = chp->transformer->process(payload);
        if (payload2.empty()) return;
      }
      // 锁内拷贝 handlers，锁外分发（回调中可安全 unsubscribe）
      std::vector<WsHandler> handlers;
      {
        std::lock_guard<std::mutex> lk(chp->mu);
        handlers.reserve(chp->subs.size());
        for (auto& [id, h] : chp->subs) handlers.push_back(h);
      }
      for (const auto& h : handlers) h(payload2);
    };
    cbs.on_error = [](const std::string&) {};
    cbs.on_close = [chp](uint16_t, const std::string&) {
      // 读线程收到对端 close 后无锁唤醒 worker 立即进入重连状态机（chp 生命周期长于 ws）
      chp->cv.notify_all();
    };

    auto r = ws->connect(pipe, ws_path, secret, cbs, kOpenTimeoutMs);
    if (r.is_ok()) {
      chp->logged_err = false;
      std::shared_ptr<PipeWebSocket> drop_immediately;
      {
        std::lock_guard<std::mutex> lk(ch->mu);
        ch->reconnect_attempt = 0;
        if (!ch->running.load(std::memory_order_acquire) ||
            self->stopping_.load(std::memory_order_acquire) || ch->subs.empty()) {
          drop_immediately = std::move(ws);
        } else {
          if (ch->transformer) ch->transformer = std::make_unique<ConnectionsTransformer>();
          ch->ws = std::move(ws);
          was_connected = true;
        }
      }
      if (drop_immediately) drop_immediately->close(500u);
    } else {
      if (!chp->logged_err) {
        chp->logged_err = true;
        CWLOG_WARN("WS connect failed: path=" + ws_path + " pipe=" +
                   utf::utf16_to_utf8(pipe) + " err=" + r.error().message);
      }
      std::unique_lock<std::mutex> lk(ch->mu);
      if (ch->wake_epoch != connect_epoch ||
          ch->reopen.load(std::memory_order_acquire) || ch->subs.empty() ||
          !ch->running.load(std::memory_order_acquire) ||
          self->stopping_.load(std::memory_order_acquire)) {
        // 连接期间已收到 refresh_clash / configure / unsubscribe：跳过陈旧退避立即下一轮
        continue;
      }
      const uint32_t attempt = ch->reconnect_attempt++;
      const uint32_t delay_ms = compute_ws_reconnect_delay_ms(attempt, dist(rng));
      const uint64_t wait_epoch = ch->wake_epoch;
      ch->cv.wait_for(lk, std::chrono::milliseconds(delay_ms), [&] {
        return !ch->running.load(std::memory_order_acquire) ||
               self->stopping_.load(std::memory_order_acquire) ||
               ch->reopen.load(std::memory_order_acquire) ||
               ch->subs.empty() || ch->wake_epoch != wait_epoch;
      });
    }
  }
}

}  // namespace clashwin::mihomo
