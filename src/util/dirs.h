#pragma once
// 配置目录体系（← utils/dirs.rs 全函数移植；品牌独立：Clash Win 自有数据目录，
// 与参考源 Clash Mini 的 io.github.clash-mini.clash-mini 完全分离，可双机共存）
// app_home_dir：便携模式 = <exe>\.config\io.github.clash-win.clash-win
//              （便携标记 = <exe>\.config\PORTABLE 文件存在）；
//              否则 %APPDATA%\io.github.clash-win.clash-win。
// 调试/测试覆盖（绝不动真实 %APPDATA%）：
//   CLASHWIN_HOME_OVERRIDE      → 直接作为 app_home_dir 返回值
//   CLASHWIN_PORTABLE_OVERRIDE  → "1" 强制便携分支 / "0" 强制非便携
#include <functional>
#include <string>

namespace clashwin::util::dirs {

// ← dirs.rs APP_ID（dev 分支 .dev 后缀不移植：C++ 侧无 verge-dev feature）
// 品牌独立：对应 github.com/qiu-yuxiao/clash-win，勿与参考源 clash-mini 共用
inline constexpr const wchar_t* kAppId = L"io.github.clash-win.clash-win";
inline constexpr const wchar_t* kClashConfig = L"config.yaml";
inline constexpr const wchar_t* kVergeConfig = L"verge.yaml";
inline constexpr const wchar_t* kProfileYaml = L"profiles.yaml";

// ← dirs.rs::init_portable_flag：探测 <exe>\.config\PORTABLE，进程内缓存一次
void init_portable_flag();
bool is_portable();
void reset_portable_cache_for_testing();

// 当前 exe 所在目录（GetModuleFileNameW 的父目录）
std::wstring exe_dir();

// 纯函数（可测）：给定 exe 目录时的便携 home
std::wstring portable_home_for(const std::wstring& exe_directory);

// ← dirs.rs::app_home_dir（含环境变量覆盖，见文件头）
std::wstring app_home_dir();

// ← dirs.rs 全函数逐个对应
std::wstring app_resources_dir();      // <exe>\resources
std::wstring app_profiles_dir();       // <home>\profiles
std::wstring app_icons_dir();          // <home>\icons
std::wstring app_logs_dir();           // <home>\logs
std::wstring app_latest_log();         // <home>\logs\latest.log
std::wstring clash_path();             // <home>\config.yaml
std::wstring verge_path();             // <home>\verge.yaml
std::wstring profiles_path();          // <home>\profiles.yaml
std::wstring service_path();           // <resources>\clash-verge-service.exe
std::wstring clash_mini_path();        // <home>\clash-win.yaml（运行时生成）
std::wstring clash_mini_check_path();  // <home>\clash-win-check.yaml（校验用）
std::wstring window_state_path();      // <home>\window_state.json
std::wstring proxy_head_state_path();  // <home>\proxy_head_state.json
std::wstring cores_dir();              // <home>\cores（内核热升级区）
std::wstring ruleset_dir();            // <home>\ruleset（gfwlist 等规则集，实机存在）
std::wstring sidecar_core_path();      // <exe>\win-mihomo.exe（sidecar 兜底）

// 以下两个会创建目录（← dirs.rs::sidecar_log_dir/service_log_dir）
std::wstring sidecar_log_dir();        // <home>\logs\sidecar
std::wstring service_log_dir();        // <home>\logs\service

// ← dirs.rs::clash_latest_log：Service→service_latest.log，其余→sidecar_latest.log
// 运行模式由 core 层在启动时注入（CMake 依赖是 core→config→util，
// util 不得反向 include core，故用 provider 回调而非直接查 CoreManager）。
// 未注入时按非 Service 处理（与 dirs.rs 的 NotRunning/Sidecar 分支一致）。
void set_clash_log_service_provider(std::function<bool()> provider);
std::wstring clash_latest_log();

// ← dirs.rs::ipc_path
std::wstring ipc_path();  // \\.\pipe\mini-mihomo-win（与 Clash Mini 的 mini-mihomo 管道错开）

}  // namespace clashwin::util::dirs
