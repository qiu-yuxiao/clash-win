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
    // BUG-342: 快速路由规则置顶生效提示（← src/utils/quick-routing.ts:382 / :432）
    {"quick_rule_added", "success",
     "profiles.page.feedback.notifications.quickRuleAdded", "rule", 2500},  // src: src/utils/quick-routing.ts:382
    {"quick_rule_removed", "success",
     "profiles.page.feedback.notifications.quickRuleRemoved", "rule", 2500},  // src: src/utils/quick-routing.ts:432
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

// 严格对照 Clash Mini v3.5.6 上游真实存在的操作结果提示（_layout.tsx / basic-settings-card.tsx / use-minimal-settings.ts）
std::string resolve_verge_patch_success_key(const std::string& k,
                                            const nlohmann::json& v) {
  if (k == "rule_fallback" && v.is_string()) {
    const std::string s = v.get<std::string>();
    if (s == "proxy") return "layout.notices.switchedToGlobalProxy";  // _layout.tsx:419
    if (s == "direct") return "layout.notices.switchedToGfwlist";     // _layout.tsx:422
    if (s == "addurl") return "layout.notices.switchedToAddUrl";      // _layout.tsx:424
  }
  if ((k == "enable_silent_start" || k == "clash_allow_lan" ||
       k == "clash_ipv6") &&
      v.is_boolean()) {
    return v.get<bool>() ? "settings.mini.settingEnabled"
                         : "settings.mini.settingDisabled";  // basic-settings-card.tsx:266,301,341
  }
  if (k == "enable_tun_sniffer") {
    return "settings.mini.tunSnifferUpdated";  // basic-settings-card.tsx:408
  }
  return {};
}

std::string resolve_verge_patch_success_message(const std::string& k,
                                                const nlohmann::json& v) {
  const std::string key = resolve_verge_patch_success_key(k, v);
  return key.empty() ? std::string() : tr8(key.c_str());
}

uint64_t begin_batch_test_notice() {
  // Mini 无常驻"测速中"提示（仅 spinner + busy 提示），故不再 push 自编常驻条。
  return 0;
}

void finish_batch_test_notice(uint64_t wait_id,
                              const scene::BatchTestSummary& summary) {
  auto& notices = events::NoticeService::instance();
  if (wait_id) notices.hide(wait_id);
  if (summary.busy) {
    notices.push("info", tr8("settings.mini.testingInProgress"));
    return;
  }
  if (!summary.executed) {
    notices.push("error", tr8("settings.mini.testNotExecuted"));
  }
  // 择优成功/全部超时：Mini 不弹结果提示，保持静默。
}

void dispatch_close_all_connections_notice(bool ok,
                                           const std::string& err_detail) {
  if (!ok) {
    events::NoticeService::instance().push(
        "error", err_detail.empty() ? "Failed to close connections" : err_detail);
  }
}

}  // namespace clashwin::ui
