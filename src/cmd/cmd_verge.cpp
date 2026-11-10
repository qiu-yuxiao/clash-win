// T09 cmd_verge（← cmd/verge.rs + feat/config.rs patch_verge bitflags 联动）
#include "cmd/cmd_install.h"

#include <windows.h>

#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "cmd/verge_flags.h"
#include "app/singleton.h"
#include "clashwin/config/constants.h"
#include "config/config.h"
#include "core/autostart.h"
#include "core/core_manager.h"
#include "system/sysproxy.h"
#include "system/service_runner.h"
#include "util/log.h"
#include "util/dirs.h"
#include "util/utf.h"

#include <mutex>

namespace clashwin::cmd {

namespace {
// ← feat/config.rs VERGE_PATCH_LOCK
std::mutex& verge_patch_lock() { static std::mutex m; return m; }
}  // namespace

// ← feat/config.rs:102-173 determine_update_flags 逐字段（含 BUG-305 TUN 切换）
uint16_t determine_update_flags(const IVerge& old_config, const IVerge& patch,
                                bool running_service) {
  uint16_t f = 0;

  // 契约 1.3（v3.1.3 BUG-305）：TUN 与系统服务生命周期严格绑定。
  // 开 TUN 需切 Service 模式；关 TUN（含 Service 模式运行中关闭）必须
  // RESTART_CORE 退出服务内核并回到用户态 Sidecar。
  const bool current_tun = old_config.enable_tun_mode.value_or(false);
  bool tun_changed = false;
  if (patch.enable_tun_mode.has_value()) {
    if (*patch.enable_tun_mode) {
      tun_changed = !current_tun || !running_service;
    } else {
      tun_changed = current_tun || running_service;
    }
  }

  bool restart_needed = patch.verge_socks_enabled.has_value() ||
                        patch.verge_http_enabled.has_value() ||
                        patch.verge_socks_port.has_value() || patch.verge_port.has_value() ||
                        patch.verge_mixed_port.has_value() ||
                        patch.enable_external_controller.has_value() || tun_changed;
  if (restart_needed) f |= kFRestartCore;
  if (patch.enable_tun_mode.has_value() || patch.rule_fallback.has_value() ||
      patch.enable_builtin_enhanced.has_value()) {
    f |= kFClashConfig | kFGroupSysTray;
  }
  if (patch.home_cards.has_value() || patch.theme_mode.has_value() ||
      patch.theme_setting.has_value()) {
    f |= kFVergeConfig;
  }
  if (patch.enable_auto_launch.has_value()) f |= kFLaunch;
  if (patch.enable_system_proxy.has_value() || patch.verge_mixed_port.has_value()) {
    f |= kFSysProxy | kFGroupSysTray;
  }
  if (patch.system_proxy_bypass.has_value() || patch.pac_file_content.has_value() ||
      patch.proxy_auto_config.has_value() || patch.enable_proxy_guard.has_value() ||
      patch.proxy_guard_duration.has_value()) {
    f |= kFSysProxy;
  }
  if (patch.language.has_value()) f |= kFLanguage;
  if (patch.app_log_level.has_value()) f |= kFLogLevel;
  if (patch.app_log_max_size.has_value() || patch.app_log_max_count.has_value()) f |= kFLogFile;
  if (patch.enable_always_on_top.has_value()) f |= kFAlwaysOnTop;
  return f;
}

namespace {
// T09 阶段落位：RESTART_CORE/CLASH_CONFIG 走内核链路；SYS_PROXY（注册表写 +
// 守护）归 T18——以钩子注入，未挂时留痕不静默；LAUNCH 注册表直写；
// LANGUAGE/ALWAYS_ON_TOP/SYSTRAY 归 T10/T17/T19 留痕。
Result<void> process_flags(uint16_t flags, const IVerge& patch) {
  auto apply_sysproxy = [&]() -> Result<void> {
    uint16_t clash_port = 0;
    {
      auto& clash = config::Config::instance().clash();
      auto arc = clash.latest_arc();
      if (arc) {
        try {
          const auto& mapping = arc->mapping();
          if (mapping.IsMap() && mapping["mixed-port"]) {
            clash_port = static_cast<uint16_t>(mapping["mixed-port"].as<int>());
          }
        } catch (...) {
          clash_port = 0;
        }
      }
    }
    auto view = system::make_verge_view(
        patch, clash_port, app::current_singleton_port());
    auto& sysopt = system::Sysopt::instance();
    auto r = sysopt.update_sysproxy(view);
    if (r.is_err()) return r;
    sysopt.refresh_guard(view);
    return Result<void>::ok();
  };

  // 1. 退场顺序（← feat/config.rs:199-209）：
  //    当关闭 OS 系统代理（如切换至 TUN 或手动模式）时，必须先清除 OS 系统代理，
  //    再执行内核重启/TUN 网卡拉起，消除双重流量接管冲突窗口；
  // 2. 入场顺序：当开启 OS 系统代理（如从 TUN 切回系统代理）或变更混合端口时，
  //    先执行 RESTART_CORE 让 Sidecar 内核就绪监听端口，再将 OS 系统代理指向该端口。
  const bool disable_sys_proxy_first =
      (flags & kFSysProxy) && patch.enable_system_proxy.has_value() &&
      !*patch.enable_system_proxy;

  if (disable_sys_proxy_first) {
    auto r = apply_sysproxy();
    if (r.is_err()) return r;
  }

  if (flags & kFRestartCore) {
    // ← Config::generate + restart_core
    auto outcome = run_enhance_pipeline();
    if (!outcome.is_valid()) return Result<void>::err({errs::kIo, outcome.message});
    if (is_core_running()) {
      auto& mgr = core::CoreManager::instance();
      core::CoreApiClientAdapter api(&mihomo_unchecked());
      core::CoreOps ops;
      ops.start = [] {
        return core::CoreManager::instance().start_auto(util::dirs::clash_mini_path());
      };
      ops.stop = [] { return core::CoreManager::instance().stop(); };
      auto r = mgr.restart_with_restore(api, ops);
      if (r.is_err()) return r;
      if (patch.enable_tun_mode.has_value() && !*patch.enable_tun_mode) {
        auto runner = system::get_service_runner();
        if (runner) (void)runner->stop_service();
      }
    }
  }

  if ((flags & kFSysProxy) && !disable_sys_proxy_first) {
    auto r = apply_sysproxy();
    if (r.is_err()) return r;
  }
  // ← feat/config.rs:221-224：RESTART_CORE 已重新生成并加载配置，
  //   跳过冗余的 CLASH_CONFIG reload（v3.1.3 BUG-305 明确要求）
  if ((flags & kFClashConfig) && !(flags & kFRestartCore)) {
    auto outcome = run_enhance_pipeline();
    if (!outcome.is_valid()) return Result<void>::err({errs::kIo, outcome.message});
    if (is_core_running()) {
      core::CoreApiClientAdapter api(&mihomo_unchecked());
      core::CoreOps ops;
      ops.start = [] { return Result<void>::err(Error{0, "n/a"}); };
      ops.stop = [] { return Result<void>::err(Error{0, "n/a"}); };
      auto r = core::CoreManager::instance().apply_config(
          api, utf::utf16_to_utf8(util::dirs::clash_mini_path()), ops);
      if (r.is_err()) return r;
      if (patch.enable_tun_mode.has_value() && !*patch.enable_tun_mode) {
        auto runner = system::get_service_runner();
        if (runner) (void)runner->stop_service();
      }
    }
    events::EventBus::refresh_clash();
  }
  if (flags & kFVergeConfig) events::EventBus::refresh_verge();
  if (flags & kFLaunch) {
    core::AutostartManager::instance().update_launch(patch.enable_auto_launch);
  }
  if (flags & kFLanguage) {
    CWLOG_INFO("patch_verge: LANGUAGE flag → " + patch.language.value_or("?"));
  }
  if (flags & kFSysTrayIcon) {
    events::EventBus::refresh_verge();
  }
  if (flags & kFLogLevel) {
    CWLOG_INFO("patch_verge: LOG_LEVEL flag → " + patch.app_log_level.value_or("?"));
  }
  if (flags & kFLogFile) {
    CWLOG_INFO("patch_verge: LOG_FILE flag (logger rotation)");
  }
  if (flags & kFAlwaysOnTop) {
    CWLOG_INFO("patch_verge: ALWAYS_ON_TOP flag");
  }
  return Result<void>::ok();
}
}  // namespace

void install_cmd_verge(CommandService& svc) {
  // ---- get_verge_config（← verge.rs fetch_verge_config）----
  svc.register_command(
      "get_verge_config",
      {[](const Json&) -> CmdResult {
         auto v = config::Config::instance().verge().latest_arc();
         return CmdResult::ok(yaml_to_json(v->to_yaml()));
       },
       consts::kTimeoutProbe});

  // ---- patch_verge_config（← feat/config.rs:240-270 patch_verge）----
  svc.register_command(
      "patch_verge_config",
      {[](const Json& args) -> CmdResult {
         std::lock_guard<std::mutex> lock(verge_patch_lock());
         IVerge patch = iverge_from_json(args);
         auto& verge = config::Config::instance().verge();
         IVerge old = *verge.latest_arc();
         verge.edit_draft([&patch](IVerge& d) { d.patch_config(patch); });

         uint16_t flags = determine_update_flags(
             old, patch,
             core::CoreManager::instance().running_mode() == core::RunningMode::Service);
         auto processed = process_flags(flags, patch);
         if (processed.is_err()) {
           verge.discard();  // ← 失败丢弃 draft
           return CmdResult::err(processed.error());
         }
         verge.apply();
         if (auto save = verge.data_arc()->save_file(); !save.is_ok()) {
           // ← save 失败回滚内存配置
           verge.edit_draft([&old](IVerge& d) { d = old; });
           verge.apply();
           return CmdResult::err(save.error());
         }
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutConfig});
}

}  // namespace clashwin::cmd
