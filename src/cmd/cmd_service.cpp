// T09 cmd_service（← cmd/service.rs：install/uninstall/reinstall/repair/available）
#include "cmd/cmd_install.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>

#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "core/service_mode.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"

namespace clashwin::cmd {

namespace {
// ← service.rs::install_service/uninstall_service：resources 独立安装器 exe +
//   非提权走 runas（UAC）
Result<void> run_service_installer(bool install) {
  std::wstring base = util::dirs::app_resources_dir();
  std::wstring exe = base + (install ? L"\\clash-verge-service-install.exe"
                                     : L"\\clash-verge-service-uninstall.exe");
  if (!std::filesystem::exists(exe)) {
    return Result<void>::err({errs::kIo, "installer not found: " + utf::utf16_to_utf8(exe)});
  }
  SHELLEXECUTEINFOW sei{};
  sei.cbSize = sizeof(sei);
  sei.lpVerb = L"runas";
  sei.lpFile = exe.c_str();
  sei.nShow = SW_HIDE;
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  if (!ShellExecuteExW(&sei)) {
    return Result<void>::err({errs::kIo, "launch installer failed (UAC cancelled?)"});
  }
  if (sei.hProcess) {
    WaitForSingleObject(sei.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    if (code != 0) {
      return Result<void>::err({errs::kIo, "installer exit code " + std::to_string(code)});
    }
  }
  return Result<void>::ok();
}

// ← service.rs execute_service_operation_sync：失败必留痕
Result<void> service_op(const char* label, bool install, bool uninstall, bool start_after) {
  CWLOG_INFO(std::string(label) + " service operation");
  if (uninstall) {
    auto r = run_service_installer(false);
    if (r.is_err()) CWLOG_WARN(std::string(label) + " uninstall failed: " + r.error().message);
  }
  if (install) {
    auto r = run_service_installer(true);
    if (r.is_err()) {
      CWLOG_ERROR(std::string(label) + " install failed: " + r.error().message);
      return r;
    }
  }
  if (start_after) {
    auto r = core::service_mode::start_service();
    if (r.is_err()) {
      CWLOG_ERROR(std::string(label) + " start failed: " + r.error().message);
      return r;
    }
    // ← wait_for_service_ipc：就绪预算 30s / 200ms
    mihomo::ServiceIpc ipc;
    ipc.connect();
    core::service_mode::ServiceIpcAdapter adapter(&ipc);
    if (!core::service_mode::wait_for_service_ready(adapter)) {
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
         // 已安装未运行 → 启动（← apply_service_status 分支）
         if (core::service_mode::is_service_installed()) {
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
         mihomo::ServiceIpc ipc;
         if (ipc.connect().is_err()) {
           return CmdResult::err({errs::kIo, "service IPC not available"});
         }
         return CmdResult::ok(Json(true));
       },
       consts::kTimeoutProbeLong});
}

}  // namespace clashwin::cmd
