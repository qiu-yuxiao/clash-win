// T09 CommandService 实现：注册表 + invoke 超时包装 + 装配
#include "cmd/command_service.h"

#include "cmd/cmd_install.h"
#include "core/core_manager.h"
#include "events/event_bus.h"
#include "events/notice.h"
#include "util/log.h"

#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace clashwin::cmd {

namespace {
// ← services/cmds.ts:17-20 IPC_TIMEOUT_NOTICE_THROTTLE_MS：同 label 5s 只弹一次
constexpr int64_t kIpcTimeoutNoticeThrottleMs = 5000;

// ← services/cmds.ts:96-109 ipcTimeoutNoticeLabels 节流表
std::mutex& ipc_timeout_mu() {
  static std::mutex m;
  return m;
}
std::unordered_map<std::string, int64_t>& ipc_timeout_labels() {
  static std::unordered_map<std::string, int64_t> m;
  return m;
}
}  // namespace

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

void CommandService::begin_long_operation() {
  long_op_count_.fetch_add(1, std::memory_order_relaxed);
}

void CommandService::end_long_operation() {
  int prev = long_op_count_.fetch_sub(1, std::memory_order_relaxed);
  if (prev <= 1) long_op_count_.store(0, std::memory_order_relaxed);
}

bool CommandService::is_long_operation_running() const {
  return long_op_count_.load(std::memory_order_relaxed) > 0;
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
  // 注意：handler/args 必须按值捕获。原先按引用捕获时，超时返回后栈对象即失效，
  // 后台线程继续访问会造成悬垂引用；而 std::async 的 future 析构会阻塞到任务结束，
  // 使“超时”形同虚设（UI 线程被拖住 → AppHang）。这里改用 packaged_task + detach。
  CmdHandler handler_copy = handler;
  Json args_copy = args;
  std::packaged_task<CmdResult()> task([handler_copy, args_copy]() {
    try {
      return handler_copy(args_copy);
    } catch (const std::exception& ex) {
      return CmdResult::err({errs::kIo, std::string("handler exception: ") + ex.what()});
    } catch (...) {
      return CmdResult::err({errs::kIo, "handler unknown exception"});
    }
  });
  std::future<CmdResult> fut = task.get_future();
  std::thread(std::move(task)).detach();
  if (fut.wait_for(std::chrono::milliseconds(timeout_ms)) != std::future_status::ready) {
    CWLOG_WARN("command timeout: " + name + " (" + std::to_string(timeout_ms) + "ms)");
    const std::string msg =
        "[" + name + "] IPC call timed out after " + std::to_string(timeout_ms) + "ms";
    // ← services/cmds.ts:89-94：长操作期间静默，不弹 Notice、不写节流表
    if (is_long_operation_running()) {
      return CmdResult::err({errs::kTimeout, "command timeout: " + name});
    }
    // ← services/cmds.ts:95-108：超时要给用户看得见的反馈（否则界面像「点了没反应」），
    //   同 label 5 秒内只弹一次，避免超时风暴刷屏。
    bool emit = false;
    {
      std::lock_guard<std::mutex> lock(ipc_timeout_mu());
      const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
      auto& labels = ipc_timeout_labels();
      auto it = labels.find(name);
      if (it == labels.end() || now - it->second >= kIpcTimeoutNoticeThrottleMs) {
        labels[name] = now;
        emit = true;
      }
    }
    if (emit) events::EventBus::notice_message("ipc_timeout", msg);
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
