#include "util/base64.h"

#include <string>

namespace clashwin::util {

namespace {

// 与 base64 crate 的 STANDARD / URL_SAFE 引擎对齐：
// - 严格要求 canonical padding（长度 %4==0，'=' 仅在末尾，1 或 2 个）
// - 末组被丢弃的低位必须为 0（decode_allow_trailing_bits = false 默认）
struct Alphabet {
  char std_char[64];
  // 返回字符在字符表中的 6bit 值；不在表内返回 -1
  int value_of(char c) const {
    for (int i = 0; i < 64; ++i)
      if (std_char[i] == c) return i;
    return -1;
  }
};

Alphabet standard_alphabet() {
  Alphabet a{};
  const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                      "abcdefghijklmnopqrstuvwxyz"
                      "0123456789+/";
  for (int i = 0; i < 64; ++i) a.std_char[i] = chars[i];
  return a;
}

Alphabet urlsafe_alphabet() {
  Alphabet a{};
  const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                      "abcdefghijklmnopqrstuvwxyz"
                      "0123456789-_";
  for (int i = 0; i < 64; ++i) a.std_char[i] = chars[i];
  return a;
}

bool is_ascii_ws(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

// 严格解码：要求 canonical padding + 末组剩余位为 0
bool decode_strict(const std::string& s, const Alphabet& alpha,
                   std::vector<uint8_t>& out) {
  const size_t n = s.size();
  if (n == 0 || n % 4 != 0) return false;

  size_t data_len = n;
  size_t pad = 0;
  while (data_len > 0 && s[data_len - 1] == '=') {
    --data_len;
    ++pad;
  }
  if (pad > 2) return false;
  // '=' 之后不允许再有数据字符（上面从尾部收缩已保证）

  out.clear();
  out.reserve((n / 4) * 3);

  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < n; ++i) {
    char c = s[i];
    if (i >= data_len) continue;  // '=' padding（尾部已收缩认定）
    int v = alpha.value_of(c);
    if (v < 0) return false;
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      uint8_t byte = static_cast<uint8_t>((acc >> bits) & 0xFF);
      if (i + 1 == data_len && bits > 0) {
        // 末数据字符：检查被丢弃的低位是否全 0（canonical）
        if ((acc & ((1u << bits) - 1)) != 0) return false;
      }
      out.push_back(byte);
    }
  }
  // padding 数与剩余位一致性：pad==1 → 末组 3 字节；pad==2 → 末组 2 字节。
  // 由 canonical 检查隐式保证：pad 数不符时 trailing bits 必非 0 或长度矛盾。
  return true;
}

}  // namespace

std::optional<std::vector<uint8_t>> decode_base64_robust(std::string_view s) {
  // ← universal_parser.rs:44-78
  // trim 后剥离全部空白（含换行）
  size_t b = 0, e = s.size();
  while (b < e && is_ascii_ws(s[b])) ++b;
  while (e > b && is_ascii_ws(s[e - 1])) --e;
  std::string clean;
  clean.reserve(e - b);
  for (size_t i = b; i < e; ++i)
    if (!is_ascii_ws(s[i])) clean += s[i];
  if (clean.empty()) return std::nullopt;

  std::vector<uint8_t> out;
  const Alphabet std_a = standard_alphabet();
  const Alphabet url_a = urlsafe_alphabet();

  // 1) standard（严格 padding）
  if (decode_strict(clean, std_a, out)) return out;
  // 2) urlsafe（严格 padding）
  if (decode_strict(clean, url_a, out)) return out;

  // 3) 补 padding 后重试（两种字符集）
  std::string padded = clean;
  while (padded.size() % 4 != 0) padded += '=';
  if (decode_strict(padded, std_a, out)) return out;
  if (decode_strict(padded, url_a, out)) return out;

  return std::nullopt;
}

}  // namespace clashwin::util
