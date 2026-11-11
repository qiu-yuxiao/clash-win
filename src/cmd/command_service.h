#pragma once
// T09 CommandService 门面（← lib.rs generate_handler! 注册表 44 项 +
// 01§5-A withIpcTimeout 超时梯度 10/15/30/60s）
//
// 【约定】
// - handler：std::function<Result<Json>(const Json& args)>；args 键名以
//   lib.rs 命令签名的 snake_case 为主、兼容前端 camelCase（见 cmd_support.h）
// - invoke(name, args)：按注册时给定的梯度在线程池执行并限时；超时返回
//   Error{code=2902, message="command timeout"}（工作线程不中断，与原版
//   withIpcTimeout 的前端放弃语义一致）
// - 桩命令（T21/T25）：注册存在，调用返回 Error{code=2903}
#pragma once

#include "cmd/cmd_support.h"

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace clashwin::cmd {

// ← 01§5-A 超时梯度（cmds.ts withIpcTimeout）
namespace consts {
inline constexpr int kTimeoutProbe = 10000;     // 探针类
inline constexpr int kTimeoutProbeLong = 15000; // 长探针（服务可用性/群测速准备）
inline constexpr int kTimeoutConfig = 30000;    // 配置 patch 类
inline constexpr int kTimeoutHeavy = 60000;     // 内核/导入/测速/升级类
}  // namespace consts

class CommandService {
 public:
  static CommandService& instance();

  // def = {handler, timeout_ms}（brace 构造，与各 cmd 文件的注册风格一致）
  void register_command(const std::string& name, std::pair<CmdHandler, int> def);
  bool exists(const std::string& name) const;
  int timeout_of(const std::string& name) const;  // 未注册 → -1
  std::vector<std::string> command_names() const;

  CmdResult invoke(const std::string& name, const Json& args = Json::object());

  // ← services/cmds.ts:24-35 beginLongOperation / endLongOperation / isLongOperationRunning
  // 长操作（切接管模式/重载内核/服务安装）期间抑制探针超时弹窗风暴
  void begin_long_operation();
  void end_long_operation();
  bool is_long_operation_running() const;

  // 装配全部命令（幂等；九个 cmd 文件的 install_* 依次调用）
  void install_all();

 private:
  CommandService() = default;
  mutable std::mutex mu_;
  std::map<std::string, std::pair<CmdHandler, int>> table_;
  bool installed_ = false;
  std::atomic<int> long_op_count_{0};
};

// RAII 守卫（等价于 Mini try { beginLongOperation() } finally { endLongOperation() }）
class LongOperationGuard {
 public:
  LongOperationGuard() { CommandService::instance().begin_long_operation(); }
  ~LongOperationGuard() { CommandService::instance().end_long_operation(); }
  LongOperationGuard(const LongOperationGuard&) = delete;
  LongOperationGuard& operator=(const LongOperationGuard&) = delete;
};

}  // namespace clashwin::cmd
