#pragma once
// ← ClashVerge src-tauri/src/enhance/seq.rs 全量移植
// SeqMap：rules/proxies 子项文件（prepend/append/delete 数组操作）。
// use_seq("proxies") 额外联动 proxy-groups：删引用 + 首个 selector 组注入新增名。
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace clashwin::enhance {

struct SeqMap {
  YAML::Node prepend;  // sequence（缺省空 seq）
  YAML::Node append;   // sequence
  std::vector<std::string> delete_;  // seq.rs delete: Vec<String>

  static SeqMap empty();
};

// ← help.rs::read_seq_map：YAML 文件 {prepend, append, delete} → SeqMap
// （语法错/顶层非 map → Error）
struct ReadSeqMapResult {
  bool ok = false;
  std::string error;
  SeqMap map;
};
ReadSeqMapResult read_seq_map_text(const std::string& yaml_text);
ReadSeqMapResult read_seq_map_file(const std::wstring& path);

// ← seq.rs::use_seq：field ∈ {"rules","proxies"}
YAML::Node use_seq(const SeqMap& seq, const YAML::Node& config,
                   const std::string& field);

}  // namespace clashwin::enhance
