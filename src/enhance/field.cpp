#include "enhance/field.h"

#include <unordered_set>

namespace clashwin::enhance::field {

const char* const kHandleFields[] = {
    "mode", "redir-port", "tproxy-port", "mixed-port", "socks-port", "port",
    "allow-lan", "log-level", "ipv6", "external-controller", "secret",
    "unified-delay",
};

// 实机对齐（见头文件注释）：不含 rules
const char* const kDefaultFields[] = {
    "proxies", "proxy-providers", "proxy-groups", "rule-providers",
};

namespace {

// plain 数字文本（供 is_string_key 判定；与 serde Value::Number 归类一致）
bool plain_number_text(const std::string& s) {
  if (s.empty()) return false;
  size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
  if (i >= s.size()) return false;
  bool digits = false, dot = false, expo = false;
  for (; i < s.size(); ++i) {
    char c = s[i];
    if (c >= '0' && c <= '9') { digits = true; continue; }
    if (c == '.' && !dot && !expo) { dot = true; continue; }
    if ((c == 'e' || c == 'E') && digits && !expo) {
      expo = true;
      if (i + 1 < s.size() && (s[i + 1] == '-' || s[i + 1] == '+')) ++i;
      continue;
    }
    return false;
  }
  return digits;
}

bool reserved_text(const std::string& s) {
  return s == "null" || s == "Null" || s == "NULL" || s == "~" ||
         s == "true" || s == "True" || s == "TRUE" || s == "false" ||
         s == "False" || s == "FALSE";
}

}  // namespace

bool is_string_key(const YAML::Node& key) {
  if (!key.IsScalar()) return false;             // 复合键：serde as_str → None
  if (key.IsNull()) return false;
  const std::string& tag = key.Tag();
  const std::string& s = key.Scalar();
  if (tag == "!") return true;                   // 引号加载 → String
  if (tag == "tag:yaml.org,2002:str") return true;
  if (tag.empty()) return true;                  // 程序构造 → 字符串语义
  // plain 加载（tag "?"）：数字/bool/null 文本归非字符串
  if (plain_number_text(s) || reserved_text(s)) return false;
  return true;
}

YAML::Node use_lowercase(const YAML::Node& config) {
  YAML::Node ret(YAML::NodeType::Map);
  if (!config || !config.IsMap()) return ret;
  for (const auto& kv : config) {
    if (!is_string_key(kv.first)) continue;
    std::string key = kv.first.Scalar();
    for (char& c : key) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    ret[key] = kv.second;
  }
  return ret;
}

YAML::Node use_sort(const YAML::Node& config) {
  YAML::Node ret(YAML::NodeType::Map);
  if (!config || !config.IsMap()) return ret;

  std::unordered_set<std::string> handled;
  for (const char* key : kHandleFields) {
    handled.insert(key);
    const YAML::Node v = config[key];
    if (v) ret[key] = v;
  }

  // 中间段：输入插入序（实机为 HashSet 随机序，见头文件注释）
  for (const auto& kv : config) {
    if (!kv.first.IsScalar() || kv.first.IsNull()) continue;
    const std::string key = kv.first.Scalar();
    if (handled.count(key)) continue;
    bool is_default = false;
    for (const char* d : kDefaultFields) {
      if (key == d) { is_default = true; break; }
    }
    if (is_default) continue;
    ret[key] = kv.second;
  }

  for (const char* key : kDefaultFields) {
    const YAML::Node v = config[key];
    if (v) ret[key] = v;
  }
  return ret;
}

std::vector<std::string> use_keys(const YAML::Node& config) {
  std::vector<std::string> out;
  if (!config || !config.IsMap()) return out;
  for (const auto& kv : config) {
    if (!is_string_key(kv.first)) continue;
    std::string key = kv.first.Scalar();
    for (char& c : key) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    out.push_back(std::move(key));
  }
  return out;
}

}  // namespace clashwin::enhance::field
