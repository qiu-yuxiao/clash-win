#pragma once
// ← ClashVerge src-tauri/src/enhance/script.rs 全量移植（QuickJS-ng 实现）
// 执行 `function main(config, profileName)` 返回新 config：
// - YAML ↔ JS 经 JSON 文本桥（serde_json 无 preserve_order → 字母序 BTreeMap；
//   JS 侧 JSON.stringify 按 JS 对象键序——nlohmann::ordered_json 两端对齐）
// - 3s 看门狗（JS_SetInterruptHandler 超时中断；对应 script.rs tokio timeout）
// - __verge_log__ console 桥（log/info/error/debug/warn/table，JSON.stringify
//   (data,null,2)；MAX_OUTPUTS 1000 / MAX_OUTPUT_SIZE 1MB / MAX_JSON_SIZE 10MB）
// - 错误语义：eval/非 string 返回 → Error；JSON 解析失败 → 降级原 config +
//   logs 追加 ("exception","Script execution failed")（script.rs:141-148）
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "util/result.h"

namespace clashwin::enhance {

struct ScriptLog {
  std::string level;
  std::string message;
};

struct UseScriptOutcome {
  YAML::Node config;                 // 成功 → 新 config；JSON 解析失败 → 原 config
  std::vector<ScriptLog> logs;
  bool degraded = false;             // true = JSON 解析失败降级（config 为原样）
};

// ← script.rs::use_script（同步版；超时/异常以 Error 返回，调用方按 mod.rs
// 语义记录 logs 且保持 config 不变）
Result<UseScriptOutcome> use_script(const std::string& script,
                                    const YAML::Node& config,
                                    const std::string& name);

// ← script.rs::strip_outer_quotes（公开供单测）
std::string strip_outer_quotes(const std::string& s);

}  // namespace clashwin::enhance
