#include "util/node.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <string_view>

#include "util/str_utils.h"

namespace clashwin::util {

namespace {

// ← node.rs::normalize_dummy_name：剥掉两端包裹符号（【】()（）[]「」，可
// 多层）与通用前缀「节点-」等。不剥区域/协议前缀（"CN2-"/"HK-"），以免误杀。
// UTF-8 多字节符号按字节串处理（均落在合法边界上）。
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
  // 前缀只剥一层（Rust strip_prefix 语义），命中后 trim
  static constexpr std::string_view kPrefixes[] = {
      "\xE8\x8A\x82\xE7\x82\xB9-",  // 节点-
      "\xE8\x8A\x82\xE7\x82\xB9:",  // 节点:
      "\xE8\x8A\x82\xE7\x82\xB9\xEF\xBC\x9A",  // 节点：
      "\xE8\x8A\x82\xE7\x82\xB9 ",  // 节点␠
  };
  for (const auto& p : kPrefixes) {
    if (starts_with(s, p)) {
      s = trim(s.substr(p.size()));
      break;
    }
  }
  return s;
}

}  // namespace

bool is_dummy_node(const std::string& name) {
  const std::string lower = to_lower(name);
  // 高置信度：包含 URL scheme 或明确的英文广告词
  if (lower.find("http://") != std::string::npos ||
      lower.find("https://") != std::string::npos ||
      lower.find("expire") != std::string::npos ||
      lower.find("traffic") != std::string::npos ||
      lower.find("website") != std::string::npos ||
      lower.find("subscribe") != std::string::npos) {
    return true;
  }
  const std::string normalized = normalize_dummy_name(lower);
  // 中文广告短语：完全匹配或以短语开头
  static constexpr std::string_view kAdPhrases[] = {
      "\xE6\xB5\x81\xE9\x87\x8F",              // 流量
      "\xE8\xBF\x87\xE6\x9C\x9F\xE6\x97\xB6\xE9\x97\xB4",  // 过期时间
      "\xE7\xBD\x91\xE5\x9D\x80",              // 网址
      "\xE5\xAE\x98\xE7\xBD\x91",              // 官网
      "\xE5\x89\xA9\xE4\xBD\x99",              // 剩余
      "\xE5\xA5\x97\xE9\xA4\x90\xE5\x88\xB0\xE6\x9C\x9F",  // 套餐到期
      "\xE7\xBB\xAD\xE8\xB4\xB9",              // 续费
      "\xE5\x85\xAC\xE5\x91\x8A",              // 公告
      "\xE8\xB4\xAD\xE4\xB9\xB0",              // 购买
      "\xE7\xBE\xA4",                          // 群
  };
  for (const auto& phrase : kAdPhrases) {
    if (normalized == phrase ||
        (normalized.size() >= phrase.size() &&
         normalized.compare(0, phrase.size(), phrase) == 0)) {
      return true;
    }
  }
  return false;
}

bool is_valid_node_server(const std::string& server) {
  const std::string s = trim(server);
  if (s.empty()) return false;
  // ← s.parse::<std::net::IpAddr>()：严格全形式 IP（inet_pton 同口径）
  {
    unsigned char buf[16];
    if (inet_pton(AF_INET, s.c_str(), buf) == 1 ||
        inet_pton(AF_INET6, s.c_str(), buf) == 1) {
      return true;
    }
  }
  return s.find('.') != std::string::npos;
}

}  // namespace clashwin::util
