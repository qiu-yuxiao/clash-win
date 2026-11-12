#include "verge.h"

#include <windows.h>

#include <mutex>

#include "clashwin/config/constants.h"
#include "util/dirs.h"
#include "util/str_utils.h"
#include "util/utf.h"
#include "migrate.h"
#include "util/log.h"
#include "yaml_serde.h"

namespace clashwin::config::verge {

using serde::opt_bool;
using serde::opt_i32;
using serde::opt_str;
using serde::opt_u16;
using serde::opt_u32;
using serde::opt_u64;
using serde::set_bool;
using serde::set_i64;
using serde::set_null;
using serde::set_str;
using serde::set_u16;
using serde::set_u32;

std::wstring verge_path() { return util::dirs::verge_path(); }

// ---------- 系统语言（← crates/clash-verge-i18n::system_language 简化对齐） ----------
std::string system_language() {
  wchar_t buf[LOCALE_NAME_MAX_LENGTH] = {};
  if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SNAME, buf,
                      LOCALE_NAME_MAX_LENGTH) > 0) {
    std::string loc = utf::utf16_to_utf8(buf);
    for (auto& c : loc) c = static_cast<char>(::tolower(c));
    // locale_alias（lib.rs:14-21）：zh 变体归一、zhtw、jp
    if (loc.rfind("zh-tw", 0) == 0 || loc.rfind("zh-hk", 0) == 0 ||
        loc.rfind("zh-mo", 0) == 0 || loc.rfind("zh-hant", 0) == 0)
      return "zhtw";
    if (loc.rfind("zh", 0) == 0) return "zh";
    if (loc.rfind("ja", 0) == 0) return "jp";
    if (loc.rfind("en", 0) == 0) return "en";
  }
  return "zh";  // DEFAULT_LANGUAGE
}

// ---------- 安装器语言（用户指令 2026-11-07） ----------
std::string language_from_installer_id(int id) {
  switch (id) {
    case 2052: return "zh";   // LANG_SIMPCHINESE（clashwin.nsi:154）
    case 1033: return "en";   // LANG_ENGLISH
    case 1049: return "ru";   // LANG_RUSSIAN
    default: return {};
  }
}

std::string installer_language() {
  // 键与值名必须与安装器一致：MUI_LANGDLL_REGISTRY_KEY="${MANUPRODUCTKEY}"
  //（= Software\Clash Win\ClashWin）、VALUENAME="Installer Language"。
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Clash Win\\ClashWin", 0,
                    KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
    return {};
  }
  wchar_t buf[64] = {};
  DWORD bytes = sizeof(buf) - sizeof(wchar_t);
  DWORD type = 0;
  std::string out;
  if (RegQueryValueExW(key, L"Installer Language", nullptr, &type,
                       reinterpret_cast<LPBYTE>(buf), &bytes) == ERROR_SUCCESS &&
      (type == REG_SZ || type == REG_EXPAND_SZ) && bytes > 0) {
    buf[bytes / sizeof(wchar_t)] = L'\0';
    out = language_from_installer_id(_wtoi(buf));
  }
  RegCloseKey(key);
  return out;
}

IVerge make_template() { return IVerge::make_template(); }
std::string dump_verge(const IVerge& v) { return serde::dump(v.to_yaml()); }
// ---------- load / save ----------
Result<IVerge> load() {
  auto node = serde::read_mapping_file(verge_path());
  if (!node.is_ok()) {
    // ← Mini: src-tauri/src/config/verge.rs:327-330（Self::new 的 Err 分支）
    //   读取/解析失败时记录 error 并回退 Self::template()，不把错误向上传播。
    //   旧实现返回 err，依赖调用方各自兜底（config.cpp 构造/init 确有兜底，
    //   但 prfitem.cpp:318 会静默丢端口），语义不等价。
    CWLOG_ERROR("verge.yaml load failed, fallback to template: " +
                node.error().message);
    return Result<IVerge>::ok(IVerge::make_template());
  }
  IVerge cfg = IVerge::from_yaml(node.value());

  // ← verge.rs::new 的迁移编排：缺失 config_version 视为 v1；
  // 备份成功才迁移，迁移后立即落盘；备份失败则中止（保持原样，下次重试）
  uint32_t from_version =
      cfg.config_version.value_or(consts::kImplicitLegacyVersion);
  if (from_version < consts::kCurrentVergeVersion) {
    auto backup = migrate::backup_before_migration(verge_path(), from_version);
    if (!backup) {
      CWLOG_WARN("verge.yaml migration backup failed, skip migration this run");
    } else {
      uint32_t migrated = migrate::migrate_verge(cfg, from_version);
      cfg.config_version = migrated;
      if (migrated != from_version) {
        auto sv = save_verge(cfg);
        if (!sv.is_ok())
          CWLOG_WARN("verge.yaml migration save failed: " + sv.error().message);
      }
    }
  }
  return Result<IVerge>::ok(std::move(cfg));
}





}  // namespace clashwin::config::verge

// ---------- IVerge 成员定义（clashwin::config） ----------
namespace clashwin::config {

using serde::opt_bool;
using serde::opt_i32;
using serde::opt_str;
using serde::opt_u16;
using serde::opt_u32;
using serde::opt_u64;
using serde::set_bool;
using serde::set_i64;
using serde::set_null;
using serde::set_str;
using serde::set_u16;
using serde::set_u32;

// ---------- 模板（← verge.rs::template() 逐字段） ----------
IVerge IVerge::make_template() {
  IVerge v;
  v.config_version = consts::kCurrentVergeVersion;
  v.app_log_max_size = consts::kAppLogMaxSize;
  v.app_log_max_count = consts::kAppLogMaxCount;
  v.clash_core = "win-mihomo";
  // 首启界面语言优先级（用户指令 2026-11-07）：
  //   安装器里选的语言 → 系统语言。安装器在开头弹语言选择，用户选中文就该中文，
  //   不该被 ja/其他系统区域盖过去。取不到安装器语言时与 Mini 出厂行为一致。
  const std::string from_installer = verge::installer_language();
  v.language = from_installer.empty() ? verge::system_language() : from_installer;
  v.theme_mode = "dark";
  v.env_type = "powershell";
  v.start_page = "/";
  v.traffic_graph = true;
  v.enable_memory_usage = true;
  v.enable_group_icon = true;
  v.pause_render_traffic_stats_on_blur = true;
  v.menu_icon = "monochrome";
  v.notice_position = "top-right";
  v.collapse_navbar = false;
  v.enable_auto_launch = false;
  v.enable_silent_start = false;
  v.enable_hover_jump_navigator = true;
  v.hover_jump_navigator_delay = consts::kHoverJumpNavigatorDelay;
  v.enable_system_proxy = true;
  v.proxy_auto_config = false;
  v.pac_file_content = consts::kDefaultPac;
  v.proxy_host = "127.0.0.1";
  v.verge_mixed_port = consts::kDefaultMixed;
  v.verge_socks_port = consts::kDefaultSocks;
  v.verge_socks_enabled = false;
  v.verge_port = consts::kDefaultHttp;
  v.verge_http_enabled = false;
  v.enable_proxy_guard = true;
  v.enable_bypass_check = true;
  v.use_default_bypass = true;
  v.proxy_guard_duration = consts::kProxyGuardDuration;
  v.auto_close_connection = false;
  v.auto_check_update = false;
  v.enable_builtin_enhanced = true;
  v.auto_log_clean = consts::kAutoLogClean;
  v.enable_always_on_top = false;
  v.enable_auto_light_weight_mode = true;
  v.auto_light_weight_minutes = consts::kAutoLightWeightMinutes;
  v.home_cards = std::nullopt;
  v.enable_external_controller = false;
  v.rule_fallback = "direct";
  return v;
}


// ---------- 序列化（字段顺序 = Rust 结构体声明顺序） ----------
namespace {

void emit_theme(const IVergeTheme& t, YAML::Node& map, const char* key) {
  YAML::Node m(YAML::NodeType::Map);
  t.primary_color ? set_str(m, "primary_color", *t.primary_color)
                  : set_null(m, "primary_color");
  t.secondary_color ? set_str(m, "secondary_color", *t.secondary_color)
                    : set_null(m, "secondary_color");
  t.primary_text ? set_str(m, "primary_text", *t.primary_text)
                 : set_null(m, "primary_text");
  t.secondary_text ? set_str(m, "secondary_text", *t.secondary_text)
                   : set_null(m, "secondary_text");
  t.info_color ? set_str(m, "info_color", *t.info_color)
               : set_null(m, "info_color");
  t.error_color ? set_str(m, "error_color", *t.error_color)
                : set_null(m, "error_color");
  t.warning_color ? set_str(m, "warning_color", *t.warning_color)
                  : set_null(m, "warning_color");
  t.success_color ? set_str(m, "success_color", *t.success_color)
                  : set_null(m, "success_color");
  t.font_family ? set_str(m, "font_family", *t.font_family)
                : set_null(m, "font_family");
  map[key] = m;
}

}  // namespace

YAML::Node IVerge::to_yaml() const {
  YAML::Node m(YAML::NodeType::Map);
  config_version ? set_u32(m, "config_version", *config_version)
                 : set_null(m, "config_version");
  app_log_level ? set_str(m, "app_log_level", *app_log_level)
                : set_null(m, "app_log_level");
  app_log_max_size ? serde::set_i64(m, "app_log_max_size",
                                    static_cast<int64_t>(*app_log_max_size))
                   : set_null(m, "app_log_max_size");
  app_log_max_count ? serde::set_i64(m, "app_log_max_count",
                                     static_cast<int64_t>(*app_log_max_count))
                    : set_null(m, "app_log_max_count");
  language ? set_str(m, "language", *language) : set_null(m, "language");
  theme_mode ? set_str(m, "theme_mode", *theme_mode) : set_null(m, "theme_mode");
  env_type ? set_str(m, "env_type", *env_type) : set_null(m, "env_type");
  start_page ? set_str(m, "start_page", *start_page) : set_null(m, "start_page");
  startup_script ? set_str(m, "startup_script", *startup_script)
                 : set_null(m, "startup_script");
  traffic_graph ? set_bool(m, "traffic_graph", *traffic_graph)
                : set_null(m, "traffic_graph");
  enable_memory_usage ? set_bool(m, "enable_memory_usage", *enable_memory_usage)
                      : set_null(m, "enable_memory_usage");
  // skip_serializing_if：None 省键
  if (enable_group_icon)
    set_bool(m, "enable_group_icon", *enable_group_icon);
  pause_render_traffic_stats_on_blur
      ? set_bool(m, "pause_render_traffic_stats_on_blur",
                 *pause_render_traffic_stats_on_blur)
      : set_null(m, "pause_render_traffic_stats_on_blur");
  if (menu_icon) set_str(m, "menu_icon", *menu_icon);
  if (menu_order) {
    YAML::Node seq(YAML::NodeType::Sequence);
    for (const auto& s : *menu_order) seq.push_back(s);
    m["menu_order"] = seq;
  }
  if (notice_position) set_str(m, "notice_position", *notice_position);
  collapse_navbar ? set_bool(m, "collapse_navbar", *collapse_navbar)
                  : set_null(m, "collapse_navbar");
  enable_tun_mode ? set_bool(m, "enable_tun_mode", *enable_tun_mode)
                  : set_null(m, "enable_tun_mode");
  enable_auto_launch ? set_bool(m, "enable_auto_launch", *enable_auto_launch)
                     : set_null(m, "enable_auto_launch");
  enable_silent_start ? set_bool(m, "enable_silent_start", *enable_silent_start)
                      : set_null(m, "enable_silent_start");
  enable_system_proxy
      ? set_bool(m, "enable_system_proxy", *enable_system_proxy)
      : set_null(m, "enable_system_proxy");
  enable_proxy_guard ? set_bool(m, "enable_proxy_guard", *enable_proxy_guard)
                     : set_null(m, "enable_proxy_guard");
  enable_bypass_check ? set_bool(m, "enable_bypass_check", *enable_bypass_check)
                      : set_null(m, "enable_bypass_check");
  use_default_bypass ? set_bool(m, "use_default_bypass", *use_default_bypass)
                     : set_null(m, "use_default_bypass");
  system_proxy_bypass ? set_str(m, "system_proxy_bypass", *system_proxy_bypass)
                      : set_null(m, "system_proxy_bypass");
  proxy_guard_duration
      ? serde::set_i64(m, "proxy_guard_duration",
                       static_cast<int64_t>(*proxy_guard_duration))
      : set_null(m, "proxy_guard_duration");
  proxy_auto_config ? set_bool(m, "proxy_auto_config", *proxy_auto_config)
                    : set_null(m, "proxy_auto_config");
  pac_file_content ? set_str(m, "pac_file_content", *pac_file_content)
                   : set_null(m, "pac_file_content");
  proxy_host ? set_str(m, "proxy_host", *proxy_host) : set_null(m, "proxy_host");
  if (theme_setting) {
    emit_theme(*theme_setting, m, "theme_setting");
  } else {
    set_null(m, "theme_setting");
  }
  if (web_ui_list) {
    YAML::Node seq(YAML::NodeType::Sequence);
    for (const auto& s : *web_ui_list) seq.push_back(s);
    m["web_ui_list"] = seq;
  } else {
    set_null(m, "web_ui_list");
  }
  if (clash_core) set_str(m, "clash_core", *clash_core);
  enable_always_on_top
      ? set_bool(m, "enable_always_on_top", *enable_always_on_top)
      : set_null(m, "enable_always_on_top");
  // home_cards: serde_json::Value → YAML（对象原样落）
  if (home_cards && home_cards->is_object()) {
    YAML::Node hc(YAML::NodeType::Map);
    for (auto it = home_cards->begin(); it != home_cards->end(); ++it) {
      const std::string& k = it.key();
      if (it.value().is_boolean()) hc[k] = it.value().get<bool>() ? "true" : "false";
      else if (it.value().is_string()) hc[k] = it.value().get<std::string>();
      else if (it.value().is_number_integer())
        hc[k] = std::to_string(it.value().get<int64_t>());
      else hc[k] = YAML::Node(YAML::NodeType::Null);
    }
    m["home_cards"] = hc;
  } else {
    set_null(m, "home_cards");
  }
  auto_close_connection
      ? set_bool(m, "auto_close_connection", *auto_close_connection)
      : set_null(m, "auto_close_connection");
  auto_check_update ? set_bool(m, "auto_check_update", *auto_check_update)
                    : set_null(m, "auto_check_update");
  default_latency_test
      ? set_str(m, "default_latency_test", *default_latency_test)
      : set_null(m, "default_latency_test");
  enable_auto_delay_detection
      ? set_bool(m, "enable_auto_delay_detection", *enable_auto_delay_detection)
      : set_null(m, "enable_auto_delay_detection");
  auto_delay_detection_interval_minutes
      ? serde::set_i64(m, "auto_delay_detection_interval_minutes",
                       static_cast<int64_t>(*auto_delay_detection_interval_minutes))
      : set_null(m, "auto_delay_detection_interval_minutes");
  enable_builtin_enhanced
      ? set_bool(m, "enable_builtin_enhanced", *enable_builtin_enhanced)
      : set_null(m, "enable_builtin_enhanced");
  proxy_layout_column
      ? set_u32(m, "proxy_layout_column", *proxy_layout_column)
      : set_null(m, "proxy_layout_column");
  if (test_list) {
    YAML::Node seq(YAML::NodeType::Sequence);
    for (const auto& t : *test_list) {
      YAML::Node ti(YAML::NodeType::Map);
      t.uid ? set_str(ti, "uid", *t.uid) : set_null(ti, "uid");
      t.name ? set_str(ti, "name", *t.name) : set_null(ti, "name");
      t.icon ? set_str(ti, "icon", *t.icon) : set_null(ti, "icon");
      t.url ? set_str(ti, "url", *t.url) : set_null(ti, "url");
      seq.push_back(ti);
    }
    m["test_list"] = seq;
  } else {
    set_null(m, "test_list");
  }
  auto_log_clean
      ? serde::set_i64(m, "auto_log_clean", static_cast<int64_t>(*auto_log_clean))
      : set_null(m, "auto_log_clean");
  verge_mixed_port ? set_u16(m, "verge_mixed_port", *verge_mixed_port)
                   : set_null(m, "verge_mixed_port");
  verge_socks_port ? set_u16(m, "verge_socks_port", *verge_socks_port)
                   : set_null(m, "verge_socks_port");
  verge_socks_enabled
      ? set_bool(m, "verge_socks_enabled", *verge_socks_enabled)
      : set_null(m, "verge_socks_enabled");
  verge_port ? set_u16(m, "verge_port", *verge_port) : set_null(m, "verge_port");
  verge_http_enabled
      ? set_bool(m, "verge_http_enabled", *verge_http_enabled)
      : set_null(m, "verge_http_enabled");
  enable_auto_light_weight_mode
      ? set_bool(m, "enable_auto_light_weight_mode",
                 *enable_auto_light_weight_mode)
      : set_null(m, "enable_auto_light_weight_mode");
  auto_light_weight_minutes
      ? serde::set_i64(m, "auto_light_weight_minutes",
                       static_cast<int64_t>(*auto_light_weight_minutes))
      : set_null(m, "auto_light_weight_minutes");
  enable_hover_jump_navigator
      ? set_bool(m, "enable_hover_jump_navigator", *enable_hover_jump_navigator)
      : set_null(m, "enable_hover_jump_navigator");
  hover_jump_navigator_delay
      ? serde::set_i64(m, "hover_jump_navigator_delay",
                       static_cast<int64_t>(*hover_jump_navigator_delay))
      : set_null(m, "hover_jump_navigator_delay");
  enable_external_controller
      ? set_bool(m, "enable_external_controller", *enable_external_controller)
      : set_null(m, "enable_external_controller");
  rule_fallback ? set_str(m, "rule_fallback", *rule_fallback)
                : set_null(m, "rule_fallback");
  return m;
}


// ---------- 反序列化 ----------
std::optional<IVergeTheme> IVergeTheme::from_yaml(const YAML::Node& m) {
  if (!m || !m.IsMap()) return std::nullopt;
  IVergeTheme t;
  t.primary_color = opt_str(m, "primary_color");
  t.secondary_color = opt_str(m, "secondary_color");
  t.primary_text = opt_str(m, "primary_text");
  t.secondary_text = opt_str(m, "secondary_text");
  t.info_color = opt_str(m, "info_color");
  t.error_color = opt_str(m, "error_color");
  t.warning_color = opt_str(m, "warning_color");
  t.success_color = opt_str(m, "success_color");
  t.font_family = opt_str(m, "font_family");
  return t;
}

YAML::Node IVergeTheme::to_yaml() const {
  YAML::Node m(YAML::NodeType::Map);
  primary_color ? set_str(m, "primary_color", *primary_color)
                : set_null(m, "primary_color");
  secondary_color ? set_str(m, "secondary_color", *secondary_color)
                  : set_null(m, "secondary_color");
  primary_text ? set_str(m, "primary_text", *primary_text)
               : set_null(m, "primary_text");
  secondary_text ? set_str(m, "secondary_text", *secondary_text)
                 : set_null(m, "secondary_text");
  info_color ? set_str(m, "info_color", *info_color) : set_null(m, "info_color");
  error_color ? set_str(m, "error_color", *error_color)
              : set_null(m, "error_color");
  warning_color ? set_str(m, "warning_color", *warning_color)
                : set_null(m, "warning_color");
  success_color ? set_str(m, "success_color", *success_color)
                : set_null(m, "success_color");
  font_family ? set_str(m, "font_family", *font_family)
              : set_null(m, "font_family");
  return m;
}

namespace {

std::optional<std::vector<std::string>> opt_str_seq(const YAML::Node& m,
                                                    const char* key) {
  const YAML::Node n = m[key];
  if (!n || n.IsNull()) return std::nullopt;
  if (!n.IsSequence()) return std::nullopt;
  std::vector<std::string> out;
  for (const auto& it : n) {
    if (!it.IsScalar()) return std::nullopt;
    out.push_back(it.Scalar());
  }
  return out;
}

}  // namespace

IVerge IVerge::from_yaml(const YAML::Node& m) {
  IVerge v;
  if (!m.IsMap()) return v;
  v.config_version = opt_u32(m, "config_version");
  v.app_log_level = opt_str(m, "app_log_level");
  v.app_log_max_size = opt_u64(m, "app_log_max_size");
  v.app_log_max_count = opt_u64(m, "app_log_max_count");
  v.language = opt_str(m, "language");
  v.theme_mode = opt_str(m, "theme_mode");
  v.env_type = opt_str(m, "env_type");
  v.start_page = opt_str(m, "start_page");
  v.startup_script = opt_str(m, "startup_script");
  v.traffic_graph = opt_bool(m, "traffic_graph");
  v.enable_memory_usage = opt_bool(m, "enable_memory_usage");
  v.enable_group_icon = opt_bool(m, "enable_group_icon");
  v.pause_render_traffic_stats_on_blur =
      opt_bool(m, "pause_render_traffic_stats_on_blur");
  v.menu_icon = opt_str(m, "menu_icon");
  v.menu_order = opt_str_seq(m, "menu_order");
  v.notice_position = opt_str(m, "notice_position");
  v.collapse_navbar = opt_bool(m, "collapse_navbar");
  v.enable_tun_mode = opt_bool(m, "enable_tun_mode");
  v.enable_auto_launch = opt_bool(m, "enable_auto_launch");
  v.enable_silent_start = opt_bool(m, "enable_silent_start");
  v.enable_system_proxy = opt_bool(m, "enable_system_proxy");
  v.enable_proxy_guard = opt_bool(m, "enable_proxy_guard");
  v.enable_bypass_check = opt_bool(m, "enable_bypass_check");
  v.use_default_bypass = opt_bool(m, "use_default_bypass");
  v.system_proxy_bypass = opt_str(m, "system_proxy_bypass");
  v.proxy_guard_duration = opt_u64(m, "proxy_guard_duration");
  v.proxy_auto_config = opt_bool(m, "proxy_auto_config");
  v.pac_file_content = opt_str(m, "pac_file_content");
  v.proxy_host = opt_str(m, "proxy_host");
  v.theme_setting = IVergeTheme::from_yaml(m["theme_setting"]);
  v.web_ui_list = opt_str_seq(m, "web_ui_list");
  v.clash_core = opt_str(m, "clash_core");
  v.enable_always_on_top = opt_bool(m, "enable_always_on_top");
  {
    const YAML::Node hc = m["home_cards"];
    if (hc && hc.IsMap()) {
      nlohmann::json obj = nlohmann::json::object();
      for (const auto& kv : hc) {
        if (!kv.first.IsScalar()) continue;
        const std::string k = kv.first.Scalar();
        if (kv.second.IsNull()) obj[k] = nullptr;
        else if (kv.second.IsScalar()) {
          const std::string& s = kv.second.Scalar();
          if (s == "true") obj[k] = true;
          else if (s == "false") obj[k] = false;
          else obj[k] = s;
        }
      }
      v.home_cards = std::move(obj);
    }
  }
  v.auto_close_connection = opt_bool(m, "auto_close_connection");
  v.auto_check_update = opt_bool(m, "auto_check_update");
  v.default_latency_test = opt_str(m, "default_latency_test");
  v.enable_auto_delay_detection = opt_bool(m, "enable_auto_delay_detection");
  v.auto_delay_detection_interval_minutes =
      opt_u64(m, "auto_delay_detection_interval_minutes");
  v.enable_builtin_enhanced = opt_bool(m, "enable_builtin_enhanced");
  v.proxy_layout_column = opt_u32(m, "proxy_layout_column");
  {
    const YAML::Node tl = m["test_list"];
    if (tl && tl.IsSequence()) {
      std::vector<IVergeTestItem> items;
      for (const auto& it : tl) {
        if (!it.IsMap()) continue;
        IVergeTestItem ti;
        ti.uid = opt_str(it, "uid");
        ti.name = opt_str(it, "name");
        ti.icon = opt_str(it, "icon");
        ti.url = opt_str(it, "url");
        items.push_back(std::move(ti));
      }
      v.test_list = std::move(items);
    }
  }
  v.auto_log_clean = opt_i32(m, "auto_log_clean");
  v.verge_mixed_port = opt_u16(m, "verge_mixed_port");
  v.verge_socks_port = opt_u16(m, "verge_socks_port");
  v.verge_socks_enabled = opt_bool(m, "verge_socks_enabled");
  v.verge_port = opt_u16(m, "verge_port");
  v.verge_http_enabled = opt_bool(m, "verge_http_enabled");
  v.enable_auto_light_weight_mode = opt_bool(m, "enable_auto_light_weight_mode");
  v.auto_light_weight_minutes = opt_u64(m, "auto_light_weight_minutes");
  v.enable_hover_jump_navigator = opt_bool(m, "enable_hover_jump_navigator");
  v.hover_jump_navigator_delay = opt_u64(m, "hover_jump_navigator_delay");
  v.enable_external_controller = opt_bool(m, "enable_external_controller");
  v.rule_fallback = opt_str(m, "rule_fallback");
  return v;
}

// ---------- patch（← verge.rs::patch_config 逐字段 is_some 才覆盖） ----------
void IVerge::patch_config(const IVerge& p) {
  if (p.app_log_level) app_log_level = p.app_log_level;
  if (p.app_log_max_size) app_log_max_size = p.app_log_max_size;
  if (p.app_log_max_count) app_log_max_count = p.app_log_max_count;
  if (p.language) language = p.language;
  if (p.theme_mode) theme_mode = p.theme_mode;
  if (p.env_type) env_type = p.env_type;
  if (p.start_page) start_page = p.start_page;
  if (p.startup_script) startup_script = p.startup_script;
  if (p.traffic_graph) traffic_graph = p.traffic_graph;
  if (p.enable_memory_usage) enable_memory_usage = p.enable_memory_usage;
  if (p.enable_group_icon) enable_group_icon = p.enable_group_icon;
  if (p.pause_render_traffic_stats_on_blur)
    pause_render_traffic_stats_on_blur = p.pause_render_traffic_stats_on_blur;
  if (p.menu_icon) menu_icon = p.menu_icon;
  if (p.menu_order) menu_order = p.menu_order;
  if (p.notice_position) notice_position = p.notice_position;
  if (p.collapse_navbar) collapse_navbar = p.collapse_navbar;
  if (p.enable_tun_mode) enable_tun_mode = p.enable_tun_mode;
  if (p.enable_auto_launch) enable_auto_launch = p.enable_auto_launch;
  if (p.enable_silent_start) enable_silent_start = p.enable_silent_start;
  if (p.enable_hover_jump_navigator)
    enable_hover_jump_navigator = p.enable_hover_jump_navigator;
  if (p.hover_jump_navigator_delay)
    hover_jump_navigator_delay = p.hover_jump_navigator_delay;
  if (p.verge_mixed_port) verge_mixed_port = p.verge_mixed_port;
  if (p.verge_socks_port) verge_socks_port = p.verge_socks_port;
  if (p.verge_socks_enabled) verge_socks_enabled = p.verge_socks_enabled;
  if (p.verge_port) verge_port = p.verge_port;
  if (p.verge_http_enabled) verge_http_enabled = p.verge_http_enabled;
  if (p.enable_system_proxy) enable_system_proxy = p.enable_system_proxy;
  if (p.enable_proxy_guard) enable_proxy_guard = p.enable_proxy_guard;
  if (p.enable_bypass_check) enable_bypass_check = p.enable_bypass_check;
  if (p.use_default_bypass) use_default_bypass = p.use_default_bypass;
  if (p.system_proxy_bypass) system_proxy_bypass = p.system_proxy_bypass;
  if (p.proxy_guard_duration) proxy_guard_duration = p.proxy_guard_duration;
  if (p.proxy_auto_config) proxy_auto_config = p.proxy_auto_config;
  if (p.pac_file_content) pac_file_content = p.pac_file_content;
  if (p.proxy_host) proxy_host = p.proxy_host;
  if (p.theme_setting) theme_setting = p.theme_setting;
  if (p.web_ui_list) web_ui_list = p.web_ui_list;
  if (p.clash_core) clash_core = p.clash_core;
  if (p.enable_always_on_top) enable_always_on_top = p.enable_always_on_top;
  if (p.auto_close_connection) auto_close_connection = p.auto_close_connection;
  if (p.auto_check_update) auto_check_update = p.auto_check_update;
  if (p.default_latency_test) default_latency_test = p.default_latency_test;
  if (p.enable_auto_delay_detection)
    enable_auto_delay_detection = p.enable_auto_delay_detection;
  if (p.auto_delay_detection_interval_minutes)
    auto_delay_detection_interval_minutes =
        p.auto_delay_detection_interval_minutes;
  if (p.enable_builtin_enhanced) enable_builtin_enhanced = p.enable_builtin_enhanced;
  if (p.proxy_layout_column) proxy_layout_column = p.proxy_layout_column;
  if (p.test_list) test_list = p.test_list;
  if (p.auto_log_clean) auto_log_clean = p.auto_log_clean;
  if (p.enable_auto_light_weight_mode)
    enable_auto_light_weight_mode = p.enable_auto_light_weight_mode;
  if (p.auto_light_weight_minutes)
    auto_light_weight_minutes = p.auto_light_weight_minutes;
  if (p.home_cards) home_cards = p.home_cards;
  if (p.enable_external_controller)
    enable_external_controller = p.enable_external_controller;
  if (p.rule_fallback) rule_fallback = p.rule_fallback;
}

std::string IVerge::get_valid_clash_core() const {
  // ← verge.rs::get_valid_clash_core
  if (clash_core) {
    std::string s = util::trim(*clash_core);
    for (const char* valid : kValidClashCores) {
      if (s == valid) return s;
    }
  }
  return "win-mihomo";
}

namespace {

struct TempTunFallbackSnapshot {
  bool active = false;
  std::optional<bool> enable_tun_mode;
  std::optional<bool> enable_system_proxy;
};

std::mutex& temp_tun_fallback_mutex() {
  static std::mutex mu;
  return mu;
}

TempTunFallbackSnapshot& temp_tun_fallback_state() {
  static TempTunFallbackSnapshot state;
  return state;
}

}  // namespace

void IVerge::enter_temp_tun_fallback(std::optional<bool> persisted_tun,
                                     std::optional<bool> persisted_sysproxy) {
  std::lock_guard<std::mutex> lk(temp_tun_fallback_mutex());
  auto& s = temp_tun_fallback_state();
  s.active = true;
  s.enable_tun_mode = persisted_tun;
  s.enable_system_proxy = persisted_sysproxy;
}

void IVerge::clear_temp_tun_fallback() {
  std::lock_guard<std::mutex> lk(temp_tun_fallback_mutex());
  auto& s = temp_tun_fallback_state();
  s.active = false;
  s.enable_tun_mode.reset();
  s.enable_system_proxy.reset();
}

bool IVerge::has_temp_tun_fallback() {
  std::lock_guard<std::mutex> lk(temp_tun_fallback_mutex());
  return temp_tun_fallback_state().active;
}

IVerge IVerge::effective_for_disk() const {
  std::lock_guard<std::mutex> lk(temp_tun_fallback_mutex());
  const auto& s = temp_tun_fallback_state();
  if (!s.active) return *this;
  IVerge copy = *this;
  copy.enable_tun_mode = s.enable_tun_mode;
  copy.enable_system_proxy = s.enable_system_proxy;
  return copy;
}

Result<void> IVerge::save_file() const { return verge::save_verge(*this); }

}  // namespace clashwin::config

// ---------- verge 命名空间自由函数 ----------
namespace clashwin::config::verge {

Result<void> save_verge(const IVerge& v) {
  const IVerge disk_cfg = v.effective_for_disk();
  return serde::save_yaml_text(verge_path(), dump_verge(disk_cfg),
                               "# Clash Verge Config");
}

}  // namespace clashwin::config::verge
