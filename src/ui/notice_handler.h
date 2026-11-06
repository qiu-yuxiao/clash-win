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
#include <string>

namespace clashwin::ui {

// 订阅 EventBus::kNoticeMessage；已安装则幂等返回。
void install_notice_handler();

// 仅测试：退订
void uninstall_notice_handler();

// 单条分发的纯函数入口（测试直接调用，绕过 EventBus）
// 返回 true 表示该 status 有对应规则并已 push。
bool dispatch_notice(const char* status, const std::string& message);

}  // namespace clashwin::ui
