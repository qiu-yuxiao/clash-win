#pragma once

namespace clashwin::config {

// 更新验签公钥（Clash Win 自有 minisign 密钥对；不再是 Mini 的公钥）。
// 值 = minisign 公钥文件全文（untrusted comment + 42 字节公钥的 Base64 两行）
// 的 Base64 编码；key id = BEFA887F30C3BAAD。
//
// 私钥**不入库**：本地产出后存放在仓库外
//   %USERPROFILE%\.clashwin\updater-keys\clashwin-updater.key
// 并经 `gh secret set MINISIGN_PRIVATE_KEY` 注入 GitHub Actions Secret；
// 发布工作流用该私钥对 setup.exe 做 minisign 签名，把 Base64(.sig) 写进
// updater/app-update.json 的 platforms.windows-x86_64.signature。
// 轮换密钥时：先发一版带新公钥的客户端，再换 CI 私钥——旧客户端只认旧公钥，
// 否则会把新签名判为「签名与更新公钥不匹配」而拒绝安装（预期行为）。
//
// 验签口径 1:1 对齐 tauri-plugin-updater 2.10.1（updater.rs:1452-1463）：
//   1) Base64 解码本值 → minisign 公钥文本 → 第 2 行 Base64 解码 42 字节 =
//      "Ed"/"ED"(2) + keynum(8) + Ed25519 公钥(32)
//   2) manifest signature 字段 Base64 解码 → 4 行文本：bin1(74) = 算法(2) +
//      keynum(8) + sig(64)、"trusted comment: " 行、global_sig(64)
//   3) 算法 "ED"（预哈希）→ Ed25519 验 BLAKE2b-512(安装包字节)；
//      "Ed"（legacy）→ 验原始字节（minisign-verify verify(…, allow_legacy=true)）
//   4) 全局签名：Ed25519 验 (sig(64) ++ trusted_comment[17..])
constexpr const char* kUpdatePubkeyFileB64 =
    "dW50cnVzdGVkIGNvbW1lbnQ6IG1pbmlzaWduIHB1YmxpYyBrZXkgQkVGQTg4N0YzMEMz"
    "QkFBRApSV1N0dXNNd2Y0ajZ2aUdCaFp3ZTRSQm1pK0hmVVZnejZKRFNvNlJFalhaV3Rp"
    "YXZoYUxjSEszUgo=";

}  // namespace clashwin::config
