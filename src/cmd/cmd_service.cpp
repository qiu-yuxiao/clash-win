// T09 / T25 cmd_service（← cmd/service.rs：install/uninstall/reinstall/repair/available）
//
// 【★ 互斥共存方针】（用户 2026-11-09 改定，最高优先级；总纲《移植基本准则.md》§5.2，
// 取代旧「共存豁免 + refuse_service_mutation」设计）：
// - clash_verge_service 服务名固定、两侧同名（改不了）；
// - 服务注册指向对方路径（Clash Mini）→ install_service 时接管：停对方服务 →
//   删注册（仅注册表，不删对方磁盘文件）→ 自家安装器重注册 → 启动；
// - 注册指向本方 → 缺失才安装、停止则启动；
// - uninstall 仍拒绝（首次裁决；Mini 后端允许但其 UI 无入口，D2/选 B 只裁定 reinstall/repair）；
// - reinstall / repair 已放开（二次裁决 2026-11-09：Mini 可以那么 Win 应该也可以，
//   ← Mini: src-tauri/src/core/service.rs:193-217）。
// 全程用户可见反馈（notice_message），不只写日志。
#include "cmd/cmd_install.h"

#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "clashwin/core/constants.h"
#include "core/service_mode.h"
#include "events/event_bus.h"
#include "system/coexist.h"
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

// 重装 / 修复（← Mini: core/service.rs:193-217 reinstall_service / force_reinstall_service）
// Mini 的实际序列：先卸载（失败仅告警不阻断，:196-199）→ 再安装（失败即报错，:201-207）。
// 本实现保持同一序列，并在安装后沿用本方已有的"就绪确认 → 必要时启动"语义；
// 每一步失败都给 notice_message，不产生静默失败。
CmdResult service_reinstall(const char* op, const char* label) {
  CWLOG_INFO(std::string(label) + " service " + op);
  auto runner = system::get_service_runner();
  if (!runner) return CmdResult::err({errs::kIo, "service runner unavailable"});

  auto ur = runner->run_uninstaller();
  if (ur.is_err()) {
    // ← Mini:196-199：卸载失败不阻断重装，只留痕
    CWLOG_WARN(std::string(label) + " uninstall step failed, continue: " + ur.error().message);
  }
  auto ir = runner->run_installer(true);
  if (ir.is_err()) {
    const std::string msg =
        std::string(label) + " 失败：服务重装未完成（" + ir.error().message + "）";
    events::EventBus::notice_message("reinstall_service::error", msg);
    return CmdResult::err({errs::kIo, msg});
  }
  if (!runner->is_installed()) {
    const std::string msg =
        std::string(label) + " 失败：安装器已执行但 clash_verge_service 仍未注册";
    events::EventBus::notice_message("reinstall_service::error", msg);
    return CmdResult::err({errs::kIo, msg});
  }
  if (runner->is_available()) {
    events::EventBus::notice_message("reinstall_service::ok",
                                     std::string(label) + " 完成：clash_verge_service 已就绪");
    return CmdResult::ok(Json{{"message", "reinstalled"}});
  }
  auto sr = service_start(label);
  if (sr.is_err()) {
    events::EventBus::notice_message(
        "reinstall_service::error",
        std::string(label) + " 失败：服务已重装但启动失败：" + sr.error().message);
    return CmdResult::err(sr.error());
  }
  events::EventBus::notice_message("reinstall_service::ok",
                                   std::string(label) + " 完成：服务已重装并启动");
  return CmdResult::ok(Json{{"message", "reinstalled_and_started"}});
}

// 统一的"破坏性操作禁用"拒绝错误（用户可见文案）
CmdResult refuse_service_mutation(const char* op) {
  return CmdResult::err({errs::kIo,
      std::string(op) + " 被禁止：Clash Win 不提供该服务自愈操作"
                        "（互斥共存方针 2026-11-09：服务注册冲突由接管解决，"
                        "如需变更请通过 Clash Mini 操作或重装 Clash Win）"});
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
         const bool installed = runner->is_installed() && runner->is_binary_valid();
         // ★ 互斥共存方针接管分支：服务已注册但 ImagePath 指向 Clash Mini 路径
         //   → 停对方服务 → 删注册（不动对方磁盘文件）→ 自家安装器重注册 → 启动
         if (installed && !runner->is_image_own()) {
           events::EventBus::notice_message(
               "install_service::info",
               std::string("检测到 ") + core::service_mode::kServiceName +
                   " 注册指向 Clash Mini 路径：Clash Win 将停止对方服务并接管注册"
                   "（指向本方路径，需要 UAC 确认）");
           (void)runner->stop_service();  // 停对方服务（内含 UAC 回退）
           if (!system::coexist::delete_service_registration()) {
             const std::string msg =
                 "接管失败：clash_verge_service 旧注册删除被拒绝（UAC 取消或权限不足）";
             events::EventBus::notice_message("install_service::error", msg);
             return CmdResult::err({errs::kIo, msg});
           }
           auto ir = runner->run_installer(true);
           if (ir.is_err() || !runner->is_installed()) {
             const std::string msg = "接管失败：服务重注册未完成（" +
                                     (ir.is_err() ? ir.error().message
                                                  : std::string("安装器已执行但仍未注册")) +
                                     "）";
             events::EventBus::notice_message("install_service::error", msg);
             return CmdResult::err({errs::kIo, msg});
           }
           auto tr = service_start("Takeover");
           if (tr.is_err()) {
             events::EventBus::notice_message(
                 "install_service::error",
                 "clash_verge_service 接管后启动失败：" + tr.error().message);
             return CmdResult::err(tr.error());
           }
           events::EventBus::notice_message(
               "install_service::ok",
               "clash_verge_service 已接管并指向本方路径（互斥共存方针，TUN 模式可用）");
           return CmdResult::ok(Json{{"message", "takeover_completed"}});
         }
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

  // ---- reinstall_service（← Mini: cmd/service.rs:28-30 → core/service.rs:470-474）----
  svc.register_command(
      "reinstall_service",
      {[](const Json&) -> CmdResult { return service_reinstall("reinstall_service", "Reinstall"); },
       consts::kTimeoutHeavy});

  // ---- repair_service（← Mini: cmd/service.rs:32-35 → core/service.rs:475-479）----
  svc.register_command(
      "repair_service",
      {[](const Json&) -> CmdResult { return service_reinstall("repair_service", "Repair"); },
       consts::kTimeoutHeavy});

  // ---- stop_service ----
  svc.register_command(
      "stop_service",
      {[](const Json&) -> CmdResult {
         auto runner = system::get_service_runner();
         if (!runner) return CmdResult::ok(Json(true));
         auto r = runner->stop_service();
         if (r.is_err()) {
           CWLOG_WARN("stop_service failed: " + r.error().message);
           return CmdResult::err(r.error());
         }
         return CmdResult::ok(Json(true));
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
