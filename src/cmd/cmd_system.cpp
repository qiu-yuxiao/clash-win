// T09 cmd_system + cmd_lightweight（← cmd/system.rs / cmd/lightweight.rs）
#include "cmd/cmd_install.h"

#include <atomic>

#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "core/core_manager.h"
#include "util/log.h"

namespace clashwin::cmd {

namespace {
// ← module/lightweight.rs：轻量模式状态位（窗口销毁/WS 断开归 T19）
std::atomic<bool> g_lightweight{false};
}  // namespace

bool is_in_lightweight_mode() { return g_lightweight.load(); }

void install_cmd_system(CommandService& svc) {
  // ---- get_running_mode（← system.rs:12-15）----
  svc.register_command(
      "get_running_mode",
      {[](const Json&) -> CmdResult {
         return CmdResult::ok(Json(core::to_string(running_mode())));
       },
       consts::kTimeoutProbe});

  // ---- frontend_log（← system.rs:20-27：前端诊断日志转发）----
  svc.register_command(
      "frontend_log",
      {[](const Json& args) -> CmdResult {
         std::string level = arg_or(args, {"level"}, Json("info"));
         std::string message = arg_or(args, {"message"}, Json(""));
         // ← system.rs：error/warn 分级，其余 info；加 [Frontend] 前缀
         if (level == "error") {
           CWLOG_ERROR("[Frontend] " + message);
         } else if (level == "warn") {
           CWLOG_WARN("[Frontend] " + message);
         } else {
           CWLOG_INFO("[Frontend] " + message);
         }
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutProbe});
}

void install_cmd_lightweight(CommandService& svc) {
  // ---- entry_lightweight_mode（← lightweight.rs:6-9；T09=状态位+事件留痕）----
  svc.register_command(
      "entry_lightweight_mode",
      {[](const Json&) -> CmdResult {
         g_lightweight.store(true);
         events::EventBus::instance().set_lightweight_filter(true);  // ← notification.rs 防线一
         CWLOG_INFO("lightweight mode entered (window teardown pending T19)");
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutProbe});

  // ---- exit_lightweight_mode（← lightweight.rs:11-14）----
  svc.register_command(
      "exit_lightweight_mode",
      {[](const Json&) -> CmdResult {
         g_lightweight.store(false);
         events::EventBus::instance().set_lightweight_filter(false);
         CWLOG_INFO("lightweight mode exited");
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutProbe});
}

}  // namespace clashwin::cmd
