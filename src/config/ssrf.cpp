// SSRF 防护（← config/prfitem.rs::validate_url_no_ssrf）
#include "config/ssrf.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <mutex>
#include <vector>

#include "util/str_utils.h"

namespace clashwin::config::ssrf {

using util::starts_with;

namespace {

void ensure_wsa() {
  static std::once_flag flag;
  std::call_once(flag, [] {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
  });
}

Error ssrf_err(const char* msg) {
  Error e;
  e.code = 40;
  e.message = msg;
  return e;
}

}  // namespace

bool ipv4_forbidden(uint32_t ip) {
  const uint8_t a = static_cast<uint8_t>(ip >> 24);
  const uint8_t b = static_cast<uint8_t>(ip >> 16);
  // is_loopback: 127.0.0.0/8
  if (a == 127) return true;
  // is_unspecified: 0.0.0.0
  if (ip == 0) return true;
  // is_private: 10/8, 172.16/12, 192.168/16
  if (a == 10) return true;
  if (a == 172 && b >= 16 && b <= 31) return true;
  if (a == 192 && b == 168) return true;
  // is_link_local: 169.254/16
  if (a == 169 && b == 254) return true;
  return false;
}

bool ipv6_forbidden(const uint16_t segs[8]) {
  // BUG-327：IPv4-mapped IPv6 ::ffff:a.b.c.d（前 80 bit 0 + 0xffff + 32-bit IPv4）
  if (segs[0] == 0 && segs[1] == 0 && segs[2] == 0 && segs[3] == 0 &&
      segs[4] == 0 && segs[5] == 0xffff) {
    const uint32_t mapped_v4 =
        (static_cast<uint32_t>(segs[6]) << 16) | static_cast<uint32_t>(segs[7]);
    if (ipv4_forbidden(mapped_v4)) return true;
  }
  // is_loopback ::1
  bool all_zero = true;
  for (int i = 0; i < 7; ++i) {
    if (segs[i] != 0) all_zero = false;
  }
  if (all_zero && segs[7] == 1) return true;
  // is_unspecified ::
  all_zero = true;
  for (int i = 0; i < 8; ++i) {
    if (segs[i] != 0) all_zero = false;
  }
  if (all_zero) return true;
  // 链路本地 fe80::/10：(seg0 & 0xffc0) == 0xfe80
  if ((segs[0] & 0xffc0) == 0xfe80) return true;
  // 唯一本地 fc00::/7：(seg0 & 0xfe00) == 0xfc00
  if ((segs[0] & 0xfe00) == 0xfc00) return true;
  return false;
}

bool ip_string_forbidden(const wchar_t* ip_wstr) {
  if (!ip_wstr || ip_wstr[0] == L'\0') return false;
  // WinHTTP CONNECTING_TO_SERVER 有时带括号或端口，先剥离首尾空白与括号
  std::wstring clean(ip_wstr);
  while (!clean.empty() && (clean.front() == L' ' || clean.front() == L'[')) {
    clean.erase(clean.begin());
  }
  if (auto rb = clean.find(L']'); rb != std::wstring::npos) {
    clean.resize(rb);
  }
  while (!clean.empty() && clean.back() == L' ') {
    clean.pop_back();
  }
  in_addr v4{};
  if (InetPtonW(AF_INET, clean.c_str(), &v4) == 1) {
    return ipv4_forbidden(ntohl(v4.S_un.S_addr));
  }
  // 处理 IPv4:port（恰好含单个 ':'）
  const size_t first_colon = clean.find(L':');
  if (first_colon != std::wstring::npos &&
      clean.find(L':', first_colon + 1) == std::wstring::npos) {
    std::wstring host_only = clean.substr(0, first_colon);
    if (InetPtonW(AF_INET, host_only.c_str(), &v4) == 1) {
      return ipv4_forbidden(ntohl(v4.S_un.S_addr));
    }
  }
  in6_addr v6{};
  // 剥离 IPv6 %scope_id（若存在）
  if (auto pct = clean.find(L'%'); pct != std::wstring::npos) {
    clean.resize(pct);
  }
  if (InetPtonW(AF_INET6, clean.c_str(), &v6) == 1) {
    uint16_t segs[8];
    for (int i = 0; i < 8; ++i) {
      segs[i] =
          static_cast<uint16_t>((v6.u.Byte[i * 2] << 8) | v6.u.Byte[i * 2 + 1]);
    }
    return ipv6_forbidden(segs);
  }
  return false;
}

bool sockaddr_forbidden(const void* sa) {
  const sockaddr* addr = static_cast<const sockaddr*>(sa);
  if (addr->sa_family == AF_INET) {
    const auto* in4 = reinterpret_cast<const sockaddr_in*>(addr);
    uint32_t ip = ntohl(in4->sin_addr.S_un.S_addr);
    return ipv4_forbidden(ip);
  }
  if (addr->sa_family == AF_INET6) {
    const auto* in6 = reinterpret_cast<const sockaddr_in6*>(addr);
    uint16_t segs[8];
    const uint8_t* b = in6->sin6_addr.u.Byte;
    for (int i = 0; i < 8; ++i) {
      segs[i] = static_cast<uint16_t>((b[i * 2] << 8) | b[i * 2 + 1]);
    }
    return ipv6_forbidden(segs);
  }
  return false;
}

Result<void> validate_url_no_ssrf(const system::UrlParts& url) {
  // 1. 静态字符串与直接 IP 校验
  if (url.is_ipv4) {
    // host 已按 WHATWG 规则归一为点分四段
    std::vector<std::string> segs;
    size_t start = 0;
    for (;;) {
      size_t dot = url.host.find('.', start);
      if (dot == std::string::npos) {
        segs.push_back(url.host.substr(start));
        break;
      }
      segs.push_back(url.host.substr(start, dot - start));
      start = dot + 1;
    }
    bool ok = segs.size() == 4;
    uint32_t ip = 0;
    if (ok) {
      for (const auto& seg : segs) {
        if (seg.empty() || seg.size() > 3 ||
            seg.find_first_not_of("0123456789") != std::string::npos) {
          ok = false;
          break;
        }
        int v = std::stoi(seg);
        if (v > 255) {
          ok = false;
          break;
        }
        ip = (ip << 8) | static_cast<uint32_t>(v);
      }
    }
    if (ok && ipv4_forbidden(ip)) {
      return Result<void>::err(
          ssrf_err("cannot fetch subscription from private/loopback IP"));
    }
  } else if (url.is_ipv6) {
    in6_addr a{};
    if (inet_pton(AF_INET6, url.host.c_str(), &a) == 1) {
      uint16_t segs[8];
      for (int i = 0; i < 8; ++i) {
        segs[i] = static_cast<uint16_t>((a.u.Byte[i * 2] << 8) | a.u.Byte[i * 2 + 1]);
      }
      if (ipv6_forbidden(segs)) {
        return Result<void>::err(
            ssrf_err("cannot fetch subscription from local/link-local IP"));
      }
    }
  } else {
    // ← url::Host::Domain 分支（prfitem.rs:141-162，BUG-327）：
    //   剥尾随 '.' 后拦截空域名、无 '.' 单标签内网名、localhost 及保留内网后缀
    std::string lower = util::to_lower(url.host);
    while (!lower.empty() && lower.back() == '.') {
      lower.pop_back();
    }
    auto has_suffix = [&](std::string_view suf) {
      return lower.size() >= suf.size() &&
             std::string_view(lower).substr(lower.size() - suf.size()) == suf;
    };
    if (lower.empty() || lower.find('.') == std::string::npos ||
        lower == "localhost" || has_suffix(".localhost") ||
        has_suffix(".local") || has_suffix(".internal") ||
        has_suffix(".lan") || has_suffix(".home.arpa") ||
        lower == "127.0.0.1" || lower == "::1" || lower == "0.0.0.0" ||
        starts_with(lower, "169.254.")) {
      return Result<void>::err(ssrf_err(
          "cannot fetch subscription from localhost/internal address"));
    }
  }

  // 2. 动态 DNS 解析校验，防止 DNS 重绑定绕过
  //    （← Rust：解析失败不拒绝；端口 unwrap_or(80) 仅影响 getaddrinfo 服务名）
  ensure_wsa();
  const std::string port_str =
      url.port_str.empty() ? "80" : url.port_str;
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* result = nullptr;
  if (getaddrinfo(url.host.c_str(), port_str.c_str(), &hints, &result) == 0 &&
      result != nullptr) {
    for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
      if (sockaddr_forbidden(it->ai_addr)) {
        freeaddrinfo(result);
        return Result<void>::err(
            ssrf_err("cannot fetch subscription from private/loopback IP"));
      }
    }
  }
  if (result) freeaddrinfo(result);
  return Result<void>::ok();
}

}  // namespace clashwin::config::ssrf
