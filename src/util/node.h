#pragma once
// 伪节点/假 server 识别（← utils/node.rs 全量移植）
// 由 monitor（T22）、enhance（T08）、lifecycle 共享：从配置生成、自愈判定到
// 节点恢复的全链路都剔除伪节点。
#include <string>

namespace clashwin::util {

// ← node.rs::is_dummy_node：识别广告/假节点。
// 高置信度（含 URL/英文广告词）直接判真；中文广告短语要求「归一化后完全
// 匹配或以短语开头」，避免误杀 "HK-流量优化"、"CN2-购买入口" 等真实节点。
bool is_dummy_node(const std::string& name);

// ← node.rs::is_valid_node_server：IP 字面量（严格全形式），或至少含一个
// 点的域名。拦 server='1' 之类占位假节点（BUG-301）。
bool is_valid_node_server(const std::string& server);

}  // namespace clashwin::util
