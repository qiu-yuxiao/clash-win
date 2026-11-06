#pragma once

namespace clashwin::config {

// 本项目自签 Ed25519 更新公钥（Raw 32 字节，标准 Base64）。
// ← 自建密钥对，非 Mini/ClashVerge 的公钥；私钥只存本机，不进仓库、不进日志：
//   C:\Users\<user>\.clashwin-keys\update-signing.key （ACL 仅当前用户）
// 签名口径：signature = Base64(Ed25519(privkey, SHA-256(installer)))。
// 发布流程须在 CI secret 中注入同一私钥后再产出 download_url/sha256/signature 三件套。
constexpr const char* kUpdatePubkeyB64 = "qFklrnq8RXrcA14KuCALLUF73Uvjkg20MIsiS24dDYU=";

}  // namespace clashwin::config
