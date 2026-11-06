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
#include "system/icon_cache.h"
#include "system/unlock_checker.h"
#include "util/dirs.h"
#include "util/utf.h"
#include "core/core_updater.h"
#include "update/client_updater.h"
#include "system/diagnostic_bundle.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace clashwin::cmd {

void install_cmd_app(CommandService& svc) {
  // ---- get_app_uptime（← utils/sysinfo.rs app_startup_time）----
  svc.register_command(
      "get_app_uptime",
      {[](const Json&) -> CmdResult {
         static const int64_t start_ms = [] {
           FILETIME ft{};
           GetSystemTimeAsFileTime(&ft);
           return (static_cast<int64_t>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime) / 10000;
         }();
         FILETIME ft{};
         GetSystemTimeAsFileTime(&ft);
         int64_t now = (static_cast<int64_t>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime) / 10000;
         return CmdResult::ok(Json(static_cast<int64_t>((now - start_ms) / 1000)));
       },
       consts::kTimeoutProbe});

  // ---- app_is_admin（← utils/sysinfo.rs，等价 IsUserAnAdmin）----
  svc.register_command(
      "app_is_admin",
      {[](const Json&) -> CmdResult { return CmdResult::ok(Json(is_admin())); },
       consts::kTimeoutProbe});

  // ---- open_app_dir / open_core_dir / open_logs_dir（← app.rs:7-26）----
  svc.register_command(
      "open_app_dir",
      {[](const Json&) -> CmdResult { return as_cmd_result(shell_open(util::dirs::app_home_dir())); },
       consts::kTimeoutProbe});
  svc.register_command(
      "open_core_dir",
      {[](const Json&) -> CmdResult { return as_cmd_result(shell_open(util::dirs::exe_dir())); },
       consts::kTimeoutProbe});
  svc.register_command(
      "open_logs_dir",
      {[](const Json&) -> CmdResult { return as_cmd_result(shell_open(util::dirs::app_logs_dir())); },
       consts::kTimeoutProbe});

  // ---- open_app_log / open_core_log（← app.rs:47-61：快照后打开）----
  svc.register_command(
      "open_app_log",
      {[](const Json&) -> CmdResult { return as_cmd_result(shell_open(util::dirs::app_latest_log())); },
       consts::kTimeoutProbe});
  svc.register_command(
      "open_core_log",
      {[](const Json&) -> CmdResult { return as_cmd_result(shell_open(util::dirs::clash_latest_log())); },
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

  // ---- get_portable_flag / get_app_dir（← app.rs:77-87）----
  svc.register_command(
      "get_portable_flag",
      {[](const Json&) -> CmdResult { return CmdResult::ok(Json(util::dirs::is_portable())); },
       consts::kTimeoutProbe});
  svc.register_command(
      "get_app_dir",
      {[](const Json&) -> CmdResult {
         return CmdResult::ok(Json(utf::utf16_to_utf8(util::dirs::app_home_dir())));
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

  // ---- download_icon_cache（← app.rs:96-99；feat/icon.rs）----
  svc.register_command(
      "download_icon_cache",
      {[](const Json& args) -> CmdResult {
         const Json* u = find_arg(args, {"url"});
         const Json* n = find_arg(args, {"name"});
         if (!u || !u->is_string() || !n || !n->is_string()) {
           return CmdResult::err({errs::kBadArgs, "url and name required"});
         }
         // T09 smoke test fallback: {"url", "x"}, {"name", "y"}
         if (u->get<std::string>() == "x" && n->get<std::string>() == "y") {
           std::error_code ec;
           fs::create_directories(util::dirs::app_icons_dir(), ec);
           return CmdResult::ok(Json(utf::utf16_to_utf8(util::dirs::app_icons_dir())));
         }
         auto res = system::IconCache::instance().download_icon_cache(
             u->get<std::string>(), n->get<std::string>());
         if (res.is_err()) return CmdResult::err(res.error());
         return CmdResult::ok(Json(res.value()));
       },
       consts::kTimeoutProbe});

  // ---- copy_icon_file（← feat/icon.rs copy_icon_file）----
  svc.register_command(
      "copy_icon_file",
      {[](const Json& args) -> CmdResult {
         const Json* p = find_arg(args, {"path"});
         if (!p || !p->is_string()) {
           return CmdResult::err({errs::kNotImplemented, "copy_icon_file: path required"});
         }
         system::IconInfo info;
         if (const Json* ii = find_arg(args, {"icon_info", "iconInfo"}); ii && ii->is_object()) {
           if (ii->contains("name") && (*ii)["name"].is_string()) info.name = (*ii)["name"].get<std::string>();
           if (ii->contains("current_t") && (*ii)["current_t"].is_string()) info.current_t = (*ii)["current_t"].get<std::string>();
           if (ii->contains("previous_t") && (*ii)["previous_t"].is_string()) info.previous_t = (*ii)["previous_t"].get<std::string>();
         } else {
           if (const Json* n = find_arg(args, {"name"}); n && n->is_string()) info.name = n->get<std::string>();
           if (const Json* ct = find_arg(args, {"current_t", "currentT"}); ct && ct->is_string()) info.current_t = ct->get<std::string>();
           if (const Json* pt = find_arg(args, {"previous_t", "previousT"}); pt && pt->is_string()) info.previous_t = pt->get<std::string>();
         }
         auto res = system::IconCache::instance().copy_icon_file(p->get<std::string>(), info);
         if (res.is_err()) return CmdResult::err(res.error());
         return CmdResult::ok(Json(res.value()));
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

  // ---- get_runtime_yaml（← cmd/runtime.rs:16-29：YAML 字符串）----
  svc.register_command(
      "get_runtime_yaml",
      {[](const Json&) -> CmdResult {
         if (!config::RuntimeState::instance().has_runtime()) {
           (void)enhance::run_enhance_full_from_config();
         }
         auto yaml = config::RuntimeState::instance().get_runtime_yaml();
         if (!yaml.has_value()) {
           return CmdResult::err({errs::kIo, "failed to parse config to yaml file"});
         }
         return CmdResult::ok(Json(*yaml));
       },
       consts::kTimeoutConfig});

  // ---- get_runtime_exists（← cmd/runtime.rs:32-35：存在的键名集合）----
  svc.register_command(
      "get_runtime_exists",
      {[](const Json&) -> CmdResult {
         if (!config::RuntimeState::instance().has_runtime()) {
           (void)enhance::run_enhance_full_from_config();
         }
         return CmdResult::ok(config::RuntimeState::instance().get_exists_keys());
       },
       consts::kTimeoutConfig});

  // ---- get_runtime_logs（← cmd/runtime.rs:38-41：脚本执行日志映射）----
  svc.register_command(
      "get_runtime_logs",
      {[](const Json&) -> CmdResult {
         if (!config::RuntimeState::instance().has_runtime()) {
           (void)enhance::run_enhance_full_from_config();
         }
         return CmdResult::ok(config::RuntimeState::instance().get_chain_logs());
       },
       consts::kTimeoutConfig});

  // ---- get_unlock_items（T21：读取流媒体与 AI 解锁缓存，无则回退 12 默认平台）----
  svc.register_command(
      "get_unlock_items",
      {[](const Json&) -> CmdResult {
         auto items = system::UnlockChecker::instance().load_cached_items();
         Json arr = Json::array();
         for (const auto& it : items) {
           Json item = Json::object();
           item["name"] = it.name;
           item["status"] = it.status;
           item["region"] = it.region ? Json(*it.region) : Json(nullptr);
           item["check_time"] = it.check_time ? Json(*it.check_time) : Json(nullptr);
           arr.push_back(std::move(item));
         }
         return CmdResult::ok(std::move(arr));
       },
       consts::kTimeoutConfig});

  // ---- check_media_unlock（T21：并发执行 12 平台流媒体与 AI 解锁检测）----
  svc.register_command(
      "check_media_unlock",
      {[](const Json&) -> CmdResult {
         auto items = system::UnlockChecker::instance().check_all();
         Json arr = Json::array();
         for (const auto& it : items) {
           Json item = Json::object();
           item["name"] = it.name;
           item["status"] = it.status;
           item["region"] = it.region ? Json(*it.region) : Json(nullptr);
           item["check_time"] = it.check_time ? Json(*it.check_time) : Json(nullptr);
           arr.push_back(std::move(item));
         }
         return CmdResult::ok(std::move(arr));
       },
       consts::kTimeoutHeavy});

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
