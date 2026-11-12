// T09 事件总线实现（防抖/节流门复用 T02 debounce.h）
#include "events/event_bus.h"

#include "clashwin/util/debounce.h"
#include "events/event_names.h"
#include "util/log.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace clashwin::events {

namespace {
int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace

struct EventBus::Impl {
  struct Sub {
    uint64_t id;
    Handler handler;
  };
  struct GateEntry {
    GateType type = GateType::Immediate;
    int interval_ms = 0;
    // Debounce / Throttle 状态（按需启用）
    std::unique_ptr<util::DebounceGate> debounce;
    std::unique_ptr<util::ThrottleGate> throttle;
    Payload pending;
    bool has_pending = false;
  };

  mutable std::mutex mu;
  std::unordered_map<std::string, std::vector<Sub>> subs;
  std::unordered_map<std::string, GateEntry> gates;
  uint64_t next_id = 1;
  bool lightweight = false;

  // 分发线程（20ms tick；仅 flush 门控事件；Immediate 在 publish 内同步派发）
  std::thread worker;
  bool worker_run = false;
  int64_t clock_offset = 0;  // 测试时钟推进

  int64_t clock() const { return now_ms() + clock_offset; }

  GateEntry& gate_for(const std::string& name) {
    auto it = gates.find(name);
    if (it == gates.end()) {
      GateEntry e;
      apply_default(name, e);
      return gates.emplace(name, std::move(e)).first->second;
    }
    return it->second;
  }

  static void apply_default(const std::string& name, GateEntry& e) {
    if (name == names::kRefreshClash) {
      e = {GateType::Debounce, consts::kRefreshClashDebounceMs, nullptr, nullptr, {}, false};
    } else if (name == names::kRefreshVerge) {
      e = {GateType::Debounce, consts::kRefreshVergeDebounceMs, nullptr, nullptr, {}, false};
    } else if (name == names::kRefreshProxies) {
      e = {GateType::Debounce, consts::kRefreshProxiesDebounceMs, nullptr, nullptr, {}, false};
    } else if (name == names::kProfileChanged) {
      e = {GateType::Throttle, consts::kProfileChangedThrottleMs, nullptr, nullptr, {}, false};
    } else {
      e = {GateType::Immediate, 0, nullptr, nullptr, {}, false};
    }
    e.debounce = e.type == GateType::Debounce ? std::make_unique<util::DebounceGate>(e.interval_ms)
                                              : nullptr;
    e.throttle = e.type == GateType::Throttle ? std::make_unique<util::ThrottleGate>(e.interval_ms)
                                              : nullptr;
  }

  void dispatch_locked_list(const std::vector<Sub>& list, const Payload& payload) {
    // 前提：持锁拷贝出的 handler 列表；实际调用在锁外（由调用方安排）
    for (const auto& s : list) {
      try {
        s.handler(payload);
      } catch (const std::exception& ex) {
        CWLOG_WARN(std::string("event handler exception: ") + ex.what());
      } catch (...) {
        CWLOG_WARN("event handler unknown exception");
      }
    }
  }
};

EventBus::EventBus(bool with_worker) : impl_(new Impl) {
  if (!with_worker) return;
  impl_->worker_run = true;
  impl_->worker = std::thread([this]() {
    while (impl_->worker_run) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      flush_pending();
    }
  });
}

EventBus::~EventBus() {
  impl_->worker_run = false;
  if (impl_->worker.joinable()) impl_->worker.join();
  delete impl_;
}

EventBus& EventBus::instance() {
  static EventBus bus;
  return bus;
}

void EventBus::set_gate(const std::string& name, GateType type, int interval_ms) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  Impl::GateEntry e;
  e.type = type;
  e.interval_ms = interval_ms;
  if (type == GateType::Debounce) e.debounce = std::make_unique<util::DebounceGate>(interval_ms);
  if (type == GateType::Throttle) e.throttle = std::make_unique<util::ThrottleGate>(interval_ms);
  impl_->gates[name] = std::move(e);
}

GateType EventBus::gate_of(const std::string& name) const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  auto& g = impl_->gate_for(name);
  return g.type;
}

uint64_t EventBus::subscribe(const std::string& name, Handler handler) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->gate_for(name);  // 确保门控条目存在
  uint64_t id = impl_->next_id++;
  impl_->subs[name].push_back({id, std::move(handler)});
  return id;
}

void EventBus::unsubscribe(uint64_t id) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  for (auto& [name, list] : impl_->subs) {
    list.erase(std::remove_if(list.begin(), list.end(),
                              [id](const Impl::Sub& s) { return s.id == id; }),
               list.end());
  }
}

size_t EventBus::subscriber_count(const std::string& name) const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  auto it = impl_->subs.find(name);
  return it == impl_->subs.end() ? 0 : it->second.size();
}

void EventBus::publish(const std::string& name, const Payload& payload) {
  // ← notification.rs 防线一：轻量模式全部过滤
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    if (impl_->lightweight) return;
  }

  std::vector<Impl::Sub> snapshot;
  bool deliver_now = false;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    auto& g = impl_->gate_for(name);
    auto sit = impl_->subs.find(name);
    if (sit != impl_->subs.end()) snapshot = sit->second;
    const int64_t t = impl_->clock();
    switch (g.type) {
      case GateType::Immediate:
        deliver_now = true;
        break;
      case GateType::Debounce:
        g.debounce->touch(t);
        g.pending = payload;
        g.has_pending = true;
        break;
      case GateType::Throttle:
        if (g.throttle->allow(t)) {
          deliver_now = true;
        } else {
          g.pending = payload;  // 尾随补发用最后一次
          g.has_pending = true;
        }
        break;
    }
  }
  if (deliver_now) {
    impl_->dispatch_locked_list(snapshot, payload);
  }
}

void EventBus::flush_pending() {
  const int64_t t = impl_->clock();
  std::vector<std::pair<std::vector<Impl::Sub>, Payload>> due;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    for (auto& [name, g] : impl_->gates) {
      bool fire = false;
      if (g.type == GateType::Debounce && g.has_pending && g.debounce->poll(t)) fire = true;
      if (g.type == GateType::Throttle && g.has_pending && g.throttle->take_trailing(t)) fire = true;
      if (fire) {
        auto sit = impl_->subs.find(name);
        due.emplace_back(sit == impl_->subs.end() ? std::vector<Impl::Sub>{} : sit->second,
                         g.pending);
        g.has_pending = false;
        g.pending = nullptr;
      }
    }
  }
  for (auto& [list, payload] : due) {
    impl_->dispatch_locked_list(list, payload);
  }
}

void EventBus::advance_for_test(int64_t ms) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->clock_offset += ms;
}

void EventBus::set_lightweight_filter(bool on) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->lightweight = on;
}

bool EventBus::lightweight_filter() const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->lightweight;
}

// ---------- 便捷发射器 ----------
void EventBus::refresh_clash() {
  instance().publish(names::kRefreshClash, Payload("yes"));
}
void EventBus::refresh_verge() {
  instance().publish(names::kRefreshVerge, Payload("yes"));
}
void EventBus::refresh_proxies() {
  instance().publish(names::kRefreshProxies, Payload("yes"));
}
void EventBus::notify_profile_changed(const std::string& uid) {
  instance().publish(names::kProfileChanged, Payload(uid));
}
void EventBus::notice_message(const char* status, const std::string& message) {
  // ← Mini: Handle::notice_message(status, msg) 只带两个字段
  instance().publish(names::kNoticeMessage,
                     Payload{{"status", status ? status : ""}, {"message", message}});
}
void EventBus::backend_delay_results(
    const std::string& group, const std::vector<std::pair<std::string, uint32_t>>& results,
    bool from_probe) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& [name, delay] : results) arr.push_back({name, delay});
  instance().publish(names::kBackendDelayResults,
                     Payload{{"group", group}, {"results", arr}, {"fromProbe", from_probe}});
}

}  // namespace clashwin::events
