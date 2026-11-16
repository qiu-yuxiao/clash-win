// T09 cmd_app（← cmd/app.rs + utils/sysinfo.rs + cmd/runtime.rs + 桩组）
#include "cmd/cmd_install.h"

#include <windows.h>
#include <shellapi.h>

#include "app/lifecycle.h"
#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "config/config.h"
#include "config/runtime_state.h"
#include "enhance/enhance.h"
#include "util/dirs.h"
#include "util/utf.h"
#include "clashwin/core/core_updater.h"
#include "clashwin/update/client_updater.h"
#include "clashwin/system/diagnostic_bundle.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace clashwin::cmd {

void install_cmd_app(CommandService& svc) {
  // ---- app_is_admin（← utils/sysinfo.rs，等价 IsUserAnAdmin）----
  svc.register_command(
      "app_is_admin",
      {[](const Json&) -> CmdResult { return CmdResult::ok(Json(is_admin())); },
       consts::kTimeoutProbe});

  // ---- show_in_folder（← app.rs:29-32）----
  svc.register_command(
      "show_in_folder",
      {[](const Json& args) -> CmdResult {
         const Json* p = find_arg(args, {"path"});
         if (!p || !p->is_string()) return CmdResult::err({errs::kBadArgs, "path required"});
         return as_cmd_result(show_in_folder(utf::utf8_to_utf16(p->get<std::string>())));
       },
       consts::kTimeoutProbe});

  // ---- open_web_url（← app.rs:35-43：仅 http/https）----
  svc.register_command(
      "open_web_url",
      {[](const Json& args) -> CmdResult {
         const Json* u = find_arg(args, {"url"});
         if (!u || !u->is_string()) return CmdResult::err({errs::kBadArgs, "url required"});
         return as_cmd_result(open_url_checked(u->get<std::string>()));
       },
       consts::kTimeoutProbe});

  // ---- get_auto_launch_status（← app.rs:90-93 + autostart.rs get_launch_status）----
  svc.register_command(
      "get_auto_launch_status",
      {[](const Json&) -> CmdResult {
         auto r = get_auto_launch_status_impl();
         if (r.is_err()) return CmdResult::err(r.error());
         return CmdResult::ok(Json(r.value()));
       },
       consts::kTimeoutProbe});

  // ---- get_runtime_config（← cmd/runtime.rs:12-16：enhance 全链产物 mapping）----
  svc.register_command(
      "get_runtime_config",
      {[](const Json&) -> CmdResult {
         if (!config::RuntimeState::instance().has_runtime()) {
           (void)enhance::run_enhance_full_from_config();
         }
         if (!config::RuntimeState::instance().has_runtime()) {
           return CmdResult::ok(nullptr);  // ← Option<Mapping> None
         }
         return CmdResult::ok(config::RuntimeState::instance().get_runtime_config_json());
       },
       consts::kTimeoutConfig});

  // ---- exit_app (<- cmd/app.rs:65) ----
  svc.register_command(
      "exit_app",
      {[](const Json&) -> CmdResult {
         std::thread([] {
           app::Lifecycle::instance().quit();
         }).detach();
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutProbe});

  // ---- restart_app (<- cmd/app.rs:71) ----
  svc.register_command(
      "restart_app",
      {[](const Json&) -> CmdResult {
         std::thread([] {
           app::Lifecycle::instance().restart_app();
         }).detach();
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutProbe});

  // ================= T24: 内核热升级 + 客户端自更新 + 诊断包 =================
  svc.register_command(
      "check_core_update",
      {[](const Json&) -> CmdResult {
         auto res = core::CoreUpdater::instance().check_latest_release();
         if (!res.is_ok()) {
           return CmdResult::err({errs::kIo, res.error().message});
         }
         return CmdResult::ok(res.value());
       },
       consts::kTimeoutProbe});

  svc.register_command(
      "start_core_upgrade",
      {[](const Json& args) -> CmdResult {
         core::GithubRelease release;
         if (args.is_object() && args.contains("tag_name")) {
           release = args.get<core::GithubRelease>();
         } else {
           auto check_res = core::CoreUpdater::instance().check_latest_release();
           if (!check_res.is_ok()) {
             return CmdResult::err({errs::kIo, check_res.error().message});
           }
           release = check_res.value();
         }
         auto res = core::CoreUpdater::instance().start_core_upgrade(release);
         if (!res.is_ok()) {
           return CmdResult::err({errs::kIo, res.error().message});
         }
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutHeavy});

  svc.register_command(
      "export_diagnostic_bundle",
      {[](const Json&) -> CmdResult {
         auto res = system::DiagnosticBundle::export_diagnostic_bundle();
         if (!res.is_ok()) {
           return CmdResult::err({errs::kIo, res.error().message});
         }
         return CmdResult::ok(Json(res.value()));
       },
       consts::kTimeoutHeavy});

  svc.register_command(
      "check_app_update",
      {[](const Json&) -> CmdResult {
         auto res = update::ClientUpdater::instance().check_app_update();
         if (!res.is_ok()) {
           return CmdResult::err({errs::kIo, res.error().message});
         }
         return CmdResult::ok(res.value().to_json());
       },
       consts::kTimeoutProbe});

  // 客户端更新包下载（← use-client-update.ts:72 clientUpdateObj.downloadAndInstall 的下载段）。
  // 仅落地到临时目录；安装/重启需提权，由调用方在本轮末尾统一列清单后再接。
  svc.register_command(
      "download_app_update",
      {[](const Json& args) -> CmdResult {
         const Json* url = find_arg(args, {"url", "download_url"});
         if (!url || !url->is_string() || url->get<std::string>().empty()) {
           return CmdResult::err({errs::kBadArgs, "missing url"});
         }
         const Json* sha = find_arg(args, {"sha256"});
         const std::string sha256 = (sha && sha->is_string()) ? sha->get<std::string>() : "";
         const Json* sig = find_arg(args, {"signature"});
         const std::string signature = (sig && sig->is_string()) ? sig->get<std::string>() : "";
         auto res = update::ClientUpdater::instance().download_app_update(
             url->get<std::string>(), sha256, signature);
         if (!res.is_ok()) {
           return CmdResult::err({errs::kIo, res.error().message});
         }
         return CmdResult::ok(Json{{"path", utf::utf16_to_utf8(res.value())}});
       },
       consts::kTimeoutHeavy});
}

}  // namespace clashwin::cmd
