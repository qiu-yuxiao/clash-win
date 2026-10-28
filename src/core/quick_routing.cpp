#include "core/quick_routing.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "cmd/cmd_support.h"
#include "events/event_bus.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/str_utils.h"
#include "util/utf.h"
#include "util/yaml_utils.h"

namespace clashwin::core::quick_routing {

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
  std::string upper = target_str;
  for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (upper == "PROXY") return RuleTarget::Proxy;
  if (upper == "REJECT") return RuleTarget::Reject;
  return RuleTarget::Direct;
}

bool is_ip_address(const std::string& str) {
  if (str.empty()) return false;
  // Check IPv4 pattern: 4 numbers separated by 3 dots
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
  if (str.find(':') == std::string::npos) return false;
  for (char c : str) {
    if (c != ':' && !std::isxdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

std::string extract_domain_suffix(const std::string& domain_or_ip) {
  std::string value = domain_or_ip;
  // Trim
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
  if (value.empty()) return "";

  // Strip scheme prefix (e.g. http://, https://)
  auto scheme_pos = value.find("://");
  if (scheme_pos != std::string::npos) {
    value = value.substr(scheme_pos + 3);
  }

  // Strip path (/), query (?), hash (#)
  auto path_pos = value.find_first_of("/?#");
  if (path_pos != std::string::npos) {
    value = value.substr(0, path_pos);
  }

  // Strip port (e.g. :443) only if not IPv6
  if (value.find(':') != std::string::npos && !is_ipv6_address(value)) {
    auto port_pos = value.find(':');
    value = value.substr(0, port_pos);
  }

  // Strip leading "www."
  if (value.rfind("www.", 0) == 0) {
    value = value.substr(4);
  }

  // If IP address, return as is
  if (is_ip_address(value) || is_ipv6_address(value)) {
    return value;
  }

  // Split by '.'
  std::vector<std::string> parts;
  size_t start = 0;
  for (size_t i = 0; i <= value.size(); ++i) {
    if (i == value.size() || value[i] == '.') {
      if (i > start) {
        parts.push_back(value.substr(start, i - start));
      }
      start = i + 1;
    }
  }

  if (parts.size() < 2) return value;

  std::string last = parts.back();
  std::string second_last = parts[parts.size() - 2];
  for (auto& c : last) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (auto& c : second_last) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  static const char* double_tlds[] = {"com", "org", "net", "gov", "edu", "co"};
  bool is_double_tld = false;
  for (const char* tld : double_tlds) {
    if (second_last == tld && last.length() <= 3) {
      is_double_tld = true;
      break;
    }
  }

  if (parts.size() >= 3 && is_double_tld) {
    return parts[parts.size() - 3] + "." + parts[parts.size() - 2] + "." + parts.back();
  }

  return parts[parts.size() - 2] + "." + parts.back();
}

std::string build_rule_string(RuleType type, const std::string& value, RuleTarget target) {
  std::string target_str = rule_target_to_string(target);
  if (type == RuleType::Process) {
    return "PROCESS-NAME," + value + "," + target_str;
  }

  // Domain or IP
  if (is_ipv6_address(value)) {
    return "IP-CIDR," + value + "/128," + target_str;
  }
  if (is_ip_address(value)) {
    return "IP-CIDR," + value + "/32," + target_str;
  }
  return "DOMAIN-SUFFIX," + extract_domain_suffix(value) + "," + target_str;
}

QuickRouteResult add_quick_routing_rule(RuleType type, const std::string& value, RuleTarget target) {
  if (value.empty()) {
    return {false, "", "Empty rule target value"};
  }

  std::string new_rule = build_rule_string(type, value, target);

  std::string match_prefix;
  if (type == RuleType::Process) {
    match_prefix = "PROCESS-NAME," + value + ",";
  } else {
    if (is_ipv6_address(value)) {
      match_prefix = "IP-CIDR," + value + "/128,";
    } else if (is_ip_address(value)) {
      match_prefix = "IP-CIDR," + value + "/32,";
    } else {
      match_prefix = "DOMAIN-SUFFIX," + extract_domain_suffix(value) + ",";
    }
  }

  std::wstring merge_path = util::dirs::app_profiles_dir() + L"\\Merge.yaml";

  YAML::Node doc;
  auto load_res = util::load_yaml_file(merge_path);
  if (load_res.is_ok() && load_res.value().IsMap()) {
    doc = load_res.value();
  } else {
    doc = YAML::Node(YAML::NodeType::Map);
  }

  std::vector<std::string> rules;
  rules.push_back(new_rule);

  if (doc["prepend-rules"] && doc["prepend-rules"].IsSequence()) {
    for (const auto& item : doc["prepend-rules"]) {
      std::string r = item.as<std::string>("");
      if (!r.empty() && r != new_rule) {
        if (match_prefix.empty() || r.rfind(match_prefix, 0) != 0) {
          rules.push_back(r);
        }
      }
    }
  }

  doc["prepend-rules"] = rules;

  auto save_res = util::save_yaml_file(merge_path, doc);
  if (!save_res.is_ok()) {
    CWLOG_ERROR("Failed to save Merge.yaml: " + save_res.error().message);
    return {false, new_rule, save_res.error().message};
  }

  // Trigger enhance pipeline to apply changes
  cmd::run_enhance_pipeline();

  // Send EventBus notification
  events::EventBus::notice_message("profile_switched", "手动路径控制规则已置顶生效: " + new_rule);

  return {true, new_rule, ""};
}

int add_quick_routing_rules(const std::string& raw_text) {
  std::vector<std::string> lines;
  size_t start = 0;
  for (size_t i = 0; i <= raw_text.size(); ++i) {
    if (i == raw_text.size() || raw_text[i] == '\n' || raw_text[i] == '\r' || raw_text[i] == ',' || raw_text[i] == ';') {
      if (i > start) {
        std::string s = raw_text.substr(start, i - start);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        if (!s.empty()) lines.push_back(s);
      }
      start = i + 1;
    }
  }

  if (lines.empty()) return 0;

  std::vector<std::string> new_rules;
  for (const auto& line : lines) {
    std::string rule = build_rule_string(RuleType::Domain, line, RuleTarget::Proxy);
    if (!rule.empty() && std::find(new_rules.begin(), new_rules.end(), rule) == new_rules.end()) {
      new_rules.push_back(rule);
    }
  }

  if (new_rules.empty()) return 0;

  std::wstring merge_path = util::dirs::app_profiles_dir() + L"\\Merge.yaml";
  YAML::Node doc;
  auto load_res = util::load_yaml_file(merge_path);
  if (load_res.is_ok() && load_res.value().IsMap()) {
    doc = load_res.value();
  } else {
    doc = YAML::Node(YAML::NodeType::Map);
  }

  std::vector<std::string> existing;
  if (doc["prepend-rules"] && doc["prepend-rules"].IsSequence()) {
    for (const auto& item : doc["prepend-rules"]) {
      std::string r = item.as<std::string>("");
      if (!r.empty()) existing.push_back(r);
    }
  }

  std::vector<std::string> to_add;
  for (const auto& nr : new_rules) {
    if (std::find(existing.begin(), existing.end(), nr) == existing.end()) {
      to_add.push_back(nr);
    }
  }

  if (to_add.empty()) return 0;

  std::vector<std::string> combined = to_add;
  combined.insert(combined.end(), existing.begin(), existing.end());
  doc["prepend-rules"] = combined;

  auto save_res = util::save_yaml_file(merge_path, doc);
  if (!save_res.is_ok()) return 0;

  cmd::run_enhance_pipeline();
  return static_cast<int>(to_add.size());
}

}  // namespace clashwin::core::quick_routing
