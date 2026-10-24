#include "yaml_serde.h"

#include <windows.h>

#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <type_traits>

#include "util/str_utils.h"
#include "util/utf.h"
#include "util/yaml_utils.h"

namespace clashwin::config::serde {

namespace {

// ---------- plain 标量合法性（保守实现：本配置域已知值全部 plain 合法） ----------
bool needs_double_quote(const std::string& s) {
  if (s.empty()) return true;
  // YAML 会误读为 null/bool 的文本必须加引号（字符串语义时）
  static const char* kReserved[] = {"null", "Null",  "NULL", "~",   "true",
                                    "True", "TRUE",  "false", "False", "FALSE"};
  for (const char* r : kReserved) {
    if (s == r) return true;
  }
  // 首字符指示符（%-directive、@/` 保留、引号、集合指示符、锚点别名等）
  char c0 = s[0];
  if (c0 == '-' || c0 == '?' || c0 == ':') {
    // "-" / "?"/ ":" 单独或后跟空白才是指示符；如 "-x"、"?:x" 可以 plain
    if (s.size() == 1 || s[1] == ' ') return true;
  }
  static const char* kLead = ",[]{}#&*!|>'\"%@`";
  if (std::strchr(kLead, c0) && c0 != '\0') return true;
  // 内嵌 ": " 或 " #"、控制字符、前后空白 → 引号
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char ch = static_cast<unsigned char>(s[i]);
    if (ch < 0x20 && ch != '\t') return true;
    if (ch == ' ') {
      if (i == 0 || i + 1 == s.size()) return true;
      if (s[i + 1] == '#') return true;
    } else if (ch == ':') {
      if (i + 1 == s.size() || s[i + 1] == ' ') return true;
    }
  }
  return false;
}

std::string quote_double(const std::string& s) {
  std::string out = "\"";
  for (char ch : s) {
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\x%02x", ch);
          out += buf;
        } else {
          out += ch;
        }
    }
  }
  out += "\"";
  return out;
}

bool is_plain_number(const std::string& s) {
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

bool looks_like_bool_or_null(const std::string& s) {
  return s == "true" || s == "false" || s == "True" || s == "False" ||
         s == "TRUE" || s == "FALSE" || s == "null" || s == "Null" ||
         s == "NULL" || s == "~";
}

// 标量文本化：bool/数字/null 直接 plain（写读语义一致）；
// 其余视为字符串：多行 → literal；plain 不安全 → 双引号
std::string scalar_text(const std::string& s) {
  if (looks_like_bool_or_null(s) || is_plain_number(s) || !needs_double_quote(s))
    return s;
  return quote_double(s);
}

std::string indent_of(int n) { return std::string(static_cast<size_t>(n), ' '); }

std::string key_text(const std::string& k) {
  return needs_double_quote(k) ? quote_double(k) : k;
}

// 多行字符串 → literal 块；返回 "|\n..." / "|-\n..." 片段（键行由调用方拼）
// 【serde/libyaml 对齐】chomp：尾单个 \n → clip("|")；无尾 \n → strip("|-")；
// 两个以上尾 \n 或含控制字符 → 双引号（保守回退）
std::string literal_or_quoted(const std::string& s, int indent) {
  if (s.empty()) return quote_double(s);
  for (char ch : s) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (c < 0x20 && c != '\n' && c != '\t') return quote_double(s);
  }
  if (s.size() >= 2 && s[s.size() - 1] == '\n' && s[s.size() - 2] == '\n')
    return quote_double(s);
  bool clip = s.back() == '\n';
  std::string body = clip ? s.substr(0, s.size() - 1) : s;
  std::string out = clip ? "|\n" : "|-\n";
  size_t start = 0;
  while (true) {
    size_t pos = body.find('\n', start);
    std::string line = (pos == std::string::npos)
                           ? body.substr(start)
                           : body.substr(start, pos - start);
    if (!line.empty()) out += indent_of(indent) + line;
    if (pos == std::string::npos) break;
    out += "\n";  // 空行不写缩进（libyaml 形态）
    start = pos + 1;
  }
  return out;
}

// 前向声明
void emit_value(const YAML::Node& n, int indent, std::string& out);

// 标量行内文本（无换行的 plain/quoted）；多行标量返回空串（由调用方走 literal）
std::string inline_scalar(const YAML::Node& n) {
  if (n.IsNull()) return "null";
  if (!n.IsScalar()) return "";
  const std::string& s = n.Scalar();
  if (s.find('\n') != std::string::npos) return "";
  return scalar_text(s);
}

void emit_scalar_block(const YAML::Node& n, int indent, std::string& out) {
  if (n.IsNull()) {
    out += "null";
    return;
  }
  const std::string& s = n.Scalar();
  if (s.find('\n') == std::string::npos) {
    out += scalar_text(s);
    return;
  }
  out += literal_or_quoted(s, indent);
}

void emit_map(const YAML::Node& m, int indent, std::string& out) {
  bool first = true;
  for (const auto& kv : m) {
    if (!first) out += "\n";
    first = false;
    std::string key =
        kv.first.IsScalar() ? key_text(kv.first.Scalar()) : quote_double("?");
    out += indent_of(indent) + key + ":";
    emit_value(kv.second, indent, out);
  }
}

void emit_seq(const YAML::Node& s, int indent, std::string& out) {
  bool first = true;
  for (const auto& item : s) {
    if (!first) out += "\n";
    first = false;
    out += indent_of(indent) + "-";
    if (item.IsNull()) {
      out += " null";
    } else if (item.IsScalar()) {
      const std::string& v = item.Scalar();
      if (v.find('\n') == std::string::npos) {
        out += " " + scalar_text(v);
      } else {
        out += " " + literal_or_quoted(v, indent + 2);
      }
    } else if (item.IsMap()) {
      // 首键内联在 "- " 后，与 serde 的 "- key: v" 形态一致
      std::string inner;
      emit_map(item, indent + 2, inner);
      // inner 首行自带 indent_of(indent+2)，替换为单个空格
      size_t lead = 0;
      while (lead < inner.size() && inner[lead] == ' ') ++lead;
      out += " " + inner.substr(lead);
    } else if (item.IsSequence()) {
      out += "\n";
      emit_seq(item, indent + 2, out);
    }
  }
}

void emit_value(const YAML::Node& n, int indent, std::string& out) {
  if (n.IsNull() || n.IsScalar()) {
    if (n.IsScalar() && n.Scalar().find('\n') != std::string::npos) {
      out += " " + literal_or_quoted(n.Scalar(), indent + 2);
    } else {
      out += " " + inline_scalar(n);
    }
    return;
  }
  if (n.IsMap()) {
    if (n.begin() == n.end()) {  // 空 map
      out += " {}";
      return;
    }
    out += "\n";
    emit_map(n, indent + 2, out);
    return;
  }
  if (n.IsSequence()) {
    if (n.begin() == n.end()) {
      out += " []";
      return;
    }
    // map 值为 seq：dash 与键同列（libyaml/serde 布局）
    out += "\n";
    emit_seq(n, indent, out);
  }
}

// ---------- 原子写盘（← help.rs::save_yaml / T02 yaml_utils 同款三步） ----------
bool read_file_binary(const std::wstring& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return true;
}

bool write_file_binary(const std::wstring& path, const std::string& data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(f);
}

}  // namespace

std::string dump(const YAML::Node& node) {
  std::string out;
  if (node.IsNull()) {
    out = "null";
  } else if (node.IsMap()) {
    if (node.begin() == node.end()) out = "{}";
    else emit_map(node, 0, out);
  } else if (node.IsSequence()) {
    if (node.begin() == node.end()) out = "[]";
    else emit_seq(node, 0, out);
  } else if (node.IsScalar()) {
    emit_scalar_block(node, 0, out);
  }
  if (out.empty() || out.back() != '\n') out += '\n';
  return out;
}

Result<void> save_yaml_text(const std::wstring& path, const std::string& body,
                            const std::string& prefix) {
  std::string data = prefix.empty() ? body : prefix + "\n\n" + body;
  std::string existing;
  if (read_file_binary(path, existing) && existing == data)
    return Result<void>::ok();

  size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) {
    Error e;
    e.code = 4;
    e.message = "failed to get parent directory";
    return Result<void>::err(std::move(e));
  }
  wchar_t pid_buf[16];
  std::swprintf(pid_buf, 16, L"%u", static_cast<unsigned>(GetCurrentProcessId()));
  std::wstring tmp = path.substr(0, slash + 1) + path.substr(slash + 1) +
                     L".tmp_" + pid_buf;
  if (!write_file_binary(tmp, data)) {
    Error e;
    e.code = 5;
    e.message = "failed to write temp file \"" + utf::utf16_to_utf8(tmp) + "\"";
    return Result<void>::err(std::move(e));
  }
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    std::wstring bak = path + L".bak";
    MoveFileExW(tmp.c_str(), bak.c_str(), MOVEFILE_REPLACE_EXISTING);
    Error e;
    e.code = 6;
    e.message = "failed to save file \"" + utf::utf16_to_utf8(path) +
                "\" (atomic rename failed)";
    return Result<void>::err(std::move(e));
  }
  return Result<void>::ok();
}

YAML::Node str_node(const std::string& v) { return YAML::Node(v); }
YAML::Node null_node() { return YAML::Node(YAML::NodeType::Null); }

void set_str(YAML::Node& map, const std::string& key, const std::string& v) {
  map[key] = v;
}
void set_bool(YAML::Node& map, const std::string& key, bool v) {
  map[key] = v ? "true" : "false";
}
void set_u16(YAML::Node& map, const std::string& key, uint16_t v) {
  map[key] = std::to_string(v);
}
void set_u32(YAML::Node& map, const std::string& key, uint32_t v) {
  map[key] = std::to_string(v);
}
void set_i64(YAML::Node& map, const std::string& key, int64_t v) {
  map[key] = std::to_string(v);
}
void set_null(YAML::Node& map, const std::string& key) {
  map[key] = YAML::Node(YAML::NodeType::Null);
}

std::optional<std::string> opt_str(const YAML::Node& map, const std::string& key) {
  const YAML::Node n = map[key];
  if (!n || !n.IsScalar() || n.IsNull()) return std::nullopt;
  return n.Scalar();
}
std::optional<bool> opt_bool(const YAML::Node& map, const std::string& key) {
  const YAML::Node n = map[key];
  if (!n || !n.IsScalar() || n.IsNull()) return std::nullopt;
  const std::string& s = n.Scalar();
  if (s == "true" || s == "True" || s == "TRUE") return true;
  if (s == "false" || s == "False" || s == "FALSE") return false;
  return std::nullopt;
}
namespace {
template <typename T>
std::optional<T> opt_num(const YAML::Node& map, const std::string& key) {
  const YAML::Node n = map[key];
  if (!n || !n.IsScalar() || n.IsNull()) return std::nullopt;
  const std::string& s = util::trim(n.Scalar());
  if (s.empty()) return std::nullopt;
  try {
    if constexpr (std::is_signed_v<T>) {
      return static_cast<T>(std::stoll(s));
    } else {
      return static_cast<T>(std::stoull(s));
    }
  } catch (...) {
    return std::nullopt;
  }
}
}  // namespace

std::optional<uint16_t> opt_u16(const YAML::Node& map, const std::string& key) {
  return opt_num<uint16_t>(map, key);
}
std::optional<uint32_t> opt_u32(const YAML::Node& map, const std::string& key) {
  return opt_num<uint32_t>(map, key);
}
std::optional<uint64_t> opt_u64(const YAML::Node& map, const std::string& key) {
  return opt_num<uint64_t>(map, key);
}
std::optional<int64_t> opt_i64(const YAML::Node& map, const std::string& key) {
  return opt_num<int64_t>(map, key);
}
std::optional<int32_t> opt_i32(const YAML::Node& map, const std::string& key) {
  return opt_num<int32_t>(map, key);
}

Result<YAML::Node> read_mapping_file(const std::wstring& path) {
  auto loaded = util::load_yaml_file(path);
  if (!loaded.is_ok()) return Result<YAML::Node>::err(loaded.error());
  YAML::Node node = loaded.value();
  if (!node.IsMap()) {
    Error e;
    e.code = 7;
    e.message = "failed to transform to yaml mapping \"" +
                utf::utf16_to_utf8(path) + "\"";
    return Result<YAML::Node>::err(std::move(e));
  }
  return Result<YAML::Node>::ok(std::move(node));
}

}  // namespace clashwin::config::serde
