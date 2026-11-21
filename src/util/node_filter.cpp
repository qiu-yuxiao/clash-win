// 伪节点/假 server 识别、多关键词过滤、半输入拦截与合粒子集首节点晋升
// （拆分自 src/util/node.cpp；← ClashVerge src-tauri/src/utils/node/filter.rs & src/utils/node.ts）
#include "util/node.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>

#include "util/str_utils.h"

namespace clashwin::util {

namespace {

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
  std::string normalized(s);
  const std::string fw_space = "\xE3\x80\x80";  // 全角空格 U+3000
  size_t pos = 0;
  while ((pos = normalized.find(fw_space, pos)) != std::string::npos) {
    normalized.replace(pos, fw_space.size(), " ");
    pos += 1;
  }
  std::vector<std::string> parts;
  size_t i = 0;
  while (i < normalized.size()) {
    while (i < normalized.size() &&
           is_ascii_ws(static_cast<unsigned char>(normalized[i])))
      ++i;
    if (i >= normalized.size()) break;
    size_t j = i;
    while (j < normalized.size() &&
           !is_ascii_ws(static_cast<unsigned char>(normalized[j])))
      ++j;
    parts.emplace_back(normalized.substr(i, j - i));
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

std::string effective_filter_text(const std::string& filter_text) {
  if (is_incomplete_filter_input(filter_text)) return {};
  return trim(filter_text);
}

std::optional<std::string> pick_subset_fallback_node(
    const std::vector<std::string>& all, const std::string& filter_text,
    const std::function<bool(const std::string&)>& is_valid,
    const std::function<bool(const std::string&)>& is_verified) {
  const std::string trimmed = effective_filter_text(filter_text);
  auto check_valid = [&](const std::string& n) {
    return is_valid ? is_valid(n) : (!is_dummy_node(n) && !is_confirmed_fake_node(n));
  };
  auto check_verified = [&](const std::string& n) {
    return is_verified ? is_verified(n) : is_verified_true_node(n);
  };
  for (const auto& n : all) {
    if (check_valid(n) && check_verified(n) && match_node_filter(n, trimmed)) {
      return n;
    }
  }
  for (const auto& n : all) {
    if (check_valid(n) && match_node_filter(n, trimmed)) {
      return n;
    }
  }
  if (trimmed.empty()) {
    for (const auto& n : all) {
      if (check_valid(n)) return n;
    }
  }
  return std::nullopt;
}

std::optional<std::string> promote_preferred_proxy_first(
    std::vector<std::string>& names,
    const std::optional<std::string>& preferred, const std::string& filter_text,
    const std::function<bool(const std::string&)>& is_valid,
    const std::function<bool(const std::string&)>& is_verified) {
  const std::string effective = effective_filter_text(filter_text);
  auto check_valid = [&](const std::string& n) {
    return is_valid ? is_valid(n) : (!is_dummy_node(n) && !is_confirmed_fake_node(n));
  };

  std::optional<std::string> target;
  if (preferred.has_value()) {
    const std::string p = trim(*preferred);
    if (!p.empty() && check_valid(p) && match_node_filter(p, effective) &&
        std::find(names.begin(), names.end(), p) != names.end()) {
      target = p;
    }
  }
  if (!target.has_value()) {
    target = pick_subset_fallback_node(names, effective, is_valid, is_verified);
  }
  if (!target.has_value()) return std::nullopt;

  auto it = std::find(names.begin(), names.end(), *target);
  if (it == names.end()) return std::nullopt;
  if (it != names.begin()) {
    std::rotate(names.begin(), it, it + 1);
  }
  return target;
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

BatchCheckIntent resolve_batch_check_intent(BatchCheckTrigger trigger) {
  const bool should_switch =
      trigger == BatchCheckTrigger::Filter || trigger == BatchCheckTrigger::F1;
  return BatchCheckIntent{should_switch, should_switch};
}

}  // namespace clashwin::util
