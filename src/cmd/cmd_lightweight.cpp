#include "cmd/cmd_install.h"
#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "app/lifecycle.h"
#include "util/log.h"

namespace clashwin::cmd {

void install_cmd_lightweight(CommandService& svc) {
  svc.register_command(
      "entry_lightweight_mode",
      {[](const Json&) -> CmdResult {
         bool ok = app::Lifecycle::instance().entry_lightweight_mode(true);
         CWLOG_INFO("entry_lightweight_mode invoked -> " + std::string(ok ? "success" : "skipped"));
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutProbe});

  svc.register_command(
      "exit_lightweight_mode",
      {[](const Json&) -> CmdResult {
         bool ok = app::Lifecycle::instance().exit_lightweight_mode();
         CWLOG_INFO("exit_lightweight_mode invoked -> " + std::string(ok ? "success" : "skipped"));
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutProbe});
}

}  // namespace clashwin::cmd
