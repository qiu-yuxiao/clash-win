#pragma once
// 通知状态 → 用户可见 toast 的映射层
// ← src/pages/_layout/utils/notification-handlers.ts（27 条 status 表）
//
// 为什么需要这一层：后端（Rust/C++）只发「状态串 + 明细」，前端负责把状态串翻成
// 本地化文案再弹 Alert。Win 侧此前 EventBus::notice_message 全仓零订阅者 ——
// 所有 import_sub_url / config_validate / install_service 结果都发进了虚空，
// 用户侧表现为「点导入没反应、切配置没反应、装服务没反应」。
//
// 语义（← notice-service.ts:169-280 normalizeNoticeMessage）：
//   key 为空                → 直接显示 msg 原文
//   key 非空 + param 非空   → t(key, { param: msg })
//   key 非空 + msg 非空     → t(key) + " " + msg        （prefixedRaw 模板）
//   key 非空 + msg 为空     → t(key)
#include <cstdint>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>
#include "ui/scenes/node_data.h"
#include "ui/scenes/quick_bar.h"

namespace clashwin::ui {

using NetworkHealthIssue = scene::NetworkHealthIssue;

// 订阅 EventBus::kNoticeMessage；已安装则幂等返回。
void install_notice_handler();

// 仅测试：退订
void uninstall_notice_handler();

// 单条分发的纯函数入口（测试直接调用，绕过 EventBus）
// 返回 true 表示该 status 有对应规则并已 push（或被内部健康态势通道消费）。
bool dispatch_notice(const char* status, const std::string& message);

// BUG-359: 后台监测器网络健康态势状态订阅（network_health::ok / offline / all_dead）
// 拦截该前缀消息并同步给顶栏 QuickBar，绝不触发底部 Snackbar Toast。
NetworkHealthIssue latest_network_health();
void set_latest_network_health(NetworkHealthIssue issue);
uint64_t subscribe_network_health(std::function<void(NetworkHealthIssue)> cb);
void unsubscribe_network_health(uint64_t id);

// ---------- 通知助手（用户裁决已删除自编项，仅留 Mini v3.5.3 对应提示） ----------
// 分流模式切换（Mini 已有的 switchedToGlobalProxy）；其余自编映射与提示已删除。
std::string resolve_verge_patch_success_key(const std::string& k,
                                            const nlohmann::json& v);
std::string resolve_verge_patch_success_message(const std::string& k,
                                                const nlohmann::json& v);
// 批量测速：Mini 仅有 busy/notExecuted 提示，无常驻测速条与结果提示
uint64_t begin_batch_test_notice();
void finish_batch_test_notice(uint64_t wait_id,
                              const scene::BatchTestSummary& summary);
// 连接管理：仅保留错误原文透传
void dispatch_close_all_connections_notice(bool ok, const std::string& err_detail);

}  // namespace clashwin::ui
