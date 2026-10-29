// T09 cmd_clash（← cmd/clash.rs + feat/clash.rs 逐命令对照）
#include "cmd/cmd_install.h"

#include "cmd/cmd_support.h"
#include "core/delay_manager.h"
#include "util/node.h"
#include "cmd/command_service.h"
#include "config/config.h"
#include "core/validate.h"
#include "enhance/enhance.h"
#include "util/dirs.h"
#include "util/utf.h"

#include <fstream>
#include <sstream>

namespace clashwin::cmd {

namespace {
// ← feat/clash.rs patch_clash：锁 → edit_draft → 需重启项判断 → 重载/重启 →
//   apply+save / discard
std::mutex& clash_patch_lock() {
  static std::mutex m;
  return m;
}

Result<void> mgr_start() {
  // start 路径：sidecar（服务模式装配属 T18/T23 的启动编排）
  return core::CoreManager::instance().start_sidecar(
      util::dirs::clash_mini_path());
}

Result<void> mgr_apply(core::CoreApiClientAdapter& api, core::CoreOps& ops) {
  return core::CoreManager::instance().apply_config(
      api, utf::utf16_to_utf8(util::dirs::clash_mini_path()), ops);
}

Result<void> reload_or_restart(bool need_restart) {
  auto outcome = run_enhance_pipeline();
  if (!outcome.is_valid()) {
    return Result<void>::err({errs::kIo, outcome.message});
  }
  if (need_restart && is_core_running()) {
    // ← restart_core：snapshot→停→启→恢复（T07 CoreManager）
    auto& mgr = core::CoreManager::instance();
    core::CoreApiClientAdapter api(&mihomo_unchecked());
    core::CoreOps ops;
    ops.start = [] { return mgr_start(); };
    ops.stop = [] { return core::CoreManager::instance().stop(); };
    return mgr.restart_with_restore(api, ops);
  }
  if (is_core_running()) {
    core::CoreApiClientAdapter api(&mihomo_unchecked());
    core::CoreOps ops;
    ops.start = [] { return Result<void>::err(Error{0, "n/a"}); };
    ops.stop = [] { return Result<void>::err(Error{0, "n/a"}); };
    return mgr_apply(api, ops);
  }
  return Result<void>::ok();
}
}  // namespace

void install_cmd_clash(CommandService& svc) {
  // ---- copy_clash_env（← feat/proxy.rs:76-110：按 env_type 生成文本写剪贴板）----
  svc.register_command(
      "copy_clash_env",
      {[](const Json&) -> CmdResult {
         std::string ip = "127.0.0.1";
         uint16_t port = 7897;
         std::string env_type = "powershell";
         try {
           const auto& verge = *config::Config::instance().verge().latest_arc();
           if (verge.proxy_host) ip = *verge.proxy_host;
           if (verge.verge_mixed_port) port = *verge.verge_mixed_port;
           if (verge.env_type) env_type = *verge.env_type;
         } catch (...) {
         }
         std::string http_proxy = "http://" + ip + ":" + std::to_string(port);
         std::string socks5_proxy = "socks5://" + ip + ":" + std::to_string(port);
         std::string text;
         if (env_type == "bash") {
           text = "export https_proxy=" + http_proxy + " http_proxy=" + http_proxy +
                  " all_proxy=" + socks5_proxy;
         } else if (env_type == "cmd") {
           text = "set http_proxy=" + http_proxy + "\r\nset https_proxy=" + http_proxy;
         } else if (env_type == "powershell") {
           text = "$env:HTTP_PROXY=\"" + http_proxy + "\"; $env:HTTPS_PROXY=\"" + http_proxy + "\"";
         } else if (env_type == "nushell") {
           text = "load-env { http_proxy: \"" + http_proxy + "\", https_proxy: \"" + http_proxy +
                  "\" }";
         } else if (env_type == "fish") {
           text = "set -x http_proxy " + http_proxy + "; set -x https_proxy " + http_proxy;
         } else {
           return CmdResult::err({errs::kBadArgs, "copy_clash_env: Invalid env type! " + env_type});
         }
         if (!copy_text_to_clipboard(text)) {
           return CmdResult::err({errs::kIo, "Failed to write to clipboard"});
         }
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutProbe});

  // ---- get_clash_info（← clash.rs:22-24：ClashInfo serde 布局）----
  svc.register_command(
      "get_clash_info",
      {[](const Json&) -> CmdResult {
         auto info = config::Config::instance().clash().latest_arc()->get_client_info();
         Json j{{"mixed_port", info.mixed_port},
                {"socks_port", info.socks_port},
                {"port", info.port},
                {"server", info.server},
                {"ipv6", info.ipv6},
                {"allow_lan", info.allow_lan}};
         j["secret"] = info.secret.has_value() ? Json(*info.secret) : Json(nullptr);
         return CmdResult::ok(j);
       },
       consts::kTimeoutProbe});

  // ---- patch_clash_config（← feat/config.rs:15-81）----
  svc.register_command(
      "patch_clash_config",
      {[](const Json& args) -> CmdResult {
         std::lock_guard<std::mutex> lock(clash_patch_lock());
         YAML::Node patch = json_to_yaml(args);
         auto& clash = config::Config::instance().clash();
         YAML::Node old = clash.latest_arc()->mapping();  // draft 克隆语义：apply 前仍是旧值
         bool old_allow_lan = old["allow-lan"] ? old["allow-lan"].as<bool>() : false;
         bool old_ipv6 = old["ipv6"] ? old["ipv6"].as<bool>() : false;
         bool new_allow_lan = patch["allow-lan"] ? patch["allow-lan"].as<bool>() : false;
         bool new_ipv6 = patch["ipv6"] ? patch["ipv6"].as<bool>() : false;
         bool allow_lan_changed = patch["allow-lan"].IsDefined() && new_allow_lan != old_allow_lan;
         bool ipv6_changed = patch["ipv6"].IsDefined() && new_ipv6 != old_ipv6;
         bool need_restart = patch["secret"].IsDefined() ||
                             patch["external-controller"].IsDefined() || allow_lan_changed ||
                             ipv6_changed;  // ← feat/config.rs:40-43

         clash.edit_draft([&patch](config::IClashTemp& d) { d.patch_config(patch); });
         auto r = reload_or_restart(need_restart);
         if (r.is_err()) {
           clash.discard();  // ← 失败丢弃 draft
           return CmdResult::err(r.error());
         }
         clash.apply();
         if (auto save = clash.data_arc()->save_file(); !save.is_ok()) {
           return CmdResult::err(save.error());
         }
         events::EventBus::refresh_clash();  // ← handle::Handle::refresh_clash()
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutConfig});

  // ---- patch_clash_mode（← feat/clash.rs:65-100：内核 PATCH + 配置落盘）----
  svc.register_command(
      "patch_clash_mode",
      {[](const Json& args) -> CmdResult {
         const Json* mode = find_arg(args, {"mode"});
         if (!mode || !mode->is_string()) return CmdResult::err({errs::kBadArgs, "mode required"});
         std::string m = mode->get<std::string>();
         if (m != "rule" && m != "global" && m != "direct") {
           return CmdResult::err({errs::kBadArgs, "mode must be rule/global/direct"});
         }
         std::lock_guard<std::mutex> lock(clash_patch_lock());
         // 内核运行 → 先 PATCH /configs
         if (is_core_running()) {
           auto r = mihomo_unchecked().patch_base_config(
               mihomo::BaseConfig::patch_of({{"mode", m}}));
           if (r.is_err()) return CmdResult::err(r.error());
         }
         auto& clash = config::Config::instance().clash();
         clash.edit_draft([&m](config::IClashTemp& d) {
           YAML::Node patch;
           patch["mode"] = m;
           d.patch_config(patch);
         });
         clash.apply();
         if (auto save = clash.data_arc()->save_file(); !save.is_ok()) {
           return CmdResult::err(save.error());
         }
         events::EventBus::refresh_clash();
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutConfig});

  // ---- start_core（← clash.rs:39-46：CoreManager::start_core + refresh）----
  svc.register_command(
      "start_core",
      {[](const Json&) -> CmdResult {
         // 启动前先重生成运行时产物（← start_core_by_sidecar generate_file(Run)）
         auto outcome = run_enhance_pipeline();
         if (!outcome.is_valid()) {
           return CmdResult::err({errs::kIo, outcome.message});
         }
         // ← lifecycle.rs prepare_startup_mode：TUN+非管理员必须走服务（T18 装配
         //   sysproxy 回退）；本阶段无 TUN 上下文时直接 sidecar
         auto r = core::CoreManager::instance().start_sidecar(util::dirs::clash_mini_path());
         if (r.is_err()) return CmdResult::err(r.error());
         events::EventBus::refresh_clash();
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutHeavy});

  // ---- stop_core（← clash.rs:49-57）----
  svc.register_command(
      "stop_core",
      {[](const Json&) -> CmdResult {
         auto r = core::CoreManager::instance().stop();
         if (r.is_err()) return CmdResult::err(r.error());
         events::EventBus::refresh_clash();
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutHeavy});

  // ---- restart_core（← clash.rs:60-68：restart_with_restore 语义在 T07）----
  svc.register_command(
      "restart_core",
      {[](const Json&) -> CmdResult {
         auto outcome = run_enhance_pipeline();
         if (!outcome.is_valid()) return CmdResult::err({errs::kIo, outcome.message});
         auto& mgr = core::CoreManager::instance();
         core::CoreApiClientAdapter api(&mihomo_unchecked());
         core::CoreOps ops;
         ops.start = [] {
           return core::CoreManager::instance().start_sidecar(
               util::dirs::clash_mini_path());
         };
         ops.stop = [] { return core::CoreManager::instance().stop(); };
         auto r = mgr.restart_with_restore(api, ops);
         if (r.is_err()) return CmdResult::err(r.error());
         events::EventBus::refresh_clash();
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutHeavy});

  // ---- test_delay（← clash.rs:74-84：失败 10000 / 超时 5000 BUG-304）----
  svc.register_command(
      "test_delay",
      {[](const Json& args) -> CmdResult {
         const Json* url = find_arg(args, {"url"});
         if (!url || !url->is_string()) return CmdResult::err({errs::kBadArgs, "url required"});
         constexpr uint32_t kDelayErrorValue = 10000;  // ← DELAY_ERROR_VALUE
         auto r = test_delay_impl(url->get<std::string>());
         if (r.is_err()) {
           // ← feat/clash.rs：整体超时 unwrap_or(Ok(NODE_DELAY_MAX_MS))；
           //   连接类错误 → 10000
           if (r.error().message.find("timeout") != std::string::npos) {
             return CmdResult::ok(Json(5000u));
           }
           return CmdResult::ok(Json(kDelayErrorValue));
         }
         return CmdResult::ok(Json(r.value()));
       },
       consts::kTimeoutHeavy});

  // ---- test_group_delay（T16：全组测速——DelayManager testing 中间态 →
  //      测量组群测 → backend-delay-results 批量注入；显式子集不缓存复用）----
  svc.register_command(
      "test_group_delay",
      {[](const Json& args) -> CmdResult {
         ensure_t16_hooks();
         std::vector<std::string> nodes;
         if (const Json* nn = find_arg(args, {"node_names"});
             nn && nn->is_array()) {
           for (const auto& n : *nn) nodes.push_back(n.get<std::string>());
         }
         if (nodes.empty()) {
           // 未传子集 → PROXY 全量（剔除 dummy）
           auto client = mihomo();
           if (client.is_err()) return CmdResult::err(client.error());
           auto g = client.value()->get_group_by_name("PROXY");
           if (g.is_err() || !g.value().all.has_value())
             return CmdResult::err({errs::kCoreDown, "PROXY group missing"});
           for (const auto& n : *g.value().all)
             if (!clashwin::util::is_dummy_node(n)) nodes.push_back(n);
         }
         auto out = core::DelayManager::instance().test_group(
             "PROXY", nodes, /*allow_cache_reuse=*/false);
         Json arr = Json::array();
         for (auto& [name, delay] : out.display) arr.push_back({name, delay});
         return CmdResult::ok(arr);
       },
       consts::kTimeoutHeavy});

  // ---- get_clash_logs（← clash.rs:87-90 + manager/state.rs get_clash_logs：
  //      Sidecar→最近日志；Service→服务日志；NotRunning→[]）----
  svc.register_command(
      "get_clash_logs",
      {[](const Json&) -> CmdResult {
         Json arr = Json::array();
         if (running_mode() == core::RunningMode::NotRunning) return CmdResult::ok(arr);
         std::wstring path = util::dirs::clash_latest_log();
         std::ifstream f(path, std::ios::binary);
         if (!f.is_open()) return CmdResult::ok(arr);
         std::vector<std::string> lines;
         std::string line;
         while (std::getline(f, line)) lines.push_back(line);
         // 取尾部 1000 行（CLASH_LOGGER 容量口径）
         size_t start = lines.size() > 1000 ? lines.size() - 1000 : 0;
         for (size_t i = start; i < lines.size(); ++i) arr.push_back(lines[i]);
         return CmdResult::ok(arr);
       },
       consts::kTimeoutProbe});

  // ---- close_all_connections（← clash.rs:93-99）----
  svc.register_command(
      "close_all_connections",
      {[](const Json&) -> CmdResult {
         auto client = mihomo();
         if (client.is_err()) return CmdResult::err(client.error());
         auto r = client.value()->close_all_connections();
         if (r.is_err()) return CmdResult::err(r.error());
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutProbe});
}

}  // namespace clashwin::cmd
