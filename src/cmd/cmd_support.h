#pragma once
// T09 命令层共享辅助（JSON↔YAML 编解码 / IVerge·PrfItem 补丁解码 /
// enhance 全链 / mihomo 客户端访问器 / 测速 / 剪贴板）
//
// 【参数约定】invoke(name, args)：args 为 JSON 对象，键优先 Rust 参数名的
// snake_case（与 lib.rs 命令签名一致），同时兼容前端 camelCase——统一经
// find_arg(args, {"snake_case", "camelCase"}) 取值。
// 【返回约定】handler 返回 Result<Json>；错误 Error{code=29xx, message}。
#include <functional>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include "clashwin/config/models.h"
#include "core/core_manager.h"
#include "util/log.h"
#include "core/validate.h"
#include "events/event_bus.h"
#include "events/notice.h"
#include "mihomo/mihomo_client.h"
#include "util/result.h"

#include <vector>

namespace clashwin::cmd {

using config::IVerge;
using config::IVergeTheme;
using config::IProfiles;
using config::PrfItem;
using config::PrfOption;
using config::PrfSelected;
using config::PrfExtra;

using Json = nlohmann::json;
using CmdResult = Result<Json>;
using CmdHandler = std::function<CmdResult(const Json&)>;

// ---------- 错误码段（29xx） ----------
namespace errs {
inline constexpr int kNotFound = 2901;        // 命令未注册
inline constexpr int kTimeout = 2902;         // invoke 超时
inline constexpr int kNotImplemented = 2903;  // 桩命令（T21/T25 落地）
inline constexpr int kBadArgs = 2904;
inline constexpr int kCoreDown = 2905;        // 内核未运行
inline constexpr int kIo = 2906;
}  // namespace errs

// ---------- 参数取值（snake/camel 双写兼容） ----------
const Json* find_arg(const Json& args, std::initializer_list<const char*> keys);
inline Json arg_or(const Json& args, std::initializer_list<const char*> keys, Json def) {
  const Json* v = find_arg(args, keys);
  return v ? *v : def;
}

// ---------- JSON ↔ YAML ----------
Json yaml_to_json(const YAML::Node& node);   // mapping/seq/scalar/null
YAML::Node json_to_yaml(const Json& j);

// ---------- typed 补丁解码（仅透传出现的字段） ----------
IVerge iverge_from_json(const Json& j);
IProfiles iprofiles_patch_from_json(const Json& j);  // 仅 current（← profiles.rs patch 语义）
PrfItem prfitem_from_json(const Json& j);
PrfOption prf_option_from_json(const Json& j);       // 可为空对象

// ---------- ValidationOutcome ↔ JSON（← 01§5-A valid/busy/invalid/skipped） ----------
Json validation_outcome_to_json(const core::validate::ValidationOutcome& o);

// ---------- mihomo 客户端访问器（管道+secret 取自 config.yaml；懒建） ----------
// core 未运行 / 内核缺失 → err（kCoreDown）
Result<mihomo::MihomoClient*> mihomo();
// 不管运行态直接给客户端（构造开销极低；调用方自行处理请求失败）
mihomo::MihomoClient& mihomo_unchecked();

// ---------- enhance 全链（← manager/config.rs perform_config_update +
// feat::enhance_profiles）----------
// run_from_config → 落盘 clash-mini.yaml → check 副本校验 →（valid 且内核运行时）
// 热应用 apply_config。返回四分类 outcome；核心侧失败折叠进 invalid。
core::validate::ValidationOutcome run_enhance_pipeline();

// ---------- 环境探针 ----------
bool is_admin();
bool is_core_running();
core::RunningMode running_mode();

// ---------- 测速（← feat/clash.rs::test_delay，NODE_DELAY_MAX_MS=2000）----------
// https 的 TLS 握手段以 TCP(+HTTP) 完成时间为口径（schannel 握手留 T16/T25 对齐）
Result<uint32_t> test_delay_impl(const std::string& url);

// ---------- 剪贴板（← feat/proxy.rs copy_clash_env）----------
bool copy_text_to_clipboard(const std::string& text);

// Result<void> → CmdResult 适配（UI/打开类命令）
inline CmdResult as_cmd_result(Result<void> r) {
  return r.is_ok() ? CmdResult::ok(Json::object()) : CmdResult::err(r.error());
}

// ---------- 系统打开（← cmd/app.rs / help.rs）----------
Result<void> shell_open(const std::wstring& target);      // ShellExecuteW open
Result<void> show_in_folder(const std::wstring& path);    // explorer /select
Result<void> open_url_checked(const std::string& url);    // 仅 http/https

// ---------- 注册表自启状态（← autostart.rs::get_launch_status）----------
Result<bool> get_auto_launch_status_impl();

}  // namespace clashwin::cmd
