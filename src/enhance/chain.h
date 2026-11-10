#pragma once
// ← ClashVerge src-tauri/src/enhance/chain.rs 全量移植
// ChainItem：profile 级 Merge/Script/Rules/Proxies 子项与全局保护件的统一
// 载体（uid + 内容）。加载失败 / 文件缺失 → 空默认值（mod.rs unwrap_or_else）。
#include <string>
#include <vector>

#include "enhance/seq.h"

namespace clashwin::enhance {

// ← chain.rs::ChainType（Merge=R*.{yaml} 内容；Script=*.js 文本；
// Rules/Proxies=SeqMap）
struct ChainItem {
  std::string uid;
  // 四选一：
  YAML::Node merge = YAML::Node(YAML::NodeType::Map);  // Merge 模式内容
  std::string script;                                  // Script 模式文本
  SeqMap seq;                                          // Rules/Proxies 模式
  enum class Kind { Merge, Script, Rules, Proxies } kind = Kind::Merge;
};

// ← chain.rs::AsyncChainItemFrom::from_async：按 PrfItem itype/file 加载；
// itype 非四类 / 无 file / 文件不存在 / 读取失败 → 空默认（None → 默认）
ChainItem chain_item_load(const std::wstring& profiles_dir,
                          const std::string& uid, const std::string& itype,
                          const std::string& file);

// ← chain.rs::ChainItem::builtin：内置脚本（顺序 hy_alpn → meta_guard；
// ChainSupport::ClashMeta 仅支持 win-mihomo 内核）
std::vector<ChainItem> builtin_chain_items(bool clash_meta_supported);

}  // namespace clashwin::enhance
