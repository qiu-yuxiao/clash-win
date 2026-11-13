#include "system/signals.h"
#include "system/guardian.h"
#include "util/log.h"
#include <format>

namespace clashwin::system {

const char* to_string(SignalType sig) {
  switch (sig) {
    case SignalType::CtrlC: return "CTRL_C";
    case SignalType::CtrlBreak: return "CTRL_BREAK";
    case SignalType::CtrlClose: return "CTRL_CLOSE";
    case SignalType::CtrlLogoff: return "CTRL_LOGOFF";
    case SignalType::CtrlShutdown: return "CTRL_SHUTDOWN";
    case SignalType::SessionQueryEnd: return "SESSION_QUERY_END";
    case SignalType::SessionEnd: return "SESSION_END";
    default: return "UNKNOWN";
  }
}

SignalHandler& SignalHandler::instance() {
  static SignalHandler handler;
  return handler;
}

SignalHandler::~SignalHandler() {
  uninstall();
}

bool SignalHandler::install(SignalCallback cb) {
  callback_ = std::move(cb);
  if (!installed_.exchange(true)) {
    if (::SetConsoleCtrlHandler(console_ctrl_proc, TRUE)) {
      CWLOG_INFO("SignalHandler: SetConsoleCtrlHandler installed successfully");
      return true;
    } else {
      installed_.store(false);
      CWLOG_WARN(std::format("SignalHandler: SetConsoleCtrlHandler failed, gle={}", ::GetLastError()));
      return false;
    }
  }
  return true;
}

void SignalHandler::uninstall() {
  if (installed_.exchange(false)) {
    ::SetConsoleCtrlHandler(console_ctrl_proc, FALSE);
    CWLOG_INFO("SignalHandler: SetConsoleCtrlHandler uninstalled");
  }
  callback_ = nullptr;
}

bool SignalHandler::handle_session_query_end(uintptr_t /*lparam*/) {
  CWLOG_INFO("SignalHandler: handle_session_query_end called");
  guardian::notify_intentional_exit_with_reason("wm_queryendsession");
  return true;
}

void SignalHandler::handle_session_end(bool session_ending, uintptr_t /*lparam*/) {
  CWLOG_INFO(std::format("SignalHandler: handle_session_end session_ending={}", session_ending));
  if (session_ending) {
    dispatch(SignalType::SessionEnd);
  } else {
    guardian::cancel_intentional_exit("wm_endsession_cancelled");
  }
}

void SignalHandler::dispatch(SignalType sig) {
  // ★ 系统关机/注销/控制台终止信号：升级 confirmed 主动退出凭证（120s TTL）
  guardian::confirm_intentional_exit_with_reason("signal_dispatch");

  bool expected = false;
  if (!is_cleaning_up_.compare_exchange_strong(expected, true)) {
    CWLOG_INFO(std::format("SignalHandler::dispatch: already cleaning up, ignoring repeated signal {}",
                           to_string(sig)));
    return;
  }

  CWLOG_INFO(std::format("SignalHandler::dispatch: dispatching signal {}", to_string(sig)));
  if (callback_) {
    callback_(sig);
  }
}

BOOL WINAPI SignalHandler::console_ctrl_proc(DWORD ctrl_type) {
  SignalType sig = SignalType::CtrlC;
  switch (ctrl_type) {
    case CTRL_C_EVENT: sig = SignalType::CtrlC; break;
    case CTRL_BREAK_EVENT: sig = SignalType::CtrlBreak; break;
    case CTRL_CLOSE_EVENT: sig = SignalType::CtrlClose; break;
    case CTRL_LOGOFF_EVENT: sig = SignalType::CtrlLogoff; break;
    case CTRL_SHUTDOWN_EVENT: sig = SignalType::CtrlShutdown; break;
    default:
      CWLOG_WARN(std::format("SignalHandler: unknown console ctrl_type {}", ctrl_type));
      return FALSE;
  }
  instance().dispatch(sig);
  return TRUE;
}

void SignalHandler::reset() {
  uninstall();
  is_cleaning_up_.store(false);
  callback_ = nullptr;
}

}  // namespace clashwin::system
