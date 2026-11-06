// T09 / T25 cmd_service（← cmd/service.rs：install/uninstall/reinstall/repair/available）
//
// 【共存铁律】clash_verge_service 是与 Clash Mini 共用的特权服务（唯一服务名，禁止第二名称）：
// - 已存在：只查询状态，停止则启动；绝不安装/重装/覆盖；
// - 缺失：允许用自带 clash-verge-service-install.exe 安装同名 clash_verge_service
//   （runas UAC 用户确认），安装后重新查询并启动；
// - uninstall / reinstall / repair 一律拒绝，避免破坏共用方。
// 全程用户可见反馈（notice_message），不只写日志。
#include "cmd/cmd_install.h"

#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "clashwin/core/constants.h"
#include "core/service_mode.h"
#include "events/event_bus.h"
#include "system/service_runner.h"
#include "util/log.h"

namespace clashwin::cmd {

namespace {

// 启动 + 就绪等待（不触碰安装器）
Result<void> service_start(const char* label) {
  CWLOG_INFO(std::string(label) + " service start");
  auto runner = system::get_service_runner();
  auto r = runner->start_service();
  if (r.is_err()) {
    CWLOG_ERROR(std::string(label) + " start failed: " + r.error().message);
    return r;
  }
  if (!runner->wait_for_ready(core::consts::kServiceWaitMaxMs)) {
    return Result<void>::err({errs::kIo, "service not ready in 30s budget"});
  }
  return Result<void>::ok();
}

// 统一的"破坏性操作禁用"拒绝错误（用户可见文案）
CmdResult refuse_service_mutation(const char* op) {
  return CmdResult::err({errs::kIo,
      std::string(op) + " 被禁止：clash_verge_service 为与 Clash Mini 共用的特权服务，"
                        "Clash Win 不会卸载/重装/修复该服务；如需变更请通过 Clash Mini 操作"});
}

}  // namespace

void install_cmd_service(CommandService& svc) {
  // ---- install_service（← Mini: src-tauri/src/cmd/service.rs:18；
  //      core/service.rs:457-495 Unavailable → install_service；共存条件式）----
  svc.register_command(
      "install_service",
      {[](const Json&) -> CmdResult {
         auto runner = system::get_service_runner();
         // 已注册但 ImagePath 失效（Mini 卸载残壳）视同未安装：
         // ← Mini: core/service.rs:470-475 ReinstallRequired / ForceReinstallRequired
         const bool installed =
             runner->is_installed() && core::service_mode::is_service_binary_valid();
         if (!installed) {
           // 缺失：用户可见提示 → 安装同名服务（UAC）→ 重新查询 → 启动
           events::EventBus::notice_message(
               "install_service::info",
               std::string("未检测到特权服务 ") + core::service_mode::kServiceName +
                   "：Clash Win 将安装共用的 clash_verge_service（需要 UAC 确认）");
           auto ir = runner->run_installer(true);
           if (ir.is_err()) {
             std::string msg = std::string("安装共用服务 clash_verge_service 失败（UAC 取消或安装器错误）：") +
                               ir.error().message;
             events::EventBus::notice_message("install_service::error", msg);
             return CmdResult::err({errs::kIo, msg});
           }
           if (!runner->is_installed()) {
             std::string msg =
                 "安装器已执行但 clash_verge_service 仍未注册，请重试或先通过 Clash Mini 安装";
             events::EventBus::notice_message("install_service::error", msg);
             return CmdResult::err({errs::kIo, msg});
           }
           auto r = service_start("Install");
           if (r.is_err()) {
             events::EventBus::notice_message("install_service::error",
                                              "clash_verge_service 已安装但启动失败：" + r.error().message);
             return CmdResult::err(r.error());
           }
           events::EventBus::notice_message(
               "install_service::ok", "共用的 clash_verge_service 已安装并启动（TUN 模式可用）");
           return CmdResult::ok(Json{{"message", "installed_and_started"}});
         }
         // 已存在：只查询状态；停止则启动，绝不安装/重装/覆盖
         if (runner->is_available()) {
           return CmdResult::ok(Json{{"message", "service_already_ready"}});
         }
         auto r = service_start("Install");
         if (r.is_err()) {
           events::EventBus::notice_message("install_service::error",
                                            "clash_verge_service 启动失败：" + r.error().message);
           return CmdResult::err(r.error());
         }
         events::EventBus::notice_message("install_service::ok",
                                          "clash_verge_service 已就绪（服务已存在，未重新安装）");
         return CmdResult::ok(Json{{"message", "started_existing_service"}});
       },
       consts::kTimeoutHeavy});

  // ---- uninstall_service：共存禁用（卸载会破坏 Clash Mini 的 TUN 能力）----
  svc.register_command(
      "uninstall_service",
      {[](const Json&) -> CmdResult { return refuse_service_mutation("uninstall_service"); },
       consts::kTimeoutHeavy});

  // ---- reinstall_service：共存禁用（覆盖可能破坏共用方版本）----
  svc.register_command(
      "reinstall_service",
      {[](const Json&) -> CmdResult { return refuse_service_mutation("reinstall_service"); },
       consts::kTimeoutHeavy});

  // ---- repair_service：共存禁用（强制重装同属覆盖操作）----
  svc.register_command(
      "repair_service",
      {[](const Json&) -> CmdResult { return refuse_service_mutation("repair_service"); },
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
