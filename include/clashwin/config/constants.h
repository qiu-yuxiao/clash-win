#pragma once
// 配置域行为常量（唯一事实来源：phase1 02 附录常量速查 + ClashVerge
// src-tauri/src/{constants.rs,config/migrate.rs,config/mod.rs,utils/tmpl.rs}）
//
// 本头文件是 T03 配置体系的常量层；T07/T08/T09/T22 直接引用，禁止各自再定义。
#include <cstdint>

namespace clashwin::config::consts {

// ---------- 文件名（← constants.rs::files + utils/dirs.rs） ----------
inline constexpr const char* kRuntimeConfig = "clash-win.yaml";
inline constexpr const char* kCheckConfig = "clash-win-check.yaml";
inline constexpr const char* kWindowState = "window_state.json";
inline constexpr const char* kProxyHeadState = "proxy_head_state.json";
inline constexpr const char* kUnlockCache = "unlock_cache.json";

// ---------- 应用版本（2026-11-06 23:20 用户裁定：Clash Win 独立版本线，
//   从 0.1.0 起步逐版递增，不与 Clash Mini 的 3.2.2 绑死；
//   3.2.2 仅作移植时的源码参照版本，不是本产品版本号） ----------
inline constexpr const char* kAppVersion = "0.1.9";

// ---------- 端口（1:1 对齐 Clash Mini 官方规范） ----------
// 取消开发期同机共存特例，全量端口与密钥回归 1:1 对齐：
// mixed 10801 / socks 10802 / http 10803 / guard 兜底 7899 /
// controller 9098（冲突时自 9098 向上寻位）/ 单实例 33335。
inline constexpr uint16_t kDefaultMixed = 10801;
inline constexpr uint16_t kDefaultSocks = 10802;
inline constexpr uint16_t kDefaultHttp = 10803;
inline constexpr uint16_t kHttpGuardFallback = 7899;
inline constexpr uint16_t kControllerPort = 9098;
inline constexpr uint16_t kSingletonPort = 33335;

// ---------- 控制器 / 管道 / 服务 ----------
inline constexpr const char* kDefaultExternalController = "127.0.0.1:9098";
inline constexpr const char* kDefaultSecret = "adapted-by-qiu-yuxiao";
inline constexpr const char* kDefaultStack = "gvisor";  // tun.stack（tun.rs 注释）

// ---------- 配置版本（← migrate.rs） ----------
inline constexpr uint32_t kImplicitLegacyVersion = 1;  // 缺失 config_version 视为 v1
inline constexpr uint32_t kCurrentVergeVersion = 3;
inline constexpr uint32_t kCurrentClashVersion = 2;
inline constexpr uint32_t kCurrentProfilesVersion = 1;

// ---------- verge 模板默认值（← verge.rs::template()） ----------
inline constexpr uint64_t kAppLogMaxSize = 128;
inline constexpr size_t kAppLogMaxCount = 40;       // v2→v3 迁移目标（旧默认 8）
inline constexpr size_t kVergeOldLogCount = 8;      // migrate.rs v2→v3 旧默认
inline constexpr uint64_t kProxyGuardDuration = 30;
inline constexpr uint64_t kAutoLightWeightMinutes = 5;
inline constexpr uint64_t kHoverJumpNavigatorDelay = 280;
inline constexpr int kAutoLogClean = 2;  // 1:1天 2:7天 3:30天 4:90天

// ---------- 窗口几何持久化（← window_manager.rs） ----------
inline constexpr int64_t kGeometrySaveIntervalMs = 1000;  // 1s 节流 + 尾随补写
inline constexpr double kGeometryMinEps = 1.0;            // DPI 取整容差
inline constexpr double kWindowDefaultWidth = 271.0;
inline constexpr double kWindowDefaultHeight = 680.0;
inline constexpr double kWindowMinWidth = 271.0;
inline constexpr double kWindowMinHeight = 126.0;
inline constexpr double kWindowMaxWidth = 640.0;
inline constexpr double kWindowMaxHeight = 860.0;

// ---------- 订阅 ----------
inline constexpr uint64_t kProfileTimeoutSeconds = 20;  // PrfOption 默认超时
inline constexpr uint64_t kAllowAutoUpdateDefault = 1;  // true（prfitem.rs:748）
inline constexpr size_t kSubscriptionMaxBytes = 50u << 20;

// ----------profiles 文件名规则（← profiles.rs::is_profile_file + prfitem.rs） ----------
// R/L/m/s/r/p/g + uid + 扩展名；全局保护件 Merge.yaml / Script.js

// ---------- DEFAULT_PAC（← config/mod.rs 逐字抄录） ----------
inline constexpr const char* kDefaultPac =
    "function FindProxyForURL(url, host) {\n"
    "  return \"PROXY 127.0.0.1:%mixed-port%; SOCKS5 127.0.0.1:%mixed-port%; "
    "DIRECT;\";\n"
    "}\n";

}  // namespace clashwin::config::consts

// ---------- 订阅/增强文件模板（← utils/tmpl.rs 逐字抄录） ----------
namespace clashwin::config::tmpl {

// 新建本地 profile 模板
inline constexpr const char* kItemLocal =
    "# Profile Template for Clash Verge\n"
    "\n"
    "proxies: []\n"
    "\n"
    "rules: []\n";

// profile 增强 Merge 模板（全局 Merge 用，含 store-selected）
inline constexpr const char* kItemMerge =
    "# Profile Enhancement Merge Template for Clash Verge\n"
    "\n"
    "profile:\n"
    "  store-selected: true\n";

// profile 级 Merge 模板（空）
inline constexpr const char* kItemMergeEmpty =
    "# Profile Enhancement Merge Template for Clash Verge\n"
    "\n";

// profile 增强 Script 模板
inline constexpr const char* kItemScript =
    "// Define main function (script entry)\n"
    "\n"
    "function main(config, profileName) {\n"
    "  return config;\n"
    "}\n";

// profile 增强 Rules 模板
inline constexpr const char* kItemRules =
    "# Profile Enhancement Rules Template for Clash Verge\n"
    "\n"
    "prepend: []\n"
    "\n"
    "append: []\n"
    "\n"
    "delete: []\n";

// profile 增强 Proxies 模板
inline constexpr const char* kItemProxies =
    "# Profile Enhancement Proxies Template for Clash Verge\n"
    "\n"
    "prepend: []\n"
    "\n"
    "append: []\n"
    "\n"
    "delete: []\n";

}  // namespace clashwin::config::tmpl
