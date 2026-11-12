#pragma once
// 进程内事件总线（← core/notification.rs send_event 的窗口 emit 等价物；
// 防抖/节流门 ← 01§5-C 常量表，门实现复用 T02 debounce.h）
//
// - subscribe(name, handler) → id；unsubscribe(id) 退订
// - publish(name, payload)：Immediate 事件同线程同步派发；Debounce/Throttle
//   事件由内部分发线程按门控节奏派发（发布可能来自 WS 读线程 → 全程加锁，
//   handler 在锁外调用，允许 handler 内再 publish）
// - 轻量模式过滤（← notification.rs 防线一）：set_lightweight_filter(true) 后
//   publish 全部丢弃（T19 接管开关）
#pragma once

#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace clashwin::events {

using Payload = nlohmann::json;
using Handler = std::function<void(const Payload&)>;

enum class GateType {
  Immediate,  // 同步派发（notice-message / delay-results / timer / update 起止）
  Debounce,   // 尾随防抖（refresh-clash/verge/proxy-config）
  Throttle,   // 首沿节流 + 尾随补发（profile-changed）
};

class EventBus {
 public:
  // 可独立实例化（测试隔离门控状态）；instance() 为进程级单例。
  // with_worker=false 时不启动内部分发线程（测试手动 flush_pending 驱动，消除时序竞争）
  explicit EventBus(bool with_worker = true);
  ~EventBus();
  EventBus(const EventBus&) = delete;
  EventBus& operator=(const EventBus&) = delete;

  static EventBus& instance();

  // 注册门控类型（默认表来自 event_names；测试可改小间隔）
  void set_gate(const std::string& name, GateType type, int interval_ms);
  GateType gate_of(const std::string& name) const;

  // handler 弱持有语义：返回 id 供退订；无所有权的调用方自行保证生命周期
  uint64_t subscribe(const std::string& name, Handler handler);
  void unsubscribe(uint64_t id);
  size_t subscriber_count(const std::string& name) const;

  void publish(const std::string& name, const Payload& payload = nullptr);

  // ← notification.rs：轻量模式直接过滤（默认 false）
  void set_lightweight_filter(bool on);
  bool lightweight_filter() const;

  // 立即冲刷所有待派发项（测试用；生产由内部分发线程驱动）
  void flush_pending();

  // 注入时钟偏移（测试确定性驱动门控；生产恒 0）。
  // 门控时间戳取 now+offset，与真实 wall-clock 解耦——负载/调度抖动不再影响判定。
  void advance_for_test(int64_t ms);

  // 便捷发射器（← Handle::refresh_clash / refresh_verge / notify_profile_changed 等）
  static void refresh_clash();    // payload "yes"
  static void refresh_verge();    // payload "yes"
  static void refresh_proxies();  // payload "yes"
  static void notify_profile_changed(const std::string& uid);
  static void notice_message(const char* status, const std::string& message);
  static void backend_delay_results(const std::string& group,
                                    const std::vector<std::pair<std::string, uint32_t>>& results,
                                    bool from_probe = true);

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace clashwin::events
