// T09 / T25 cmd_service（← cmd/service.rs：install/uninstall/reinstall/repair/available）
#include "cmd/cmd_install.h"

#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "clashwin/core/constants.h"
#include "system/service_runner.h"
#include "util/log.h"

namespace clashwin::cmd {

namespace {

Result<void> service_op(const char* label, bool install, bool uninstall, bool start_after) {
  CWLOG_INFO(std::string(label) + " service operation");
  auto runner = system::get_service_runner();
  if (uninstall) {
    auto r = runner->run_installer(false);
    if (r.is_err()) CWLOG_WARN(std::string(label) + " uninstall failed: " + r.error().message);
  }
  if (install) {
    auto r = runner->run_installer(true);
    if (r.is_err()) {
      CWLOG_ERROR(std::string(label) + " install failed: " + r.error().message);
      return r;
    }
  }
  if (start_after) {
    auto r = runner->start_service();
    if (r.is_err()) {
      CWLOG_ERROR(std::string(label) + " start failed: " + r.error().message);
      return r;
    }
    if (!runner->wait_for_ready(core::consts::kServiceWaitMaxMs)) {
      return Result<void>::err({errs::kIo, "service not ready in 30s budget"});
    }
  }
  return Result<void>::ok();
}

}  // namespace

void install_cmd_service(CommandService& svc) {
  // ---- install_service（← service.rs:17-20 InstallRequired）----
  svc.register_command(
      "install_service",
      {[](const Json&) -> CmdResult {
         auto runner = system::get_service_runner();
         // 已安装未运行 → 启动（← apply_service_status 分支）
         if (runner->is_installed()) {
           auto r = service_op("Install", false, false, true);
           return r.is_err() ? CmdResult::err(r.error()) : CmdResult::ok(Json::object());
         }
         auto r = service_op("Install", true, false, true);
         return r.is_err() ? CmdResult::err(r.error()) : CmdResult::ok(Json::object());
       },
       consts::kTimeoutHeavy});

  // ---- uninstall_service（← service.rs:22-24）----
  svc.register_command(
      "uninstall_service",
      {[](const Json&) -> CmdResult {
         auto r = service_op("Uninstall", false, true, false);
         return r.is_err() ? CmdResult::err(r.error()) : CmdResult::ok(Json::object());
       },
       consts::kTimeoutHeavy});

  // ---- reinstall_service（← service.rs:26-28：先卸载再安装）----
  svc.register_command(
      "reinstall_service",
      {[](const Json&) -> CmdResult {
         auto r = service_op("Reinstall", true, true, true);
         return r.is_err() ? CmdResult::err(r.error()) : CmdResult::ok(Json::object());
       },
       consts::kTimeoutHeavy});

  // ---- repair_service（← service.rs:30-32：强制重装）----
  svc.register_command(
      "repair_service",
      {[](const Json&) -> CmdResult {
         auto r = service_op("Repair", true, true, true);
         return r.is_err() ? CmdResult::err(r.error()) : CmdResult::ok(Json::object());
       },
       consts::kTimeoutHeavy});

  // ---- is_service_available（← service.rs:34-38：管道存在 + IPC 可连）----
  svc.register_command(
      "is_service_available",
      {[](const Json&) -> CmdResult {
         auto runner = system::get_service_runner();
         if (!runner->is_available()) {
           return CmdResult::err({errs::kIo, "service IPC not available"});
         }
         return CmdResult::ok(Json(true));
       },
       consts::kTimeoutProbeLong});
}

}  // namespace clashwin::cmd
