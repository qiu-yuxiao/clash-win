#pragma once
// 解锁结果缓存（前端 localStorage clash_verge_unlock_results /
// clash_verge_unlock_time 的落盘等价物 ← src/pages/unlock.tsx:31-39,148-183）
// 文件：<home>\unlock_cache.json；形态：
// { "time": "2026-10-25T12:00:00Z" | null,
//   "items": [ { "name": "...", "status": "...", "region": ...,
//                "check_time": ... } ] }
// 读取时按 name（trim+lower）去重，Pending 优先级最高（前端
// getStatusPriority：Pending=0 高于其他）。
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "util/result.h"

namespace clashwin::config::unlock_cache {

struct UnlockItem {
  std::string name;
  std::string status;
  std::optional<std::string> region;
  std::optional<std::string> check_time;
};

struct UnlockCache {
  std::vector<UnlockItem> items;
  std::optional<std::string> time;
};

// ← saveResultsToStorage：items 全量 + time（time 为空则不更新 time 字段语义
//   与前端一致：仅当有值时写入）
Result<void> save(const std::vector<UnlockItem>& items,
                  const std::optional<std::string>& time);

// ← loadResultsFromStorage：文件缺失/损坏 → nullopt；items 去重后返回
std::optional<UnlockCache> load();

// ← dedupeUnlockItems：name 归一（trim+lower）去重；Pending 优先级高者胜，
//   平级合并 region/check_time（preferred 为新项）
std::vector<UnlockItem> dedupe_items(std::vector<UnlockItem> items);

std::wstring cache_path();

}  // namespace clashwin::config::unlock_cache
