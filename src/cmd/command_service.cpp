// T09 CommandService 实现：注册表 + invoke 超时包装 + 装配
#include "cmd/command_service.h"

#include "cmd/cmd_install.h"
#include "core/core_manager.h"
#include "events/event_bus.h"
#include "events/notice.h"
#include "util/log.h"

#include <future>
#include <thread>

namespace clashwin::cmd {

CommandService& CommandService::instance() {
  static CommandService svc;
  return svc;
}

void CommandService::register_command(const std::string& name, std::pair<CmdHandler, int> def) {
  std::lock_guard<std::mutex> lock(mu_);
  table_[name] = std::move(def);
}

bool CommandService::exists(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mu_);
  return table_.count(name) != 0;
}

int CommandService::timeout_of(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = table_.find(name);
  return it == table_.end() ? -1 : it->second.second;
}

std::vector<std::string> CommandService::command_names() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<std::string> out;
  out.reserve(table_.size());
  for (const auto& [name, _] : table_) out.push_back(name);
  return out;
}

CmdResult CommandService::invoke(const std::string& name, const Json& args) {
  CmdHandler handler;
  int timeout_ms = consts::kTimeoutConfig;
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = table_.find(name);
    if (it == table_.end()) {
      return CmdResult::err({errs::kNotFound, "unknown command: " + name});
    }
    handler = it->second.first;
    timeout_ms = it->second.second;
  }
  // ← 01§5-A withIpcTimeout：超时返回错误（工作线程自然走完，与前端放弃语义一致）
  std::future<CmdResult> fut = std::async(std::launch::async, [&handler, &args] {
    try {
      return handler(args);
    } catch (const std::exception& ex) {
      return CmdResult::err({errs::kIo, std::string("handler exception: ") + ex.what()});
    } catch (...) {
      return CmdResult::err({errs::kIo, "handler unknown exception"});
    }
  });
  if (fut.wait_for(std::chrono::milliseconds(timeout_ms)) != std::future_status::ready) {
    CWLOG_WARN("command timeout: " + name + " (" + std::to_string(timeout_ms) + "ms)");
    return CmdResult::err({errs::kTimeout, "command timeout: " + name});
  }
  return fut.get();
}

void CommandService::install_all() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (installed_) return;
    installed_ = true;
  }
  // 不持锁装配（register_command 自行加锁，避免递归死锁）
  install_cmd_profile(*this);
  install_cmd_clash(*this);
  install_cmd_verge(*this);
  install_cmd_system(*this);
  install_cmd_service(*this);
  install_cmd_lightweight(*this);
  install_cmd_network(*this);
  install_cmd_proxy(*this);
  install_cmd_app(*this);

  // ← T07 Notifier 接事件总线：内核层告警（BUG-298 类）→ verge://notice-message
  core::CoreManager::instance().set_notifier([](const char* status, const std::string& message) {
    events::EventBus::notice_message(status, message);
  });
}

}  // namespace clashwin::cmd
