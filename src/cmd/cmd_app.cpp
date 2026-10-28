// T09 cmd_app（← cmd/app.rs + utils/sysinfo.rs + cmd/runtime.rs + 桩组）
#include "cmd/cmd_install.h"

#include <windows.h>
#include <shellapi.h>

#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "config/config.h"
#include "enhance/enhance.h"
#include "system/unlock_checker.h"
#include "util/dirs.h"
#include "util/utf.h"

#include <filesystem>
#include <fstream>
#include <sstream>

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

  // ---- download_icon_cache（← app.rs:96-99；T09 只做目录+桩逻辑，T25 落下载）----
  svc.register_command(
      "download_icon_cache",
      {[](const Json&) -> CmdResult {
         std::error_code ec;
         fs::create_directories(util::dirs::app_icons_dir(), ec);
         if (ec) return CmdResult::err({errs::kIo, "create icons dir failed"});
         return CmdResult::ok(Json(utf::utf16_to_utf8(util::dirs::app_icons_dir())));
       },
       consts::kTimeoutProbe});

  // ---- copy_icon_file（lib.rs 注册表在列；feat::copy_icon_file 归 T25）----
  svc.register_command(
      "copy_icon_file",
      {[](const Json&) -> CmdResult {
         return CmdResult::err({errs::kNotImplemented, "copy_icon_file: not implemented (T25)"});
       },
       consts::kTimeoutProbe});

  // ---- get_runtime_config（← cmd/runtime.rs:12-16：enhance 全链产物 mapping）----
  svc.register_command(
      "get_runtime_config",
      {[](const Json&) -> CmdResult {
         auto runtime = enhance::run_from_config();
         if (runtime.is_err()) return CmdResult::ok(nullptr);  // ← Option<Mapping> None
         return CmdResult::ok(yaml_to_json(runtime.value()));
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

  // ================= 桩组（T24/T25 落地；注册存在，调用返回未实现） =================
  auto stub = [](const char* name) {
    return std::pair<CmdHandler, int>{
        [name](const Json&) -> CmdResult {
          return CmdResult::err({errs::kNotImplemented,
                                 std::string(name) + ": not implemented (T24/T25)"});
        },
        consts::kTimeoutHeavy};
  };
  svc.register_command("export_diagnostic_bundle", stub("export_diagnostic_bundle"));
  svc.register_command("check_core_update", stub("check_core_update"));
  svc.register_command("start_core_upgrade", stub("start_core_upgrade"));
}

}  // namespace clashwin::cmd
