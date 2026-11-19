#pragma once
// ← ClashVerge src-tauri/src/enhance/mod.rs::enhance() 流水线全量移植
// ①收集 clash.yaml+当前订阅 → ②get_merged_proxies（重名 ` #n (HHMMSS)` 后缀）
// → ③全局 Merge → ④profile 级 Rules/Proxies/Merge →
// ⑤merge_default_config（clash.yaml 键覆盖；tun 深合并；dns/profile 仅补缺；
//   socks/http 未启用删键；redir/tproxy 跳过；config_version 剥离；
//   external-controller 受开关控制）→ ⑥原生 C++ 内建卫兵 → ⑦enforce_mini_agreements
// → ⑧use_tun → ⑨use_sort → ⑩apply_mandatory_dns_settings → ⑪境外明文 DNS→DoH
//
// 设计：enhance_core 为纯函数（EnhanceInput 全量注入，测试友好，G3 golden 轨
// 可直接指实机目录）；run_from_config 为生产装配薄层（从 Config 单例读输入）。
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "clashwin/config/models.h"
#include "enhance/chain.h"
#include "util/result.h"

namespace clashwin::enhance {

// ← mod.rs::ConfigValues（verge 侧开关集）
struct EnhanceVergeOptions {
  std::string clash_core = "win-mihomo";   // get_valid_clash_core()
  bool enable_tun = false;                  // enable_tun_mode
  std::optional<bool> enable_tun_sniffer;   // enable_tun_sniffer（nullopt = 自动）
  bool enable_builtin = true;               // enable_builtin_enhanced
  bool socks_enabled = false;               // verge_socks_enabled
  bool http_enabled = false;                // verge_http_enabled
  bool enable_external_controller = false;  // enable_external_controller
  std::string rule_fallback = "direct";     // rule_fallback
  std::string default_latency_test;         // 空 → cp.cloudflare.com/generate_204
};

// ← mod.rs::ProfileItems（profile 级链条项）
struct EnhanceProfileItems {
  YAML::Node config;          // 当前订阅 mapping（无 current → 空 map）
  std::string profile_name;   // 保留兼容（无脚本引擎后不再传参）
  ChainItem merge_item;       // profile 级 Merge
  ChainItem rules_item;       // profile 级 Rules
  ChainItem proxies_item;     // profile 级 Proxies
  ChainItem global_merge;     // 全局 Merge（uid "Merge"）
};

// get_merged_proxies 的输入：items 全集 + profiles 目录
struct MergedProxiesSource {
  std::wstring profiles_dir;
  // 仅 remote/local 参与合并（itype, file, updated）
  struct Item {
    std::string itype;
    std::string file;
    int64_t updated = 0;  // Unix 秒 → HHMMSS（本地时区）
  };
  std::vector<Item> items;
};

struct EnhanceInput {
  YAML::Node clash_config;             // IClashTemp mapping（config.yaml）
  EnhanceVergeOptions verge;
  EnhanceProfileItems profile;
  MergedProxiesSource merged_proxies_source;
  // ← BUG-405：持久 PROXY 选点与当前过滤词（pure enhance_core 可直接注入）
  std::optional<std::string> preferred_proxy;
  std::string filter_text;
};

struct EnhanceOutput {
  YAML::Node config;
  std::unordered_set<std::string> exists_keys;
};

// ← mod.rs::apply_builtin_guards：原生 C++ 内建卫兵
// （mode: script -> rule；hysteria 字符串 alpn -> 单元素数组）
YAML::Node apply_builtin_guards(YAML::Node config,
                                const std::string& clash_core,
                                bool enable_builtin);

// ← mod.rs::enhance() 核心（纯函数）
EnhanceOutput enhance_core(const EnhanceInput& input);

// ← mod.rs::get_merged_proxies：合并所有 remote/local 订阅 proxies；
// 重名 (name, HHMMSS) 计数 >1 → `{name} #{idx} ({suffix})`，否则
// `{name} ({suffix})`。返回 proxies 序列节点（可能为空 seq）。
YAML::Node get_merged_proxies(const MergedProxiesSource& source);

// 生产装配：从 Config::instance() 读取三份草稿 + profiles 目录文件。
// 失败（current 订阅读取错误）→ Error。
Result<YAML::Node> run_from_config();

// 完整全量流水线：返回 EnhanceOutput 并同步至 config::RuntimeState 缓存
Result<EnhanceOutput> run_enhance_full_from_config();

// ← mod.rs::read_persisted_proxy_and_filter / sync_runtime_proxy_group_head（BUG-405）：
// 读取当前激活 Profile 的持久化 PROXY 选点与 proxy_head_state.json filterText，
// 并同步刷新配置中的 profile.store-selected=false 与 PROXY.proxies[0]。
std::pair<std::optional<std::string>, std::string> read_persisted_proxy_and_filter();
void sync_runtime_proxy_group_head(YAML::Node& config);

// 单独格式化 updated → HHMMSS（本地时区；0/异常 → 000000）
std::string format_updated_suffix(int64_t updated);

}  // namespace clashwin::enhance
