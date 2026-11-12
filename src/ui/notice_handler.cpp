// 通知状态映射实现（← pages/_layout/utils/notification-handlers.ts 全表）
#include "ui/notice_handler.h"

#include <algorithm>
#include <mutex>
#include <optional>
#include <vector>

#include "events/event_bus.h"
#include "events/event_names.h"
#include "events/notice.h"
#include "i18n/i18n.h"
#include "ui/i18n_text.h"
#include "ui/scenes/node_list.h"
#include "util/log.h"

namespace clashwin::ui {

namespace {

// 一条映射规则。key == nullptr → 直接显示消息原文（Mini 里就是 showNotice.error(msg)）。
// param != nullptr → 消息作为该插值参数（Mini: showNotice.x(key, { param: msg })）。
// 否则消息按 prefixedRaw 语义追加在译文之后。
struct NoticeRule {
  const char* status;
  const char* type;    // success | info | warning | error
  const char* key;     // nullptr → 原文
  const char* param;   // 插值参数名
  int duration;        // 0 → 用 NoticeService 按类型的缺省值
  bool prefer_raw_when_nonempty = false;  // true：msg 非空时直接显示完整句，为空时回退译文
};

// ---------- ← notification-handlers.ts:12-88 的 status 表 + 后端/组件补充 ----------
constexpr NoticeRule kRules[] = {
    // 订阅导入
    {"import_sub_url::ok", "success",
     "shared.feedback.notifications.importSubscriptionSuccess", nullptr, 0},
    {"import_sub_url::error", "error", nullptr, nullptr, 0},

    // 配置保存/切换
    {"set_config::error", "error", nullptr, nullptr, 0},
    {"reactivate_profiles::error", "error", nullptr, nullptr, 0},
    {"update_failed", "error", nullptr, nullptr, 0},

    // 更新器（走代理成功/失败）
    {"update_with_clash_proxy", "success",
     "settings.feedback.notifications.updater.withClashProxySuccess", nullptr, 0},
    {"update_failed_even_with_clash", "error",
     "settings.feedback.notifications.updater.withClashProxyFailed", nullptr, 0},

    // 校验：整体
    {"config_validate::boot_error", "error", "shared.feedback.validation.config.bootFailed",
     nullptr, 0},
    {"config_validate::timeout", "error", "shared.feedback.validation.config.bootFailed",
     nullptr, 0},
    {"config_validate::core_change", "error",
     "shared.feedback.validation.config.coreChangeFailed", nullptr, 0},
    {"config_validate::error", "error", "shared.feedback.validation.config.failed", nullptr, 0},
    {"config_validate::process_terminated", "error",
     "shared.feedback.validation.config.processTerminated", nullptr, 0},
    {"config_validate::stdout_error", "error", "shared.feedback.validation.config.failed",
     nullptr, 0},

    // 校验：脚本
    {"config_validate::script_error", "error", "shared.feedback.validation.script.fileError",
     nullptr, 0},
    {"config_validate::script_syntax_error", "error",
     "shared.feedback.validation.script.syntaxError", nullptr, 0},
    {"config_validate::script_missing_main", "error",
     "shared.feedback.validation.script.missingMain", nullptr, 0},
    {"config_validate::file_not_found", "error",
     "shared.feedback.validation.script.fileNotFound", nullptr, 0},

    // 校验：YAML
    {"config_validate::yaml_syntax_error", "error",
     "shared.feedback.validation.yaml.syntaxError", nullptr, 0},
    {"config_validate::yaml_read_error", "error", "shared.feedback.validation.yaml.readError",
     nullptr, 0},
    {"config_validate::yaml_mapping_error", "error",
     "shared.feedback.validation.yaml.mappingError", nullptr, 0},
    {"config_validate::yaml_key_error", "error", "shared.feedback.validation.yaml.keyError",
     nullptr, 0},
    {"config_validate::yaml_error", "error", "shared.feedback.validation.yaml.generalError",
     nullptr, 0},

    // 校验：Merge 覆写
    {"config_validate::merge_syntax_error", "error",
     "shared.feedback.validation.merge.syntaxError", nullptr, 0},
    {"config_validate::merge_mapping_error", "error",
     "shared.feedback.validation.merge.mappingError", nullptr, 0},
    {"config_validate::merge_key_error", "error", "shared.feedback.validation.merge.keyError",
     nullptr, 0},
    {"config_validate::merge_error", "error", "shared.feedback.validation.merge.generalError",
     nullptr, 0},

    // 内核切换
    {"config_core::change_success", "success",
     "settings.feedback.notifications.clash.changeSuccess", nullptr, 0},
    {"config_core::change_error", "error",
     "settings.feedback.notifications.clash.changeFailed", nullptr, 0},

    // ---------- Win 侧状态串（Mini 对应点为组件内直接 showNotice） ----------
    // ← hooks/use-service-installer.ts:19-44：info 安装中 → success/failed
    {"install_service::info", "info", "settings.statuses.clashService.installing", nullptr, 0,
     true},
    {"install_service::ok", "success",
     "settings.feedback.notifications.clashService.installSuccess", nullptr, 0, true},
    {"install_service::error", "error", nullptr, nullptr, 0},
    {"reinstall_service::ok", "success",
     "settings.feedback.notifications.clashService.installSuccess", nullptr, 0, true},
    {"reinstall_service::error", "error", nullptr, nullptr, 0},
    // ← core/manager/lifecycle.rs:237 Handle::notice_message("warning", ...)
    {"warning", "warning", nullptr, nullptr, 0},
    // BUG-342: 快速路由规则置顶生效提示（← src/utils/quick-routing.ts:106）
    {"quick_rule_added", "success",
     "profiles.page.feedback.notifications.quickRuleAdded", "rule", 2500},  // src: src/utils/quick-routing.ts:106
    // src: src/utils/quick-routing.ts:106 兼容历史 status 串
    {"profile_switched", "success",
     "profiles.page.feedback.notifications.quickRuleAdded", "rule", 2500},  // src: src/utils/quick-routing.ts:106
    // ← _layout.tsx:929-932：切换订阅配置成功提示
    {"profile_switched::ok", "success",
     "profiles.page.feedback.notifications.profileSwitched", nullptr, 2500},  // src: src/pages/_layout.tsx:930
    // ← _layout.tsx:938-952：更新订阅 info → success
    {"update_subscription::info", "info", "layout.notices.updatingSubscription", nullptr, 0},
    {"update_subscription::ok", "success", "layout.notices.subscriptionUpdated", nullptr, 0},
    // ← _layout.tsx:954-963：删除配置成功
    {"profile_deleted::ok", "success",
     "shared.feedback.notifications.common.deleteSuccess", nullptr, 0},
    // ← components/connection/connection-table.tsx:301：success(key, {address}, 2000)
    {"copied", "success", "connections.notices.copied", "address", 2000},
    // ← services/cmds.ts:84-110 withIpcTimeout：原文 `[label] IPC call timed out after Nms`
    {"ipc_timeout", "error", nullptr, nullptr, 0},
    // 服务管控通知（← CoreManager start_service_locked / notify）
    {"service_unavailable", "error", nullptr, nullptr, 0},
    {"service_missing", "info", nullptr, nullptr, 0},
    {"service_install_error", "error", nullptr, nullptr, 0},
    {"service_start_error", "error", nullptr, nullptr, 0},
};

const NoticeRule* find_rule(const char* status) {
  if (!status) return nullptr;
  for (const auto& r : kRules) {
    if (std::string(r.status) == status) return &r;
  }
  return nullptr;
}

uint64_t g_sub_id = 0;

struct HealthSub {
  uint64_t id;
  std::function<void(NetworkHealthIssue)> cb;
};
std::mutex g_health_mu;
NetworkHealthIssue g_latest_health = NetworkHealthIssue::None;
uint64_t g_next_health_id = 1;
std::vector<HealthSub> g_health_subs;

bool try_consume_network_health(const char* status, const std::string& message) {
  const std::string s = status ? status : "";
  constexpr const char* kPrefix = "network_health::";
  std::string token;
  if (message.rfind(kPrefix, 0) == 0) {
    token = message;
  } else if (s.rfind(kPrefix, 0) == 0) {
    token = s;
  } else {
    return false;
  }
  if (token == "network_health::ok") {
    set_latest_network_health(NetworkHealthIssue::None);
  } else if (token == "network_health::offline") {
    set_latest_network_health(NetworkHealthIssue::Offline);
  } else if (token == "network_health::all_dead") {
    set_latest_network_health(NetworkHealthIssue::AllDead);
  }
  return true;
}

}  // namespace

NetworkHealthIssue latest_network_health() {
  std::lock_guard<std::mutex> lk(g_health_mu);
  return g_latest_health;
}

void set_latest_network_health(NetworkHealthIssue issue) {
  std::vector<std::function<void(NetworkHealthIssue)>> cbs;
  {
    std::lock_guard<std::mutex> lk(g_health_mu);
    g_latest_health = issue;
    cbs.reserve(g_health_subs.size());
    for (const auto& sub : g_health_subs) {
      if (sub.cb) cbs.push_back(sub.cb);
    }
  }
  for (const auto& cb : cbs) cb(issue);
}

uint64_t subscribe_network_health(std::function<void(NetworkHealthIssue)> cb) {
  std::lock_guard<std::mutex> lk(g_health_mu);
  const uint64_t id = g_next_health_id++;
  g_health_subs.push_back({id, std::move(cb)});
  return id;
}

void unsubscribe_network_health(uint64_t id) {
  std::lock_guard<std::mutex> lk(g_health_mu);
  g_health_subs.erase(
      std::remove_if(g_health_subs.begin(), g_health_subs.end(),
                     [id](const HealthSub& s) { return s.id == id; }),
      g_health_subs.end());
}

bool dispatch_notice(const char* status, const std::string& message) {
  // BUG-359: 优先拦截 network_health::* 内部状态广播，绝不弹底部 Toast
  if (try_consume_network_health(status, message)) {
    return true;
  }

  const NoticeRule* rule = find_rule(status);
  if (!rule) {
    // ← notification-handlers.ts:93 console.warn(`未处理的通知状态: ${status}`)
    CWLOG_WARN(std::string("未处理的通知状态: ") + (status ? status : "(null)"));
    return false;
  }

  ensure_i18n();
  auto& i18n = clashwin::i18n::I18n::instance();
  std::string msg_arg = message;
  // BUG-342: 若后端 quick_routing.cpp 传入带中文前缀的串，剥离前缀仅保留规则体插值 {{rule}}
  constexpr const char* kLegacyQuickPrefix = "手动路径控制规则已置顶生效: ";
  if (status && std::string(status) == "profile_switched" &&
      msg_arg.rfind(kLegacyQuickPrefix, 0) == 0) {
    msg_arg = msg_arg.substr(std::char_traits<char>::length(kLegacyQuickPrefix));
  }

  std::string text;
  if (!rule->key || (rule->prefer_raw_when_nonempty && !msg_arg.empty())) {
    text = msg_arg;  // 原文（或完整状态句不重复拼接译文前缀）
  } else if (rule->param && !msg_arg.empty()) {
    text = i18n.t(rule->key, {{rule->param, msg_arg}});
    if (text == rule->key) text = message;
  } else {
    text = i18n.t(rule->key);
    // prefixedRaw：译文 + 空格 + 明细
    if (!msg_arg.empty()) text += " " + msg_arg;
  }
  if (text.empty()) return true;  // 空文案不弹（避免空白 Alert）

  auto& notices = events::NoticeService::instance();
  if (rule->duration > 0) {
    notices.push(rule->type, text, rule->duration);
  } else {
    notices.push(rule->type, text);
  }
  return true;
}

void install_notice_handler() {
  if (g_sub_id != 0) return;  // 幂等
  g_sub_id = events::EventBus::instance().subscribe(
      events::names::kNoticeMessage, [](const events::Payload& payload) {
        if (!payload.is_object()) return;
        const std::string status = payload.value("status", "");
        const std::string msg = payload.value("message", "");
        dispatch_notice(status.c_str(), msg);
      });
}

void uninstall_notice_handler() {
  if (g_sub_id == 0) return;
  events::EventBus::instance().unsubscribe(g_sub_id);
  g_sub_id = 0;
}

std::string resolve_verge_patch_success_key(const std::string& k,
                                            const nlohmann::json& v) {
  if (k == "rule_fallback" && v.is_string()) {
    const std::string fb = v.get<std::string>();
    if (fb == "proxy") return "layout.notices.switchedToGlobalProxy";
    if (fb == "direct") return "layout.notices.switchedToGfwList";
    return {};  // "addurl" 会立即打开添加网址弹窗，提交后再弹规则变更结果
  }
  if (k == "theme_mode" && v.is_string()) {
    const std::string m = v.get<std::string>();
    if (m == "light") return "layout.notices.themeSwitchedLight";
    if (m == "dark") return "layout.notices.themeSwitchedDark";
    return "layout.notices.themeSwitchedSystem";
  }
  if (k == "clash_mini_control_skin" && v.is_string()) {
    return "layout.notices.skinSwitched";
  }
  if (k == "language" && v.is_string()) {
    return "layout.notices.languageSwitched";
  }
  const bool on = v.is_boolean() && v.get<bool>();
  if (k == "enable_auto_launch") {
    return on ? "layout.notices.autoLaunchEnabled"
              : "layout.notices.autoLaunchDisabled";
  }
  if (k == "enable_silent_start") {
    return on ? "layout.notices.silentStartEnabled"
              : "layout.notices.silentStartDisabled";
  }
  if (k == "clash_allow_lan") {
    return on ? "layout.notices.allowLanEnabled"
              : "layout.notices.allowLanDisabled";
  }
  if (k == "clash_ipv6") {
    return on ? "layout.notices.ipv6Enabled" : "layout.notices.ipv6Disabled";
  }
  return {};
}

std::string resolve_verge_patch_success_message(const std::string& k,
                                                const nlohmann::json& v) {
  const std::string key = resolve_verge_patch_success_key(k, v);
  if (key.empty()) return {};
  if (k == "clash_mini_control_skin" && v.is_string()) {
    const std::string raw = v.get<std::string>();
    std::string display = raw;
    if (raw == "retro-3d") display = "Trump-3D";
    else if (raw == "original") display = "Original";
    else if (raw == "modern-flat") display = "Modern";
    else if (raw == "frosted-glass") display = "Frosted";
    else if (raw == "cyberpunk") display = "Cyberpunk";
    else if (raw == "monochrome") display = "Monochrome";
    return tr8(key.c_str(), {{"name", display}});
  }
  return tr8(key.c_str());
}

void dispatch_node_switch_notice(bool ok, bool changed,
                                 const std::string& node_name,
                                 const std::string& err_detail) {
  auto& notices = events::NoticeService::instance();
  if (ok) {
    if (!changed || node_name.empty()) return;
    // 豁免: Mini 无对应物 — 用户特约补齐节点切换成功提示 2000ms（出处: 无）
    notices.push(
        "success",
        tr8("layout.notices.nodeSwitched",
            {{"name", scene::clean_node_display_name(node_name)}}),
        2000);
  } else {
    notices.push(
        "error",
        tr8("layout.notices.nodeSwitchFailed",
            {{"message", err_detail.empty() ? "Mihomo API error" : err_detail}}));
  }
}

void dispatch_single_delay_notice(const std::string& node_name, int delay_ms) {
  if (node_name.empty()) return;
  auto& notices = events::NoticeService::instance();
  const std::string clean = scene::clean_node_display_name(node_name);
  if (delay_ms > 0 && delay_ms < 10000) {
    // 豁免: Mini 无对应物 — 用户特约补齐单节点测速结果提示 2000ms（出处: 无）
    notices.push("success",
                 tr8("layout.notices.nodeDelayResult",
                     {{"name", clean}, {"delay", std::to_string(delay_ms)}}),
                 2000);
  } else {
    // 豁免: Mini 无对应物 — 用户特约补齐单节点测速超时提示 2500ms（出处: 无）
    notices.push("warning",
                 tr8("layout.notices.nodeDelayTimeout", {{"name", clean}}),
                 2500);
  }
}

uint64_t begin_batch_test_notice() {
  return events::NoticeService::instance().push(
      "info", tr8("layout.notices.batchTestingNodes"), 0);
}

void finish_batch_test_notice(uint64_t wait_id,
                              const scene::BatchTestSummary& summary) {
  auto& notices = events::NoticeService::instance();
  if (wait_id) notices.hide(wait_id);
  if (summary.busy) {
    notices.push("info", tr8("settings.mini.testingInProgress"));
    return;
  }
  if (summary.selected && !summary.selected_node.empty()) {
    // 豁免: Mini 无对应物 — 用户特约补齐批量测速择优结果提示 2500ms（出处: 无）
    notices.push(
        "success",
        tr8("layout.notices.batchTestSelected",
            {{"name", scene::clean_node_display_name(summary.selected_node)},
             {"delay", std::to_string(summary.selected_delay_ms)}}),
        2500);
    return;
  }
  if (summary.executed) {
    notices.push("warning", tr8("layout.notices.batchTestAllTimeout"));
  } else {
    notices.push("error", tr8("settings.mini.testNotExecuted"));
  }
}

void dispatch_sort_mode_notice(int sort_type) {
  const char* tip_key = "proxies.page.tooltips.sortDefault";
  if (sort_type == 1) tip_key = "proxies.page.tooltips.sortDelay";
  else if (sort_type == 2) tip_key = "proxies.page.tooltips.sortName";
  // 豁免: Mini 无对应物 — 用户特约补齐节点排序切换提示 1500ms（出处: 无）
  events::NoticeService::instance().push(
      "info",
      tr8("layout.notices.sortChanged", {{"mode", tr8(tip_key)}}),
      1500);
}

void dispatch_pin_window_notice(bool pinned) {
  // 豁免: Mini 无对应物 — 用户特约补齐窗口置顶切换提示 2000ms（出处: 无）
  events::NoticeService::instance().push(
      "success",
      tr8(pinned ? "layout.notices.windowPinned"
                 : "layout.notices.windowUnpinned"),
      2000);
}

void dispatch_close_all_connections_notice(bool ok,
                                           const std::string& err_detail) {
  if (ok) {
    // 豁免: Mini 无对应物 — 用户特约补齐断开全部活动连接结果提示 2000ms（出处: 无）
    events::NoticeService::instance().push(
        "success", tr8("layout.notices.allConnectionsClosed"), 2000);
  } else {
    events::NoticeService::instance().push(
        "error", err_detail.empty() ? "Failed to close connections" : err_detail);
  }
}

}  // namespace clashwin::ui
