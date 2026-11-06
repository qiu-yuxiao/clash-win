#pragma once
// BLAKE2b（RFC 7693）哈希。
// 用途：B-D44 更新器验签口径 1:1 对齐 tauri-plugin-updater 2.10.1 →
//   minisign-verify（updater.rs:1452-1463 verify_signature →
//   minisign_verify::PublicKey::verify(data, &sig, true)）。Mini 实际签名
//   blob 算法字节为 "ED"（0x45,0x44）= 预哈希变体，验签前须先对文件字节
//   求 BLAKE2b-512（64 字节摘要），再做 Ed25519 验证。
#include <cstddef>
#include <cstdint>
#include <vector>

namespace clashwin::util {

class Blake2b {
 public:
  // out_len：摘要长度 1..64（minisign 用 64）
  explicit Blake2b(size_t out_len = 64);
  void update(const uint8_t* data, size_t len);
  void final(uint8_t* out);  // 写出 out_len 字节

  // 一次性便捷接口
  static std::vector<uint8_t> digest(const uint8_t* data, size_t len,
                                     size_t out_len = 64);

 private:
  void compress(const uint8_t block[128], bool is_last, uint64_t counter);
  uint64_t h_[8];
  uint8_t buf_[128];
  size_t buflen_ = 0;
  uint64_t total_ = 0;  // 已喂入总字节数
  uint64_t count_ = 0;  // 已压缩累计字节数（compress 的 t 计数器）
  size_t out_len_;
};

}  // namespace clashwin::util
