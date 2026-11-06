// 通知状态映射实现（← pages/_layout/utils/notification-handlers.ts 全表）
#include "ui/notice_handler.h"

#include <optional>
#include <vector>

#include "events/event_bus.h"
#include "events/event_names.h"
#include "events/notice.h"
#include "i18n/i18n.h"
#include "ui/i18n_text.h"
#include "util/log.h"

namespace clashwin::ui {

namespace {

// 一条映射规则。key == nullptr → 直接显示消息原文（Mini 里就是 showNotice.error(msg)）。
// param != nullptr → 消息作为该插值参数（Mini: showNotice.x(key, { param: msg })）。
// 否则消息按 prefixedRaw 语义追加在译文之后。
struct NoticeRule {
  const char* status;
  const char* type;    // success | info | error
  const char* key;     // nullptr → 原文
  const char* param;   // 插值参数名
  int duration;        // 0 → 用 NoticeService 按类型的缺省值
};

// ---------- ← notification-handlers.ts:12-88 的 27 条 status 表 ----------
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
    // ← hooks/use-service-installer.ts:19-46：info 安装中 → success/failed
    {"install_service::info", "info", "settings.statuses.clashService.installing", nullptr, 0},
    {"install_service::ok", "success",
     "settings.feedback.notifications.clashService.installSuccess", nullptr, 0},
    {"install_service::error", "error", nullptr, nullptr, 0},
    // ← utils/quick-routing.ts:105-109：success(key, raw, 2500)
    {"profile_switched", "success",
     "profiles.page.feedback.notifications.profileSwitched", nullptr, 2500},
    // ← _layout.tsx:929-932：success(key, 1000)（切换配置，无明细）
    {"profile_switched::ok", "success",
     "profiles.page.feedback.notifications.profileSwitched", nullptr, 1000},
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
};

const NoticeRule* find_rule(const char* status) {
  if (!status) return nullptr;
  for (const auto& r : kRules) {
    if (std::string(r.status) == status) return &r;
  }
  return nullptr;
}

uint64_t g_sub_id = 0;

}  // namespace

bool dispatch_notice(const char* status, const std::string& message) {
  const NoticeRule* rule = find_rule(status);
  if (!rule) {
    // ← notification-handlers.ts:93 console.warn(`未处理的通知状态: ${status}`)
    CWLOG_WARN(std::string("未处理的通知状态: ") + (status ? status : "(null)"));
    return false;
  }

  ensure_i18n();
  auto& i18n = clashwin::i18n::I18n::instance();
  std::string text;
  if (!rule->key) {
    text = message;  // 原文
  } else if (rule->param && !message.empty()) {
    text = i18n.t(rule->key, {{rule->param, message}});
  } else {
    text = i18n.t(rule->key);
    // prefixedRaw：译文 + 空格 + 明细
    if (!message.empty()) text += " " + message;
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

}  // namespace clashwin::ui
