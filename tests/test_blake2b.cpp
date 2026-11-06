// BLAKE2b（RFC 7693）测试：官方向量 + 流式/一次性一致性（minisign 预哈希口径依赖）。
#include "test_util.h"

#include <string>
#include <vector>

#include "util/blake2b.h"

using clashwin::util::Blake2b;

static std::string hex(const std::vector<uint8_t>& v) {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (uint8_t b : v) {
    s += d[b >> 4];
    s += d[b & 0xF];
  }
  return s;
}

UT_TEST(blake2b_512_empty_official) {
  // RFC 7693 Appendix A 官方向量：BLAKE2b-512("")
  auto d = Blake2b::digest(nullptr, 0, 64);
  UT_CHECK_EQ(hex(d),
              "786a02f742015903c6c6fd852552d272912f4740e15847618a86e217f71f541"
              "9d25e1031afee585313896444934eb04b903a685b1448b755d56f701afe9be2c"
              "e");
}

UT_TEST(blake2b_512_abc_official) {
  // RFC 7693 Appendix A 官方向量：BLAKE2b-512("abc")
  const std::string s = "abc";
  auto d = Blake2b::digest(reinterpret_cast<const uint8_t*>(s.data()), s.size(),
                           64);
  UT_CHECK_EQ(hex(d),
              "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
              "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923");
}

UT_TEST(blake2b_512_streaming_matches_oneshot) {
  // 流式（任意分块 update）与一次性 digest 必须一致；覆盖块界 128 前后规模
  for (size_t n : {1u, 127u, 128u, 129u, 255u, 256u, 257u, 1000u, 4096u}) {
    std::vector<uint8_t> input(n);
    for (size_t i = 0; i < n; ++i) input[i] = static_cast<uint8_t>(i * 7 + 3);
    auto one = Blake2b::digest(input.data(), input.size(), 64);
    Blake2b b(64);
    size_t off = 0;
    while (off < n) {
      size_t take = (off % 3) + 1;  // 1..3 字节小块喂入，打穿缓冲逻辑
      if (take > n - off) take = n - off;
      b.update(input.data() + off, take);
      off += take;
    }
    std::vector<uint8_t> streamed(64);
    b.final(streamed.data());
    UT_CHECK_EQ(hex(streamed), hex(one));
  }
}

UT_TEST(blake2b_512_chunk_boundary_128k) {
  // 恰好 128 的整数倍：最后一块须以 f_last=1 压缩（末块留缓冲语义）
  const size_t n = 128 * 3;
  std::vector<uint8_t> input(n, 0xAB);
  auto one = Blake2b::digest(input.data(), input.size(), 64);
  Blake2b b(64);
  b.update(input.data(), 128);
  b.update(input.data() + 128, 128);
  b.update(input.data() + 256, 128);
  std::vector<uint8_t> streamed(64);
  b.final(streamed.data());
  UT_CHECK_EQ(hex(streamed), hex(one));
  // 与 384 字节单次 digest 相同即通过（上面等值断言已覆盖）
}
