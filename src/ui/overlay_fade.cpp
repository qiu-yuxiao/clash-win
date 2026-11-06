#include "overlay_fade.h"

#include <windows.h>

#include <algorithm>

namespace clashwin::ui {

void OverlayFade::start(float to, int dur_ms, int64_t now) {
  from_ = value_;
  to_ = to;
  start_ms_ = now;
  dur_ms_ = dur_ms;
}

void OverlayFade::jump_to(float v) {
  value_ = std::max(0.0f, std::min(1.0f, v));
  from_ = value_;
  to_ = value_;
  start_ms_ = 0;
  dur_ms_ = 0;
}

bool OverlayFade::advance() { return advance_at(now_ms()); }

bool OverlayFade::advance_at(int64_t now) {
  if (from_ == to_) return false;
  const float p =
      dur_ms_ <= 0
          ? 1.0f
          : std::min(1.0f, static_cast<float>(now - start_ms_) /
                               static_cast<float>(dur_ms_));
  value_ = from_ + (to_ - from_) * p;
  if (p >= 1.0f) {
    value_ = to_;
    from_ = to_;  // 过渡结束 → 停止自续帧
    return false;
  }
  return true;
}

int64_t OverlayFade::now_ms() {
  return static_cast<int64_t>(::GetTickCount64());
}

}  // namespace clashwin::ui
