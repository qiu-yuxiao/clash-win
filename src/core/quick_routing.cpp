#include "core/quick_routing.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <sstream>
#include <unordered_set>

#include "cmd/cmd_support.h"
#include "events/event_bus.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/str_utils.h"
#include "util/utf.h"
#include "util/yaml_utils.h"

namespace clashwin::core::quick_routing {

namespace {

std::mutex& merge_file_mutex() {
  static std::mutex mu;
  return mu;
}

bool is_ascii_digits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

std::string trim_copy(const std::string& s) { return util::trim(s); }

std::string to_lower_copy(const std::string& s) { return util::to_lower(s); }

std::string to_upper_copy(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return out;
}

std::vector<std::string> split_char(const std::string& s, char delim) {
  std::vector<std::string> parts;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == delim) {
      parts.push_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return parts;
}

std::string strip_scheme_prefix(const std::string& input) {
  // 匹配 ^[a-zA-Z][a-zA-Z0-9+.-]*://
  if (input.empty() || !std::isalpha(static_cast<unsigned char>(input[0]))) {
    return input;
  }
  size_t i = 1;
  while (i < input.size()) {
    char c = input[i];
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' ||
        c == '.') {
      ++i;
      continue;
    }
    break;
  }
  if (i + 2 < input.size() && input[i] == ':' && input[i + 1] == '/' &&
      input[i + 2] == '/') {
    return input.substr(i + 3);
  }
  return input;
}

std::string split_first_of(const std::string& s, const char* seps) {
  size_t p = s.find_first_of(seps);
  return (p == std::string::npos) ? s : s.substr(0, p);
}

std::vector<std::string> read_merge_prepend_rules_from_node(const YAML::Node& doc) {
  std::vector<std::string> out;
  if (!doc || !doc.IsMap()) return out;
  const YAML::Node raw = static_cast<const YAML::Node&>(doc)["prepend-rules"];
  if (!raw || !raw.IsSequence()) return out;
  for (const auto& item : raw) {
    if (item && item.IsScalar() && !item.IsNull()) {
      out.push_back(item.Scalar());
    }
  }
  return out;
}

}  // namespace

std::string rule_target_to_string(RuleTarget target) {
  switch (target) {
    case RuleTarget::Direct:
      return "DIRECT";
    case RuleTarget::Proxy:
      return "PROXY";
    case RuleTarget::Reject:
      return "REJECT";
  }
  return "DIRECT";
}

RuleTarget rule_target_from_string(const std::string& target_str) {
  std::string upper = to_upper_copy(target_str);
  if (upper == "PROXY") return RuleTarget::Proxy;
  if (upper == "REJECT") return RuleTarget::Reject;
  return RuleTarget::Direct;
}

bool is_ip_address(const std::string& str) {
  if (str.empty()) return false;
  int parts = 0;
  size_t start = 0;
  for (size_t i = 0; i <= str.size(); ++i) {
    if (i == str.size() || str[i] == '.') {
      if (i == start || i - start > 3) return false;
      int val = 0;
      for (size_t k = start; k < i; ++k) {
        if (!std::isdigit(static_cast<unsigned char>(str[k]))) return false;
        val = val * 10 + (str[k] - '0');
      }
      if (val > 255) return false;
      ++parts;
      start = i + 1;
    }
  }
  return parts == 4;
}

bool is_ipv6_address(const std::string& str) {
  if (str.empty() || str.find('/') != std::string::npos) return false;
  size_t colons = 0;
  for (char c : str) {
    if (c == ':') {
      ++colons;
    } else if (c != '.' && !std::isxdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return colons >= 2;
}

std::string normalize_quick_target_host(const std::string& raw) {
  std::string host = trim_copy(raw);
  if (host.empty()) return "";

  if (host.front() == '[') {
    size_t close_idx = host.find(']');
    if (close_idx != std::string::npos && close_idx > 1) {
      host = host.substr(1, close_idx - 1);
    }
  } else {
    size_t first_colon = host.find(':');
    size_t last_colon = host.rfind(':');
    if (first_colon != std::string::npos && first_colon == last_colon) {
      std::string maybe_port = host.substr(first_colon + 1);
      if (is_ascii_digits(maybe_port)) {
        host = host.substr(0, first_colon);
      }
    }
  }

  size_t zone_idx = host.find('%');
  if (zone_idx != std::string::npos) {
    host = host.substr(0, zone_idx);
  }

  // Strip leading `*.` or `.` repeatedly
  while (true) {
    if (host.rfind("*.", 0) == 0) {
      host = host.substr(2);
    } else if (!host.empty() && host.front() == '.') {
      host = host.substr(1);
    } else {
      break;
    }
  }
  while (!host.empty() && host.back() == '.') {
    host.pop_back();
  }
  return to_lower_copy(trim_copy(host));
}

std::string extract_domain_suffix(const std::string& domain_or_ip) {
  std::string value = trim_copy(domain_or_ip);
  if (value.empty()) return "";
  value = strip_scheme_prefix(value);
  value = split_first_of(value, "/?#");
  std::string host = normalize_quick_target_host(value);
  if (is_ip_address(host) || is_ipv6_address(host)) return host;
  if (host.rfind("www.", 0) == 0) host = host.substr(4);

  auto parts = split_char(host, '.');
  std::vector<std::string> clean_parts;
  for (const auto& p : parts) {
    if (!p.empty()) clean_parts.push_back(p);
  }
  if (clean_parts.size() < 2) return host;
  const std::string& last = clean_parts.back();
  const std::string& second_last = clean_parts[clean_parts.size() - 2];
  static const char* double_tlds[] = {"com", "org", "net", "gov", "edu", "co"};
  bool is_double = false;
  for (const char* tld : double_tlds) {
    if (second_last == tld && last.size() <= 3) {
      is_double = true;
      break;
    }
  }
  if (clean_parts.size() >= 3 && is_double) {
    return clean_parts[clean_parts.size() - 3] + "." + second_last + "." + last;
  }
  return second_last + "." + last;
}

std::string build_exact_quick_routing_rule(RuleType type,
                                           const std::string& value,
                                           RuleTarget target) {
  const std::string target_str = rule_target_to_string(target);
  if (type == RuleType::Process) {
    std::string proc = trim_copy(value);
    return proc.empty() ? "" : ("PROCESS-NAME," + proc + "," + target_str);
  }

  std::string host = normalize_quick_target_host(value);
  if (host.empty()) return "";

  if (is_ip_address(host)) {
    return "IP-CIDR," + host + "/32," + target_str;
  }
  if (is_ipv6_address(host)) {
    return "IP-CIDR6," + host + "/128," + target_str;
  }
  return "DOMAIN," + host + "," + target_str;
}

std::string build_rule_string(RuleType type, const std::string& value, RuleTarget target) {
  return build_exact_quick_routing_rule(type, value, target);
}

std::string build_proxy_rule(const std::string& input) {
  std::string raw = trim_copy(input);
  if (raw.empty()) return "";
  raw = strip_scheme_prefix(raw);

  // Detect explicit IPv4/IPv6 CIDR before path stripping removes `/prefix`.
  std::string cidr_candidate = trim_copy(split_first_of(raw, "?#"));
  auto slash_pos = cidr_candidate.find('/');
  if (slash_pos != std::string::npos &&
      cidr_candidate.find('/', slash_pos + 1) == std::string::npos) {
    std::string base_part = cidr_candidate.substr(0, slash_pos);
    std::string prefix_part = cidr_candidate.substr(slash_pos + 1);
    if (prefix_part.size() <= 3 && is_ascii_digits(prefix_part)) {
      int prefix = std::stoi(prefix_part);
      std::string base_ip = normalize_quick_target_host(base_part);
      if (is_ip_address(base_ip) && prefix >= 0 && prefix <= 32) {
        return "IP-CIDR," + base_ip + "/" + std::to_string(prefix) + ",PROXY";
      }
      if (is_ipv6_address(base_ip) && prefix >= 0 && prefix <= 128) {
        return "IP-CIDR6," + base_ip + "/" + std::to_string(prefix) + ",PROXY";
      }
    }
  }

  std::string host_port_raw = trim_copy(split_first_of(raw, "/?#"));
  if (host_port_raw.empty()) return "";

  const bool explicit_wildcard =
      (host_port_raw.rfind("*.", 0) == 0 || host_port_raw.front() == '.');
  std::string cleaned = normalize_quick_target_host(host_port_raw);
  if (cleaned.empty()) return "";

  if (is_ip_address(cleaned)) {
    return "IP-CIDR," + cleaned + "/32,PROXY";
  }
  if (is_ipv6_address(cleaned)) {
    return "IP-CIDR6," + cleaned + "/128,PROXY";
  }
  if (cleaned.find('.') == std::string::npos) return "";

  return explicit_wildcard ? ("DOMAIN-SUFFIX," + cleaned + ",PROXY")
                           : ("DOMAIN," + cleaned + ",PROXY");
}

std::optional<ManagedMergeRule> parse_managed_merge_rule_line(const std::string& line) {
  std::string trimmed = trim_copy(line);
  if (trimmed.empty()) return std::nullopt;

  auto parts = split_char(trimmed, ',');
  for (auto& p : parts) p = trim_copy(p);
  if (parts.size() < 3) return std::nullopt;

  std::string rule_type = to_upper_copy(parts[0]);
  if (rule_type != "DOMAIN" && rule_type != "DOMAIN-SUFFIX" &&
      rule_type != "IP-CIDR" && rule_type != "IP-CIDR6" &&
      rule_type != "PROCESS-NAME") {
    return std::nullopt;
  }

  std::string target_upper = to_upper_copy(parts[2]);
  if (target_upper != "DIRECT" && target_upper != "PROXY" &&
      target_upper != "REJECT") {
    return std::nullopt;
  }

  const std::string& raw_pattern = parts[1];
  if (raw_pattern.empty()) return std::nullopt;

  ManagedMergeRule out;
  out.raw = trimmed;
  out.rule_type = rule_type;
  out.value = (rule_type == "PROCESS-NAME") ? raw_pattern : to_lower_copy(raw_pattern);
  out.target = rule_target_from_string(target_upper);

  if (rule_type == "IP-CIDR" && util::ends_with(out.value, "/32")) {
    out.match_key = out.value.substr(0, out.value.size() - 3);
  } else if (rule_type == "IP-CIDR6" && util::ends_with(out.value, "/128")) {
    out.match_key = out.value.substr(0, out.value.size() - 4);
  } else if (rule_type == "PROCESS-NAME") {
    out.match_key = to_lower_copy(out.value);
  } else {
    out.match_key = out.value;
  }

  if (parts.size() > 3) {
    for (size_t i = 3; i < parts.size(); ++i) {
      if (i > 3) out.extra += ",";
      out.extra += parts[i];
    }
  }
  return out;
}

std::string get_rule_identity_key(const std::string& rule_str) {
  auto parsed = parse_managed_merge_rule_line(rule_str);
  if (!parsed) return "";
  std::string norm_val = (parsed->rule_type == "PROCESS-NAME")
                             ? to_lower_copy(parsed->value)
                             : parsed->value;
  return parsed->rule_type + ":" + norm_val;
}

std::vector<ManagedMergeRule> parse_managed_merge_rules_from_yaml(
    const std::string& merge_yaml) {
  std::vector<ManagedMergeRule> out;
  if (trim_copy(merge_yaml).empty()) return out;
  try {
    YAML::Node doc = YAML::Load(merge_yaml);
    if (!doc || !doc.IsMap()) return out;
    for (const auto& line : read_merge_prepend_rules_from_node(doc)) {
      if (auto parsed = parse_managed_merge_rule_line(line)) {
        out.push_back(std::move(*parsed));
      }
    }
  } catch (const std::exception& e) {
    CWLOG_WARN(std::string("Failed to parse Merge prepend-rules: ") + e.what());
  }
  return out;
}

std::vector<std::string> upsert_prepend_rule(
    const std::vector<std::string>& existing,
    const std::string& new_rule,
    const std::vector<std::string>& replace_raw_rules) {
  const std::string new_key = get_rule_identity_key(new_rule);
  std::unordered_set<std::string> replace_set;
  for (const auto& r : replace_raw_rules) {
    std::string t = trim_copy(r);
    if (!t.empty()) replace_set.insert(std::move(t));
  }

  std::vector<std::string> result;
  result.reserve(existing.size() + 1);
  result.push_back(new_rule);
  for (const auto& rule : existing) {
    std::string trimmed = trim_copy(rule);
    if (trimmed.empty() || trimmed == new_rule) continue;
    if (replace_set.count(trimmed) > 0) continue;
    if (!new_key.empty() && get_rule_identity_key(trimmed) == new_key) continue;
    result.push_back(rule);
  }
  return result;
}

std::pair<std::vector<std::string>, std::vector<std::string>> remove_prepend_rules(
    const std::vector<std::string>& existing,
    const std::string& raw_rule,
    const std::vector<std::string>& identity_keys) {
  const std::string target_raw = trim_copy(raw_rule);
  std::unordered_set<std::string> key_set;
  for (const auto& k : identity_keys) {
    if (!k.empty()) key_set.insert(k);
  }

  std::vector<std::string> next_rules;
  std::vector<std::string> removed;
  for (const auto& rule : existing) {
    std::string trimmed = trim_copy(rule);
    std::string rule_key = trimmed.empty() ? "" : get_rule_identity_key(trimmed);
    const bool matches_raw = !target_raw.empty() && trimmed == target_raw;
    const bool matches_key = !rule_key.empty() && key_set.count(rule_key) > 0;
    if (matches_raw || matches_key) {
      removed.push_back(trimmed.empty() ? rule : trimmed);
    } else {
      next_rules.push_back(rule);
    }
  }
  return {std::move(next_rules), std::move(removed)};
}

std::vector<std::string> build_target_identity_keys(RuleType type,
                                                    const std::string& value) {
  std::string exact_rule =
      build_exact_quick_routing_rule(type, value, RuleTarget::Direct);
  if (exact_rule.empty()) return {};
  std::string key = get_rule_identity_key(exact_rule);
  if (key.empty()) return {};
  return {std::move(key)};
}

std::vector<ManagedMergeRule> read_managed_merge_rules() {
  std::vector<ManagedMergeRule> out;
  std::wstring merge_path = util::dirs::app_profiles_dir() + L"\\Merge.yaml";
  std::lock_guard<std::mutex> lock(merge_file_mutex());
  auto load_res = util::load_yaml_file(merge_path);
  if (!load_res.is_ok() || !load_res.value().IsMap()) return out;
  for (const auto& line : read_merge_prepend_rules_from_node(load_res.value())) {
    if (auto parsed = parse_managed_merge_rule_line(line)) {
      out.push_back(std::move(*parsed));
    }
  }
  return out;
}

QuickRouteResult add_quick_routing_rule(
    RuleType type, const std::string& value, RuleTarget target,
    const std::vector<std::string>& replace_raw_rules) {
  std::string new_rule = build_exact_quick_routing_rule(type, value, target);
  if (new_rule.empty()) {
    return {false, "", "Empty or invalid rule target value"};
  }

  std::wstring merge_path = util::dirs::app_profiles_dir() + L"\\Merge.yaml";
  {
    std::lock_guard<std::mutex> lock(merge_file_mutex());
    YAML::Node doc;
    auto load_res = util::load_yaml_file(merge_path);
    if (load_res.is_ok() && load_res.value().IsMap()) {
      doc = load_res.value();
    } else {
      doc = YAML::Node(YAML::NodeType::Map);
    }
    std::vector<std::string> existing = read_merge_prepend_rules_from_node(doc);
    doc["prepend-rules"] = upsert_prepend_rule(existing, new_rule, replace_raw_rules);
    auto save_res = util::save_yaml_file(merge_path, doc);
    if (!save_res.is_ok()) {
      CWLOG_ERROR("Failed to save Merge.yaml: " + save_res.error().message);
      return {false, new_rule, save_res.error().message};
    }
  }

  cmd::enhance_profiles_live();
  events::EventBus::notice_message("quick_rule_added", new_rule);
  return {true, new_rule, ""};
}

QuickRouteResult remove_quick_routing_rule(const std::string& raw_rule,
                                           std::optional<RuleType> target_type,
                                           const std::string& target_value) {
  std::vector<std::string> identity_keys;
  if (target_type.has_value() && !target_value.empty()) {
    identity_keys = build_target_identity_keys(*target_type, target_value);
  }
  if (trim_copy(raw_rule).empty() && identity_keys.empty()) {
    return {false, "", "Missing removal matcher"};
  }

  std::vector<std::string> removed_list;
  std::wstring merge_path = util::dirs::app_profiles_dir() + L"\\Merge.yaml";
  {
    std::lock_guard<std::mutex> lock(merge_file_mutex());
    auto load_res = util::load_yaml_file(merge_path);
    if (!load_res.is_ok() || !load_res.value().IsMap()) {
      return {false, "", "Merge.yaml not found"};
    }
    YAML::Node doc = load_res.value();
    std::vector<std::string> existing = read_merge_prepend_rules_from_node(doc);
    if (existing.empty()) {
      return {false, "", "No prepend-rules in Merge.yaml"};
    }
    auto [next_rules, removed] =
        remove_prepend_rules(existing, raw_rule, identity_keys);
    if (removed.empty()) {
      return {false, "", "Matching rule not found"};
    }
    removed_list = std::move(removed);
    doc["prepend-rules"] = next_rules;
    auto save_res = util::save_yaml_file(merge_path, doc);
    if (!save_res.is_ok()) {
      CWLOG_ERROR("Failed to save Merge.yaml: " + save_res.error().message);
      return {false, removed_list.front(), save_res.error().message};
    }
  }

  cmd::enhance_profiles_live();
  events::EventBus::notice_message("quick_rule_removed", removed_list.front());
  return {true, removed_list.front(), ""};
}

int add_quick_routing_rules(const std::string& raw_text) {
  std::vector<std::string> lines;
  size_t start = 0;
  for (size_t i = 0; i <= raw_text.size(); ++i) {
    if (i == raw_text.size() || raw_text[i] == '\n' || raw_text[i] == '\r' ||
        raw_text[i] == ',' || raw_text[i] == ';') {
      if (i > start) {
        std::string s = trim_copy(raw_text.substr(start, i - start));
        if (!s.empty()) lines.push_back(std::move(s));
      }
      start = i + 1;
    }
  }
  if (lines.empty()) return 0;

  std::vector<std::string> rules;
  for (const auto& line : lines) {
    std::string r = build_proxy_rule(line);
    if (!r.empty()) rules.push_back(std::move(r));
  }
  if (rules.empty()) return 0;

  int added_count = 0;
  std::wstring merge_path = util::dirs::app_profiles_dir() + L"\\Merge.yaml";
  {
    std::lock_guard<std::mutex> lock(merge_file_mutex());
    YAML::Node doc;
    auto load_res = util::load_yaml_file(merge_path);
    if (load_res.is_ok() && load_res.value().IsMap()) {
      doc = load_res.value();
    } else {
      doc = YAML::Node(YAML::NodeType::Map);
    }

    std::vector<std::string> current = read_merge_prepend_rules_from_node(doc);
    std::vector<std::string> added;
    std::unordered_set<std::string> seen_keys;
    for (const auto& rule : rules) {
      const std::string key = get_rule_identity_key(rule);
      const bool in_current =
          std::find(current.begin(), current.end(), rule) != current.end();
      if (!in_current && (key.empty() || seen_keys.count(key) == 0)) {
        if (!key.empty()) seen_keys.insert(key);
        added.push_back(rule);
      }
    }
    if (added.empty()) return 0;

    for (auto it = added.rbegin(); it != added.rend(); ++it) {
      current = upsert_prepend_rule(current, *it);
    }
    added_count = static_cast<int>(added.size());
    doc["prepend-rules"] = current;
    auto save_res = util::save_yaml_file(merge_path, doc);
    if (!save_res.is_ok()) return 0;
  }

  cmd::enhance_profiles_live();
  return added_count;
}

}  // namespace clashwin::core::quick_routing
