#include "util/node.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string_view>

#include <nlohmann/json.hpp>

#include "util/dirs.h"
#include "util/log.h"
#include "util/str_utils.h"
#include "util/utf.h"

#pragma comment(lib, "bcrypt.lib")

namespace clashwin::util {

namespace {

constexpr const wchar_t* kVerifyStoreFile = L"node_https_verify.json";

std::shared_mutex& verify_store_mu() {
  static std::shared_mutex mu;
  return mu;
}

std::optional<NodeVerifyStoreData>& verify_store_slot() {
  static std::optional<NodeVerifyStoreData> slot;
  return slot;
}

std::shared_mutex& topology_mu() {
  static std::shared_mutex mu;
  return mu;
}

std::optional<std::unordered_map<std::string, std::string>>& runtime_fps_slot() {
  static std::optional<std::unordered_map<std::string, std::string>> slot;
  return slot;
}

std::optional<std::unordered_set<std::string>>& runtime_tainted_slot() {
  static std::optional<std::unordered_set<std::string>> slot;
  return slot;
}

std::mutex& test_cfg_mu() {
  static std::mutex mu;
  return mu;
}

bool g_disable_disk_persistence_for_test = false;
std::optional<std::wstring> g_test_verify_store_path;

int64_t current_unix_ts() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::optional<std::wstring> resolve_verify_store_path() {
  {
    std::lock_guard<std::mutex> lock(test_cfg_mu());
    if (g_test_verify_store_path.has_value()) {
      return g_test_verify_store_path;
    }
    if (g_disable_disk_persistence_for_test) {
      return std::nullopt;
    }
  }
  const std::wstring home = dirs::app_home_dir();
  if (home.empty()) return std::nullopt;
  return home + L"\\" + kVerifyStoreFile;
}

bool is_ascii_alpha_byte(unsigned char b) {
  return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z');
}

bool is_ascii_digit_byte(unsigned char b) { return b >= '0' && b <= '9'; }

bool is_ascii_lower_byte(unsigned char b) { return b >= 'a' && b <= 'z'; }

bool is_ascii_lower_or_digit(unsigned char b) {
  return is_ascii_lower_byte(b) || is_ascii_digit_byte(b);
}

bool is_code_context_byte(std::optional<unsigned char> b) {
  if (!b.has_value()) return false;
  unsigned char c = *b;
  return c == '-' || c == '_' || c == '[' || c == ']' || is_ascii_digit_byte(c);
}

bool is_ascii_ws(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

std::string ltrim_ascii_ws(std::string_view sv) {
  size_t i = 0;
  while (i < sv.size() && is_ascii_ws(static_cast<unsigned char>(sv[i]))) ++i;
  return std::string(sv.substr(i));
}

std::vector<std::string> split_keep_empty(std::string_view s, char delim) {
  std::vector<std::string> parts;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == delim) {
      parts.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return parts;
}

std::vector<std::string> split_ascii_whitespace(std::string_view s) {
  std::vector<std::string> parts;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && is_ascii_ws(static_cast<unsigned char>(s[i]))) ++i;
    if (i >= s.size()) break;
    size_t j = i;
    while (j < s.size() && !is_ascii_ws(static_cast<unsigned char>(s[j]))) ++j;
    parts.emplace_back(s.substr(i, j - i));
    i = j;
  }
  return parts;
}

// ← node.rs:61-157 REGION_SUBSTRINGS_CJK（96 条，一字不差）
constexpr std::string_view kRegionSubstringsCjk[] = {
    "香港",     "台湾",       "台灣",       "澳门",     "澳門",     "日本",
    "新加坡",   "狮城",       "美国",       "美國",     "韩国",     "韓國",
    "首尔",     "东京",       "大阪",       "硅谷",     "洛杉矶",   "西雅图",
    "圣何塞",   "纽约",       "英国",       "英國",     "伦敦",     "德国",
    "德國",     "法兰克福",   "法国",       "法國",     "巴黎",     "荷兰",
    "荷蘭",     "阿姆斯特丹", "俄罗斯",     "俄羅斯",   "莫斯科",   "加拿大",
    "多伦多",   "温哥华",     "澳洲",       "澳大利亚", "悉尼",     "墨尔本",
    "新西兰",   "印度",       "孟买",       "印尼",     "雅加达",   "马来西亚",
    "吉隆坡",   "泰国",       "曼谷",       "越南",     "菲律宾",   "土耳其",
    "伊斯坦布尔", "阿根廷",   "巴西",       "圣保罗",   "智利",     "墨西哥",
    "南非",     "埃及",       "阿联酋",     "迪拜",     "以色列",   "乌克兰",
    "波兰",     "瑞典",       "瑞士",       "苏黎世",   "意大利",   "西班牙",
    "爱尔兰",   "芬兰",       "挪威",       "丹麦",     "奥地利",   "比利时",
    "捷克",     "匈牙利",     "罗马尼亚",   "卢森堡",   "冰岛",     "葡萄牙",
    "希腊",     "中国",       "上海",       "北京",     "广州",     "深圳",
    "欧洲",     "北美",       "南美",       "亚太",     "hong kong",
};

// ← node.rs:159-269 REGION_ASCII_TOKENS
constexpr std::string_view kRegionAsciiTokens[] = {
    "hk",      "hkg",       "hongkong",  "tw",        "tpe",     "taiwan",
    "mo",      "mac",       "jp",        "jpn",       "japan",   "tyo",
    "osa",     "nrt",       "kix",       "tokyo",     "osaka",   "sg",
    "sgp",     "sin",       "singapore", "usa",       "america", "lax",
    "sjc",     "sea",       "ord",       "ewr",       "nyc",     "iad",
    "dfw",     "kr",        "kor",       "korea",     "icn",     "sel",
    "seoul",   "uk",        "gbr",       "britain",   "england", "lon",
    "london",  "de",        "deu",       "germany",   "fra",     "frankfurt",
    "fr",      "france",    "par",       "paris",     "nl",      "nld",
    "netherlands", "ams",   "amsterdam", "ru",        "rus",     "russia",
    "mow",     "moscow",    "ca",        "can",       "canada",  "yyz",
    "yvr",     "toronto",   "au",        "aus",       "australia", "syd",
    "sydney",  "mel",       "ind",       "india",     "bom",     "idn",
    "indonesia", "cgk",     "mys",       "malaysia",  "kul",     "th",
    "tha",     "thailand",  "bkk",       "vn",        "vnm",     "vietnam",
    "ph",      "phl",       "philippines", "mnl",     "tr",      "tur",
    "turkey",  "ist",       "arg",       "argentina", "bra",     "brazil",
    "gru",     "uae",       "dxb",       "dubai",     "bgp",     "iplc",
    "iepl",
};

// ← node.rs:271-275 BANNER_DOMAIN_TLDS
constexpr std::string_view kBannerDomainTlds[] = {
    "com",  "net",   "org",  "xyz",  "top", "vip", "cc",  "io",   "me",
    "club", "link",  "site", "shop", "info", "icu", "cyou", "online", "store",
    "tech", "app",   "dev",  "cloud", "pw",  "co",  "tv",  "im",   "la",
    "pro",  "ltd",   "work", "fun",  "live", "ink", "buzz", "click", "cn",
    "hk",   "tw",    "jp",   "sg",   "uk",   "us",  "ru",  "kr",   "de",
};

bool contains_ascii_token(std::string_view token) {
  for (const auto kw : kRegionAsciiTokens) {
    if (kw == token) return true;
  }
  return false;
}

bool contains_banner_tld(std::string_view tld) {
  for (const auto t : kBannerDomainTlds) {
    if (t == tld) return true;
  }
  return false;
}

// ← node.rs:333-380
bool has_embedded_banner_domain(const std::string& name, const std::string& lower) {
  bool has_colon = name.find(':') != std::string::npos ||
                   name.find("：") != std::string::npos;
  bool is_ascii = true;
  for (unsigned char c : name) {
    if (c >= 0x80) {
      is_ascii = false;
      break;
    }
  }
  if (!(has_colon || !is_ascii)) return false;

  const size_t n = lower.size();
  for (size_t idx = 0; idx < n; ++idx) {
    if (lower[idx] != '.' || idx < 2 || idx + 2 >= n) continue;
    unsigned char prev = static_cast<unsigned char>(lower[idx - 1]);
    if (!is_ascii_lower_or_digit(prev)) continue;

    size_t l = idx;
    while (l > 0) {
      unsigned char c = static_cast<unsigned char>(lower[l - 1]);
      if (is_ascii_lower_or_digit(c) || c == '-') {
        --l;
      } else {
        break;
      }
    }
    if (idx - l < 2) continue;

    size_t r = idx + 1;
    while (r < n && is_ascii_lower_byte(static_cast<unsigned char>(lower[r]))) {
      ++r;
    }
    if (r == idx + 1) continue;
    if (r < n) {
      unsigned char next = static_cast<unsigned char>(lower[r]);
      if (is_ascii_lower_or_digit(next) || next == '.') continue;
    }
    std::string_view tld(lower.data() + idx + 1, r - (idx + 1));
    if (contains_banner_tld(tld)) return true;
  }
  return false;
}

// ← node.rs:383-446
bool has_colon_quota_or_date(const std::string& lower) {
  const size_t ascii_colon = lower.find(':');
  const size_t fw_colon = lower.find("：");
  size_t pos = std::string::npos;
  size_t delim_len = 0;
  if (ascii_colon != std::string::npos &&
      (fw_colon == std::string::npos || ascii_colon <= fw_colon)) {
    pos = ascii_colon;
    delim_len = 1;
  } else if (fw_colon != std::string::npos) {
    pos = fw_colon;
    delim_len = 3;
  } else {
    return false;
  }

  std::string_view rhs = std::string_view(lower).substr(pos + delim_len);
  size_t i = 0;
  while (i < rhs.size()) {
    if (!is_ascii_digit_byte(static_cast<unsigned char>(rhs[i]))) {
      ++i;
      continue;
    }
    const size_t num_start = i;
    if (num_start > 0 &&
        (is_ascii_digit_byte(static_cast<unsigned char>(rhs[num_start - 1])) ||
         rhs[num_start - 1] == '.')) {
      ++i;
      continue;
    }
    while (i < rhs.size() && is_ascii_digit_byte(static_cast<unsigned char>(rhs[i]))) {
      ++i;
    }
    const size_t int_end = i;

    // 1. YYYY[-/.年]D
    if (int_end - num_start == 4 && rhs.substr(num_start, 2) == "20") {
      std::string_view tail = rhs.substr(int_end);
      size_t sep_len = 0;
      if (!tail.empty() &&
          (tail[0] == '-' || tail[0] == '/' || tail[0] == '.')) {
        sep_len = 1;
      } else if (starts_with(tail, "年")) {
        sep_len = 3;
      }
      if (sep_len > 0 && tail.size() > sep_len &&
          is_ascii_digit_byte(static_cast<unsigned char>(tail[sep_len]))) {
        return true;
      }
    }

    // 2. 配额 / 倒计时形态
    size_t after_num = int_end;
    if (after_num + 1 < rhs.size() && rhs[after_num] == '.' &&
        is_ascii_digit_byte(static_cast<unsigned char>(rhs[after_num + 1]))) {
      ++after_num;
      while (after_num < rhs.size() &&
             is_ascii_digit_byte(static_cast<unsigned char>(rhs[after_num]))) {
        ++after_num;
      }
    }
    const std::string after_ws = ltrim_ascii_ws(rhs.substr(after_num));
    for (const std::string_view unit : {"gib", "tib", "gb", "tb", "mb", "pb"}) {
      if (starts_with(after_ws, unit) &&
          (after_ws.size() == unit.size() ||
           !is_ascii_lower_byte(static_cast<unsigned char>(after_ws[unit.size()])))) {
        return true;
      }
    }
    if (starts_with(after_ws, "天") || starts_with(after_ws, "小时")) {
      return true;
    }
    for (const std::string_view unit : {"days", "day"}) {
      if (starts_with(after_ws, unit) &&
          (after_ws.size() == unit.size() ||
           !is_ascii_lower_byte(static_cast<unsigned char>(after_ws[unit.size()])))) {
        return true;
      }
    }
  }
  return false;
}

bool is_structural_banner_name(const std::string& name, const std::string& lower) {
  if (has_region_hint(name)) return false;
  return has_embedded_banner_domain(name, lower) || has_colon_quota_or_date(lower);
}

// ← node.rs::normalize_dummy_name
std::string normalize_dummy_name(const std::string& name) {
  std::string s = trim(name);
  struct Wrapper {
    std::string_view open, close;
  };
  static constexpr Wrapper kWrappers[] = {
      {"\xE3\x80\x90", "\xE3\x80\x91"},  // 【】
      {"(", ")"},
      {"\xEF\xBC\x88", "\xEF\xBC\x89"},  // （）
      {"[", "]"},
      {"\xE3\x80\x8C", "\xE3\x80\x8D"},  // 「」
  };
  for (;;) {
    const size_t before = s.size();
    for (const auto& w : kWrappers) {
      if (s.size() >= w.open.size() + w.close.size() &&
          starts_with(s, w.open) && ends_with(s, w.close)) {
        s = s.substr(w.open.size(), s.size() - w.open.size() - w.close.size());
        s = trim(s);
        break;
      }
    }
    if (s.size() == before) break;
  }
  static constexpr std::string_view kPrefixes[] = {
      "\xE8\x8A\x82\xE7\x82\xB9-",             // 节点-
      "\xE8\x8A\x82\xE7\x82\xB9:",             // 节点:
      "\xE8\x8A\x82\xE7\x82\xB9\xEF\xBC\x9A",  // 节点：
      "\xE8\x8A\x82\xE7\x82\xB9 ",             // 节点␠
  };
  for (const auto& p : kPrefixes) {
    if (starts_with(s, p)) {
      s = trim(s.substr(p.size()));
      break;
    }
  }
  return s;
}

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

void ensure_verify_store_loaded() {
  {
    std::shared_lock<std::shared_mutex> rlock(verify_store_mu());
    if (verify_store_slot().has_value()) return;
  }
  NodeVerifyStoreData loaded;
  if (auto p = resolve_verify_store_path(); p.has_value()) {
    loaded = load_verify_store_from_path(*p);
  }
  std::unique_lock<std::shared_mutex> wlock(verify_store_mu());
  if (!verify_store_slot().has_value()) {
    verify_store_slot() = std::move(loaded);
  }
}

void set_runtime_topology_if_none(
    std::unordered_map<std::string, std::string> map,
    std::unordered_set<std::string> tainted) {
  std::unique_lock<std::shared_mutex> wlock(topology_mu());
  if (!runtime_fps_slot().has_value()) {
    runtime_fps_slot() = std::move(map);
  }
  if (!runtime_tainted_slot().has_value()) {
    runtime_tainted_slot() = std::move(tainted);
  }
}

bool is_tainted_banner_fp(const std::string& fp) {
  ensure_runtime_node_fingerprints_loaded();
  std::shared_lock<std::shared_mutex> rlock(topology_mu());
  const auto& slot = runtime_tainted_slot();
  return slot.has_value() && slot->count(fp) > 0;
}

std::optional<std::string> fingerprint_for_node(const std::string& name) {
  ensure_runtime_node_fingerprints_loaded();
  std::shared_lock<std::shared_mutex> rlock(topology_mu());
  const auto& slot = runtime_fps_slot();
  if (!slot.has_value()) return std::nullopt;
  auto it = slot->find(name);
  if (it == slot->end()) return std::nullopt;
  return it->second;
}

void persist_verify_store_snapshot(const NodeVerifyStoreData& snapshot) {
  auto path_opt = resolve_verify_store_path();
  if (!path_opt.has_value()) return;
  if (!save_verify_store_to_path(*path_opt, snapshot)) {
    CWLOG_WARN("保存节点 HTTPS 验真状态失败");
  }
}

size_t clear_unverified_failures_in_records(
    std::map<std::string, NodeVerifyRecord>& records) {
  size_t cleared = 0;
  for (auto it = records.begin(); it != records.end();) {
    if (!it->second.verified_true && it->second.fail_count > 0) {
      ++cleared;
      it = records.erase(it);
    } else {
      ++it;
    }
  }
  return cleared;
}

}  // namespace

bool has_region_hint(const std::string& name) {
  if (trim(name).empty()) return false;

  // 1. 国旗 Emoji（Regional Indicator Symbols U+1F1E6..=U+1F1FF -> F0 9F 87 [A6..BF]）
  const auto* bytes = reinterpret_cast<const unsigned char*>(name.data());
  const size_t n = name.size();
  for (size_t i = 0; i + 3 < n; ++i) {
    if (bytes[i] == 0xF0 && bytes[i + 1] == 0x9F && bytes[i + 2] == 0x87 &&
        bytes[i + 3] >= 0xA6 && bytes[i + 3] <= 0xBF) {
      return true;
    }
  }

  // 2. 中文地区/城市名与带空格地区词
  const std::string lower = to_lower(name);
  for (const auto kw : kRegionSubstringsCjk) {
    if (lower.find(kw) != std::string::npos) return true;
  }

  // 3. 独立边界的 ASCII 地区/专线字母段（邻接 `.` 时跳过）
  size_t i = 0;
  while (i < n) {
    if (!is_ascii_alpha_byte(bytes[i])) {
      ++i;
      continue;
    }
    const size_t start = i;
    while (i < n && is_ascii_alpha_byte(bytes[i])) ++i;
    const size_t end = i;
    const std::optional<unsigned char> left =
        (start > 0) ? std::optional<unsigned char>(bytes[start - 1])
                    : std::nullopt;
    const std::optional<unsigned char> right =
        (end < n) ? std::optional<unsigned char>(bytes[end]) : std::nullopt;
    if (left == '.' || right == '.') continue;

    const std::string_view raw(name.data() + start, end - start);
    const std::string token = to_lower(std::string(raw));
    if (contains_ascii_token(token)) return true;
    if (token == "cn" && (raw == "CN" || right == '2')) return true;
    if (token == "us" && (raw == "US" || is_code_context_byte(left) ||
                          is_code_context_byte(right))) {
      return true;
    }
  }
  return false;
}

bool is_dummy_node(const std::string& name) {
  if (name.empty()) return true;
  if (name == "DIRECT") return true;
  const std::string lower = to_lower(name);
  if (lower.find("http://") != std::string::npos ||
      lower.find("https://") != std::string::npos ||
      lower.find("expire") != std::string::npos ||
      lower.find("traffic") != std::string::npos ||
      lower.find("website") != std::string::npos ||
      lower.find("subscribe") != std::string::npos) {
    return true;
  }
  const std::string normalized = normalize_dummy_name(lower);
  static constexpr std::string_view kAdPhrases[] = {
      "\xE6\xB5\x81\xE9\x87\x8F",                          // 流量
      "\xE8\xBF\x87\xE6\x9C\x9F\xE6\x97\xB6\xE9\x97\xB4",  // 过期时间
      "\xE7\xBD\x91\xE5\x9D\x80",                          // 网址
      "\xE5\xAE\x98\xE7\xBD\x91",                          // 官网
      "\xE5\x89\xA9\xE4\xBD\x99",                          // 剩余
      "\xE5\xA5\x97\xE9\xA4\x90\xE5\x88\xB0\xE6\x9C\x9F",  // 套餐到期
      "\xE7\xBB\xAD\xE8\xB4\xB9",                          // 续费
      "\xE5\x85\xAC\xE5\x91\x8A",                          // 公告
      "\xE8\xB4\xAD\xE4\xB9\xB0",                          // 购买
      "\xE7\xBE\xA4",                                      // 群
  };
  for (const auto& phrase : kAdPhrases) {
    if (normalized == phrase ||
        (normalized.size() >= phrase.size() &&
         normalized.compare(0, phrase.size(), phrase) == 0)) {
      return true;
    }
  }
  return is_structural_banner_name(name, lower);
}

bool is_valid_node_server(const std::string& server) {
  const std::string s = trim(server);
  if (s.empty()) return false;
  unsigned char buf[16];
  if (inet_pton(AF_INET, s.c_str(), buf) == 1 ||
      inet_pton(AF_INET6, s.c_str(), buf) == 1) {
    return true;
  }
  return s.find('.') != std::string::npos;
}

bool match_node_filter(const std::string& name, const std::string& filter_text) {
  const std::string trimmed = trim(filter_text);
  if (trimmed.empty()) return true;

  const std::string name_lower = to_lower(name);
  std::string filter_lower = to_lower(trimmed);
  // 替换全角 ｜ 为 |
  {
    const std::string fw = "\xEF\xBD\x9C";
    size_t pos = 0;
    while ((pos = filter_lower.find(fw, pos)) != std::string::npos) {
      filter_lower.replace(pos, fw.size(), "|");
      pos += 1;
    }
  }
  // 消除管道符两侧空白
  auto pipe_parts = split_keep_empty(filter_lower, '|');
  std::string collapsed;
  for (size_t idx = 0; idx < pipe_parts.size(); ++idx) {
    if (idx > 0) collapsed.push_back('|');
    collapsed.append(trim(pipe_parts[idx]));
  }

  for (const auto& token : split_ascii_whitespace(collapsed)) {
    const bool is_neg =
        starts_with(token, "-") || starts_with(token, "!") ||
        starts_with(token, "\xEF\xBC\x81");  // ！
    if (is_neg) {
      for (const auto& part : split_keep_empty(token, '|')) {
        std::string term = part;
        if (starts_with(term, "-") || starts_with(term, "!")) {
          term = term.substr(1);
        } else if (starts_with(term, "\xEF\xBC\x81")) {
          term = term.substr(3);
        }
        term = trim(term);
        if (!term.empty() && name_lower.find(term) != std::string::npos) {
          return false;
        }
      }
    } else {
      std::vector<std::string> branches;
      for (const auto& raw_b : split_keep_empty(token, '|')) {
        std::string b = trim(raw_b);
        if (!b.empty()) branches.push_back(std::move(b));
      }
      if (!branches.empty()) {
        bool any = false;
        for (const auto& b : branches) {
          if (name_lower.find(b) != std::string::npos) {
            any = true;
            break;
          }
        }
        if (!any) return false;
      }
    }
  }
  return true;
}

// BUG-392（Mini v3.5.1 utils/node.ts:528-568）：过滤输入完整性判定。
namespace {
std::string normalize_filter_for_tokens(std::string_view text) {
  std::string s = trim(std::string(text));
  const std::string fw = "\xEF\xBD\x9C";  // ｜
  size_t pos = 0;
  while ((pos = s.find(fw, pos)) != std::string::npos) {
    s.replace(pos, fw.size(), "|");
    pos += 1;
  }
  return s;
}

std::string strip_exclusion_prefix(std::string token) {
  if (starts_with(token, "-") || starts_with(token, "!")) {
    return trim(token.substr(1));
  }
  static const std::string kFullWidthBang = "\xEF\xBC\x81";  // ！
  if (starts_with(token, kFullWidthBang)) {
    return trim(token.substr(kFullWidthBang.size()));
  }
  return token;
}
}  // namespace

bool has_effective_filter_term(const std::string& filter_text) {
  const std::string normalized = normalize_filter_for_tokens(filter_text);
  if (normalized.empty()) return false;
  for (const auto& token : split_ascii_whitespace(normalized)) {
    for (const auto& part : split_keep_empty(token, '|')) {
      if (!strip_exclusion_prefix(trim(part)).empty()) return true;
    }
  }
  return false;
}

bool is_incomplete_filter_input(const std::string& filter_text) {
  const std::string trimmed = trim(filter_text);
  if (!has_effective_filter_term(trimmed)) return true;
  if (trimmed.size() >= 3 &&
      trimmed.compare(trimmed.size() - 3, 3, "\xEF\xBD\x9C") == 0) {
    return true;  // 全角 ｜ 结尾
  }
  if (!trimmed.empty() && trimmed.back() == '|') return true;
  const std::string normalized = normalize_filter_for_tokens(trimmed);
  const auto tokens = split_ascii_whitespace(normalized);
  if (tokens.empty()) return true;
  const auto segments = split_keep_empty(tokens.back(), '|');
  const std::string last_part =
      segments.empty() ? std::string() : trim(segments.back());
  return last_part == "-" || last_part == "!" || last_part == "\xEF\xBC\x81";
}

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

bool NodeVerifyRecord::is_confirmed_fake_at(int64_t now_ts) const {
  if (verified_true || fail_count <= kHttpsVerifyMaxFails) return false;
  if (last_verified_at <= 0) return true;
  const int64_t diff =
      (now_ts >= last_verified_at) ? (now_ts - last_verified_at) : 0;
  return diff < kHttpsVerifyBanTtlSecs;
}

bool NodeVerifyRecord::is_confirmed_fake() const {
  return is_confirmed_fake_at(current_unix_ts());
}

void set_runtime_node_fingerprints(
    std::unordered_map<std::string, std::string> map) {
  std::unique_lock<std::shared_mutex> wlock(topology_mu());
  runtime_fps_slot() = std::move(map);
}

void set_runtime_tainted_banner_fps(std::unordered_set<std::string> fps) {
  std::unique_lock<std::shared_mutex> wlock(topology_mu());
  runtime_tainted_slot() = std::move(fps);
}

void ensure_runtime_node_fingerprints_loaded() {
  {
    std::shared_lock<std::shared_mutex> rlock(topology_mu());
    if (runtime_fps_slot().has_value() && runtime_tainted_slot().has_value()) {
      return;
    }
  }
  bool skip_disk = false;
  {
    std::lock_guard<std::mutex> lock(test_cfg_mu());
    skip_disk = g_disable_disk_persistence_for_test &&
                !g_test_verify_store_path.has_value();
  }
  std::unordered_map<std::string, std::string> map;
  std::unordered_set<std::string> tainted;
  if (!skip_disk) {
    const std::wstring cfg_path = dirs::clash_mini_path();
    std::ifstream f(cfg_path, std::ios::binary);
    if (f.is_open()) {
      std::ostringstream oss;
      oss << f.rdbuf();
      try {
        YAML::Node val = YAML::Load(oss.str());
        if (val && val.IsMap() && val["proxies"] && val["proxies"].IsSequence()) {
          std::vector<RawProxyEntry> raw_entries;
          for (const auto& item : val["proxies"]) {
            if (!item.IsMap()) continue;
            const YAML::Node name_n = static_cast<const YAML::Node&>(item)["name"];
            if (!name_n || !name_n.IsScalar() || name_n.IsNull()) continue;
            const std::string& name_s = name_n.Scalar();
            const YAML::Node srv_n = static_cast<const YAML::Node&>(item)["server"];
            if (srv_n && srv_n.IsScalar() && !srv_n.IsNull() &&
                !is_valid_node_server(srv_n.Scalar())) {
              continue;
            }
            auto fp = proxy_endpoint_fingerprint(item);
            if (fp.has_value()) map[name_s] = *fp;
            RawProxyEntry e;
            e.name = name_s;
            e.fingerprint = std::move(fp);
            e.is_dummy = is_dummy_node(name_s);
            raw_entries.push_back(std::move(e));
          }
          tainted = resolve_proxy_clone_topology(raw_entries).tainted_fps;
        }
      } catch (...) {
      }
    }
  }
  set_runtime_topology_if_none(std::move(map), std::move(tainted));
}

bool is_https_quarantined_fp(const std::string& fp) {
  ensure_verify_store_loaded();
  const int64_t now_ts = current_unix_ts();
  std::shared_lock<std::shared_mutex> rlock(verify_store_mu());
  const auto& slot = verify_store_slot();
  if (!slot.has_value()) return false;
  auto it = slot->records.find(fp);
  return it != slot->records.end() && it->second.is_confirmed_fake_at(now_ts);
}

bool is_confirmed_fake_fp(const std::string& fp) {
  return is_https_quarantined_fp(fp) || is_tainted_banner_fp(fp);
}

bool is_verified_true_fp(const std::string& fp) {
  ensure_verify_store_loaded();
  std::shared_lock<std::shared_mutex> rlock(verify_store_mu());
  const auto& slot = verify_store_slot();
  if (!slot.has_value()) return false;
  auto it = slot->records.find(fp);
  return it != slot->records.end() && it->second.verified_true;
}

bool is_confirmed_fake_node(const std::string& name) {
  auto fp = fingerprint_for_node(name);
  return fp.has_value() && is_confirmed_fake_fp(*fp);
}

bool is_verified_true_node(const std::string& name) {
  auto fp = fingerprint_for_node(name);
  return fp.has_value() && is_verified_true_fp(*fp);
}

bool record_has_verify_failure(bool verified_true, uint8_t fail_count,
                               int64_t last_verified_at, int64_t now_ts) {
  if (verified_true || fail_count < kHttpsVerifyMigrateMinFails) return false;
  if (last_verified_at <= 0) return true;
  const int64_t diff =
      (now_ts >= last_verified_at) ? (now_ts - last_verified_at) : 0;
  return diff < kHttpsVerifyBanTtlSecs;
}

bool has_verify_failure_node(const std::string& name) {
  auto fp = fingerprint_for_node(name);
  if (!fp.has_value()) return false;
  ensure_verify_store_loaded();
  const int64_t now_ts = current_unix_ts();
  std::shared_lock<std::shared_mutex> rlock(verify_store_mu());
  const auto& slot = verify_store_slot();
  if (!slot.has_value()) return false;
  auto it = slot->records.find(*fp);
  if (it == slot->records.end()) return false;
  return record_has_verify_failure(it->second.verified_true,
                                   it->second.fail_count,
                                   it->second.last_verified_at, now_ts);
}

std::vector<std::string> confirmed_fake_node_names() {
  ensure_runtime_node_fingerprints_loaded();
  std::vector<std::pair<std::string, std::string>> pairs;
  std::unordered_set<std::string> tainted_fps;
  {
    std::shared_lock<std::shared_mutex> rlock(topology_mu());
    if (runtime_fps_slot().has_value()) {
      for (const auto& [k, v] : *runtime_fps_slot()) {
        pairs.emplace_back(k, v);
      }
    }
    if (runtime_tainted_slot().has_value()) {
      tainted_fps = *runtime_tainted_slot();
    }
  }
  if (pairs.empty()) return {};

  ensure_verify_store_loaded();
  const int64_t now_ts = current_unix_ts();
  std::unordered_set<std::string> https_fake_fps;
  {
    std::shared_lock<std::shared_mutex> rlock(verify_store_mu());
    if (verify_store_slot().has_value()) {
      const auto& recs = verify_store_slot()->records;
      for (const auto& [name, fp] : pairs) {
        (void)name;
        if (tainted_fps.count(fp) > 0) continue;
        auto it = recs.find(fp);
        if (it != recs.end() && it->second.is_confirmed_fake_at(now_ts)) {
          https_fake_fps.insert(fp);
        }
      }
    }
  }

  std::vector<std::string> out;
  for (const auto& [name, fp] : pairs) {
    if (tainted_fps.count(fp) > 0 || https_fake_fps.count(fp) > 0) {
      out.push_back(name);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool has_pending_unverified_nodes(const std::vector<std::string>& candidates) {
  if (candidates.empty()) return false;
  ensure_runtime_node_fingerprints_loaded();
  std::vector<std::string> fps;
  std::unordered_set<std::string> tainted_fps;
  {
    std::shared_lock<std::shared_mutex> rlock(topology_mu());
    if (runtime_fps_slot().has_value()) {
      const auto& map = *runtime_fps_slot();
      for (const auto& name : candidates) {
        auto it = map.find(name);
        if (it != map.end()) fps.push_back(it->second);
      }
    }
    if (runtime_tainted_slot().has_value()) {
      tainted_fps = *runtime_tainted_slot();
    }
  }
  if (fps.empty()) return false;

  ensure_verify_store_loaded();
  const int64_t now_ts = current_unix_ts();
  std::shared_lock<std::shared_mutex> rlock(verify_store_mu());
  if (!verify_store_slot().has_value()) return true;
  const auto& recs = verify_store_slot()->records;
  for (const auto& fp : fps) {
    if (tainted_fps.count(fp) > 0) continue;
    auto it = recs.find(fp);
    if (it == recs.end() ||
        (!it->second.verified_true && !it->second.is_confirmed_fake_at(now_ts))) {
      return true;
    }
  }
  return false;
}

VerifyBatchOutcome apply_verify_batch_to_records(
    std::map<std::string, NodeVerifyRecord>& records,
    const std::vector<std::string>& passed_fps,
    const std::vector<std::string>& failed_fps, int64_t now_ts) {
  VerifyBatchOutcome outcome;
  std::unordered_set<std::string> passed_set;
  passed_set.reserve(passed_fps.size());

  for (const auto& fp : passed_fps) {
    if (!passed_set.insert(fp).second) continue;
    auto& rec = records[fp];
    if (!rec.verified_true) {
      rec.verified_true = true;
      ++outcome.newly_verified;
      outcome.dirty = true;
    }
    if (rec.fail_count != 0) {
      rec.fail_count = 0;
      outcome.dirty = true;
    }
    if (rec.last_verified_at != now_ts) {
      rec.last_verified_at = now_ts;
      outcome.dirty = true;
    }
  }

  if (passed_set.empty()) return outcome;

  std::unordered_set<std::string> seen_failed;
  seen_failed.reserve(failed_fps.size());
  for (const auto& fp : failed_fps) {
    if (passed_set.count(fp) > 0 || !seen_failed.insert(fp).second) continue;
    auto& rec = records[fp];
    if (rec.verified_true || rec.is_confirmed_fake_at(now_ts)) continue;
    if (rec.fail_count > kHttpsVerifyMaxFails) rec.fail_count = 0;
    if (rec.fail_count < 255) rec.fail_count = static_cast<uint8_t>(rec.fail_count + 1);
    rec.last_verified_at = now_ts;
    outcome.dirty = true;
    if (rec.is_confirmed_fake_at(now_ts)) ++outcome.newly_confirmed_fake;
  }
  return outcome;
}

VerifyBatchOutcome record_https_verify_results(
    const std::vector<std::string>& passed_names,
    const std::vector<std::string>& failed_names) {
  ensure_runtime_node_fingerprints_loaded();
  std::vector<std::string> passed_fps;
  std::vector<std::string> failed_fps;
  {
    std::shared_lock<std::shared_mutex> rlock(topology_mu());
    if (runtime_fps_slot().has_value()) {
      const auto& map = *runtime_fps_slot();
      for (const auto& n : passed_names) {
        if (auto it = map.find(n); it != map.end()) passed_fps.push_back(it->second);
      }
      for (const auto& n : failed_names) {
        if (auto it = map.find(n); it != map.end()) failed_fps.push_back(it->second);
      }
    }
  }
  if (passed_fps.empty() && failed_fps.empty()) return {};

  ensure_verify_store_loaded();
  const int64_t now_ts = current_unix_ts();
  VerifyBatchOutcome outcome;
  std::optional<NodeVerifyStoreData> snapshot;
  {
    std::unique_lock<std::shared_mutex> wlock(verify_store_mu());
    if (!verify_store_slot().has_value()) {
      verify_store_slot() = NodeVerifyStoreData{};
    }
    outcome = apply_verify_batch_to_records(verify_store_slot()->records,
                                            passed_fps, failed_fps, now_ts);
    if (outcome.dirty) snapshot = *verify_store_slot();
  }
  if (snapshot.has_value()) {
    persist_verify_store_snapshot(*snapshot);
  }
  return outcome;
}

size_t clear_unverified_https_failures() {
  ensure_verify_store_loaded();
  size_t cleared = 0;
  std::optional<NodeVerifyStoreData> snapshot;
  {
    std::unique_lock<std::shared_mutex> wlock(verify_store_mu());
    if (!verify_store_slot().has_value()) {
      verify_store_slot() = NodeVerifyStoreData{};
    }
    cleared = clear_unverified_failures_in_records(verify_store_slot()->records);
    if (cleared > 0) snapshot = *verify_store_slot();
  }
  if (snapshot.has_value()) {
    persist_verify_store_snapshot(*snapshot);
  }
  return cleared;
}

NodeVerifyStoreData load_verify_store_from_path(const std::wstring& path) {
  NodeVerifyStoreData data;
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return data;
  std::ostringstream oss;
  oss << f.rdbuf();
  nlohmann::json j = nlohmann::json::parse(oss.str(), nullptr, false);
  if (j.is_discarded() || !j.is_object() || !j.contains("records") ||
      !j["records"].is_object()) {
    return data;
  }
  for (auto it = j["records"].begin(); it != j["records"].end(); ++it) {
    if (!it.value().is_object()) continue;
    NodeVerifyRecord rec;
    rec.verified_true = it.value().value("verified_true", false);
    const int fc = it.value().value("fail_count", 0);
    rec.fail_count = static_cast<uint8_t>(std::clamp(fc, 0, 255));
    rec.last_verified_at = it.value().value("last_verified_at", int64_t{0});
    data.records.emplace(it.key(), rec);
  }
  return data;
}

bool save_verify_store_to_path(const std::wstring& path,
                               const NodeVerifyStoreData& data) {
  std::error_code ec;
  std::filesystem::path p(path);
  if (p.has_parent_path()) {
    std::filesystem::create_directories(p.parent_path(), ec);
  }
  nlohmann::json recs = nlohmann::json::object();
  for (const auto& [fp, rec] : data.records) {
    recs[fp] = nlohmann::json{
        {"verified_true", rec.verified_true},
        {"fail_count", rec.fail_count},
        {"last_verified_at", rec.last_verified_at},
    };
  }
  nlohmann::json root = nlohmann::json{{"records", std::move(recs)}};
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return false;
  out << root.dump(2);
  return out.good();
}

void reset_verify_state_for_test(bool disable_disk_persistence) {
  {
    std::lock_guard<std::mutex> lock(test_cfg_mu());
    g_disable_disk_persistence_for_test = disable_disk_persistence;
    g_test_verify_store_path.reset();
  }
  {
    std::unique_lock<std::shared_mutex> wlock(verify_store_mu());
    verify_store_slot().reset();
  }
  {
    std::unique_lock<std::shared_mutex> wlock(topology_mu());
    runtime_fps_slot().reset();
    runtime_tainted_slot().reset();
  }
}

void set_verify_store_path_for_test(const std::optional<std::wstring>& path) {
  {
    std::lock_guard<std::mutex> lock(test_cfg_mu());
    g_test_verify_store_path = path;
  }
  std::unique_lock<std::shared_mutex> wlock(verify_store_mu());
  verify_store_slot().reset();
}

void set_verify_record_for_test(const std::string& fp,
                                const std::optional<NodeVerifyRecord>& rec) {
  ensure_verify_store_loaded();
  std::unique_lock<std::shared_mutex> wlock(verify_store_mu());
  if (!verify_store_slot().has_value()) {
    verify_store_slot() = NodeVerifyStoreData{};
  }
  if (rec.has_value()) {
    verify_store_slot()->records[fp] = *rec;
  } else {
    verify_store_slot()->records.erase(fp);
  }
}

bool is_supported_subscription_or_node_input(std::string_view raw) {
  auto trim_sv = [](std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
      s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
      s.remove_suffix(1);
    }
    return s;
  };
  std::vector<std::string_view> lines;
  size_t pos = 0;
  while (pos <= raw.size()) {
    size_t nl = raw.find('\n', pos);
    std::string_view seg =
        (nl == std::string_view::npos) ? raw.substr(pos) : raw.substr(pos, nl - pos);
    seg = trim_sv(seg);
    if (!seg.empty()) lines.push_back(seg);
    if (nl == std::string_view::npos) break;
    pos = nl + 1;
  }
  if (lines.empty()) return false;

  auto starts_with_ci = [](std::string_view s, std::string_view pfx) {
    if (s.size() < pfx.size()) return false;
    for (size_t i = 0; i < pfx.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(s[i])) !=
          std::tolower(static_cast<unsigned char>(pfx[i]))) {
        return false;
      }
    }
    return true;
  };
  auto has_ws = [](std::string_view s) {
    for (char ch : s) {
      if (std::isspace(static_cast<unsigned char>(ch))) return true;
    }
    return false;
  };

  bool has_http = false;
  for (auto line : lines) {
    if (starts_with_ci(line, "http://") || starts_with_ci(line, "https://")) {
      has_http = true;
      break;
    }
  }
  if (has_http) {
    return lines.size() == 1 && !has_ws(lines[0]);
  }

  static constexpr std::string_view kSchemes[] = {
      "vmess://", "ss://",     "trojan://",    "vless://",
      "hysteria2://", "hysteria://", "anytls://", "tuic://",
      "wireguard://", "socks5://", "socks://",
  };
  for (auto line : lines) {
    bool ok = false;
    for (auto sch : kSchemes) {
      if (starts_with_ci(line, sch)) {
        ok = true;
        break;
      }
    }
    if (!ok) return false;
  }
  return true;
}

}  // namespace clashwin::util
