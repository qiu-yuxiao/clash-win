#pragma once
// yaml-cpp 封装：保插入序（enhance 规则链顺序的命门）+ 深合并 + 原子写盘
//
// 【顺序语义结论（已查证 vendored yaml-cpp 源码并附单测证明）】
// yaml-cpp Node 的 Map 内部结构为
//   using node_map = std::vector<std::pair<node*, node*>>;
//   （third_party/yaml-cpp/include/yaml-cpp/node/detail/node_data.h:117）
// 新键 insert_map_pair 走 m_map.emplace_back（尾部追加），旧键赋值为线性查找
// 原位覆盖。因此：
//   - 加载顺序 = 文档顺序；迭代/ Dump 顺序 = 插入顺序；
//   - node[key] = v 对已存在键保持原位，新键追加到尾部；
//   - 全程禁止转 std::map / std::unordered_map（会丢序），直接用 YAML::Node。
// 【坑】以 YAML::Node 作键调用 operator[] 按节点对象身份（node::is）比较，
//   不按标量值比较——跨文档合并会插出重复键。跨文档操作一律先用
//   key.Scalar() 转成字符串键再查（deep_merge/set_key 已封装）。
#include <string>

#include <yaml-cpp/yaml.h>

#include "util/result.h"

namespace clashwin::util {

using PreserveOrderNode = YAML::Node;  // 保插入序的顺序 map（语义别名，见文件头）

// 读 YAML 文件（支持 UTF-8 BOM 剥除）；语法错误 → Error{message}
Result<YAML::Node> load_yaml_file(const std::wstring& path);

// Dump 保序序列化（补尾部换行，对齐 serde_yaml_ng::to_string 产物形态）
Result<std::string> emit_yaml(const YAML::Node& node);

// 原子写盘（← help.rs::save_yaml）：内容未变化跳过写；tmp 文件 +
// MoveFileExW(REPLACE_EXISTING)；替换失败把 tmp 改名为 <path>.bak 并报错
Result<void> save_yaml_file(const std::wstring& path, const YAML::Node& node,
                            const std::string& prefix = {});

// 深合并（← enhance/merge.rs 的通用语义，T08 在其上叠 prepend/append/delete）：
// 两键均为 map → 递归合并；否则 overlay 值整体覆盖。
// base 已有键保持原位，overlay 新键按 overlay 顺序追加。原地修改 base。
void deep_merge(YAML::Node& base, const YAML::Node& overlay);

// 保序键写入：键存在 → 原位覆盖；不存在 → 尾部追加
void set_key(YAML::Node& map, const std::string& key, const YAML::Node& value);

}  // namespace clashwin::util
