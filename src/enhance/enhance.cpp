#include "enhance/enhance.h"

#include <windows.h>

#include <ctime>
#include <fstream>
#include <map>
#include <sstream>

#include "config/config.h"
#include "config/prfitem.h"
#include "config/runtime_state.h"
#include "enhance/dns_defaults.h"
#include "enhance/enhance_constants.h"
#include "enhance/field.h"
#include "enhance/merge.h"
#include "enhance/mini_agreements.h"
#include "enhance/seq.h"
#include "enhance/tun.h"
#include "util/utf.h"
#include "util/yaml_utils.h"

namespace clashwin::enhance {

namespace {

bool has_key(const YAML::Node& map, const std::string& key) {
  const YAML::Node v = static_cast<const YAML::Node&>(map)[key];
  return v.IsDefined();
}

// ← help.rs::read_mapping：读文件 → apply_merge → 校验顶层 map
bool read_mapping_file(const std::wstring& path, YAML::Node& out) {
  auto loaded = util::load_yaml_file(path);
  if (!loaded.is_ok()) return false;
  YAML::Node node = loaded.value();
  if (!node.IsMap()) return false;
  out = node;
  return true;
}

// ← mod.rs::process_global_items / process_profile_items 的 Script 分支
void run_script_item(const ChainItem& item, const std::string& script,
                     YAML::Node& config, std::vector<std::string>& exists_keys,
                     std::vector<std::pair<std::string, std::vector<ScriptLog>>>& logs,
                     const std::string& profile_name) {
  auto res = use_script(script, config, profile_name);
  if (res.is_ok()) {
    UseScriptOutcome& outcome = res.value();
    for (const auto& k : field::use_keys(outcome.config)) {
      exists_keys.push_back(k);
    }
    config = outcome.config;
    if (!outcome.logs.empty()) logs.push_back({item.uid, std::move(outcome.logs)});
  } else {
    logs.push_back({item.uid, {{"exception", res.error().message}}});
  }
}

}  // namespace

std::string format_updated_suffix(int64_t updated) {
  // ← chrono::Local.timestamp_opt(ts, 0).format("%H%M%S")
  time_t t = static_cast<time_t>(updated);
  struct tm lt;
  if (localtime_s(&lt, &t) != 0) return "000000";
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02d%02d%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
  return buf;
}

YAML::Node get_merged_proxies(const MergedProxiesSource& source) {
  struct RawProxy {
    YAML::Node node;
    std::string name;
    std::string suffix;
  };
  std::vector<RawProxy> raw_proxies;

  for (const auto& item : source.items) {
    if (item.itype != "remote" && item.itype != "local") continue;
    if (item.file.empty()) continue;
    std::wstring path = source.profiles_dir;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') path += L'\\';
    path += utf::utf8_to_utf16(item.file);
    YAML::Node mapping;
    if (!read_mapping_file(path, mapping)) continue;
    const YAML::Node proxies =
        static_cast<const YAML::Node&>(mapping)["proxies"];
    if (!proxies || !proxies.IsSequence()) continue;
    std::string suffix = format_updated_suffix(item.updated);
    for (const auto& proxy : proxies) {
      std::string original_name = "Proxy";
      if (proxy.IsMap()) {
        const YAML::Node name = proxy["name"];
        // ← Value::as_str：字符串语义才有效（tag "!" 或 plain 非数字/bool/null）
        if (field::is_string_key(name)) original_name = name.Scalar();
      }
      raw_proxies.push_back({proxy, original_name, suffix});
    }
  }

  // 识别 (name, suffix) 重复
  std::map<std::pair<std::string, std::string>, int> counts;
  for (const auto& r : raw_proxies) {
    ++counts[{r.name, r.suffix}];
  }
  std::map<std::pair<std::string, std::string>, int> counters;

  YAML::Node all(YAML::NodeType::Sequence);
  for (RawProxy& r : raw_proxies) {
    auto key = std::make_pair(r.name, r.suffix);
    int count = counts[key];
    std::string new_name;
    if (count > 1) {
      int idx = ++counters[key];
      new_name = r.name + " #" + std::to_string(idx) + " (" + r.suffix + ")";
    } else {
      new_name = r.name + " (" + r.suffix + ")";
    }
    YAML::Node proxy = r.node;
    if (proxy.IsMap()) {
      YAML::Node n(new_name);
      n.SetTag("!");
      proxy["name"] = n;
    }
    all.push_back(proxy);
  }
  return all;
}

namespace {

// ← mod.rs::merge_default_config 全量移植
YAML::Node merge_default_config(const YAML::Node& config,
                                const YAML::Node& clash_config, bool socks_enabled,
                                bool http_enabled,
                                bool enable_external_controller) {
  YAML::Node cfg = config;

  for (const auto& kv : clash_config) {
    if (!kv.first.IsScalar()) continue;
    const std::string key = kv.first.Scalar();
    const YAML::Node& value = kv.second;

    if (key == "tun") {
      YAML::Node tun;
      {
        const YAML::Node existing = static_cast<const YAML::Node&>(cfg)["tun"];
        tun = (existing && existing.IsMap()) ? YAML::Clone(existing)
                                             : YAML::Node(YAML::NodeType::Map);
      }
      const YAML::Node patch = value.IsMap() ? value : YAML::Node(YAML::NodeType::Map);
      for (const auto& pkv : patch) {
        if (!pkv.first.IsScalar()) continue;
        tun[pkv.first.Scalar()] = pkv.second;
      }
      cfg["tun"] = tun;
    } else if (key == "dns" || key == "profile") {
      // dns / profile 是「订阅优先」的复合节点：仅补缺，不覆盖
      YAML::Node section;
      {
        const YAML::Node existing = static_cast<const YAML::Node&>(cfg)[key];
        section = (existing && existing.IsMap()) ? YAML::Clone(existing)
                                                 : YAML::Node(YAML::NodeType::Map);
      }
      const YAML::Node defaults =
          value.IsMap() ? value : YAML::Node(YAML::NodeType::Map);
      for (const auto& dkv : defaults) {
        if (!dkv.first.IsScalar()) continue;
        const std::string dkey = dkv.first.Scalar();
        if (!has_key(section, dkey)) {
          section[dkey] = dkv.second;
        }
      }
      cfg[key] = section;
    } else if (key == "tcp-concurrent") {
      // Mini 为省资源自加的标量默认值：订阅显式声明时订阅优先，仅补缺 false
      if (!has_key(cfg, "tcp-concurrent")) {
        cfg["tcp-concurrent"] = value;
      }
    } else {
      if (key == "socks-port" && !socks_enabled) {
        cfg.remove("socks-port");
        continue;
      }
      if (key == "port" && !http_enabled) {
        cfg.remove("port");
        continue;
      }
      // redir-port / tproxy-port：Windows 下其他平台专属键，跳过不写入
      if (key == "redir-port" || key == "tproxy-port") continue;
      // config_version 是 Clash Mini 自己的配置格式版本号，不进运行时配置
      if (key == "config_version") continue;
      // external-controller 键的开关逻辑：禁用 → 空字符串
      if (key == "external-controller") {
        if (enable_external_controller) {
          cfg["external-controller"] = value;
        } else {
          cfg["external-controller"] = std::string("");
        }
      } else {
        cfg[key] = value;
      }
    }
  }
  return cfg;
}

}  // namespace

EnhanceOutput enhance_core(const EnhanceInput& input) {
  const EnhanceVergeOptions& verge = input.verge;
  const EnhanceProfileItems& profile = input.profile;

  // ① 当前订阅
  YAML::Node config = profile.config && profile.config.IsMap()
                          ? YAML::Clone(profile.config)
                          : YAML::Node(YAML::NodeType::Map);

  // ② 合并所有 remote/local 订阅 proxies
  config["proxies"] = get_merged_proxies(input.merged_proxies_source);

  std::vector<std::string> exists_keys;
  std::vector<std::pair<std::string, std::vector<ScriptLog>>> result_logs;

  // ③ 全局 Merge/Script
  for (const auto& k : field::use_keys(config)) exists_keys.push_back(k);
  if (profile.global_merge.kind == ChainItem::Kind::Merge) {
    for (const auto& k : field::use_keys(profile.global_merge.merge)) {
      exists_keys.push_back(k);
    }
    config = use_merge(profile.global_merge.merge, config);
  }
  if (profile.global_script.kind == ChainItem::Kind::Script) {
    run_script_item(profile.global_script, profile.global_script.script, config,
                    exists_keys, result_logs, profile.profile_name);
  }

  // ④ profile 级 Rules/Proxies/Merge/Script
  if (profile.rules_item.kind == ChainItem::Kind::Rules) {
    config = use_seq(profile.rules_item.seq, config, "rules");
  }
  if (profile.proxies_item.kind == ChainItem::Kind::Proxies) {
    config = use_seq(profile.proxies_item.seq, config, "proxies");
  }
  if (profile.merge_item.kind == ChainItem::Kind::Merge) {
    for (const auto& k : field::use_keys(profile.merge_item.merge)) {
      exists_keys.push_back(k);
    }
    config = use_merge(profile.merge_item.merge, config);
  }
  if (profile.script_item.kind == ChainItem::Kind::Script) {
    run_script_item(profile.script_item, profile.script_item.script, config,
                    exists_keys, result_logs, profile.profile_name);
  }

  // ⑤ merge_default_config
  config = merge_default_config(config, input.clash_config, verge.socks_enabled,
                                verge.http_enabled,
                                verge.enable_external_controller);

  // ⑥ 内置脚本（enable_builtin；core 匹配 win-mihomo；core 空 → 支持）
  if (verge.enable_builtin) {
    bool meta_supported = verge.clash_core.empty() ||
                          verge.clash_core == "win-mihomo";
    for (const auto& item : builtin_chain_items(meta_supported)) {
      auto res = use_script(item.script, config, "");
      if (res.is_ok()) {
        config = res.value().config;  // 内置脚本失败仅记日志，config 保持
      }
    }
  }

  // ⑦ enforce_mini_agreements
  MiniAgreementOptions ma;
  ma.test_url = verge.default_latency_test.empty()
                    ? "https://cp.cloudflare.com/generate_204"
                    : verge.default_latency_test;
  ma.rule_fallback = verge.rule_fallback.empty() ? "direct" : verge.rule_fallback;
  config = enforce_mini_agreements(config, ma);

  // ⑧ use_tun_with_sniffer → ⑨ use_sort → ⑩ apply_mandatory_dns_settings → ⑪ DNS 上游治理
  config = use_tun_with_sniffer(config, verge.enable_tun, verge.enable_tun_sniffer);
  config = field::use_sort(config);
  config = apply_mandatory_dns_settings(config);
  config = sanitize_dns_upstreams(config);

  EnhanceOutput out;
  out.config = std::move(config);
  for (auto& k : exists_keys) out.exists_keys.insert(std::move(k));
  out.script_logs = std::move(result_logs);
  return out;
}

Result<EnhanceOutput> run_enhance_full_from_config() {
  auto& cfg = config::Config::instance();

  EnhanceInput input;
  input.clash_config = cfg.clash().latest_arc()->mapping();

  const auto& verge = *cfg.verge().latest_arc();
  input.verge.clash_core = verge.get_valid_clash_core();
  input.verge.enable_tun = verge.enable_tun_mode.value_or(false);
  input.verge.enable_tun_sniffer = verge.enable_tun_sniffer;
  input.verge.enable_builtin = verge.enable_builtin_enhanced.value_or(true);
  input.verge.socks_enabled = verge.verge_socks_enabled.value_or(false);
  input.verge.http_enabled = verge.verge_http_enabled.value_or(false);
  input.verge.enable_external_controller =
      verge.enable_external_controller.value_or(false);
  input.verge.rule_fallback = verge.rule_fallback.value_or("direct");
  input.verge.default_latency_test = verge.default_latency_test.value_or("");

  const auto& profiles = *cfg.profiles().latest_arc();
  const std::optional<std::string>& current = profiles.current;
  if (profiles.items && current) {
    const config::PrfItem* cur = profiles.get_item(*current);
    if (cur) {
      // 当前订阅 mapping
      if (cur->file) {
        std::wstring dir = config::prfitem::profiles_dir();
        std::wstring path = dir;
        if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
          path += L'\\';
        path += utf::utf8_to_utf16(*cur->file);
        YAML::Node mapping;
        if (!read_mapping_file(path, mapping)) {
          Error e;
          e.code = 1;
          e.message = "failed to read current profile \"" + *current + "\"";
          return Result<EnhanceOutput>::err(std::move(e));
        }
        input.profile.config = mapping;
      } else {
        Error e;
        e.code = 1;
        e.message = "failed to get the file field";
        return Result<EnhanceOutput>::err(std::move(e));
      }
      input.profile.profile_name = cur->name.value_or("");
      auto load_item = [&](const std::optional<std::string>& uid,
                           const char* fallback, ChainItem::Kind fallback_kind) {
        std::string u = uid && !uid->empty() ? *uid : fallback;
        const config::PrfItem* it = profiles.get_item(u);
        if (it && it->itype && it->file) {
          return chain_item_load(config::prfitem::profiles_dir(), u, *it->itype,
                                 *it->file);
        }
        // ← mod.rs unwrap_or_else：缺失回退空默认（uid "" / fallback uid）
        ChainItem empty;
        empty.uid = uid && !uid->empty() ? "" : fallback;
        empty.kind = fallback_kind;
        if (fallback_kind == ChainItem::Kind::Script) {
          empty.script = consts::kItemScript;
        }
        return empty;
      };
      input.profile.merge_item =
          load_item(cur->option ? cur->option->merge : std::nullopt, "Merge",
                    ChainItem::Kind::Merge);
      input.profile.script_item =
          load_item(cur->option ? cur->option->script : std::nullopt, "Script",
                    ChainItem::Kind::Script);
      input.profile.rules_item =
          load_item(cur->option ? cur->option->rules : std::nullopt, "Rules",
                    ChainItem::Kind::Rules);
      input.profile.proxies_item =
          load_item(cur->option ? cur->option->proxies : std::nullopt, "Proxies",
                    ChainItem::Kind::Proxies);
      input.profile.global_merge =
          chain_item_load(config::prfitem::profiles_dir(), "Merge", "merge",
                          "Merge.yaml");
      input.profile.global_script =
          chain_item_load(config::prfitem::profiles_dir(), "Script", "script",
                          "Script.js");
    }
  }

  // get_merged_proxies 数据源：全部 remote/local items
  input.merged_proxies_source.profiles_dir = config::prfitem::profiles_dir();
  if (profiles.items) {
    for (const auto& it : *profiles.items) {
      if (it.itype && (*it.itype == "remote" || *it.itype == "local") && it.file) {
        input.merged_proxies_source.items.push_back(
            {*it.itype, *it.file, it.updated.value_or(0)});
      }
    }
  }

  EnhanceOutput out = enhance_core(input);
  config::RuntimeState::instance().update(out);
  return Result<EnhanceOutput>::ok(std::move(out));
}

Result<YAML::Node> run_from_config() {
  auto res = run_enhance_full_from_config();
  if (res.is_err()) return Result<YAML::Node>::err(res.error());
  return Result<YAML::Node>::ok(std::move(res.value().config));
}

}  // namespace clashwin::enhance
