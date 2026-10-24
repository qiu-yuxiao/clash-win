#pragma once
// 核心数据模型（← 05 文档 §3.1；字段与顺序逐一对齐
// ClashVerge src-tauri/src/config/{verge.rs:23-215,clash.rs,profiles.rs:18-28,
// prfitem.rs:20-125}——Rust 侧全部为 Option，此处对应 std::optional 以支持
// patch 语义（is_some 才覆盖）与 serde 序列化形态（None → null / 字段跳过）。
//
// 【序列化契约】serde_yaml_ng 按字段声明顺序输出；`#[serde(skip_serializing_if
// = "Option::is_none")]` 的字段在 None 时整键省略，其余 None 输出 `key: null`。
// 各模型 to_yaml/from_yaml 严格遵守（T03 模板 diff 门禁依赖字节级一致）。
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include "util/result.h"

namespace clashwin::config {

// ---------- IVergeTheme（← verge.rs:202-215，无 skip 属性 → None 输出 null） ----------
struct IVergeTheme {
  std::optional<std::string> primary_color;
  std::optional<std::string> secondary_color;
  std::optional<std::string> primary_text;
  std::optional<std::string> secondary_text;
  std::optional<std::string> info_color;
  std::optional<std::string> error_color;
  std::optional<std::string> warning_color;
  std::optional<std::string> success_color;
  std::optional<std::string> font_family;

  YAML::Node to_yaml() const;
  static std::optional<IVergeTheme> from_yaml(const YAML::Node& node);
};

// ---------- IVergeTestItem（← verge.rs:194-200） ----------
struct IVergeTestItem {
  std::optional<std::string> uid;
  std::optional<std::string> name;
  std::optional<std::string> icon;
  std::optional<std::string> url;
};

// ---------- IVerge（← verge.rs:23-192，字段顺序 = serde 输出顺序） ----------
struct IVerge {
  // skip_serializing_if 字段标记（None → 键省略；其余 None → null）
  struct Skip {};
  // 字段顺序即序列化顺序，勿重排
  std::optional<uint32_t> config_version;
  std::optional<std::string> app_log_level;
  std::optional<uint64_t> app_log_max_size;
  std::optional<uint64_t> app_log_max_count;
  std::optional<std::string> language;
  std::optional<std::string> theme_mode;
  std::optional<std::string> env_type;
  std::optional<std::string> start_page;
  std::optional<std::string> startup_script;
  std::optional<bool> traffic_graph;
  std::optional<bool> enable_memory_usage;
  std::optional<bool> enable_group_icon;  // skip
  std::optional<bool> pause_render_traffic_stats_on_blur;
  std::optional<std::string> menu_icon;  // skip
  std::optional<std::vector<std::string>> menu_order;  // skip
  std::optional<std::string> notice_position;          // skip
  std::optional<bool> collapse_navbar;
  std::optional<bool> enable_tun_mode;
  std::optional<bool> enable_auto_launch;
  std::optional<bool> enable_silent_start;
  std::optional<bool> enable_system_proxy;
  std::optional<bool> enable_proxy_guard;
  std::optional<bool> enable_bypass_check;
  std::optional<bool> use_default_bypass;
  std::optional<std::string> system_proxy_bypass;
  std::optional<uint64_t> proxy_guard_duration;
  std::optional<bool> proxy_auto_config;
  std::optional<std::string> pac_file_content;
  std::optional<std::string> proxy_host;
  std::optional<IVergeTheme> theme_setting;
  std::optional<std::vector<std::string>> web_ui_list;
  std::optional<std::string> clash_core;  // skip
  std::optional<bool> enable_always_on_top;
  std::optional<nlohmann::json> home_cards;
  std::optional<bool> auto_close_connection;
  std::optional<bool> auto_check_update;
  std::optional<std::string> default_latency_test;
  std::optional<bool> enable_auto_delay_detection;
  std::optional<uint64_t> auto_delay_detection_interval_minutes;
  std::optional<bool> enable_builtin_enhanced;
  std::optional<uint32_t> proxy_layout_column;
  std::optional<std::vector<IVergeTestItem>> test_list;
  std::optional<int32_t> auto_log_clean;
  std::optional<uint16_t> verge_mixed_port;
  std::optional<uint16_t> verge_socks_port;
  std::optional<bool> verge_socks_enabled;
  std::optional<uint16_t> verge_port;
  std::optional<bool> verge_http_enabled;
  std::optional<bool> enable_auto_light_weight_mode;
  std::optional<uint64_t> auto_light_weight_minutes;
  std::optional<bool> enable_hover_jump_navigator;
  std::optional<uint64_t> hover_jump_navigator_delay;
  std::optional<bool> enable_external_controller;
  std::optional<std::string> rule_fallback;

  // ← verge.rs::template()（出厂默认；language 取 system_language()）
  static IVerge make_template();

  // ← verge.rs::patch_config（逐字段 is_some 才覆盖；bitflags 联动属 T09）
  void patch_config(const IVerge& patch);

  // ← verge.rs::save_file（写 <home>\verge.yaml，前缀 "# Clash Verge Config"）
  Result<void> save_file() const;

  // ← verge.rs::VALID_CLASH_CORES / get_valid_clash_core
  static constexpr const char* kValidClashCores[] = {"mini-mihomo"};
  std::string get_valid_clash_core() const;

  YAML::Node to_yaml() const;
  static IVerge from_yaml(const YAML::Node& node);
};

// ---------- PrfSelected（← prfitem.rs:66-70） ----------
struct PrfSelected {
  std::optional<std::string> name;
  std::optional<std::string> now;

  YAML::Node to_yaml() const;
  static std::optional<PrfSelected> from_yaml(const YAML::Node& node);
};

// ---------- PrfExtra（← prfitem.rs:72-78） ----------
struct PrfExtra {
  uint64_t upload = 0;
  uint64_t download = 0;
  uint64_t total = 0;
  uint64_t expire = 0;

  bool operator==(const PrfExtra&) const = default;
  YAML::Node to_yaml() const;
  static std::optional<PrfExtra> from_yaml(const YAML::Node& node);
};

// ---------- PrfOption（← prfitem.rs:80-125） ----------
struct PrfOption {
  std::optional<std::string> user_agent;             // skip
  std::optional<bool> with_proxy;                    // skip
  std::optional<bool> self_proxy;                    // skip
  std::optional<uint64_t> update_interval;           // skip（分钟）
  std::optional<uint64_t> timeout_seconds;           // skip（默认 20）
  std::optional<bool> danger_accept_invalid_certs;   // skip
  std::optional<bool> allow_auto_update;             // skip（serde default true）
  std::optional<std::string> merge;                  // 关联 merge 子项 uid
  std::optional<std::string> script;
  std::optional<std::string> rules;
  std::optional<std::string> proxies;
  std::optional<std::string> groups;

  // ← prfitem.rs::PrfOption::merge（one 为主，other 非空字段覆盖）
  static std::optional<PrfOption> merge_opt(const std::optional<PrfOption>& one,
                                            const std::optional<PrfOption>& other);

  YAML::Node to_yaml() const;
  static std::optional<PrfOption> from_yaml(const YAML::Node& node);
};

// ---------- PrfItem 类型（prfitem.rs itype 字符串） ----------
enum class PrfType { Remote, Local, Merge, Script, Rules, Proxies, Groups };

const char* prf_type_to_string(PrfType t);          // remote/local/merge/...
std::optional<PrfType> prf_type_from_string(const std::string& s);

// ---------- PrfItem（← prfitem.rs:19-64；字段顺序 = serde 输出顺序） ----------
struct PrfItem {
  std::optional<std::string> uid;
  std::optional<std::string> itype;  // 保留字符串形态（serde rename "type"）
  std::optional<std::string> name;
  std::optional<std::string> file;
  std::optional<std::string> desc;   // skip
  std::optional<std::string> url;    // skip
  std::optional<std::vector<PrfSelected>> selected;  // skip
  std::optional<PrfExtra> extra;     // skip
  std::optional<int64_t> updated;    // Unix 秒
  std::optional<PrfOption> option;   // skip
  std::optional<std::string> home;   // skip
  std::optional<std::string> file_data;  // serde skip：仅内存，不落盘

  PrfType type() const;  // itype 字符串 → 枚举（未知值 → Remote 兜底）
  void set_type(PrfType t);

  // ---------- 本地构造（T03 范围；from_url 下载留 T06/T15） ----------
  // ← prfitem.rs::from_local（uid 前缀 L，挂 merge/script/rules/proxies 子项；
  //   子项创建失败 → Error 传播）
  static Result<PrfItem> from_local(const std::string& name,
                                    const std::string& desc,
                                    std::optional<std::string> file_data,
                                    const std::optional<PrfOption>& option);
  // ← prfitem.rs::from_merge（uid 非空用 ITEM_MERGE，否则新 uid + ITEM_MERGE_EMPTY）
  static PrfItem from_merge(std::optional<std::string> uid);
  static PrfItem from_script(std::optional<std::string> uid);
  static PrfItem from_rules();
  static PrfItem from_proxies();
  // ← prfitem.rs::from_url：T03 仅占位（网络下载 + 三级解析 + SSRF 属 T06/T15）
  static Result<PrfItem> from_url(const std::string& url,
                                  const std::optional<std::string>& name,
                                  const std::optional<std::string>& desc,
                                  const std::optional<PrfOption>& option);

  // ← prfitem.rs::read_file / save_file（profiles/ 目录；CRLF 归一后跳过无变化写）
  Result<std::string> read_file() const;
  Result<void> save_file_data(const std::string& data) const;

  // 文件路径：<home>\profiles\<file>
  std::optional<std::wstring> file_path() const;

  YAML::Node to_yaml() const;
  static std::optional<PrfItem> from_yaml(const YAML::Node& node);
};

// ---------- IProfiles（← profiles.rs:17-28） ----------
struct IProfiles {
  std::optional<uint32_t> config_version;
  std::optional<std::string> current;
  std::optional<std::vector<PrfItem>> items;

  // ← profiles.rs::new（读 profiles.yaml + 迁移 + 补缺失 uid；失败 → default）
  static IProfiles load();
  // ← profiles.rs::save_file（前缀 "# Profiles Config for Clash Verge"）
  Result<void> save_file() const;

  // ← profiles.rs::patch_config（仅修 current，且 uid 必须在 items 中）
  void patch_config(const IProfiles& patch);

  const PrfItem* get_item(const std::string& uid) const;       // 未找到 → nullptr
  PrfItem* get_item_mut(const std::string& uid);
  // ← profiles.rs::append_item（同 uid 更新元数据/option 合并；remote+url 去重复用；
  //   首个 remote/local 项自动设为 current）
  Result<void> append_item(PrfItem& item);
  // ← profiles.rs::delete_item（连删 option 关联子项文件；删 current 自动顺延）
  Result<bool> delete_item(const std::string& uid);
  // ← profiles.rs::reorder
  Result<void> reorder(const std::string& active_id, const std::string& over_id);
  // ← profiles.rs::patch_item（file 字段禁止外部修改——路径穿越防护）
  Result<void> patch_item(const std::string& uid, const PrfItem& item);

  // ← config.rs::ensure_default_profile_items：全局 Merge/Script 保护件
  Result<void> ensure_default_profile_items();

  // ← profiles.rs::cleanup_orphaned_files（以 items 为准清理 profiles/ 冗余文件）
  Result<void> cleanup_orphaned_files() const;

  YAML::Node to_yaml() const;
  static IProfiles from_yaml(const YAML::Node& node);
};

// ---------- IClashTemp（← clash.rs:15-17：Mapping 语义，键动态存在） ----------
// 保留 yaml-cpp 节点（插入序 = 文档序），类型化 accessor 走 guard 函数。
class IClashTemp {
 public:
  IClashTemp() = default;
  explicit IClashTemp(YAML::Node mapping) : mapping_(std::move(mapping)) {}

  // ← clash.rs::new：读 config.yaml →（版本检测先于模板合并！）→ 补模板键 →
  //   备份+迁移 → secret 修正 → controller 冲突寻位 → guard
  static IClashTemp load();
  // ← clash.rs::template()（config_version=2 + 全部出厂键值，插入序照抄）
  static IClashTemp make_template();

  const YAML::Node& mapping() const { return mapping_; }
  YAML::Node& mapping() { return mapping_; }

  // ← clash.rs::patch_config（键级覆盖）
  void patch_config(const YAML::Node& patch);

  // ← clash.rs::save_config（前缀 "# Generated by Clash Verge"）
  Result<void> save_file() const;

  // ---------- guard 系列（← clash.rs:242-352 逐条） ----------
  static uint16_t guard_mixed_port(const YAML::Node& map);   // 默认 10801，0→10801
  static uint16_t guard_socks_port(const YAML::Node& map);   // 默认 10802，0→10802
  static uint16_t guard_port(const YAML::Node& map);         // 默认 7899，0→7899
  // external-controller 解析；':' 前缀补 127.0.0.1；非法 → 127.0.0.1:9098
  static std::string guard_server_ctrl(const YAML::Node& map);
  static std::string guard_external_controller(const YAML::Node& map);
  // client 口径：unspecified IP → 127.0.0.1
  static std::string guard_client_ctrl(const YAML::Node& map);
  // external-controller-pipe：恒为 ipc_path()（\\.\pipe\mini-mihomo）
  static std::string guard_external_controller_ipc();

  uint16_t get_mixed_port() const { return guard_mixed_port(mapping_); }
  uint16_t get_socks_port() const { return guard_socks_port(mapping_); }
  uint16_t get_port() const { return guard_port(mapping_); }

  // ← clash.rs::get_client_info
  struct ClashInfo {
    uint16_t mixed_port = 10801;
    uint16_t socks_port = 10802;
    uint16_t port = 7899;
    std::string server;  // external-controller（client 口径）
    std::optional<std::string> secret;
    bool ipv6 = false;
    bool allow_lan = false;
  };
  ClashInfo get_client_info() const;

  // ← clash.rs::is_port_conflict + find_free_controller_port：
  //   bind 探测；绑定失败时向 127.0.0.1:port/configs 发 100ms HTTP 探测，
  //   无成功响应判冲突；冲突则自 9098 起寻第一个可绑定端口
  static bool is_port_conflict(uint16_t port, const std::string& secret);
  static uint16_t find_free_controller_port(uint16_t start_port);
  // 载入时的 controller 冲突自动寻位（对 mapping_ 原地改写）
  void resolve_controller_conflict();

  // 全量 guard（端口四项 + pipe 落回 mapping_）
  void apply_guard();

 private:
  YAML::Node mapping_;
};

}  // namespace clashwin::config
