#pragma once
// PrfItem 本地部分（← config/prfitem.rs：from_local/from_merge/from_script/
// from_rules/from_proxies/read_file/save_file）；from_url 下载/解析链路
// 属 T06/T15，此处仅接口占位。
#include "clashwin/config/models.h"

namespace clashwin::config::prfitem {

// uid 生成（← help.rs::get_uid：nanoid 11 位 + 前缀；T02 已提供 gen_uid）
std::string gen_uid_prefixed(std::string_view prefix);

// 当前 Unix 秒（← chrono::Local::now().timestamp()）
int64_t now_timestamp();

// ← prfitem.rs::ensure_enhance_items：确保 merge/script/rules/proxies 子项
// 存在（缺失则创建并追加到 profiles）；groups 只读不创建（防死 g*.yaml）
Result<PrfOption> ensure_enhance_items(const std::optional<PrfOption>& option);

// 文件名规则：R/L/m/s/r/p/g + uid + 扩展名（yaml / js）
std::string file_name_for(const std::string& uid, bool is_script);

// profiles/ 目录路径
std::wstring profiles_dir();

// ← profiles.rs::write_file_if_changed（内容一致跳过写）
Result<void> write_file_if_changed(const std::wstring& path,
                                   const std::string& content);

}  // namespace clashwin::config::prfitem

namespace clashwin::config {

// ← prfitem.rs::parse_subscription_content：订阅内容三级回退解析
//   ① data 本身是含 proxies/proxy-providers 的 YAML mapping →（mapping, 原文）
//   ② Base64 解码后为该形态 YAML →（mapping, 解码文）
//   ③ URI-list 解析 →（mapping, serde 序列化文）
//   全部失败 → Error
Result<std::pair<YAML::Node, std::string>> parse_subscription_content(
    const std::string& data);

}  // namespace clashwin::config
