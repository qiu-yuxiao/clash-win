// minisign 验签端到端测试（B-D44 收敛）。
// 向量口径：Ed25519 密钥对按 minisign 文件格式封装；签名 = "ED" 预哈希变体
// （Ed25519(BLAKE2b-512(data))），全局签名 = Ed25519(sig(64) ++ trusted_comment[17..])；
// 与 tauri-plugin-updater 2.10.1 verify_signature / minisign-verify 语义一致。
#include "test_util.h"

#include <string>
#include <vector>

#include "minisign_vector.h"
#include "config/update_keys.h"
#include "update/client_updater.h"
#include "util/base64.h"

using clashwin::update::verify_installer_signature;

UT_TEST(minisign_valid_signature_passes) {
  std::vector<uint8_t> data(kTestData, kTestData + sizeof(kTestData));
  std::string err;
  const bool ok = verify_installer_signature(data, kTestSignatureB64, &err,
                                             kTestPubkeyFileB64);
  UT_CHECK_MSG(ok, err.c_str());
}

UT_TEST(minisign_tampered_data_fails) {
  std::vector<uint8_t> data(kTestDataTampered,
                            kTestDataTampered + sizeof(kTestDataTampered));
  std::string err;
  const bool ok = verify_installer_signature(data, kTestSignatureB64, &err,
                                             kTestPubkeyFileB64);
  UT_CHECK(!ok);
  UT_CHECK_EQ(err, "安装包签名校验失败");
}

UT_TEST(minisign_wrong_global_signature_fails) {
  // 全局签名对应另一条 trusted comment → 主签名可通过但全局校验必须失败
  std::vector<uint8_t> data(kTestData, kTestData + sizeof(kTestData));
  std::string err;
  const bool ok = verify_installer_signature(data, kTestSignatureBadTrustedB64,
                                             &err, kTestPubkeyFileB64);
  UT_CHECK(!ok);
  UT_CHECK_EQ(err, "安装包签名校验失败");
}

UT_TEST(minisign_garbage_signature_fails) {
  std::vector<uint8_t> data(kTestData, kTestData + sizeof(kTestData));
  std::string err;
  UT_CHECK(!verify_installer_signature(data, "not-base64!!", &err,
                                       kTestPubkeyFileB64));
  UT_CHECK(!verify_installer_signature(data, "", &err, kTestPubkeyFileB64));
  UT_CHECK(!verify_installer_signature(data, "AAAA", &err,
                                       kTestPubkeyFileB64));  // 非 4 行
}

UT_TEST(minisign_clashwin_pubkey_structure) {
  // 编译进客户端的更新验签公钥（config::kUpdatePubkeyFileB64）结构校验：
  // Base64 → 两行文本；第 2 行 Base64 → 42 字节 = "Ed" + keynum + 32 字节公钥。
  // v0.2.2 起该常量已由 Mini 公钥换成 Clash Win 自有密钥对（提交 d3a27aa，
  // 见 src/config/update_keys.h）：key id = BEFA887F30C3BAAD，
  // keynum = ADBAC3307F88FABE（恰为 key id 的逆序字节）。轮换密钥时两处须同步。
  // 注：下方 minisign_real_mini_manifest_signature_structure 校验的是硬编码的
  // 历史 Mini manifest 签名向量，其 keynum 仍是 Mini 的，与当前公钥无关。
  using clashwin::util::decode_base64_robust;
  const auto pk_file =
      decode_base64_robust(clashwin::config::kUpdatePubkeyFileB64);
  UT_CHECK(pk_file.has_value());
  const std::string text(pk_file->begin(), pk_file->end());
  UT_CHECK(text.rfind("untrusted comment: minisign public key BEFA887F30C3BAAD",
                      0) == 0);
  const size_t p1 = text.find('\n');
  UT_CHECK(p1 != std::string::npos);
  const auto bin = decode_base64_robust(text.substr(p1 + 1));
  UT_CHECK(bin.has_value());
  UT_CHECK_EQ(bin->size(), 42u);
  UT_CHECK_EQ((*bin)[0], 0x45);  // 'E'
  UT_CHECK_EQ((*bin)[1], 0x64);  // 'd' → "Ed"（公钥算法标识）
  // 当前 Clash Win 公钥 keynum（= key id BEFA887F30C3BAAD 的逆序字节）
  const uint8_t expect_keynum[8] = {0xAD, 0xBA, 0xC3, 0x30,
                                    0x7F, 0x88, 0xFA, 0xBE};
  UT_CHECK(std::equal(expect_keynum, expect_keynum + 8, bin->data() + 2));
}

UT_TEST(minisign_real_mini_manifest_signature_structure) {
  // 真实 Mini manifest（updater/app-update.json，与 Mini 源逐字节一致）的
  // signature 字段：Base64 → 4 行文本 → bin1 74 字节（"ED" + keynum + sig）。
  // keynum 须与公钥 keynum（60B6CD84214C3410）一致。
  using clashwin::util::decode_base64_robust;
  const char* kRealMiniSig =
      "dW50cnVzdGVkIGNvbW1lbnQ6IHNpZ25hdHVyZSBmcm9tIHRhdXJpIHNlY3JldCBrZXkKUlVS"
      "Z3RzMkVJVXcwRUZHWlBTMjdxV2hBMkNDblFUd3N0NEJxVW42UHI2K2NNN0VnWHdRLzNxWmxj"
      "SEdhY3dtWm1obldZUFd3b2xPam1xZzVYaUVIZXJxeDJSNFAxdHIyUUEwPQp0cnVzdGVkIGNv"
      "bW1lbnQ6IHRpbWVzdGFtcDoxNzkzMTczODU1CWZpbGU6Q2xhc2ggTWluaV8zLjIuMl94NjQt"
      "c2V0dXAuZXhlCnpyQ2trcjNsaUhjRC9mR09uK214NDQ1NDVlaXd5cVY5aWpvdHpIKzh3eGRI"
      "Ky9YTWQ1cGh5bUtQUlNLZHVybHRITEdMOWlzYTF3YTBGOVdZN0tBUUN3PT0K";
  const auto txt = decode_base64_robust(kRealMiniSig);
  UT_CHECK(txt.has_value());
  const std::string text(txt->begin(), txt->end());
  size_t p1 = text.find('\n'), p2 = text.find('\n', p1 + 1),
         p3 = text.find('\n', p2 + 1), p4 = text.find('\n', p3 + 1);
  UT_CHECK(p1 != std::string::npos && p2 != std::string::npos &&
           p3 != std::string::npos);
  UT_CHECK(text.rfind("untrusted comment: signature from tauri secret key", 0) ==
           0);
  const auto bin1 = decode_base64_robust(text.substr(p1 + 1, p2 - p1 - 1));
  UT_CHECK(bin1.has_value());
  UT_CHECK_EQ(bin1->size(), 74u);
  UT_CHECK_EQ((*bin1)[0], 0x45);  // 'E'
  UT_CHECK_EQ((*bin1)[1], 0x44);  // 'D' → 预哈希变体（BLAKE2b-512 后 Ed25519）
  // keynum 与公钥匹配：60 B6 CD 84 21 4C 34 10
  const uint8_t expect_keynum[8] = {0x60, 0xB6, 0xCD, 0x84,
                                    0x21, 0x4C, 0x34, 0x10};
  UT_CHECK(std::equal(expect_keynum, expect_keynum + 8, bin1->data() + 2));
  UT_CHECK(text.substr(p2 + 1, 17) == "trusted comment: ");
  UT_CHECK(p4 != std::string::npos);
  const auto g = decode_base64_robust(text.substr(p3 + 1));
  UT_CHECK(g.has_value());
  UT_CHECK_EQ(g->size(), 64u);
}
