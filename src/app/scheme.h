#pragma once
// Scheme（T19）：深层链接（Deep Link）解析与订阅导入
// 支持协议：clash://install-config, clash-verge://install-config, clash-mini://install-config
// 外层剥除：[...] 或 ["..."] 或 "..."
// 最多 2 次 URL 解码：util::decode_subscription_url
// 导入订阅：PrfItem::from_url 并入库保存，触发通知
// 内核重载：仅在导入前无 active profile (current) 且新配置成功激活时触发

#include <optional>
#include <string>
#include <string_view>
#include "util/result.h"

namespace clashwin::app {

struct DeepLinkInfo {
  std::string url;
  std::optional<std::string> name;
};

// 剥除外层括号/引号并解析深层链接
Result<DeepLinkInfo> parse_deep_link(std::string_view link);

// 解析并执行导入订阅链路
Result<void> resolve_scheme(std::string_view link);

}  // namespace clashwin::app
