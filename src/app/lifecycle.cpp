#include "app/lifecycle.h"
#include "config/config.h"
#include "events/event_bus.h"
#include "system/guard_monitor.h"
#include "system/tray.h"
#include "util/log.h"

namespace clashwin::app {

Lifecycle& Lifecycle::instance() {
  static Lifecycle s_instance;
  return s_instance;
}

bool Lifecycle::entry_lightweight_mode(bool force) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!force) {
    auto verge = config::Config::instance().verge().latest_arc();
    bool enable_auto = verge ? verge->enable_auto_light_weight_mode.value_or(false) : false;
    if (!enable_auto) {
      CWLOG_INFO("Lifecycle: enable_auto_light_weight_mode is false, only hiding window");
      if (window_) window_->hide();
      if (tray_) tray_->update_lite_mode_menu(false);
      return true;
    }
  }

  uint8_t expected = static_cast<uint8_t>(LifecycleState::Normal);
  if (!state_.compare_exchange_strong(expected, static_cast<uint8_t>(LifecycleState::InLightweight))) {
    CWLOG_INFO("Lifecycle: not in Normal state, entry skipped");
    if (tray_) tray_->update_lite_mode_menu(is_in_lightweight_mode());
    return false;
  }

  CWLOG_INFO("Lifecycle: entering lightweight mode");

  if (window_) {
    window_->hide();
    window_->release_d2d_resources();
    window_->disconnect_ws();
  }

  events::EventBus::instance().set_lightweight_filter(true);

  if (tray_) {
    tray_->update_lite_mode_menu(true);
  }

  system::CoreMonitor::instance().tick_once();
  return true;
}

bool Lifecycle::exit_lightweight_mode() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (state() == LifecycleState::Normal) {
    CWLOG_INFO("Lifecycle: in Normal state, showing and activating window");
    if (window_) window_->show_and_activate();
    if (tray_) tray_->update_lite_mode_menu(false);
    return true;
  }

  uint8_t expected = static_cast<uint8_t>(LifecycleState::InLightweight);
  if (!state_.compare_exchange_strong(expected, static_cast<uint8_t>(LifecycleState::Exiting))) {
    CWLOG_INFO("Lifecycle: already exiting or in invalid state");
    return false;
  }

  CWLOG_INFO("Lifecycle: exiting lightweight mode");

  events::EventBus::instance().set_lightweight_filter(false);

  if (window_) {
    window_->restore_d2d_resources();
    window_->show_and_activate();
    window_->reconnect_ws();
  }

  state_.store(static_cast<uint8_t>(LifecycleState::Normal), std::memory_order_release);

  if (tray_) {
    tray_->update_lite_mode_menu(false);
  }

  system::CoreMonitor::instance().tick_once();
  return true;
}

void Lifecycle::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.store(static_cast<uint8_t>(LifecycleState::Normal), std::memory_order_release);
  events::EventBus::instance().set_lightweight_filter(false);
  window_ = nullptr;
  tray_ = nullptr;
}

}  // namespace clashwin::app
