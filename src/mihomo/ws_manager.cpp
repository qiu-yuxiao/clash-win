// T05 WS 四通道订阅管理实现
#include "mihomo/ws_manager.h"

#include <nlohmann/json.hpp>

#include "mihomo/pipe_ws.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <random>
#include <set>
#include <thread>
#include <utility>

namespace clashwin::mihomo {

namespace {
constexpr int kReconnectDelayMs = 1000;  // ← use-mihomo-ws-subscription.ts RECONNECT_DELAY_MS
                                          // （原版为固定 1s 重连，非指数退避）
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

struct WsManager::Channel {
  std::string ws_path;                 // /traffic /memory /connections /logs?level=
  std::mutex mu;
  std::map<uint64_t, WsHandler> subs;  // 订阅者（引用计数即 subs.size()）
  std::shared_ptr<PipeWebSocket> ws;   // 共享底层 WS
  std::atomic<bool> reopen{false};     // configure() 后要求断开重连
  std::atomic<bool> running{false};
  std::thread worker;
  // connections 通道专用：内核裸流 → snapshot/delta 信封（原版插件层职责）；
  // 仅被该通道的 WS 读线程调用，无需加锁
  std::unique_ptr<ConnectionsTransformer> transformer;
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
