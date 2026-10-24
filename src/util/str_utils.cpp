#include "util/str_utils.h"

#include <cstdint>

namespace clashwin::util {

namespace {

bool is_ascii_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void append_utf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

const char kReplacement[] = "\xEF\xBF\xBD";  // U+FFFD

// UTF-8 lossy 清洗：非法序列以 U+FFFD 替换（对齐 String::from_utf8_lossy；
// 非法子序列按"最大子部"替换为一个 U+FFFD）
std::string utf8_lossy(const std::string& bytes) {
  std::string out;
  out.reserve(bytes.size());
  size_t i = 0;
  const size_t n = bytes.size();
  while (i < n) {
    unsigned char c = static_cast<unsigned char>(bytes[i]);
    if (c < 0x80) {
      out += bytes[i];
      ++i;
      continue;
    }
    int len = 0;
    uint32_t min_cp = 0;
    if (c >= 0xC2 && c <= 0xDF) {
      len = 2;
      min_cp = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
      len = 3;
      min_cp = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
      len = 4;
      min_cp = 0x10000;
    }
    bool ok = len > 0 && i + static_cast<size_t>(len) <= n;
    if (ok) {
      uint32_t cp = c & (len == 2 ? 0x1F : len == 3 ? 0x0F : 0x07);
      for (int k = 1; k < len && ok; ++k) {
        unsigned char cc = static_cast<unsigned char>(bytes[i + k]);
        if ((cc & 0xC0) != 0x80) {
          ok = false;
        } else {
          cp = (cp << 6) | (cc & 0x3F);
        }
      }
      if (ok && (cp < min_cp || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF))
        ok = false;
      if (ok) {
        out.append(bytes, i, static_cast<size_t>(len));
        i += static_cast<size_t>(len);
        continue;
      }
    }
    // 非法：消费首字节（若首字节合法但后续截断/坏续字节，消费到坏点）
    out += kReplacement;
    if (len == 0) {
      ++i;  // 非法首字节，跳 1
    } else {
      size_t j = i + 1;
      while (j < n &&
             (static_cast<unsigned char>(bytes[j]) & 0xC0) == 0x80 &&
             j - i < static_cast<size_t>(len))
        ++j;
      i = j;  // 消费整个"疑似子部"为一个 U+FFFD
    }
  }
  return out;
}

}  // namespace

std::string trim(std::string_view s) {
  size_t b = 0, e = s.size();
  while (b < e && is_ascii_space(s[b])) ++b;
  while (e > b && is_ascii_space(s[e - 1])) --e;
  return std::string(s.substr(b, e - b));
}

std::vector<std::string> split(std::string_view s, char delim) {
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

std::vector<std::string> split_no_empty(std::string_view s, char delim) {
  std::vector<std::string> parts;
  for (auto& p : split(s, delim))
    if (!p.empty()) parts.push_back(std::move(p));
  return parts;
}

bool starts_with(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool ends_with(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() &&
         s.substr(s.size() - suffix.size()) == suffix;
}

std::string to_lower(std::string_view s) {
  std::string out(s);
  for (char& c : out)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return out;
}

std::string percent_decode(std::string_view s) {
  std::string bytes;
  bytes.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      int hi = hex_val(s[i + 1]), lo = hex_val(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        bytes += static_cast<char>((hi << 4) | lo);
        i += 2;
        continue;
      }
    }
    bytes += s[i];
  }
  return utf8_lossy(bytes);
}

std::string percent_decode_max2(std::string_view s) {
  std::string candidate(s);
  for (int i = 0; i < 2; ++i) {
    std::string next = percent_decode(candidate);
    if (next == candidate) break;
    candidate = std::move(next);
  }
  return candidate;
}

bool is_probably_url(std::string_view s) {
  // scheme = [A-Za-z][A-Za-z0-9+.-]* ，后随 ':' 且 ':' 之后非空
  if (s.empty() || !((s[0] >= 'a' && s[0] <= 'z') ||
                     (s[0] >= 'A' && s[0] <= 'Z')))
    return false;
  size_t i = 1;
  while (i < s.size() &&
         ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
          (s[i] >= '0' && s[i] <= '9') || s[i] == '+' || s[i] == '-' ||
          s[i] == '.'))
    ++i;
  if (i < 2 || i >= s.size() || s[i] != ':') return false;
  return i + 1 < s.size();
}

std::string decode_subscription_url(std::string_view raw_url) {
  // ← scheme.rs:62-80：已可解析为 URL 则原样返回（避免嵌套订阅 URL 双重解码）
  if (is_probably_url(raw_url)) return std::string(raw_url);

  std::string candidate(raw_url);
  for (int i = 0; i < 2; ++i) {
    std::string next = percent_decode(candidate);
    if (next == candidate) break;
    candidate = std::move(next);
    if (is_probably_url(candidate)) break;
  }
  return candidate;
}

Result<std::string> fix_dirty_url(std::string_view input) {
  // ← prfitem.rs:859-883。轻量 URL 解析（完整解析器在 T06）：
  // scheme://host/path?query#fragment
  std::string url = trim(input);

  // scheme 校验
  size_t scheme_end = std::string::npos;
  for (size_t i = 0; i < url.size(); ++i) {
    char c = url[i];
    if (c == ':') {
      scheme_end = i;
      break;
    }
    if (i == 0 ? !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
               : !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '+' || c == '-' ||
                   c == '.')) {
      break;
    }
  }
  if (scheme_end == std::string::npos || scheme_end < 2 ||
      url.compare(scheme_end, 3, "://") != 0) {
    Error e;
    e.code = 1;
    e.message = "failed to parse subscription URL: invalid url";
    return Result<std::string>::err(std::move(e));
  }

  std::string scheme = to_lower(url.substr(0, scheme_end));
  size_t host_begin = scheme_end + 3;

  // 先剥 fragment（'#'）
  std::string frag;
  size_t hash_pos = url.find('#', host_begin);
  if (hash_pos != std::string::npos) {
    frag = url.substr(hash_pos);
    url.resize(hash_pos);
  }

  // host 到第一个 '/' 或 '?' 为止（host 小写，与 Url::parse 对齐）
  size_t path_begin = url.size();
  for (size_t i = host_begin; i < url.size(); ++i) {
    if (url[i] == '/' || url[i] == '?') {
      path_begin = i;
      break;
    }
  }
  std::string host = to_lower(url.substr(host_begin, path_begin - host_begin));

  std::string query;
  size_t q_pos = url.find('?', path_begin);
  if (q_pos != std::string::npos) query = url.substr(q_pos + 1);

  std::string result;
  if (q_pos == std::string::npos) {
    // 无 query：path 内含 '&' 时把首个 '&' 改为 '?'
    std::string path = url.substr(path_begin);
    size_t amp = path.find('&');
    if (amp != std::string::npos) {
      std::string clean_path = path.substr(0, amp);
      std::string dirty_params = path.substr(amp + 1);
      result = scheme + "://" + host + clean_path + "?" + dirty_params;
    } else {
      result = scheme + "://" + host + path;
    }
  } else {
    result = scheme + "://" + host + url.substr(path_begin);
  }
  result += frag;
  return Result<std::string>::ok(std::move(result));
}

}  // namespace clashwin::util
