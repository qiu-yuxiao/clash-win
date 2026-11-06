#pragma once

#include <cstddef>
#include <cstdint>

namespace clashwin::util {

// Ed25519 验签（实现取自 TweetNaCl 20140427，public domain，已裁剪到仅保留 verify 路径）。
// message 由调用方提供完整缓冲；本项目更新包验签签的是 32 字节 SHA-256 摘要。
bool ed25519_verify(const uint8_t* message, size_t message_len,
                    const uint8_t signature[64], const uint8_t public_key[32]);

}  // namespace clashwin::util
