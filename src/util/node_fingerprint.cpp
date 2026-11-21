// 节点端点指纹计算与订阅克隆拓扑连坐去重
// （拆分自 src/util/node.cpp；← ClashVerge src-tauri/src/utils/node/fingerprint.rs）
#include "util/node.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdlib>

#include "util/str_utils.h"

#pragma comment(lib, "bcrypt.lib")

namespace clashwin::util {

namespace {

bool is_yaml_bool_scalar(const YAML::Node& v, bool& out_bool) {
  if (!v.IsScalar() || v.IsNull() || v.Tag() == "!") return false;
  const std::string& s = v.Scalar();
  if (s == "true" || s == "True" || s == "TRUE") {
    out_bool = true;
    return true;
  }
  if (s == "false" || s == "False" || s == "FALSE") {
    out_bool = false;
    return true;
  }
  return false;
}

bool is_yaml_number_scalar(const YAML::Node& v) {
  if (!v.IsScalar() || v.IsNull() || v.Tag() == "!") return false;
  const std::string& s = v.Scalar();
  if (s.empty()) return false;
  size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
  if (i >= s.size()) return false;
  bool seen_digit = false;
  bool seen_dot = false;
  for (; i < s.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c >= '0' && c <= '9') {
      seen_digit = true;
    } else if (c == '.' && !seen_dot) {
      seen_dot = true;
    } else {
      return false;
    }
  }
  return seen_digit;
}

void write_canonical_yaml(std::string& out, const YAML::Node& val) {
  if (!val.IsDefined() || val.IsNull()) {
    out.append("null");
    return;
  }
  if (val.IsScalar()) {
    const std::string& s = val.Scalar();
    bool b = false;
    if (is_yaml_bool_scalar(val, b)) {
      out.append(b ? "true" : "false");
    } else if (is_yaml_number_scalar(val)) {
      out.append("n:");
      out.append(s);
    } else {
      out.append("s:");
      out.append(std::to_string(s.size()));
      out.push_back(':');
      out.append(s);
    }
    return;
  }
  if (val.IsSequence()) {
    out.push_back('[');
    for (size_t idx = 0; idx < val.size(); ++idx) {
      if (idx > 0) out.push_back(',');
      write_canonical_yaml(out, val[idx]);
    }
    out.push_back(']');
    return;
  }
  if (val.IsMap()) {
    std::vector<std::pair<std::string, std::string>> entries;
    entries.reserve(val.size());
    for (const auto& kv : val) {
      std::string ks;
      std::string vs;
      write_canonical_yaml(ks, kv.first);
      write_canonical_yaml(vs, kv.second);
      entries.emplace_back(std::move(ks), std::move(vs));
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    out.push_back('{');
    for (size_t idx = 0; idx < entries.size(); ++idx) {
      if (idx > 0) out.push_back(',');
      out.append(entries[idx].first);
      out.push_back(':');
      out.append(entries[idx].second);
    }
    out.push_back('}');
  }
}

std::string sha256_hex(const std::string& data) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::string hex;
  do {
    if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(
            &alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
      break;
    }
    DWORD obj_len = 0;
    DWORD cb = 0;
    if (!BCRYPT_SUCCESS(::BCryptGetProperty(
            alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj_len),
            sizeof(obj_len), &cb, 0))) {
      break;
    }
    std::vector<UCHAR> obj(obj_len);
    if (!BCRYPT_SUCCESS(::BCryptCreateHash(
            alg, &hash, obj.data(), obj_len, nullptr, 0, 0))) {
      break;
    }
    if (!data.empty()) {
      if (!BCRYPT_SUCCESS(::BCryptHashData(
              hash,
              const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(data.data())),
              static_cast<ULONG>(data.size()), 0))) {
        break;
      }
    }
    UCHAR digest[32]{};
    if (!BCRYPT_SUCCESS(::BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
      break;
    }
    static constexpr char kHex[] = "0123456789abcdef";
    hex.reserve(64);
    for (UCHAR b : digest) {
      hex.push_back(kHex[b >> 4]);
      hex.push_back(kHex[b & 0x0F]);
    }
  } while (false);
  if (hash) ::BCryptDestroyHash(hash);
  if (alg) ::BCryptCloseAlgorithmProvider(alg, 0);
  return hex;
}

bool is_preferred_clone_name(const std::string& name) {
  return name.find(':') == std::string::npos &&
         name.find("：") == std::string::npos;
}

uint8_t clone_name_rank(const std::string& name) {
  const bool region = has_region_hint(name);
  const bool no_colon = is_preferred_clone_name(name);
  if (region && no_colon) return 3;
  if (region) return 2;
  if (no_colon) return 1;
  return 0;
}

}  // namespace

std::optional<std::string> proxy_endpoint_fingerprint(const YAML::Node& mapping) {
  if (!mapping || !mapping.IsMap()) return std::nullopt;
  const YAML::Node server = static_cast<const YAML::Node&>(mapping)["server"];
  const bool server_ok =
      server && server.IsScalar() && !server.IsNull() && !trim(server.Scalar()).empty();
  const YAML::Node port = static_cast<const YAML::Node&>(mapping)["port"];
  bool port_ok = false;
  if (port && port.IsScalar() && !port.IsNull()) {
    const std::string& ps = trim(port.Scalar());
    if (port.Tag() != "!" && is_yaml_number_scalar(port)) {
      char* endp = nullptr;
      long long pv = std::strtoll(ps.c_str(), &endp, 10);
      if (endp == ps.data() + ps.size() && pv > 0) port_ok = true;
    }
    if (!port_ok && port.Tag() == "!" && !ps.empty()) {
      port_ok = true;
    } else if (!port_ok && !is_yaml_number_scalar(port) && !ps.empty()) {
      port_ok = true;
    }
  }
  if (!server_ok || !port_ok) return std::nullopt;

  std::vector<std::pair<std::string, std::string>> entries;
  entries.reserve(mapping.size());
  for (const auto& kv : mapping) {
    if (kv.first.IsScalar() && !kv.first.IsNull() &&
        kv.first.Scalar() == "name") {
      continue;
    }
    std::string ks;
    std::string vs;
    write_canonical_yaml(ks, kv.first);
    write_canonical_yaml(vs, kv.second);
    entries.emplace_back(std::move(ks), std::move(vs));
  }
  std::sort(entries.begin(), entries.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  std::string canonical;
  for (const auto& [k, v] : entries) {
    canonical.append(k);
    canonical.push_back('=');
    canonical.append(v);
    canonical.push_back(';');
  }
  std::string hex = sha256_hex(canonical);
  if (hex.empty()) return std::nullopt;
  return hex;
}

std::pair<std::vector<std::string>, size_t> deduplicate_cloned_proxies(
    const std::vector<std::pair<std::string, std::optional<std::string>>>& entries) {
  std::vector<std::string> names;
  names.reserve(entries.size());
  std::unordered_map<std::string, size_t> fp_to_idx;
  size_t removed = 0;

  for (const auto& [name, fp_opt] : entries) {
    if (!fp_opt.has_value()) {
      names.push_back(name);
      continue;
    }
    auto it = fp_to_idx.find(*fp_opt);
    if (it != fp_to_idx.end()) {
      ++removed;
      if (clone_name_rank(name) > clone_name_rank(names[it->second])) {
        names[it->second] = name;
      }
    } else {
      fp_to_idx.emplace(*fp_opt, names.size());
      names.push_back(name);
    }
  }
  return {std::move(names), removed};
}

ProxyTopologyOutcome resolve_proxy_clone_topology(
    const std::vector<RawProxyEntry>& entries) {
  struct FpGroupStats {
    size_t total = 0;
    bool has_dummy = false;
    bool has_regional_non_dummy = false;
    bool all_colon = true;
  };
  std::unordered_map<std::string, FpGroupStats> group_stats;
  for (const auto& entry : entries) {
    if (!entry.fingerprint.has_value()) continue;
    auto& stats = group_stats[*entry.fingerprint];
    ++stats.total;
    if (entry.is_dummy) {
      stats.has_dummy = true;
    } else if (has_region_hint(entry.name)) {
      stats.has_regional_non_dummy = true;
    }
    if (is_preferred_clone_name(entry.name)) {
      stats.all_colon = false;
    }
  }

  std::unordered_set<std::string> tainted_fps;
  for (const auto& [fp, s] : group_stats) {
    if (s.total >= 2 && !s.has_regional_non_dummy &&
        (s.has_dummy || s.all_colon)) {
      tainted_fps.insert(fp);
    }
  }

  size_t tainted_banner_removed = 0;
  std::vector<std::pair<std::string, std::optional<std::string>>> clean_entries;
  clean_entries.reserve(entries.size());
  for (const auto& entry : entries) {
    if (entry.fingerprint.has_value() &&
        tainted_fps.count(*entry.fingerprint) > 0) {
      if (!entry.is_dummy) ++tainted_banner_removed;
      continue;
    }
    if (entry.is_dummy) continue;
    clean_entries.emplace_back(entry.name, entry.fingerprint);
  }

  auto [names, clone_removed] = deduplicate_cloned_proxies(clean_entries);
  ProxyTopologyOutcome out;
  out.names = std::move(names);
  out.clone_removed = clone_removed;
  out.tainted_banner_removed = tainted_banner_removed;
  out.tainted_fps = std::move(tainted_fps);
  return out;
}

}  // namespace clashwin::util
