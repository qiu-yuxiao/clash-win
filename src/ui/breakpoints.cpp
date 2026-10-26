// 断点状态机实现（T11）。
#include "breakpoints.h"

#include <algorithm>

namespace clashwin::ui::win {

void Breakpoints::set_physical_size(int phys_w, int phys_h,
                                    double scale_factor) {
  WinState prev = state();
  narrow_ = static_cast<double>(phys_w) <=
            kMiniWidthThreshold * scale_factor + kMiniPhysSlack;
  mini_ = narrow_ && static_cast<double>(phys_h) <=
                       kMiniHeightThreshold * scale_factor + kMiniPhysSlack;
  logical_w_ = static_cast<double>(phys_w) / scale_factor;
  logical_h_ = static_cast<double>(phys_h) / scale_factor;
  if (state() != prev) notify(prev);
}

void Breakpoints::set_logical_size(double w, double h) {
  WinState prev = state();
  narrow_ = w <= kMiniWidthThreshold;
  mini_ = narrow_ && h <= kMiniHeightThreshold;
  logical_w_ = w;
  logical_h_ = h;
  if (state() != prev) notify(prev);
}

void Breakpoints::set_large(bool large) {
  if (large_ == large) return;
  WinState prev = state();
  large_ = large;
  notify(prev);
}

void Breakpoints::notify(WinState prev) {
  WinState cur = state();
  for (auto& [id, cb] : subs_) cb(prev, cur);
}

uint64_t Breakpoints::subscribe(std::function<void(WinState, WinState)> cb) {
  uint64_t id = next_id_++;
  subs_.emplace_back(id, std::move(cb));
  return id;
}

void Breakpoints::unsubscribe(uint64_t id) {
  subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
                             [id](const auto& p) { return p.first == id; }),
              subs_.end());
}

}  // namespace clashwin::ui::win
