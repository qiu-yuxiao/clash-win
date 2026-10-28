// T09 cmd_verge（← cmd/verge.rs + feat/config.rs patch_verge bitflags 联动）
#include "cmd/cmd_install.h"

#include <windows.h>

#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "clashwin/config/constants.h"
#include "config/config.h"
#include "system/sysproxy.h"
#include "util/log.h"
#include "util/dirs.h"
#include "util/utf.h"

#include <mutex>

namespace clashwin::cmd {

namespace {
// ← feat/config.rs VERGE_PATCH_LOCK
std::mutex& verge_patch_lock() { static std::mutex m; return m; }

// ← feat/config.rs:84-100 UpdateFlags（u16 bitflags）
enum UpdateFlags : uint16_t {
  kFRestartCore = 1 << 0,
  kFClashConfig = 1 << 1,
  kFVergeConfig = 1 << 2,
  kFLaunch = 1 << 3,
  kFSysProxy = 1 << 4,
  kFSysTrayIcon = 1 << 5,
  kFLanguage = 1 << 11,
  kFLogLevel = 1 << 12,
  kFLogFile = 1 << 13,
  kFAlwaysOnTop = 1 << 14,
  kFGroupSysTray = kFSysTrayIcon,
};

// ← feat/config.rs:102-173 determine_update_flags 逐字段
uint16_t determine_update_flags(const IVerge& patch) {
  uint16_t f = 0;
  bool restart_needed = patch.verge_socks_enabled.has_value() ||
                        patch.verge_http_enabled.has_value() ||
                        patch.verge_socks_port.has_value() || patch.verge_port.has_value() ||
                        patch.verge_mixed_port.has_value() ||
                        patch.enable_external_controller.has_value();
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

// ← feat/config.rs:176-238 process_terminated_flags。
// T09 阶段落位：RESTART_CORE/CLASH_CONFIG 走内核链路；SYS_PROXY（注册表写 +
// 守护）归 T18——以钩子注入，未挂时留痕不静默；LAUNCH 注册表直写；
// LANGUAGE/ALWAYS_ON_TOP/SYSTRAY 归 T10/T17/T19 留痕。
Result<void> process_flags(uint16_t flags, const IVerge& patch) {
  if (flags & kFRestartCore) {
    // ← Config::generate + restart_core
    auto outcome = run_enhance_pipeline();
    if (!outcome.is_valid()) return Result<void>::err({errs::kIo, outcome.message});
    if (is_core_running()) {
      auto& mgr = core::CoreManager::instance();
      core::CoreApiClientAdapter api(&mihomo_unchecked());
      core::CoreOps ops;
      ops.start = [] {
        return core::CoreManager::instance().start_sidecar(util::dirs::clash_mini_path());
      };
      ops.stop = [] { return core::CoreManager::instance().stop(); };
      auto r = mgr.restart_with_restore(api, ops);
      if (r.is_err()) return r;
    }
  }
  // ← feat/config.rs:182-189 注释：SYS_PROXY 必须先于 CLASH_CONFIG（TUN 切换
  //   双重接管窗口）。T18 前为留痕桩。
  // ← feat/config.rs:190-193：SYS_PROXY flag → update_sysproxy + refresh_guard
  //   （T18 转正；access 未装配时 Sysopt 留痕放行——测试环境安全）
  if (flags & kFSysProxy) {
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
        patch, clash_port, config::consts::kSingletonPort);
    auto& sysopt = system::Sysopt::instance();
    auto r = sysopt.update_sysproxy(view);
    if (r.is_err()) return r;
    sysopt.refresh_guard(view);
  }
  if (flags & kFClashConfig) {
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
    }
    events::EventBus::refresh_clash();
  }
  if (flags & kFVergeConfig) events::EventBus::refresh_verge();
  if (flags & kFLaunch) {
    // ← autostart::update_launch：写/删注册表 Run 值（键名 "Clash Mini" + --silent；
    //    T22 对账复用）
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE,
                      &key) == ERROR_SUCCESS) {
      if (patch.enable_auto_launch.value_or(false)) {
        std::wstring exe = util::dirs::exe_dir() + L"\\clashwin.exe --silent";
        RegSetValueExW(key, L"Clash Mini", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(exe.c_str()),
                       static_cast<DWORD>((exe.size() + 1) * sizeof(wchar_t)));
      } else {
        RegDeleteValueW(key, L"Clash Mini");
      }
      RegCloseKey(key);
    }
  }
  if (flags & kFLanguage) {
    CWLOG_INFO("patch_verge: LANGUAGE flag → " + patch.language.value_or("?") +
               " (i18n pending T10)");
  }
  if (flags & kFSysTrayIcon) {
    CWLOG_INFO("patch_verge: SYSTRAY_ICON flag (tray pending T19)");
  }
  if (flags & kFLogLevel) {
    CWLOG_INFO("patch_verge: LOG_LEVEL flag → " + patch.app_log_level.value_or("?"));
  }
  if (flags & kFLogFile) {
    CWLOG_INFO("patch_verge: LOG_FILE flag (logger rotation pending T25)");
  }
  if (flags & kFAlwaysOnTop) {
    CWLOG_INFO("patch_verge: ALWAYS_ON_TOP flag (window pending T11)");
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

         uint16_t flags = determine_update_flags(patch);
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
