#pragma once

namespace clashwin::config {

// 更新验签公钥（B-D44 收敛：← MC:src-tauri/tauri.conf.json:32 逐字节）。
// 值 = minisign 公钥文件全文（untrusted comment + 42 字节公钥的 Base64 两行）
// 的 Base64 编码；对应 Mini 仓库 qiu-yuxiao/clash-mini 的签名密钥对。
// 验签口径 1:1 对齐 tauri-plugin-updater 2.10.1（updater.rs:1452-1463）：
//   1) Base64 解码本值 → minisign 公钥文本 → 第 2 行 Base64 解码 42 字节 =
//      "Ed"(2) + keynum(8) + Ed25519 公钥(32)
//   2) manifest signature 字段 Base64 解码 → 4 行文本：bin1(74) = 算法(2) +
//      keynum(8) + sig(64)、"trusted comment: " 行、global_sig(64)
//   3) 算法 "ED"（预哈希）→ Ed25519 验 BLAKE2b-512(安装包字节)；
//      "Ed"（legacy）→ 验原始字节（minisign-verify verify(…, allow_legacy=true)）
//   4) 全局签名：Ed25519 验 (sig(64) ++ trusted_comment[17..])
constexpr const char* kUpdatePubkeyFileB64 =
    "dW50cnVzdGVkIGNvbW1lbnQ6IG1pbmlzaWduIHB1YmxpYyBrZXk6IDEwMzQ0QzIxODRDREI2"
    "NjAKUldSZ3RzMkVJVXcwRUtHTk1RcDE2SE1zVzd2VFE2akhleHZXWTlSbk1QZmYxYlNGWTcz"
    "cXVaOXgK";

}  // namespace clashwin::config
