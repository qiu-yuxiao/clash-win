#pragma once

#include <windows.h>
#include <functional>
#include <atomic>
#include <string>

namespace clashwin::system {

enum class SignalType {
  CtrlC = 0,
  CtrlBreak = 1,
  CtrlClose = 2,
  CtrlLogoff = 5,
  CtrlShutdown = 6,
  SessionQueryEnd = 100,
  SessionEnd = 101
};

const char* to_string(SignalType sig);

using SignalCallback = std::function<void(SignalType)>;

class SignalHandler {
 public:
  static SignalHandler& instance();

  // Install Win32 console control handler
  bool install(SignalCallback cb);
  void uninstall();

  // Session message dispatchers from MainWindow WndProc
  bool handle_session_query_end(uintptr_t lparam);
  void handle_session_end(bool session_ending, uintptr_t lparam);

  // Trigger signal for testing or simulation
  void dispatch(SignalType sig);

  // Console control callback invoked by Windows OS
  static BOOL WINAPI console_ctrl_proc(DWORD ctrl_type);

  bool is_installed() const { return installed_.load(); }

  // Reset state (for tests)
  void reset();

 private:
  SignalHandler() = default;
  ~SignalHandler();

  std::atomic<bool> installed_{false};
  std::atomic<bool> is_cleaning_up_{false};
  SignalCallback callback_;
};

}  // namespace clashwin::system
