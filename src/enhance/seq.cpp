#include "enhance/seq.h"

#include <unordered_set>

#include "util/yaml_utils.h"

namespace clashwin::enhance {

namespace {

YAML::Node empty_seq() { return YAML::Node(YAML::NodeType::Sequence); }

std::vector<std::string> collect_proxy_names(const YAML::Node& seq) {
  std::vector<std::string> out;
  if (!seq || !seq.IsSequence()) return out;
  for (const auto& item : seq) {
    if (item.IsMap()) {
      const YAML::Node name = item["name"];
      if (name && name.IsScalar() && !name.IsNull()) {
        out.push_back(name.Scalar());
      }
    } else if (item.IsScalar() && !item.IsNull()) {
      out.push_back(item.Scalar());
    }
  }
  return out;
}

// ← seq.rs::is_selector_group：type select / selector（ASCII 小写比较）
bool is_selector_group(const YAML::Node& group_map) {
  if (!group_map || !group_map.IsMap()) return false;
  const YAML::Node type = group_map["type"];
  if (!type || !type.IsScalar() || type.IsNull()) return false;
  std::string v = type.Scalar();
  for (char& c : v) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return v == "select" || v == "selector";
}

bool delete_contains(const std::vector<std::string>& del,
                     const std::string& name) {
  for (const auto& d : del) {
    if (d == name) return true;
  }
  return false;
}

}  // namespace

SeqMap SeqMap::empty() {
  SeqMap m;
  m.prepend = empty_seq();
  m.append = empty_seq();
  return m;
}

ReadSeqMapResult read_seq_map_text(const std::string& yaml_text) {
  ReadSeqMapResult r;
  try {
    YAML::Node node = YAML::Load(yaml_text);
    if (!node.IsMap()) {
      r.error = "failed to transform to yaml mapping";
      return r;
    }
    SeqMap m;
    m.prepend = node["prepend"] ? node["prepend"] : empty_seq();
    m.append = node["append"] ? node["append"] : empty_seq();
    const YAML::Node del = node["delete"];
    if (del && del.IsSequence()) {
      for (const auto& d : del) {
        if (d.IsScalar() && !d.IsNull()) m.delete_.push_back(d.Scalar());
      }
    }
    r.map = std::move(m);
    r.ok = true;
    return r;
  } catch (const std::exception& ex) {
    r.error = ex.what();
    return r;
  }
}

ReadSeqMapResult read_seq_map_file(const std::wstring& path) {
  auto loaded = util::load_yaml_file(path);
  ReadSeqMapResult r;
  if (!loaded.is_ok()) {
    r.error = loaded.error().message;
    return r;
  }
  YAML::Node node = loaded.value();
  if (!node.IsMap()) {
    r.error = "failed to transform to yaml mapping";
    return r;
  }
  SeqMap m;
  m.prepend = node["prepend"] ? node["prepend"] : empty_seq();
  m.append = node["append"] ? node["append"] : empty_seq();
  const YAML::Node del = node["delete"];
  if (del && del.IsSequence()) {
    for (const auto& d : del) {
      if (d.IsScalar() && !d.IsNull()) m.delete_.push_back(d.Scalar());
    }
  }
  r.map = std::move(m);
  r.ok = true;
  return r;
}

YAML::Node use_seq(const SeqMap& seq, const YAML::Node& config,
                   const std::string& field) {
  YAML::Node cfg = config;

  // field == "proxies"：收集新增节点名（prepend+append，保序去重）
  std::vector<std::string> added_proxy_names;
  if (field == "proxies") {
    std::unordered_set<std::string> seen;
    for (const auto& n : collect_proxy_names(seq.prepend)) {
      if (seen.insert(n).second) added_proxy_names.push_back(n);
    }
    for (const auto& n : collect_proxy_names(seq.append)) {
      if (seen.insert(n).second) added_proxy_names.push_back(n);
    }
  }

  YAML::Node new_seq(YAML::NodeType::Sequence);
  if (seq.prepend && seq.prepend.IsSequence()) {
    for (const auto& item : seq.prepend) new_seq.push_back(item);
  }

  const YAML::Node origin = cfg[field];
  if (origin && origin.IsSequence()) {
    for (const auto& item : origin) {
      // 过滤 deleted（字符串项按值，映射项按 name）
      if (item.IsScalar() && !item.IsNull()) {
        if (delete_contains(seq.delete_, item.Scalar())) continue;
      } else if (item.IsMap()) {
        const YAML::Node name = item["name"];
        if (name && name.IsScalar() && !name.IsNull() &&
            delete_contains(seq.delete_, name.Scalar())) {
          continue;
        }
      }
      new_seq.push_back(item);
    }
  }

  if (seq.append && seq.append.IsSequence()) {
    for (const auto& item : seq.append) new_seq.push_back(item);
  }
  cfg[field] = new_seq;

  // proxies 联动 proxy-groups：过滤删除项 + 向首个 selector 组注入新增名
  if (field == "proxies") {
    YAML::Node groups = cfg["proxy-groups"];
    if (groups && groups.IsSequence()) {
      YAML::Node new_groups(YAML::NodeType::Sequence);
      bool appended_to_selector = false;
      for (const auto& group : groups) {
        if (group.IsMap()) {
          YAML::Node group_map = group;
          YAML::Node proxies_seq;
          bool has_seq = false;
          const YAML::Node proxies = group_map["proxies"];
          if (proxies && proxies.IsSequence()) {
            YAML::Node filtered(YAML::NodeType::Sequence);
            for (const auto& p : proxies) {
              if (p.IsScalar() && !p.IsNull() &&
                  delete_contains(seq.delete_, p.Scalar())) {
                continue;
              }
              filtered.push_back(p);
            }
            proxies_seq = filtered;
            has_seq = true;
          }

          if (!appended_to_selector && !added_proxy_names.empty() &&
              is_selector_group(group_map)) {
            YAML::Node merged(YAML::NodeType::Sequence);
            std::unordered_set<std::string> existing;
            for (const auto& name : added_proxy_names) {
              if (existing.insert(name).second) merged.push_back(name);
            }
            const YAML::Node base = has_seq ? proxies_seq : empty_seq();
            for (const auto& value : base) {
              if (value.IsScalar() && !value.IsNull()) {
                if (!existing.insert(value.Scalar()).second) continue;
              }
              merged.push_back(value);
            }
            proxies_seq = merged;
            has_seq = true;
            appended_to_selector = true;
          }

          if (has_seq) group_map["proxies"] = proxies_seq;
          new_groups.push_back(group_map);
        } else {
          new_groups.push_back(group);
        }
      }
      cfg["proxy-groups"] = new_groups;
    }
  }

  return cfg;
}

}  // namespace clashwin::enhance
