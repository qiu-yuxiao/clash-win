#pragma once
// 事件名 + 防抖/节流常量唯一事实源
// ← 01_frontend_census §5-C（use-layout-events.ts / app-data-provider.tsx /
//   use-core-update.ts）+ core/notification.rs FrontendEvent 序列化表。
// 本表是 T09 event_bus 的门控配置源；T10/T13/T14/T17/T21 直接引用事件名。
#include <cstdint>

namespace clashwin::events::names {

// ← notification.rs FrontendEvent::serialize_event
inline constexpr const char* kRefreshClash = "verge://refresh-clash-config";
inline constexpr const char* kRefreshVerge = "verge://refresh-verge-config";
inline constexpr const char* kRefreshProxies = "verge://refresh-proxy-config";
inline constexpr const char* kNoticeMessage = "verge://notice-message";
inline constexpr const char* kProfileChanged = "profile-changed";
inline constexpr const char* kTimerUpdated = "verge://timer-updated";
inline constexpr const char* kProfileUpdateStarted = "profile-update-started";
inline constexpr const char* kProfileUpdateCompleted = "profile-update-completed";
inline constexpr const char* kBackendDelayResults = "verge://backend-delay-results";
// ← 04§3：{status,progress,message}；进度序列 checking(5)→downloading(10-84)
//   →verifying(85)→extracting(90)→done(100)/error(0)
inline constexpr const char* kCoreUpgradeProgress = "core-upgrade-progress";

}  // namespace clashwin::events::names

namespace clashwin::events::consts {

// ← 01§5-C 防抖/节流（毫秒）
inline constexpr int kRefreshClashDebounceMs = 250;    // use-layout-events.ts:63
inline constexpr int kRefreshVergeDebounceMs = 250;    // use-layout-events.ts:88
inline constexpr int kRefreshProxiesDebounceMs = 200;  // app-data-provider.tsx:321
inline constexpr int kProfileChangedThrottleMs = 800;  // app-data-provider.tsx:307

// ← notice-service.ts DEFAULT_DURATIONS / MAX_NOTICES
inline constexpr int kNoticeSuccessMs = 3000;
inline constexpr int kNoticeInfoMs = 5000;
inline constexpr int kNoticeErrorMs = 8000;
inline constexpr size_t kNoticeMaxCount = 50;

// 轻量模式下事件过滤（← notification.rs：is_in_lightweight_mode → 全部不发）
// T19 接入后生效；T09 阶段恒 false。

}  // namespace clashwin::events::consts
