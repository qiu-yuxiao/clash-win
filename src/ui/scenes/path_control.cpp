#include "path_control.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "util/dirs.h"

namespace clashwin::ui::scene {

namespace qr = clashwin::core::quick_routing;

namespace {

std::string trim_copy(const std::string& s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

std::string lower_ascii(std::string s) {
  for (char& ch : s) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return s;
}

std::string upper_ascii(std::string s) {
  for (char& ch : s) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return s;
}

bool parse_two_digits(const char* p, int& out) {
  if (!std::isdigit(static_cast<unsigned char>(p[0])) ||
      !std::isdigit(static_cast<unsigned char>(p[1]))) {
    return false;
  }
  out = (p[0] - '0') * 10 + (p[1] - '0');
  return true;
}

bool parse_four_digits(const char* p, int& out) {
  int hi = 0;
  int lo = 0;
  if (!parse_two_digits(p, hi) || !parse_two_digits(p + 2, lo)) return false;
  out = hi * 100 + lo;
  return true;
}

bool is_ipv4_only(const std::string& norm) {
  return qr::is_ip_address(norm) && !qr::is_ipv6_address(norm);
}

std::optional<ConnectionTarget> target_from_domain_or_ip(const std::string& raw) {
  std::string norm = qr::normalize_quick_target_host(raw);
  if (norm.empty()) return std::nullopt;
  if (qr::is_ipv6_address(norm)) {
    return ConnectionTarget{qr::RuleType::Domain, norm, "ipv6:" + norm};
  }
  if (qr::is_ip_address(norm)) {
    return ConnectionTarget{qr::RuleType::Domain, norm, "ipv4:" + norm};
  }
  return ConnectionTarget{qr::RuleType::Domain, norm, "domain:" + norm};
}

std::string join_chains(const std::vector<std::string>& chains) {
  std::string out;
  for (const auto& c : chains) {
    if (c.empty()) continue;
    if (!out.empty()) out += " / ";
    out += c;
  }
  return out;
}

nlohmann::json metadata_to_json(const ConnMetadata& m) {
  return nlohmann::json{
      {"network", m.network},
      {"type", m.type},
      {"host", m.host},
      {"source_ip", m.source_ip},
      {"source_port", m.source_port},
      {"destination_port", m.destination_port},
      {"destination_ip", m.destination_ip},
      {"remote_destination", m.remote_destination},
      {"process", m.process},
      {"process_path", m.process_path},
  };
}

ConnMetadata metadata_from_json(const nlohmann::json& j) {
  ConnMetadata m;
  if (!j.is_object()) return m;
  m.network = j.value("network", "");
  m.type = j.value("type", "");
  m.host = j.value("host", "");
  m.source_ip = j.value("source_ip", "");
  m.source_port = j.value("source_port", "");
  m.destination_port = j.value("destination_port", "");
  m.destination_ip = j.value("destination_ip", "");
  m.remote_destination = j.value("remote_destination", "");
  m.process = j.value("process", "");
  m.process_path = j.value("process_path", "");
  return m;
}

std::optional<qr::ManagedMergeRule> match_disconnected_record_rule(
    const DisconnectedTargetRecord& rec,
    const std::vector<qr::ManagedMergeRule>& managed_rules) {
  ConnItem probe;
  probe.metadata = rec.metadata;
  if (probe.metadata.host.empty() && rec.target_type == qr::RuleType::Domain &&
      !qr::is_ip_address(rec.target_value)) {
    probe.metadata.host = rec.target_value;
  }
  if (probe.metadata.destination_ip.empty() && rec.target_type == qr::RuleType::Domain &&
      qr::is_ip_address(rec.target_value)) {
    probe.metadata.destination_ip = rec.target_value;
  }
  if (probe.metadata.process.empty() && rec.target_type == qr::RuleType::Process) {
    probe.metadata.process = rec.target_value;
  }
  return find_matching_managed_rule(probe, managed_rules);
}

ConnItem make_synthetic_disconnected_row(
    const DisconnectedTargetRecord& rec,
    const std::optional<qr::ManagedMergeRule>& matched_rule) {
  ConnItem row;
  row.id = "synthetic:disconnected:" + rec.key;
  row.is_synthetic = true;
  row.disconnected_key = rec.key;
  row.target_type = rec.target_type;
  row.target_value = rec.target_value;
  row.target_key = rec.key;
  row.metadata = rec.metadata;
  if (row.metadata.network.empty()) {
    row.metadata.network = !rec.network.empty() ? rec.network : "tcp";
  }
  if (row.metadata.type.empty()) row.metadata.type = "Disconnected";
  row.network = row.metadata.network;
  row.chains_cat = !rec.chains_cat.empty() ? rec.chains_cat : "Proxy";
  row.chains = {"DISCONNECTED"};
  row.rule = matched_rule ? matched_rule->rule_type : std::string("DISCONNECTED");
  row.rule_payload = matched_rule ? matched_rule->value : rec.target_value;
  row.host = !rec.host.empty() ? rec.host : format_path_control_host_label(row);
  row.managed_rule = matched_rule;
  row.control_status = PathControlStatus::Disconnected;
  return row;
}

ConnItem make_synthetic_managed_rule_row(const qr::ManagedMergeRule& rule) {
  ConnItem row;
  row.id = "synthetic:rule:" + rule.raw;
  row.is_synthetic = true;
  row.managed_rule = rule;
  row.network = "RULE";
  row.metadata.network = "RULE";
  row.metadata.type = rule.rule_type;
  if (rule.rule_type == "DOMAIN" || rule.rule_type == "DOMAIN-SUFFIX") {
    row.metadata.host = rule.match_key;
  } else if (rule.rule_type == "IP-CIDR" || rule.rule_type == "IP-CIDR6") {
    row.metadata.destination_ip = rule.match_key;
  } else if (rule.rule_type == "PROCESS-NAME") {
    row.metadata.process = rule.value;
  }

  std::optional<ConnectionTarget> target;
  if (rule.rule_type == "PROCESS-NAME") {
    target = ConnectionTarget{qr::RuleType::Process, rule.value,
                              "process:" + rule.match_key};
  } else {
    target = target_from_domain_or_ip(rule.match_key);
    if (!target.has_value()) {
      target = ConnectionTarget{qr::RuleType::Domain, rule.match_key,
                                extract_target_key_for_managed_rule(rule)};
    }
  }
  row.target_type = target->type;
  row.target_value = target->value;
  row.target_key = target->key;

  row.chains = {qr::rule_target_to_string(rule.target)};
  row.chains_cat = row.chains_category();
  row.rule = rule.rule_type;
  row.rule_payload = rule.value;
  switch (rule.target) {
    case qr::RuleTarget::Direct:
      row.control_status = PathControlStatus::Direct;
      break;
    case qr::RuleTarget::Reject:
      row.control_status = PathControlStatus::Reject;
      break;
    case qr::RuleTarget::Proxy:
    default:
      row.control_status = PathControlStatus::Proxy;
      break;
  }
  row.host = format_path_control_host_label(row);
  return row;
}

}  // namespace

int64_t parse_iso8601_utc_ms(const std::string& iso) {
  // Expected: YYYY-MM-DDTHH:MM:SS(.fraction)?(Z|[+-]HH:MM)
  if (iso.size() < 19) return 0;
  int year = 0, mon = 0, day = 0, hour = 0, min = 0, sec = 0;
  if (!parse_four_digits(iso.data(), year)) return 0;
  if (iso[4] != '-' || !parse_two_digits(iso.data() + 5, mon)) return 0;
  if (iso[7] != '-' || !parse_two_digits(iso.data() + 8, day)) return 0;
  if (iso[10] != 'T' && iso[10] != 't' && iso[10] != ' ') return 0;
  if (!parse_two_digits(iso.data() + 11, hour)) return 0;
  if (iso[13] != ':' || !parse_two_digits(iso.data() + 14, min)) return 0;
  if (iso[16] != ':' || !parse_two_digits(iso.data() + 17, sec)) return 0;

  std::tm tm_utc{};
  tm_utc.tm_year = year - 1900;
  tm_utc.tm_mon = mon - 1;
  tm_utc.tm_mday = day;
  tm_utc.tm_hour = hour;
  tm_utc.tm_min = min;
  tm_utc.tm_sec = sec;
  tm_utc.tm_isdst = 0;
  time_t epoch_sec = _mkgmtime(&tm_utc);
  if (epoch_sec < 0) return 0;

  size_t pos = 19;
  int64_t frac_ms = 0;
  if (pos < iso.size() && iso[pos] == '.') {
    ++pos;
    int scale = 100;
    while (pos < iso.size() && std::isdigit(static_cast<unsigned char>(iso[pos]))) {
      if (scale >= 1) {
        frac_ms += (iso[pos] - '0') * scale;
        scale /= 10;
      }
      ++pos;
    }
  }

  int64_t tz_offset_ms = 0;
  if (pos < iso.size()) {
    char tz = iso[pos];
    if (tz == '+' || tz == '-') {
      int sign = (tz == '+') ? 1 : -1;
      ++pos;
      int tz_h = 0, tz_m = 0;
      if (pos + 2 <= iso.size() && parse_two_digits(iso.data() + pos, tz_h)) {
        pos += 2;
        if (pos < iso.size() && iso[pos] == ':') ++pos;
        if (pos + 2 <= iso.size()) parse_two_digits(iso.data() + pos, tz_m);
        tz_offset_ms = static_cast<int64_t>(sign) * (tz_h * 3600LL + tz_m * 60LL) * 1000LL;
      }
    }
  }

  return static_cast<int64_t>(epoch_sec) * 1000LL + frac_ms - tz_offset_ms;
}

std::optional<ConnectionTarget> extract_connection_target(
    const ConnItem& item,
    const std::optional<qr::ManagedMergeRule>& fallback_rule) {
  std::string raw_host = trim_copy(item.metadata.host);
  if (!raw_host.empty()) {
    return target_from_domain_or_ip(raw_host);
  }

  std::string raw_ip = trim_copy(item.metadata.destination_ip);
  if (raw_ip.empty()) raw_ip = trim_copy(item.metadata.remote_destination);
  if (!raw_ip.empty()) {
    return target_from_domain_or_ip(raw_ip);
  }

  std::string proc = trim_copy(item.metadata.process);
  if (!proc.empty()) {
    return ConnectionTarget{qr::RuleType::Process, proc, "process:" + lower_ascii(proc)};
  }

  if (fallback_rule.has_value()) {
    if (fallback_rule->rule_type == "PROCESS-NAME") {
      return ConnectionTarget{qr::RuleType::Process, fallback_rule->value,
                              "process:" + fallback_rule->match_key};
    }
    return target_from_domain_or_ip(fallback_rule->match_key);
  }

  return std::nullopt;
}

bool matches_connection_target(const ConnItem& item, const ConnectionTarget& target) {
  if (target.type == qr::RuleType::Process) {
    std::string proc = lower_ascii(trim_copy(item.metadata.process));
    return !proc.empty() && proc == lower_ascii(trim_copy(target.value));
  }
  std::string host = qr::normalize_quick_target_host(item.metadata.host);
  if (target.key.rfind("domain:", 0) == 0) {
    return !host.empty() && host == target.value;
  }
  std::string dst_ip = qr::normalize_quick_target_host(
      !item.metadata.destination_ip.empty() ? item.metadata.destination_ip
                                           : item.metadata.remote_destination);
  return (!host.empty() && host == target.value) ||
         (!dst_ip.empty() && dst_ip == target.value);
}

std::string extract_target_key_for_managed_rule(const qr::ManagedMergeRule& rule) {
  if (rule.rule_type == "DOMAIN") return "domain:" + rule.match_key;
  if (rule.rule_type == "DOMAIN-SUFFIX") return "domain-suffix:" + rule.match_key;
  if (rule.rule_type == "IP-CIDR") {
    return is_ipv4_only(rule.match_key) ? ("ipv4:" + rule.match_key)
                                         : ("cidr4:" + rule.value);
  }
  if (rule.rule_type == "IP-CIDR6") {
    return qr::is_ipv6_address(rule.match_key) ? ("ipv6:" + rule.match_key)
                                                : ("cidr6:" + rule.value);
  }
  return "process:" + rule.match_key;
}

std::optional<qr::ManagedMergeRule> find_matching_managed_rule(
    const ConnItem& item,
    const std::vector<qr::ManagedMergeRule>& rules) {
  if (rules.empty()) return std::nullopt;
  std::string host = qr::normalize_quick_target_host(item.metadata.host);
  std::string dst_ip = qr::normalize_quick_target_host(
      !item.metadata.destination_ip.empty() ? item.metadata.destination_ip
                                           : item.metadata.remote_destination);
  std::string proc = lower_ascii(trim_copy(item.metadata.process));

  for (const auto& rule : rules) {
    if (rule.rule_type == "DOMAIN") {
      if (!host.empty() && host == rule.match_key) {
        return rule;
      }
    } else if (rule.rule_type == "DOMAIN-SUFFIX") {
      if (!host.empty() && !rule.match_key.empty()) {
        if (host == rule.match_key) return rule;
        if (host.size() > rule.match_key.size() + 1 &&
            host[host.size() - rule.match_key.size() - 1] == '.' &&
            host.compare(host.size() - rule.match_key.size(), rule.match_key.size(),
                         rule.match_key) == 0) {
          return rule;
        }
      }
    } else if (rule.rule_type == "IP-CIDR" || rule.rule_type == "IP-CIDR6") {
      if ((!dst_ip.empty() && dst_ip == rule.match_key) ||
          (!host.empty() && host == rule.match_key)) {
        return rule;
      }
    } else if (rule.rule_type == "PROCESS-NAME") {
      if (!proc.empty() && proc == rule.match_key) {
        return rule;
      }
    }
  }
  return std::nullopt;
}

PathControlStatus resolve_connection_control_status(
    const ConnItem& item,
    const std::optional<qr::ManagedMergeRule>& managed_rule,
    bool is_disconnected) {
  if (is_disconnected) return PathControlStatus::Disconnected;
  if (item.is_synthetic) return item.control_status;

  for (const auto& ch : item.chains) {
    std::string up = upper_ascii(ch);
    if (up == "REJECT" || up == "REJECT-DROP") return PathControlStatus::Reject;
  }
  for (const auto& ch : item.chains) {
    if (upper_ascii(ch) == "DIRECT") return PathControlStatus::Direct;
  }

  const auto& rule_ref = managed_rule.has_value() ? managed_rule : item.managed_rule;
  if (rule_ref.has_value() && rule_ref->target == qr::RuleTarget::Reject) {
    return PathControlStatus::Reject;
  }
  return PathControlStatus::Proxy;
}

std::string path_control_status_key(PathControlStatus status) {
  switch (status) {
    case PathControlStatus::Direct:
      return "Direct";
    case PathControlStatus::Reject:
      return "Reject";
    case PathControlStatus::Disconnected:
      return "Disconnected";
    case PathControlStatus::Proxy:
    default:
      return "Proxy";
  }
}

bool reconcile_disconnected_records(PathControlState& state,
                                    const std::vector<ConnItem>& active) {
  if (state.records.empty() || active.empty()) return false;
  std::unordered_map<std::string, int64_t> latest_active_start_by_key;
  for (const auto& conn : active) {
    auto target = extract_connection_target(conn);
    if (!target.has_value()) continue;
    int64_t start_ms = parse_iso8601_utc_ms(conn.start);
    if (start_ms <= 0) continue;
    auto it = latest_active_start_by_key.find(target->key);
    if (it == latest_active_start_by_key.end() || start_ms > it->second) {
      latest_active_start_by_key[target->key] = start_ms;
    }
  }

  bool changed = false;
  std::vector<DisconnectedTargetRecord> kept;
  kept.reserve(state.records.size());
  for (const auto& rec : state.records) {
    auto it = latest_active_start_by_key.find(rec.key);
    // src: path-control.ts:372 latestStart >= record.disconnectedAt - RECONNECT_CLOCK_SKEW_TOLERANCE_MS
    if (it != latest_active_start_by_key.end() &&
        it->second >= rec.disconnected_at_ms - kReconnectClockSkewToleranceMs) {
      changed = true;
      continue;
    }
    kept.push_back(rec);
  }
  if (changed) state.records = std::move(kept);
  return changed;
}

std::vector<ConnItem> compose_active_path_control_connections(
    const std::vector<ConnItem>& raw_active,
    const std::vector<qr::ManagedMergeRule>& managed_rules,
    const PathControlState& state) {
  std::unordered_set<std::string> disc_id_set(state.disconnected_ids.begin(),
                                              state.disconnected_ids.end());

  std::unordered_set<std::string> live_target_keys;
  std::unordered_set<std::string> live_matched_rule_raws;

  std::vector<ConnItem> out;
  out.reserve(raw_active.size() + state.records.size() + managed_rules.size());
  for (const auto& raw : raw_active) {
    ConnItem item = raw;
    auto target = extract_connection_target(item);
    if (target.has_value()) {
      item.target_type = target->type;
      item.target_value = target->value;
      item.target_key = target->key;
      live_target_keys.insert(target->key);
    }
    item.managed_rule = find_matching_managed_rule(item, managed_rules);
    if (item.managed_rule.has_value()) {
      live_matched_rule_raws.insert(item.managed_rule->raw);
    }
    bool is_disc = disc_id_set.count(item.id) > 0;
    item.control_status =
        resolve_connection_control_status(item, item.managed_rule, is_disc);
    out.push_back(std::move(item));
  }

  std::unordered_set<std::string> covered_keys = live_target_keys;
  std::unordered_set<std::string> covered_rule_raws = live_matched_rule_raws;

  for (const auto& rec : state.records) {
    if (live_target_keys.count(rec.key)) continue;
    auto matched_rule = match_disconnected_record_rule(rec, managed_rules);
    if (matched_rule.has_value()) {
      covered_rule_raws.insert(matched_rule->raw);
    }
    covered_keys.insert(rec.key);
    out.push_back(make_synthetic_disconnected_row(rec, matched_rule));
  }

  for (const auto& rule : managed_rules) {
    std::string rule_key = extract_target_key_for_managed_rule(rule);
    if (covered_rule_raws.count(rule.raw) || covered_keys.count(rule_key)) {
      continue;
    }
    covered_rule_raws.insert(rule.raw);
    covered_keys.insert(rule_key);
    out.push_back(make_synthetic_managed_rule_row(rule));
  }

  prioritize_path_control_connections(out);
  return out;
}

std::vector<ConnItem> enrich_closed_path_control_connections(
    const std::vector<ConnItem>& raw_closed,
    const std::vector<qr::ManagedMergeRule>& managed_rules,
    const PathControlState& state) {
  std::unordered_set<std::string> disc_id_set(state.disconnected_ids.begin(),
                                              state.disconnected_ids.end());

  std::vector<ConnItem> out;
  out.reserve(raw_closed.size());
  for (const auto& raw : raw_closed) {
    ConnItem item = raw;
    if (auto target = extract_connection_target(item)) {
      item.target_type = target->type;
      item.target_value = target->value;
      item.target_key = target->key;
    }
    item.managed_rule = find_matching_managed_rule(item, managed_rules);
    bool is_disc = disc_id_set.count(item.id) > 0;
    item.control_status =
        resolve_connection_control_status(item, item.managed_rule, is_disc);
    out.push_back(std::move(item));
  }
  return out;
}

std::string format_path_control_host_label(const ConnItem& item) {
  std::string host = trim_copy(item.metadata.host);
  if (!host.empty()) {
    if (item.is_synthetic && item.managed_rule &&
        item.managed_rule->rule_type == "DOMAIN-SUFFIX") {
      return "*." + host;
    }
    return host;
  }
  std::string dest_ip = trim_copy(item.metadata.remote_destination);
  if (dest_ip.empty()) dest_ip = trim_copy(item.metadata.destination_ip);
  if (!dest_ip.empty()) return dest_ip;
  std::string proc = trim_copy(item.metadata.process);
  return proc.empty() ? std::string("--") : proc;
}

void prioritize_path_control_connections(std::vector<ConnItem>& items) {
  std::vector<ConnItem> syn_disc;
  std::vector<ConnItem> syn_reject;
  std::vector<ConnItem> live_rows;
  std::vector<ConnItem> syn_idle;

  for (auto& row : items) {
    if (!row.is_synthetic) {
      live_rows.push_back(std::move(row));
    } else if (row.control_status == PathControlStatus::Disconnected) {
      syn_disc.push_back(std::move(row));
    } else if (row.control_status == PathControlStatus::Reject) {
      syn_reject.push_back(std::move(row));
    } else {
      syn_idle.push_back(std::move(row));
    }
  }

  items.clear();
  items.reserve(syn_disc.size() + syn_reject.size() + live_rows.size() + syn_idle.size());
  for (auto& r : syn_disc) items.push_back(std::move(r));
  for (auto& r : syn_reject) items.push_back(std::move(r));
  for (auto& r : live_rows) items.push_back(std::move(r));
  for (auto& r : syn_idle) items.push_back(std::move(r));
}

void PathControlStore::load_default() {
  std::wstring dir = util::dirs::app_home_dir();
  std::filesystem::path p(dir);
  load_from_file((p / L"path_control_state.json").wstring());
}

bool PathControlStore::load_from_file(const std::wstring& path) {
  path_ = path;
  state_ = {};
  std::ifstream ifs(std::filesystem::path(path_), std::ios::binary);
  if (!ifs.is_open()) return false;
  try {
    nlohmann::json j;
    ifs >> j;
    if (!j.is_object()) return false;
    if (j.contains("records") && j["records"].is_array()) {
      for (const auto& rj : j["records"]) {
        if (!rj.is_object()) continue;
        DisconnectedTargetRecord r;
        r.key = rj.value("key", "");
        r.target_value = rj.value("target_value", "");
        if (r.key.empty() || r.target_value.empty()) continue;
        std::string tt = rj.value("target_type", "domain");
        r.target_type = (tt == "process") ? qr::RuleType::Process : qr::RuleType::Domain;
        r.host = rj.value("host", "");
        r.chains_cat = rj.value("chains_cat", "");
        r.network = rj.value("network", "");
        if (rj.contains("metadata")) r.metadata = metadata_from_json(rj["metadata"]);
        r.chains_summary = rj.value("chains_summary", "");
        r.rule = rj.value("rule", "");
        r.rule_payload = rj.value("rule_payload", "");
        r.disconnected_at_ms = rj.value("disconnected_at_ms", int64_t{0});
        state_.records.push_back(std::move(r));
        if (state_.records.size() >= kMaxDisconnectedRecords) break;
      }
    }
    if (j.contains("disconnected_ids") && j["disconnected_ids"].is_array()) {
      for (const auto& idj : j["disconnected_ids"]) {
        if (!idj.is_string()) continue;
        std::string id = idj.get<std::string>();
        if (!id.empty()) state_.disconnected_ids.push_back(std::move(id));
        if (state_.disconnected_ids.size() >= kMaxDisconnectedIds) break;
      }
    }
    return true;
  } catch (...) {
    state_ = {};
    return false;
  }
}

bool PathControlStore::save() const {
  if (path_.empty()) return false;
  try {
    std::filesystem::path p(path_);
    if (p.has_parent_path()) {
      std::error_code ec;
      std::filesystem::create_directories(p.parent_path(), ec);
    }
    nlohmann::json recs = nlohmann::json::array();
    for (const auto& r : state_.records) {
      recs.push_back(nlohmann::json{
          {"key", r.key},
          {"target_type", r.target_type == qr::RuleType::Process ? "process" : "domain"},
          {"target_value", r.target_value},
          {"host", r.host},
          {"chains_cat", r.chains_cat},
          {"network", r.network},
          {"metadata", metadata_to_json(r.metadata)},
          {"chains_summary", r.chains_summary},
          {"rule", r.rule},
          {"rule_payload", r.rule_payload},
          {"disconnected_at_ms", r.disconnected_at_ms},
      });
    }
    nlohmann::json root = {
        {"records", std::move(recs)},
        {"disconnected_ids", state_.disconnected_ids},
    };
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs << root.dump(2);
    return ofs.good();
  } catch (...) {
    return false;
  }
}

void PathControlStore::mark_disconnected(const ConnItem& item, int64_t now_ms) {
  if (!item.id.empty() && item.id.rfind("synthetic:", 0) != 0) {
    auto it = std::remove(state_.disconnected_ids.begin(),
                          state_.disconnected_ids.end(), item.id);
    state_.disconnected_ids.erase(it, state_.disconnected_ids.end());
    state_.disconnected_ids.insert(state_.disconnected_ids.begin(), item.id);
    if (state_.disconnected_ids.size() > kMaxDisconnectedIds) {
      state_.disconnected_ids.resize(kMaxDisconnectedIds);
    }
  }

  auto target = extract_connection_target(item, item.managed_rule);
  if (!target.has_value()) {
    save();
    return;
  }

  auto it = std::remove_if(state_.records.begin(), state_.records.end(),
                           [&](const DisconnectedTargetRecord& r) {
                             return r.key == target->key;
                           });
  state_.records.erase(it, state_.records.end());

  DisconnectedTargetRecord rec;
  rec.key = target->key;
  rec.target_type = target->type;
  rec.target_value = target->value;
  rec.host = !item.metadata.host.empty() ? item.metadata.host
                                         : format_path_control_host_label(item);
  rec.chains_cat = item.chains_cat;
  rec.network = item.network;
  rec.metadata = item.metadata;
  rec.chains_summary = join_chains(item.chains);
  rec.rule = item.rule;
  rec.rule_payload = item.rule_payload;
  rec.disconnected_at_ms = now_ms;
  state_.records.insert(state_.records.begin(), std::move(rec));
  if (state_.records.size() > kMaxDisconnectedRecords) {
    state_.records.resize(kMaxDisconnectedRecords);
  }
  save();
}

bool PathControlStore::clear_disconnected_target(const std::string& target_key,
                                                 const std::string& conn_id) {
  bool changed = false;
  if (!target_key.empty()) {
    size_t before = state_.records.size();
    state_.records.erase(
        std::remove_if(state_.records.begin(), state_.records.end(),
                       [&](const DisconnectedTargetRecord& r) {
                         return r.key == target_key;
                       }),
        state_.records.end());
    if (state_.records.size() != before) changed = true;
  }
  if (!conn_id.empty()) {
    size_t before = state_.disconnected_ids.size();
    state_.disconnected_ids.erase(
        std::remove(state_.disconnected_ids.begin(),
                    state_.disconnected_ids.end(), conn_id),
        state_.disconnected_ids.end());
    if (state_.disconnected_ids.size() != before) changed = true;
  }
  if (changed) save();
  return changed;
}

void PathControlStore::clear_all() {
  state_.records.clear();
  state_.disconnected_ids.clear();
  save();
}

bool PathControlStore::reconcile(const std::vector<ConnItem>& active) {
  if (!reconcile_disconnected_records(state_, active)) return false;
  save();
  return true;
}

}  // namespace clashwin::ui::scene
