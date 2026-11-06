// BLAKE2b 实现（RFC 7693 伪代码直译；无密钥模式）。
// 参考实现逐行核对：IV/SIGMA/rotate 量与 RFC 7693 §3.2-3.3 一致。
#include "blake2b.h"

#include <cstring>

namespace clashwin::util {

namespace {

inline uint64_t rotr64(uint64_t x, int n) {
  return (x >> n) | (x << (64 - n));
}

inline uint64_t load64(const uint8_t* p) {
  uint64_t v;
  std::memcpy(&v, p, 8);  // 小端机直读；按 RFC 以小端解释
  return v;
}

// RFC 7693 §3.2 IV（同 SHA-512 IV）
const uint64_t kIV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
    0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};

// RFC 7693 §2.7 SIGMA（12 轮；第 10/11 轮重复第 0/1 轮）
const uint8_t kSigma[12][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
};

constexpr size_t kBlockSize = 128;

// G 混合函数（RFC 7693 §3.1）
inline void g(uint64_t v[16], int a, int b, int c, int d, uint64_t x,
              uint64_t y) {
  v[a] = v[a] + v[b] + x;
  v[d] = rotr64(v[d] ^ v[a], 32);
  v[c] = v[c] + v[d];
  v[b] = rotr64(v[b] ^ v[c], 24);
  v[a] = v[a] + v[b] + y;
  v[d] = rotr64(v[d] ^ v[a], 16);
  v[c] = v[c] + v[d];
  v[b] = rotr64(v[b] ^ v[c], 63);
}

}  // namespace

Blake2b::Blake2b(size_t out_len) : out_len_(out_len) {
  // 参数块（无密钥、fanout=1、depth=1）：h[0] = 0x01010000 ^ out_len
  for (int i = 0; i < 8; ++i) h_[i] = kIV[i];
  h_[0] ^= 0x01010000ULL ^ static_cast<uint64_t>(out_len_);
}

void Blake2b::compress(const uint8_t block[kBlockSize], bool is_last,
                       uint64_t counter) {
  uint64_t m[16];
  for (int i = 0; i < 16; ++i) m[i] = load64(block + i * 8);
  uint64_t v[16];
  for (int i = 0; i < 8; ++i) v[i] = h_[i];
  for (int i = 0; i < 8; ++i) v[8 + i] = kIV[i];
  v[12] ^= counter;  // t = 本块及之前累计字节数（含本块）
  if (is_last) v[14] = ~v[14];
  for (int r = 0; r < 12; ++r) {
    const uint8_t* s = kSigma[r];
    g(v, 0, 4, 8, 12, m[s[0]], m[s[1]]);
    g(v, 1, 5, 9, 13, m[s[2]], m[s[3]]);
    g(v, 2, 6, 10, 14, m[s[4]], m[s[5]]);
    g(v, 3, 7, 11, 15, m[s[6]], m[s[7]]);
    g(v, 0, 5, 10, 15, m[s[8]], m[s[9]]);
    g(v, 1, 6, 11, 12, m[s[10]], m[s[11]]);
    g(v, 2, 7, 8, 13, m[s[12]], m[s[13]]);
    g(v, 3, 4, 9, 14, m[s[14]], m[s[15]]);
  }
  for (int i = 0; i < 8; ++i) h_[i] ^= v[i] ^ v[i + 8];
}

void Blake2b::update(const uint8_t* data, size_t len) {
  total_ += len;
  const uint8_t* p = data;
  size_t remain = len;
  while (remain > 0) {
    size_t take = kBlockSize - buflen_;
    if (take > remain) take = remain;
    std::memcpy(buf_ + buflen_, p, take);
    buflen_ += take;
    p += take;
    remain -= take;
    // 缓冲满且后面还有数据 → 压缩该块（非末块）；最后一块留到 final
    if (buflen_ == kBlockSize && remain > 0) {
      count_ += kBlockSize;
      compress(buf_, false, count_);
      buflen_ = 0;
    }
  }
}

void Blake2b::final(uint8_t* out) {
  count_ += buflen_;  // 末块计数 = 总输入长度（空输入 → 0）
  if (buflen_ < kBlockSize) std::memset(buf_ + buflen_, 0, kBlockSize - buflen_);
  compress(buf_, true, count_);
  for (size_t i = 0; i < out_len_; ++i)
    out[i] = static_cast<uint8_t>(h_[i / 8] >> (8 * (i % 8)));
}

std::vector<uint8_t> Blake2b::digest(const uint8_t* data, size_t len,
                                     size_t out_len) {
  Blake2b b(out_len);
  b.update(data, len);
  std::vector<uint8_t> out(out_len);
  b.final(out.data());
  return out;
}

}  // namespace clashwin::util
