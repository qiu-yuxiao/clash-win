// WinHTTP 同步下载封装（← utils/network.rs NetworkManager 逐行为对齐）
#include "system/net_http.h"

#include <winhttp.h>
#include <wininet.h>  // SECURITY_FLAG_IGNORE_*

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <vector>

#include "clashwin/config/constants.h"
#include "util/str_utils.h"
#include "util/utf.h"

#pragma comment(lib, "winhttp.lib")

namespace clashwin::system {

using clashwin::utf::utf16_to_utf8;
using clashwin::utf::utf8_to_utf16;
using util::starts_with;
using util::to_lower;
using util::trim;

namespace {

constexpr uint64_t kDefaultTimeoutS = 20;  // ← PrfOption timeout_seconds 默认
constexpr const char* kDefaultUserAgent = "clash-verge/v0.1.0";  // 应用版本 0.1.0

bool cancel_signaled(HANDLE ev) {
  if (!ev) return false;
  return WaitForSingleObject(ev, 0) == WAIT_OBJECT_0;
}

std::string last_error_msg(const char* what, DWORD err) {
  return std::string(what) + " failed (winhttp error " + std::to_string(err) + ")";
}

}  // namespace

// ---------- HttpResponse ----------

const std::string* HttpResponse::header(std::string_view name) const {
  for (const auto& h : headers) {
    if (h.name.size() == name.size() &&
        std::equal(name.begin(), name.end(), h.name.begin(),
                   [](char a, char b) {
                     return tolower(static_cast<unsigned char>(a)) ==
                            tolower(static_cast<unsigned char>(b));
                   })) {
      return &h.value;
    }
  }
  return nullptr;
}

// ---------- UrlParts ----------

uint16_t UrlParts::effective_port() const {
  if (!port_str.empty()) {
    uint32_t v = 0;
    for (char c : port_str) {
      if (c < '0' || c > '9') return 0;
      v = v * 10 + static_cast<uint32_t>(c - '0');
      if (v > 65535) return 0;
    }
    return static_cast<uint16_t>(v);
  }
  return scheme == "https" ? 443 : 80;
}

std::string UrlParts::request_target() const {
  std::string out = path.empty() ? "/" : path;
  if (!query.empty()) {
    out += "?";
    out += query;
  }
  return out;
}

std::string UrlParts::host_header() const {
  std::string out;
  if (is_ipv6) {
    out += "[";
    out += host;
    out += "]";
  } else {
    out += host;
  }
  const uint16_t def = scheme == "https" ? 443 : 80;
  if (!port_str.empty() && effective_port() != def) {
    out += ":";
    out += std::to_string(effective_port());
  }
  return out;
}

std::string UrlParts::serialize() const {
  std::string out = scheme + "://";
  if (!username.empty() || !password.empty()) {
    out += username;
    if (!password.empty()) {
      out += ":";
      out += password;
    }
    out += "@";
  }
  out += host_header();
  out += path.empty() ? "/" : path;
  if (!query.empty()) {
    out += "?";
    out += query;
  }
  if (!fragment.empty()) {
    out += "#";
    out += fragment;
  }
  return out;
}

// ---------- WHATWG IPv4 主机解析（SSRF 绕过面：'2130706433'/'127.1' 等） ----------

namespace {

// ← WHATWG parse-IPv4-number：0x 十六进制 / 前导 0 八进制 / 十进制；
// 空 → 0；>2^32-1 → 失败（nullopt）
std::optional<uint64_t> parse_ipv4_number(std::string_view part) {
  if (part.empty()) return 0;
  uint64_t value = 0;
  int radix = 10;
  size_t i = 0;
  if (part.size() >= 2 && part[0] == '0' && (part[1] == 'x' || part[1] == 'X')) {
    radix = 16;
    i = 2;
  } else if (part.size() >= 2 && part[0] == '0') {
    radix = 8;
    i = 1;
  }
  if (i == part.size()) return 0;  // "0x" → 0
  for (; i < part.size(); ++i) {
    char c = part[i];
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (radix == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (radix == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return std::nullopt;
    value = value * radix + d;
    if (value > 0xFFFFFFFFull) return std::nullopt;
  }
  return value;
}

bool is_all_decimal(std::string_view s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

bool ipv4_numberish_part(std::string_view s) {
  if (is_all_decimal(s)) return true;
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    for (size_t i = 2; i < s.size(); ++i) {
      char c = s[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F'))) {
        return false;
      }
    }
    return true;
  }
  return false;
}

std::string u32_to_dotted(uint32_t ip) {
  return std::to_string((ip >> 24) & 0xFF) + "." +
         std::to_string((ip >> 16) & 0xFF) + "." +
         std::to_string((ip >> 8) & 0xFF) + "." + std::to_string(ip & 0xFF);
}

// WHATWG IPv4 解析：>4 段 → 域名（返回 false 不报错）；段非法 → Error
enum class V4Result { NotIpv4, Ok, Invalid };
V4Result whatwg_parse_ipv4(std::string_view host, std::string& out) {
  std::vector<std::string_view> parts;
  {
    size_t start = 0;
    for (;;) {
      size_t dot = host.find('.', start);
      if (dot == std::string_view::npos) {
        parts.push_back(host.substr(start));
        break;
      }
      parts.push_back(host.substr(start, dot - start));
      start = dot + 1;
      if (dot + 1 == host.size()) {
        parts.push_back(host.substr(start));  // 尾点 → 空末段
        break;
      }
    }
  }
  if (parts.size() > 1 && parts.back().empty()) parts.pop_back();  // 尾点剥离
  if (parts.size() > 4) return V4Result::NotIpv4;
  if (parts.empty()) return V4Result::NotIpv4;

  std::vector<uint64_t> numbers;
  for (auto& p : parts) {
    auto n = parse_ipv4_number(p);
    if (!n) return V4Result::Invalid;
    numbers.push_back(*n);
  }
  const uint64_t last = numbers.back();
  for (size_t i = 0; i + 1 < numbers.size(); ++i) {
    if (numbers[i] > 255) return V4Result::Invalid;
  }
  uint64_t limit = 256;
  for (size_t i = 0; i < 4 - numbers.size(); ++i) limit *= 256;
  if (last >= limit) return V4Result::Invalid;

  uint32_t ipv4 = static_cast<uint32_t>(last);
  for (size_t i = 0; i + 1 < numbers.size(); ++i) {
    ipv4 += static_cast<uint32_t>(numbers[i] << (8 * (3 - i)));
  }
  out = u32_to_dotted(ipv4);
  return V4Result::Ok;
}

}  // namespace

Result<UrlParts> parse_http_url(const std::string& url) {
  // scheme: [A-Za-z][A-Za-z0-9+.-]*:
  size_t scheme_end = std::string::npos;
  for (size_t i = 0; i < url.size(); ++i) {
    char c = url[i];
    if (c == ':') {
      scheme_end = i;
      break;
    }
    if (i == 0) {
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) break;
    } else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.')) {
      break;
    }
  }
  if (scheme_end == std::string::npos || scheme_end == 0) {
    Error e;
    e.code = 30;
    e.message = "failed to parse subscription URL: relative URL without a base, input: " + url;
    return Result<UrlParts>::err(std::move(e));
  }
  UrlParts parts;
  parts.scheme = to_lower(url.substr(0, scheme_end));
  if (parts.scheme != "http" && parts.scheme != "https") {
    Error e;
    e.code = 30;
    e.message = "failed to parse subscription URL: unsupported scheme \"" +
                parts.scheme + "\"";
    return Result<UrlParts>::err(std::move(e));
  }
  std::string_view rest(url);
  rest = rest.substr(scheme_end + 1);
  if (!starts_with(rest, "//")) {
    Error e;
    e.code = 30;
    e.message = "failed to parse subscription URL: missing authority, input: " + url;
    return Result<UrlParts>::err(std::move(e));
  }
  rest = rest.substr(2);

  // authority 终止于 '/' '?' '#'
  size_t auth_end = rest.size();
  for (size_t i = 0; i < rest.size(); ++i) {
    if (rest[i] == '/' || rest[i] == '?' || rest[i] == '#') {
      auth_end = i;
      break;
    }
  }
  std::string_view authority = rest.substr(0, auth_end);
  std::string_view tail = rest.substr(auth_end);

  // userinfo：按最后一个 '@'（WHATWG/reqwest 口径）
  size_t at = authority.rfind('@');
  if (at != std::string_view::npos) {
    std::string_view ui = authority.substr(0, at);
    authority = authority.substr(at + 1);
    size_t colon = ui.find(':');
    if (colon == std::string_view::npos) {
      parts.username = std::string(ui);
    } else {
      parts.username = std::string(ui.substr(0, colon));
      parts.password = std::string(ui.substr(colon + 1));
    }
  }

  // hostport：[v6] 或 host:port
  std::string host_raw;
  if (!authority.empty() && authority.front() == '[') {
    size_t close = authority.find(']');
    if (close == std::string_view::npos) {
      Error e;
      e.code = 30;
      e.message = "failed to parse subscription URL: invalid IPv6 address";
      return Result<UrlParts>::err(std::move(e));
    }
    host_raw = to_lower(authority.substr(1, close - 1));
    parts.is_ipv6 = true;
    std::string_view port_part = authority.substr(close + 1);
    if (!port_part.empty()) {
      if (port_part.front() != ':') {
        Error e;
        e.code = 30;
        e.message = "failed to parse subscription URL: invalid characters after IPv6";
        return Result<UrlParts>::err(std::move(e));
      }
      parts.port_str = std::string(port_part.substr(1));
    }
  } else {
    size_t colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
      host_raw = std::string(authority.substr(0, colon));
      parts.port_str = std::string(authority.substr(colon + 1));
    } else {
      host_raw = std::string(authority);
    }
  }
  if (host_raw.empty()) {
    Error e;
    e.code = 30;
    e.message = "failed to parse subscription URL: empty host";
    return Result<UrlParts>::err(std::move(e));
  }
  // 端口校验：全数字且 ≤65535（空串 = 未指定）
  if (!parts.port_str.empty()) {
    bool ok = !parts.port_str.empty();
    for (char c : parts.port_str) {
      if (c < '0' || c > '9') {
        ok = false;
        break;
      }
    }
    uint32_t v = 0;
    if (ok) {
      for (char c : parts.port_str) v = v * 10 + (c - '0');
      if (v > 65535) ok = false;
    }
    if (!ok) {
      Error e;
      e.code = 30;
      e.message = "failed to parse subscription URL: invalid port";
      return Result<UrlParts>::err(std::move(e));
    }
    // ← Url::parse 默认端口剥离（http:80 / https:443）
    if ((parts.scheme == "http" && v == 80) ||
        (parts.scheme == "https" && v == 443)) {
      parts.port_str.clear();
    }
  }

  // host：percent-decode（url crate 对 host 先解码再分类）→ 数字主机归一 → 域名小写
  std::string decoded = util::percent_decode(host_raw);
  if (parts.is_ipv6) {
    parts.host = to_lower(decoded);
  } else {
    // ← WHATWG ends-in-a-number：尾点剥离后末段可解析为 IPv4 number 才尝试
    std::string_view probe(decoded);
    if (!probe.empty() && probe.back() == '.') probe.remove_suffix(1);
    size_t last_dot = probe.rfind('.');
    std::string_view last =
        (last_dot == std::string_view::npos) ? probe : probe.substr(last_dot + 1);
    bool ends_in_number = !last.empty() && parse_ipv4_number(last).has_value();
    if (ends_in_number) {
      std::string dotted;
      V4Result r = whatwg_parse_ipv4(decoded, dotted);
      if (r == V4Result::Invalid) {
        Error e;
        e.code = 30;
        e.message = "failed to parse subscription URL: invalid IPv4 address";
        return Result<UrlParts>::err(std::move(e));
      }
      if (r == V4Result::Ok) {
        parts.host = dotted;
        parts.is_ipv4 = true;
      } else {
        parts.host = to_lower(decoded);  // >4 段 → 域名（WHATWG "return input"）
      }
    } else {
      parts.host = to_lower(decoded);
    }
  }

  // path / query / fragment
  size_t frag = tail.find('#');
  std::string_view before_frag = tail;
  if (frag != std::string_view::npos) {
    parts.fragment = std::string(tail.substr(frag + 1));
    before_frag = tail.substr(0, frag);
  }
  size_t q = before_frag.find('?');
  if (q != std::string_view::npos) {
    parts.query = std::string(before_frag.substr(q + 1));
    parts.path = std::string(before_frag.substr(0, q));
  } else {
    parts.path = std::string(before_frag);
  }
  if (parts.path.empty()) parts.path = "/";
  return Result<UrlParts>::ok(std::move(parts));
}

// ---------- 订阅响应头解析纯函数 ----------

// ← help.rs::parse_str::<u64>：';' 分段 → trim → splitn(2,'=') → 键精确匹配
std::optional<uint64_t> parse_str_u64(std::string_view target,
                                      std::string_view key) {
  size_t start = 0;
  while (start <= target.size()) {
    size_t semi = target.find(';', start);
    std::string_view seg = (semi == std::string_view::npos)
                               ? target.substr(start)
                               : target.substr(start, semi - start);
    const std::string seg_str = trim(seg);
    size_t eq = seg_str.find('=');
    if (eq != std::string::npos) {
      std::string_view k = seg_str;
      k = k.substr(0, eq);
      std::string_view v = seg_str;
      v = v.substr(eq + 1);
      if (k == key) {
        // v.parse::<u64>()：全数字（可带 '+'）
        std::string_view num = v;
        if (!num.empty() && num.front() == '+') num.remove_prefix(1);
        if (!num.empty() && num.find_first_not_of("0123456789") == std::string_view::npos) {
          uint64_t val = 0;
          bool overflow = false;
          for (char c : num) {
            uint64_t d = static_cast<uint64_t>(c - '0');
            if (val > (UINT64_MAX - d) / 10) {
              overflow = true;
              break;
            }
            val = val * 10 + d;
          }
          if (!overflow) return val;
        }
        return std::nullopt;  // 键匹配但解析失败 → find_map 终止
      }
    }
    if (semi == std::string_view::npos) break;
    start = semi + 1;
  }
  return std::nullopt;
}

// ← prfitem.rs：头名（小写）等于 subscription-userinfo，或以
// "-subscription-userinfo" 结尾（prefix.is_empty() || prefix.ends_with('-')）
bool is_subscription_userinfo_header(std::string_view lower_name) {
  constexpr std::string_view kSuffix = "subscription-userinfo";
  if (lower_name == kSuffix) return true;
  if (lower_name.size() <= kSuffix.size()) return false;
  if (lower_name.substr(lower_name.size() - kSuffix.size()) != kSuffix) {
    return false;
  }
  return lower_name[lower_name.size() - kSuffix.size() - 1] == '-';
}

// ← Subscription-Userinfo 头解析（值形如 "upload=704784; download=...; ..."）
std::optional<SubscriptionUserinfo> parse_subscription_userinfo(
    std::string_view value) {
  auto u = parse_str_u64(value, "upload");
  auto d = parse_str_u64(value, "download");
  auto t = parse_str_u64(value, "total");
  auto e = parse_str_u64(value, "expire");
  if (!u && !d && !t && !e) return std::nullopt;
  SubscriptionUserinfo out;
  out.upload = u.value_or(0);
  out.download = d.value_or(0);
  out.total = t.value_or(0);
  out.expire = e.value_or(0);
  return out;
}

// ← Content-Disposition filename*（RFC 5987）/filename
// 原版先把头值做 Debug 加引号再 trim（见任务踩坑记录），此处等价实现在原文
// 上直接提取参数：filename* 优先（percent-decode 后按 "''" 取最后一段），
// 其次 filename（剥包裹引号）。
namespace {

std::optional<std::string> cd_param(std::string_view value,
                                    std::string_view key) {
  size_t start = 0;
  while (start <= value.size()) {
    size_t semi = value.find(';', start);
    std::string_view seg = (semi == std::string_view::npos)
                               ? value.substr(start)
                               : value.substr(start, semi - start);
    const std::string seg_str = trim(seg);
    size_t eq = seg_str.find('=');
    if (eq != std::string::npos &&
        std::string_view(seg_str).substr(0, eq) == key) {
      return seg_str.substr(eq + 1);
    }
    if (semi == std::string_view::npos) break;
    start = semi + 1;
  }
  return std::nullopt;
}

std::string trim_quotes(std::string s) {
  // 原版经 Rust Debug 转义后出现的 \" 序列归一为纯引号，再剥两端引号
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '"') continue;
    out += s[i];
  }
  size_t b = out.find_first_not_of('"');
  size_t e = out.find_last_not_of('"');
  if (b == std::string::npos) return "";
  return out.substr(b, e - b + 1);
}

}  // namespace

std::optional<std::string> parse_content_disposition_filename(
    std::string_view value) {
  if (auto fn_star = cd_param(value, "filename*")) {
    std::string decoded = util::percent_decode(*fn_star);
    // split("''").last()
    size_t pos = decoded.rfind("''");
    if (pos != std::string::npos) {
      return decoded.substr(pos + 2);
    }
    return decoded;
  }
  if (auto fn = cd_param(value, "filename")) {
    return trim_quotes(*fn);
  }
  return std::nullopt;
}

// ← help.rs::get_last_part_and_decode
std::string get_last_part_and_decode(std::string_view url) {
  std::string_view path = url.substr(0, url.find('?'));
  size_t last_slash = path.rfind('/');
  std::string_view last =
      (last_slash == std::string_view::npos) ? path : path.substr(last_slash + 1);
  return util::percent_decode(last);
}

// ← profile-update-interval：时 → 分
std::optional<uint64_t> parse_profile_update_interval(std::string_view value) {
  std::string v = trim(value);
  if (v.empty() || v.find_first_not_of("0123456789") != std::string_view::npos) {
    return std::nullopt;
  }
  uint64_t val = 0;
  for (char c : v) {
    uint64_t d = static_cast<uint64_t>(c - '0');
    if (val > (UINT64_MAX - d) / 10) return std::nullopt;
    val = val * 10 + d;
  }
  return val * 60;
}

// ---------- 系统代理读取（← sysproxy::Sysproxy::get_system_proxy 使用面） ----------

std::optional<std::string> read_system_proxy() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet "
                    L"Settings",
                    0, KEY_READ, &key) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  std::optional<std::string> result;
  DWORD enable = 0;
  DWORD size = sizeof(enable);
  if (RegQueryValueExW(key, L"ProxyEnable", nullptr, nullptr,
                       reinterpret_cast<LPBYTE>(&enable),
                       &size) == ERROR_SUCCESS &&
      enable != 0) {
    wchar_t buf[2048];
    size = sizeof(buf);
    DWORD type = 0;
    if (RegQueryValueExW(key, L"ProxyServer", nullptr, &type,
                         reinterpret_cast<LPBYTE>(buf),
                         &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) {
      std::string server = utf16_to_utf8(buf);
      // "http=host:port;https=..." 形态 → 取 http 条目
      if (server.find('=') != std::string::npos) {
        size_t start = 0;
        while (start <= server.size()) {
          size_t semi = server.find(';', start);
          std::string entry = (semi == std::string::npos)
                                  ? server.substr(start)
                                  : server.substr(start, semi - start);
          if (starts_with(entry, "http=")) {
            server = entry.substr(5);
            break;
          }
          if (semi == std::string::npos) break;
          start = semi + 1;
        }
      }
      // 剥 "http://" 前缀
      if (starts_with(server, "http://")) server = server.substr(7);
      server = trim(server);
      if (!server.empty()) result = server;
    }
  }
  RegCloseKey(key);
  return result;
}

// ---------- 请求执行 ----------

namespace {

void CALLBACK security_callback(HINTERNET h, DWORD_PTR, DWORD status,
                                LPVOID, DWORD) {
  if (status == WINHTTP_CALLBACK_STATUS_SENDING_REQUEST) {
    DWORD_PTR ctx = 0;
    DWORD sz = sizeof(ctx);
    if (WinHttpQueryOption(h, WINHTTP_OPTION_CONTEXT_VALUE, &ctx, &sz) &&
        ctx == 1) {
      DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                    SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                    SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                    SECURITY_FLAG_IGNORE_REVOCATION;
      WinHttpSetOption(h, WINHTTP_OPTION_SECURITY_FLAGS, &flags,
                       sizeof(flags));
    }
  }
}

// Location → 绝对 URL（绝对 / 根相对 / 相对当前目录）
std::string absolutize(const UrlParts& cur, const std::string& location) {
  if (starts_with(location, "http://") || starts_with(location, "https://")) {
    return location;
  }
  std::string base = cur.scheme + "://" + cur.host_header();
  if (!location.empty() && location.front() == '/') {
    return base + location;
  }
  // 相对路径：去掉当前 path 最后一段
  std::string dir = cur.path;
  size_t slash = dir.rfind('/');
  dir = (slash == std::string::npos) ? "/" : dir.substr(0, slash + 1);
  return base + dir + location;
}

struct RawResponse {
  uint16_t status = 0;
  std::vector<HttpHeader> headers;
  std::vector<std::string> locations;  // 同名 Location 可能多个
};

// 读完整响应头（RAW_HEADERS_CRLF 拆行）
Result<RawResponse> receive_headers(HINTERNET request) {
  DWORD size = 0;
  WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                      WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER,
                      &size, WINHTTP_NO_HEADER_INDEX);
  std::wstring raw(size / sizeof(wchar_t) + 1, L'\0');
  if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                           WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &size,
                           WINHTTP_NO_HEADER_INDEX)) {
    Error e;
    e.code = 32;
    e.message = last_error_msg("WinHttpQueryHeaders", GetLastError());
    return Result<RawResponse>::err(std::move(e));
  }
  std::string text = utf16_to_utf8(raw.c_str());

  RawResponse out;
  size_t start = 0;
  bool first = true;
  while (start <= text.size()) {
    size_t nl = text.find("\r\n", start);
    std::string line = (nl == std::string::npos)
                           ? text.substr(start)
                           : text.substr(start, nl - start);
    if (first) {
      first = false;
      // "HTTP/1.1 200 OK"
      size_t sp1 = line.find(' ');
      if (sp1 != std::string::npos) {
        size_t sp2 = line.find(' ', sp1 + 1);
        std::string code = (sp2 == std::string::npos)
                               ? line.substr(sp1 + 1)
                               : line.substr(sp1 + 1, sp2 - sp1 - 1);
        out.status = static_cast<uint16_t>(atoi(code.c_str()));
      }
    } else if (!line.empty()) {
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        HttpHeader h;
        h.name = trim(line.substr(0, colon));
        h.value = trim(line.substr(colon + 1));
        if (h.name == "Location") out.locations.push_back(h.value);
        out.headers.push_back(std::move(h));
      }
    }
    if (nl == std::string::npos) break;
    start = nl + 2;
  }
  return Result<RawResponse>::ok(std::move(out));
}

// 重定向后的方法/体（reqwest::redirect 语义）
struct RedirectAdjust {
  std::string method;
  std::optional<std::string> body;
};

RedirectAdjust redirect_adjust(const std::string& method,
                               const std::optional<std::string>& body,
                               uint16_t status) {
  if (status == 307 || status == 308) {
    return {method, body};  // 保持方法与体
  }
  if (status == 301 || status == 302 || status == 303) {
    if (method == "GET" || method == "HEAD") return {method, body};
    return {"GET", std::nullopt};  // POST 等 → GET 并丢体
  }
  return {method, body};
}

}  // namespace

namespace detail {

Result<HttpResponse> http_request(const std::string& method_in,
                                  const std::string& url,
                                  std::optional<std::string> body_in,
                                  const HttpOptions& opts) {
  const uint64_t timeout_s = opts.timeout_seconds.value_or(kDefaultTimeoutS);
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(timeout_s);
  auto remain_ms = [&]() -> int {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now())
                    .count();
    return static_cast<int>(std::max<int64_t>(0, left));
  };

  // ← network.rs：User-Agent 默认 clash-verge/v{version}
  const std::string ua = opts.user_agent.value_or(kDefaultUserAgent);

  // ← 代理三态（ProxyType::Localhost 端口经注入 resolver）
  std::optional<std::string> proxy;
  switch (opts.proxy_type) {
    case ProxyType::None:
      break;
    case ProxyType::Localhost: {
      uint16_t port = config::consts::kDefaultMixed;  // Clash Win 默认 mixed-port（错开 Mini 10801）
      if (opts.localhost_mixed_port) {
        if (auto p = opts.localhost_mixed_port()) port = *p;
      }
      proxy = "127.0.0.1:" + std::to_string(port);
      break;
    }
    case ProxyType::System:
      proxy = read_system_proxy();  // 系统代理未启用 → 直连（← network.rs）
      break;
  }

  HINTERNET session =
      WinHttpOpen(utf8_to_utf16(ua).c_str(), WINHTTP_ACCESS_TYPE_NO_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    Error e;
    e.code = 31;
    e.message = last_error_msg("WinHttpOpen", GetLastError());
    return Result<HttpResponse>::err(std::move(e));
  }

  std::string current_url = url;
  std::string method = method_in;
  std::optional<std::string> body = std::move(body_in);
  std::optional<std::string> auth_header;
  bool first_leg = true;
  int redirects = 0;
  std::optional<HttpResponse> final_resp;
  std::optional<Error> final_err;

  for (;;) {
    if (cancel_signaled(opts.cancel_event)) {
      Error e;
      e.code = 39;
      e.message = "request cancelled";
      final_err = std::move(e);
      break;
    }
    auto pr = parse_http_url(current_url);
    if (!pr.is_ok()) {
      final_err = pr.error();
      break;
    }
    UrlParts parts = pr.value();
    // ← network.rs get_with_tls_mode：URL 内嵌 userinfo → Authorization Basic，
    //   仅首段使用（重定向 host 变更时剥除）
    if (first_leg && (!parts.username.empty() || !parts.password.empty())) {
      std::string user = util::percent_decode(parts.username);
      std::string pass = util::percent_decode(parts.password);
      std::string cred = user + ":" + pass;
      auth_header =
          "Basic " + util::encode_base64(cred);
    }
    first_leg = false;

    const bool is_https = parts.scheme == "https";
    const uint16_t port = parts.effective_port();

    HINTERNET connect = WinHttpConnect(
        session, utf8_to_utf16(parts.host).c_str(),
        port == 80 && !is_https && parts.port_str.empty()
            ? INTERNET_DEFAULT_HTTP_PORT
            : (port == 443 && is_https && parts.port_str.empty()
                   ? INTERNET_DEFAULT_HTTPS_PORT
                   : port),
        0);
    if (!connect) {
      Error e;
      e.code = 31;
      e.message = last_error_msg("WinHttpConnect", GetLastError());
      final_err = std::move(e);
      break;
    }
    HINTERNET request = WinHttpOpenRequest(
        connect, utf8_to_utf16(method).c_str(),
        utf8_to_utf16(parts.request_target()).c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        is_https ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
      Error e;
      e.code = 31;
      e.message = last_error_msg("WinHttpOpenRequest", GetLastError());
      WinHttpCloseHandle(connect);
      final_err = std::move(e);
      break;
    }

    // 代理（请求级覆盖）
    WINHTTP_PROXY_INFO pi{};
    if (proxy) {
      std::wstring proxy_w = utf8_to_utf16(*proxy);
      pi.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
      pi.lpszProxy = const_cast<LPWSTR>(proxy_w.c_str());
      WinHttpSetOption(request, WINHTTP_OPTION_PROXY, &pi, sizeof(pi));
    } else {
      pi.dwAccessType = WINHTTP_ACCESS_TYPE_NO_PROXY;
      WinHttpSetOption(request, WINHTTP_OPTION_PROXY, &pi, sizeof(pi));
    }

    // 手动重定向（reqwest limited(10) 对齐；跨 host 剥 Authorization）
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy,
                     sizeof(policy));

    // 阶段超时（剩余预算；connect 上限 min(t,10) ← connect_timeout）
    int rem = remain_ms();
    int connect_ms = std::min(rem, static_cast<int>(std::min<uint64_t>(timeout_s, 10) * 1000));
    WinHttpSetTimeouts(request, connect_ms, connect_ms, rem, rem);

    if (is_https && opts.accept_invalid_certs) {
      WinHttpSetStatusCallback(request, security_callback,
                               WINHTTP_CALLBACK_STATUS_SENDING_REQUEST, 0);
      DWORD_PTR ctx = 1;
      WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &ctx,
                       sizeof(ctx));
    }

    std::string req_headers = "User-Agent: " + ua + "\r\n";
    if (auth_header) req_headers += "Authorization: " + *auth_header + "\r\n";
    std::wstring headers_w = utf8_to_utf16(req_headers);

    BOOL sent;
    if (body) {
      sent = WinHttpSendRequest(
          request, headers_w.c_str(),
          static_cast<DWORD>(headers_w.size()), body->data(),
          static_cast<DWORD>(body->size()),
          static_cast<DWORD>(body->size()), 0);
    } else {
      sent = WinHttpSendRequest(request, headers_w.c_str(),
                                static_cast<DWORD>(headers_w.size()),
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    }
    if (!sent) {
      Error e;
      e.code = 33;
      e.message = "Request failed: " + last_error_msg("WinHttpSendRequest",
                                                      GetLastError());
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      final_err = std::move(e);
      break;
    }
    if (!WinHttpReceiveResponse(request, nullptr)) {
      Error e;
      e.code = 33;
      e.message = "Request failed: " + last_error_msg("WinHttpReceiveResponse",
                                                      GetLastError());
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      final_err = std::move(e);
      break;
    }

    auto hr = receive_headers(request);
    if (!hr.is_ok()) {
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      final_err = hr.error();
      break;
    }
    RawResponse raw = hr.value();

    // 重定向判定
    bool is_redirect = (raw.status == 301 || raw.status == 302 ||
                        raw.status == 303 || raw.status == 307 ||
                        raw.status == 308) &&
                       !raw.locations.empty();
    if (is_redirect && redirects < opts.max_redirects) {
      std::string location = raw.locations.front();
      std::string next = absolutize(parts, location);
      auto np = parse_http_url(next);
      if (!np.is_ok()) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        Error e;
        e.code = 34;
        e.message = "invalid redirect location";
        final_err = std::move(e);
        break;
      }
      // host 变更 → 剥 Authorization（reqwest 敏感头策略）
      const UrlParts& next_parts = np.value();
      if (next_parts.host != parts.host ||
          next_parts.effective_port() != parts.effective_port() ||
          next_parts.scheme != parts.scheme) {
        auth_header.reset();
      }
      RedirectAdjust adj = redirect_adjust(method, body, raw.status);
      method = adj.method;
      body = adj.body;
      current_url = next;
      ++redirects;
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      continue;
    }

    // 最终响应：读体（← network.rs 分块累计 + 上限 + UTF-8 严格校验）
    HttpResponse resp;
    resp.status = raw.status;
    resp.headers = std::move(raw.headers);
    std::string body_out;
    bool failed = false;
    for (;;) {
      if (cancel_signaled(opts.cancel_event)) {
        Error e;
        e.code = 39;
        e.message = "request cancelled";
        final_err = std::move(e);
        failed = true;
        break;
      }
      if (remain_ms() <= 0) {
        Error e;
        e.code = 33;
        e.message = "Stream read error: operation timed out";
        final_err = std::move(e);
        failed = true;
        break;
      }
      DWORD avail = 0;
      if (!WinHttpQueryDataAvailable(request, &avail)) {
        Error e;
        e.code = 35;
        e.message = "Stream read error: " +
                    last_error_msg("WinHttpQueryDataAvailable", GetLastError());
        final_err = std::move(e);
        failed = true;
        break;
      }
      if (avail == 0) break;
      if (body_out.size() + avail > opts.max_response_bytes) {
        Error e;
        e.code = 36;
        e.message = "Subscription file size exceeds the 50MB limit";
        final_err = std::move(e);
        failed = true;
        break;
      }
      std::string buf(avail, '\0');
      DWORD got = 0;
      if (!WinHttpReadData(request, buf.data(), avail, &got)) {
        Error e;
        e.code = 35;
        e.message = "Stream read error: " +
                    last_error_msg("WinHttpReadData", GetLastError());
        final_err = std::move(e);
        failed = true;
        break;
      }
      buf.resize(got);
      body_out += buf;
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    if (failed) break;
    // ← String::from_utf8 严格校验
    if (!util::is_valid_utf8(body_out)) {
      Error e;
      e.code = 37;
      e.message = "Failed to decode response as UTF-8";
      final_err = std::move(e);
      break;
    }
    resp.body = std::move(body_out);
    final_resp = std::move(resp);
    break;
  }

  WinHttpCloseHandle(session);
  if (final_err) return Result<HttpResponse>::err(std::move(*final_err));
  return Result<HttpResponse>::ok(std::move(*final_resp));
}

}  // namespace detail

Result<HttpResponse> http_get(const std::string& url, const HttpOptions& opts) {
  return detail::http_request("GET", url, std::nullopt, opts);
}

Result<HttpResponse> http_post(const std::string& url, std::string body,
                               const HttpOptions& opts) {
  return detail::http_request("POST", url, std::move(body), opts);
}

Result<void> http_download_file(
    const std::string& url, const std::wstring& dest_path, const HttpOptions& opts,
    std::function<void(uint64_t, uint64_t)> on_progress) {
  const uint64_t timeout_s = opts.timeout_seconds.value_or(kDefaultTimeoutS);
  const std::string ua = opts.user_agent.value_or(kDefaultUserAgent);

  // ← network.rs：代理三态（与 http_request 同口径）
  std::optional<std::string> proxy;
  switch (opts.proxy_type) {
    case ProxyType::None:
      break;
    case ProxyType::Localhost: {
      uint16_t port = config::consts::kDefaultMixed;
      if (opts.localhost_mixed_port) {
        if (auto p = opts.localhost_mixed_port()) port = *p;
      }
      proxy = "127.0.0.1:" + std::to_string(port);
      break;
    }
    case ProxyType::System:
      proxy = read_system_proxy();
      break;
  }

  HINTERNET session = WinHttpOpen(utf8_to_utf16(ua).c_str(),
                                  WINHTTP_ACCESS_TYPE_NO_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    Error e;
    e.code = 31;
    e.message = last_error_msg("WinHttpOpen", GetLastError());
    return Result<void>::err(std::move(e));
  }

  std::string current_url = url;
  int redirects = 0;
  for (;;) {
    if (cancel_signaled(opts.cancel_event)) {
      WinHttpCloseHandle(session);
      return Result<void>::err(Error{39, "request cancelled"});
    }
    auto pr = parse_http_url(current_url);
    if (!pr.is_ok()) {
      WinHttpCloseHandle(session);
      return Result<void>::err(pr.error());
    }
    UrlParts parts = pr.value();
    const bool is_https = parts.scheme == "https";

    HINTERNET connect = WinHttpConnect(session,
                                       utf8_to_utf16(parts.host).c_str(),
                                       parts.effective_port(), 0);
    if (!connect) {
      WinHttpCloseHandle(session);
      return Result<void>::err(Error{31, "WinHttpConnect failed"});
    }
    HINTERNET request = WinHttpOpenRequest(
        connect, L"GET", utf8_to_utf16(parts.request_target()).c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        is_https ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
      WinHttpCloseHandle(connect);
      WinHttpCloseHandle(session);
      return Result<void>::err(Error{31, "WinHttpOpenRequest failed"});
    }

    WINHTTP_PROXY_INFO pi{};
    std::wstring proxy_w;
    if (proxy) {
      proxy_w = utf8_to_utf16(*proxy);
      pi.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
      pi.lpszProxy = const_cast<LPWSTR>(proxy_w.c_str());
    } else {
      pi.dwAccessType = WINHTTP_ACCESS_TYPE_NO_PROXY;
    }
    WinHttpSetOption(request, WINHTTP_OPTION_PROXY, &pi, sizeof(pi));

    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy,
                     sizeof(policy));

    const int total_ms = static_cast<int>(std::min<uint64_t>(timeout_s, 600) * 1000);
    const int connect_ms = static_cast<int>(std::min<uint64_t>(timeout_s, 10) * 1000);
    WinHttpSetTimeouts(request, connect_ms, connect_ms, total_ms, total_ms);

    std::string req_headers = "User-Agent: " + ua + "\r\n";
    std::wstring headers_w = utf8_to_utf16(req_headers);
    if (!WinHttpSendRequest(request, headers_w.c_str(),
                            static_cast<DWORD>(headers_w.size()),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
      Error e{33, "Request failed: " + last_error_msg("WinHttpSendRequest",
                                                      GetLastError())};
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      WinHttpCloseHandle(session);
      return Result<void>::err(std::move(e));
    }

    auto hr = receive_headers(request);
    if (!hr.is_ok()) {
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      WinHttpCloseHandle(session);
      return Result<void>::err(hr.error());
    }
    const RawResponse& raw = hr.value();
    if ((raw.status == 301 || raw.status == 302 || raw.status == 303 ||
         raw.status == 307 || raw.status == 308) &&
        !raw.locations.empty() && redirects < opts.max_redirects) {
      current_url = absolutize(parts, raw.locations.front());
      ++redirects;
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      continue;
    }
    if (raw.status < 200 || raw.status > 299) {
      std::string msg = "HTTP status error: " + std::to_string(raw.status);
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      WinHttpCloseHandle(session);
      return Result<void>::err(Error{33, msg});
    }

    uint64_t total = 0;
    for (const auto& h : raw.headers) {
      if (to_lower(h.name) == "content-length") {
        try {
          total = std::stoull(h.value);
        } catch (...) {
          total = 0;
        }
        break;
      }
    }

    std::ofstream out(dest_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      WinHttpCloseHandle(session);
      return Result<void>::err(Error{31, "failed to open destination file"});
    }

    uint64_t downloaded = 0;
    std::vector<char> buf(65536);
    for (;;) {
      if (cancel_signaled(opts.cancel_event)) {
        out.close();
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return Result<void>::err(Error{39, "request cancelled"});
      }
      DWORD avail = 0;
      if (!WinHttpQueryDataAvailable(request, &avail)) {
        out.close();
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return Result<void>::err(Error{35, "Stream read error: " +
                                           last_error_msg("WinHttpQueryDataAvailable",
                                                           GetLastError())});
      }
      if (avail == 0) break;
      if (avail > buf.size()) buf.resize(avail);
      DWORD got = 0;
      if (!WinHttpReadData(request, buf.data(), avail, &got)) {
        out.close();
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return Result<void>::err(Error{35, "Stream read error: " +
                                           last_error_msg("WinHttpReadData",
                                                          GetLastError())});
      }
      if (got > 0) out.write(buf.data(), got);
      downloaded += got;
      if (out.bad()) {
        out.close();
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return Result<void>::err(Error{31, "failed to write download chunk"});
      }
      if (on_progress) on_progress(downloaded, total);
    }
    out.close();

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return Result<void>::ok();
  }
}

}  // namespace clashwin::system
