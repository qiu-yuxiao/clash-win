#pragma once
// proxy_head_state.json 读写（← cmd/proxy.rs:98-116 全语义）
// 结构：{ "<profile_uid>": { "<group>": { "filterText": "", "open": false,
//         "showType": true, "sortType": 1 } } }
// 形态：serde_json::to_string_pretty（2 空格缩进、无尾换行）
// 读取方对撕裂/损坏文件静默退化为默认值，故落盘走原子替换。
#include <nlohmann/json.hpp>

#include "util/result.h"

namespace clashwin::config::proxy_head_state {

// ← save_proxy_head_state：整体覆盖保存（state 为前端传入的任意 JSON 对象）
Result<void> save(const nlohmann::json& state);

// ← get_proxy_head_state：文件不存在 → 空对象；损坏 → 空对象（原版语义）
nlohmann::json load();

// 类型化取值：单组头部状态（缺失 → nullopt）
struct GroupHeadState {
  std::string filter_text;
  bool open = false;
  bool show_type = true;
  int64_t sort_type = 0;
};
std::optional<GroupHeadState> get_group_state(const nlohmann::json& state,
                                              const std::string& uid,
                                              const std::string& group);

// 类型化写入：更新/插入单组头部状态（其余组保持不变）
nlohmann::json set_group_state(nlohmann::json state, const std::string& uid,
                               const std::string& group,
                               const GroupHeadState& value);

std::wstring state_path();

}  // namespace clashwin::config::proxy_head_state
