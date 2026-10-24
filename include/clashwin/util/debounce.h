#pragma once
// 防抖/节流门（← utils/debounce.ts、lib.rs 各常量；常量本体集中在
// events/event_names.h —— T09 落位：refresh 250ms 防抖 / proxy 200ms 节流 /
// profile-changed 800ms 节流 / 托盘 300ms / 窗口 625ms 等）
//
// 设计为"门"（判定器，不含回调）：由使用方在时钟节拍（UI 定时器/事件泵）里
// poll/take，纯单线程使用，now_ms 必须单调递增（steady_clock 毫秒）。
#include <cstdint>

namespace clashwin::util {

// 防抖：touch 不断推迟到期时刻；静默 delay_ms 后 poll 恰好放行一次（尾随触发）
class DebounceGate {
 public:
  explicit DebounceGate(int delay_ms) : delay_ms_(delay_ms) {}

  // 记录一次事件，重置静默计时并标记待触发
  void touch(int64_t now_ms) {
    last_ms_ = now_ms;
    pending_ = true;
  }

  // 到期（静默 delay_ms 且有待触发项）→ 返回 true 并消费
  bool poll(int64_t now_ms) {
    if (pending_ && now_ms - last_ms_ >= delay_ms_) {
      pending_ = false;
      return true;
    }
    return false;
  }

  void cancel() { pending_ = false; }
  bool pending() const { return pending_; }

 private:
  int delay_ms_;
  int64_t last_ms_ = 0;
  bool pending_ = false;
};

// 节流：窗口前沿立即放行一次；窗口内的调用被拒但记为"尾随待发"，
// 窗口结束后由 take_trailing 补发一次（保证首尾语义，不丢最后一次状态）
class ThrottleGate {
 public:
  explicit ThrottleGate(int window_ms) : window_ms_(window_ms) {}

  // 前导放行判定：窗口外 → true 并开启新窗口；窗口内 → false 并记尾随
  bool allow(int64_t now_ms) {
    if (now_ms - last_fire_ms_ >= window_ms_) {
      last_fire_ms_ = now_ms;
      trailing_pending_ = false;
      return true;
    }
    trailing_pending_ = true;
    return false;
  }

  // 窗口已关闭且有窗口内被拒调用 → 补发一次
  bool take_trailing(int64_t now_ms) {
    if (trailing_pending_ && now_ms - last_fire_ms_ >= window_ms_) {
      trailing_pending_ = false;
      return true;
    }
    return false;
  }

  void reset() {
    last_fire_ms_ = kNever;
    trailing_pending_ = false;
  }

 private:
  static constexpr int64_t kNever = -(1LL << 62);
  int window_ms_;
  int64_t last_fire_ms_ = kNever;
  bool trailing_pending_ = false;
};

}  // namespace clashwin::util
