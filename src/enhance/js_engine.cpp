#include "enhance/js_engine.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <mutex>

#include <nlohmann/json.hpp>

#include "enhance/enhance_constants.h"
#include "enhance/field.h"

extern "C" {
#include <quickjs.h>
}

namespace clashwin::enhance {

namespace {

using ordered_json = nlohmann::ordered_json;

// ---------- YAML ↔ JSON（serde_json 语义对齐：Number/Bool/String） ----------

bool plain_number_text(const std::string& s) {
  if (s.empty()) return false;
  size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
  if (i >= s.size()) return false;
  bool digits = false, dot = false, expo = false;
  for (; i < s.size(); ++i) {
    char c = s[i];
    if (c >= '0' && c <= '9') { digits = true; continue; }
    if (c == '.' && !dot && !expo) { dot = true; continue; }
    if ((c == 'e' || c == 'E') && digits && !expo) {
      expo = true;
      if (i + 1 < s.size() && (s[i + 1] == '-' || s[i + 1] == '+')) ++i;
      continue;
    }
    return false;
  }
  return digits;
}

bool plain_bool_text(const std::string& s) {
  return s == "true" || s == "True" || s == "TRUE" || s == "false" ||
         s == "False" || s == "FALSE";
}

ordered_json yaml_to_json(const YAML::Node& node) {
  if (!node.IsDefined() || node.IsNull()) return ordered_json(nullptr);
  if (node.IsScalar()) {
    const std::string& s = node.Scalar();
    // serde_json 语义对齐：Value::String → JSON string；Number/Bool → number/bool。
    // tag "!"（引号加载 / 程序显式标注）强制字符串；tag 空/?" 按文本归类
    // （tag 空的数字文本来自程序构造或 JSON 往返，语义为数字）。
    if (node.Tag() == "!") return ordered_json(s);
    if (plain_bool_text(s)) return ordered_json(s == "true" || s == "True" || s == "TRUE");
    if (plain_number_text(s)) {
      if (s.find_first_of(".eE") == std::string::npos) {
        try {
          return ordered_json(static_cast<int64_t>(std::stoll(s)));
        } catch (...) {}
      }
      try {
        return ordered_json(std::stod(s));
      } catch (...) {}
      return ordered_json(s);
    }
    return ordered_json(s);
  }
  if (node.IsSequence()) {
    ordered_json arr = ordered_json::array();
    for (const auto& item : node) arr.push_back(yaml_to_json(item));
    return arr;
  }
  ordered_json obj = ordered_json::object();
  for (const auto& kv : node) {
    obj[kv.first.Scalar()] = yaml_to_json(kv.second);
  }
  return obj;
}

YAML::Node json_to_yaml(const ordered_json& j) {
  YAML::Node out;
  if (j.is_null()) {
    out = YAML::Node(YAML::NodeType::Null);
  } else if (j.is_boolean()) {
    YAML::Node n(j.get<bool>() ? "true" : "false");
    n.SetTag("?");
    out = n;
  } else if (j.is_number_integer()) {
    out = YAML::Node(std::to_string(j.get<int64_t>()));
  } else if (j.is_number_float()) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.17g", j.get<double>());
    out = YAML::Node(buf);
  } else if (j.is_string()) {
    YAML::Node n(j.get<std::string>());
    n.SetTag("!");
    out = n;
  } else if (j.is_array()) {
    out = YAML::Node(YAML::NodeType::Sequence);
    for (const auto& item : j) out.push_back(json_to_yaml(item));
  } else if (j.is_object()) {
    out = YAML::Node(YAML::NodeType::Map);
    for (auto it = j.begin(); it != j.end(); ++it) {
      out[it.key()] = json_to_yaml(it.value());
    }
  } else {
    out = YAML::Node(YAML::NodeType::Null);
  }
  return out;
}

// ---------- 沙箱执行 ----------

struct ScriptEnv {
  std::vector<ScriptLog> outputs;
  size_t total_size = 0;
  std::chrono::steady_clock::time_point deadline;
  bool timed_out = false;
  bool output_limit_hit = false;
};

// __verge_log__(level, data)：输出限额（MAX_OUTPUTS / MAX_OUTPUT_SIZE）。
// 返回 true=已记录；false=超限（由 console 包装层 throw new Error —— C 回调内
// JS_Throw* + return JS_EXCEPTION 在 quickjs-ng 中不沿 JS try/catch 传播，
// 必须 JS 层抛错才能与 Rust/boa 的 catch → "__error_flag__" 降级路径对齐）
JSValue js_verge_log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv,
                     void* opaque) {
  auto* env = static_cast<ScriptEnv*>(opaque);
  if (argc < 2) return JS_FALSE;
  if (env->outputs.size() >= consts::kMaxOutputs) {
    env->output_limit_hit = true;
    return JS_FALSE;
  }
  size_t level_len = 0, data_len = 0;
  const char* level = JS_ToCStringLen(ctx, &level_len, argv[0]);
  if (!level) return JS_EXCEPTION;
  const char* data = JS_ToCStringLen(ctx, &data_len, argv[1]);
  if (!data) {
    JS_FreeCString(ctx, level);
    return JS_EXCEPTION;
  }
  size_t new_size = env->total_size + level_len + data_len;
  if (new_size > consts::kMaxOutputSize) {
    JS_FreeCString(ctx, level);
    JS_FreeCString(ctx, data);
    env->output_limit_hit = true;
    return JS_FALSE;
  }
  env->total_size = new_size;
  env->outputs.push_back({std::string(level, level_len), std::string(data, data_len)});
  JS_FreeCString(ctx, level);
  JS_FreeCString(ctx, data);
  return JS_TRUE;
}

// 3s 看门狗：超时后中断 JS 执行（对应 script.rs tokio::time::timeout）
int interrupt_handler(JSContext*, void* opaque) {
  auto* env = static_cast<ScriptEnv*>(opaque);
  if (std::chrono::steady_clock::now() >= env->deadline) {
    env->timed_out = true;
    return 1;
  }
  return 0;
}

Result<UseScriptOutcome> run_impl(const std::string& script,
                                  const YAML::Node& config,
                                  const std::string& name) {
  auto make_err = [](const std::string& msg) {
    Error e;
    e.code = 1;
    e.message = msg;
    return Result<UseScriptOutcome>::err(std::move(e));
  };

  YAML::Node lowered = field::use_lowercase(config);
  ordered_json config_json = yaml_to_json(lowered);
  std::string config_str;
  try {
    config_str = config_json.dump();
  } catch (const std::exception& ex) {
    return make_err(std::string("config serialize failed: ") + ex.what());
  }
  if (config_str.size() > consts::kMaxJsonSize) {
    return make_err("Configuration size exceeds maximum allowed size");
  }

  // name 序列化（JSON 引号转义）
  std::string effective_name = name.empty() ? "global" : name;
  std::string safe_name;
  try {
    safe_name = ordered_json(effective_name).dump();
  } catch (...) {
    return make_err("name serialize failed");
  }

  std::string code;
  code.reserve(script.size() + config_str.size() + 256);
  code += "try{\n";
  code += "const __verge_name = " + safe_name + ";\n";
  code += script;
  code += ";\n";
  code += "JSON.stringify(main(" + config_str + ",__verge_name)||'')\n";
  code += "} catch(err) {\n";
  code += "`__error_flag__ ${err.toString()}`\n";
  code += "}";

  JSRuntime* rt = JS_NewRuntime();
  if (!rt) return make_err("failed to create JS runtime");
  JS_SetMemoryLimit(rt, 1u << 30);  // 1GB 兜底（防大配置 OOM 拖死宿主）

  ScriptEnv env;
  env.deadline = std::chrono::steady_clock::now() +
                 std::chrono::milliseconds(consts::kScriptTimeoutMs);
  JS_SetInterruptHandler(rt, interrupt_handler, &env);

  JSContext* ctx = JS_NewContext(rt);
  if (!ctx) {
    JS_FreeRuntime(rt);
    return make_err("failed to create JS context");
  }

  Result<UseScriptOutcome> result = make_err("unreachable");
  do {
    // 沙箱加固：冻结原型链防止逃逸
    static const char* kSandboxPrelude =
        "Object.freeze(Object.prototype);\n"
        "Object.freeze(Function.prototype);\n"
        "Object.freeze(Array.prototype);\n"
        "Object.freeze(String.prototype);\n"
        "Object.freeze(Number.prototype);\n"
        "Object.freeze(Boolean.prototype);\n"
        "delete globalThis.Proxy;\n"
        "delete globalThis.Reflect;\n";
    JSValue pre = JS_Eval(ctx, kSandboxPrelude, ::strlen(kSandboxPrelude),
                          "<sandbox>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(pre)) {
      JS_FreeValue(ctx, pre);
      result = make_err("sandbox prelude failed");
      break;
    }
    JS_FreeValue(ctx, pre);

    // 注册 __verge_log__ + console。
    // 说明：JS_NewCFunction 无 opaque 参数，用全局单线程槽传递 ScriptEnv
    // （脚本执行与注册同线程同生命周期）。
    static ScriptEnv* g_env_slot = nullptr;
    g_env_slot = &env;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue log_fn = JS_NewCFunction(
        ctx,
        [](JSContext* c, JSValueConst self, int argc, JSValueConst* argv) -> JSValue {
          return js_verge_log(c, self, argc, argv, g_env_slot);
        },
        "__verge_log__", 2);
    JS_SetPropertyStr(ctx, global, "__verge_log__", log_fn);

    static const char* kConsole =
        "var console = Object.freeze({\n"
        "log(data){if(!__verge_log__('log',JSON.stringify(data, null, 2))) throw new Error('Maximum number of log outputs exceeded')},\n"
        "info(data){if(!__verge_log__('info',JSON.stringify(data, null, 2))) throw new Error('Maximum number of log outputs exceeded')},\n"
        "error(data){if(!__verge_log__('error',JSON.stringify(data, null, 2))) throw new Error('Maximum number of log outputs exceeded')},\n"
        "debug(data){if(!__verge_log__('debug',JSON.stringify(data, null, 2))) throw new Error('Maximum number of log outputs exceeded')},\n"
        "warn(data){if(!__verge_log__('warn',JSON.stringify(data, null, 2))) throw new Error('Maximum number of log outputs exceeded')},\n"
        "table(data){if(!__verge_log__('table',JSON.stringify(data, null, 2))) throw new Error('Maximum number of log outputs exceeded')},\n"
        "});";
    JSValue cv = JS_Eval(ctx, kConsole, ::strlen(kConsole), "<console>",
                         JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(cv)) {
      JS_FreeValue(ctx, cv);
      JS_FreeValue(ctx, global);
      result = make_err("console prelude failed");
      break;
    }
    JS_FreeValue(ctx, cv);

    JSValue ret = JS_Eval(ctx, code.c_str(), code.size(), "<script>",
                          JS_EVAL_TYPE_GLOBAL);
    if (env.timed_out) {
      JS_FreeValue(ctx, ret);
      JS_FreeValue(ctx, global);
      result = make_err("script execution timed out after 3000ms");
      break;
    }
    if (JS_IsException(ret)) {
      // 调试可见性：异常细节打到 stderr（error message 本身保持 Rust 文案）
      JSValue exc = JS_GetException(ctx);
      if (!JS_IsNull(exc) && !JS_IsUndefined(exc)) {
        size_t elen = 0;
        const char* emsg = JS_ToCStringLen(ctx, &elen, exc);
        if (emsg) {
          std::fprintf(stderr, "    js exception: %s\n", emsg);
          JS_FreeCString(ctx, emsg);
        }
      }
      JS_FreeValue(ctx, exc);
      JS_FreeValue(ctx, ret);
      JS_FreeValue(ctx, global);
      result = make_err("main function should return object");
      break;
    }
    if (!JS_IsString(ret)) {
      JS_FreeValue(ctx, ret);
      JS_FreeValue(ctx, global);
      result = make_err("main function should return object");
      break;
    }
    size_t len = 0;
    const char* cstr = JS_ToCStringLen(ctx, &len, ret);
    std::string json_text = cstr ? std::string(cstr, len) : std::string();
    if (cstr) JS_FreeCString(ctx, cstr);
    JS_FreeValue(ctx, ret);
    JS_FreeValue(ctx, global);

    if (json_text.size() > consts::kMaxJsonSize) {
      result = make_err("Script result exceeds maximum allowed size");
      break;
    }

    UseScriptOutcome out;
    std::string stripped = strip_outer_quotes(json_text);
    ordered_json parsed;
    bool parse_ok = true;
    try {
      parsed = ordered_json::parse(stripped);
    } catch (...) {
      parse_ok = false;
    }
    if (parse_ok && parsed.is_object()) {
      out.config = field::use_lowercase(json_to_yaml(parsed));
      out.logs = std::move(env.outputs);
      result = Result<UseScriptOutcome>::ok(std::move(out));
      break;
    }

    // JSON 解析失败（含 catch 分支的 "__error_flag__ ..."）→ 降级原 config
    out.config = config;
    out.degraded = true;
    out.logs = std::move(env.outputs);
    out.logs.push_back({"exception", "Script execution failed"});
    result = Result<UseScriptOutcome>::ok(std::move(out));
  } while (false);

  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);
  return result;
}

}  // namespace

std::string strip_outer_quotes(const std::string& s) {
  if (s.size() < 2) return s;
  char f = s.front(), b = s.back();
  if ((f == '"' && b == '"') || (f == '\'' && b == '\'')) {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

Result<UseScriptOutcome> use_script(const std::string& script,
                                    const YAML::Node& config,
                                    const std::string& name) {
  return run_impl(script, config, name);
}

}  // namespace clashwin::enhance
