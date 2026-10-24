// base64 鲁棒解码测试（样例取自 ClashVerge universal_parser.rs 测试域）
#include "test_util.h"

#include <string>

#include "util/base64.h"

using clashwin::util::decode_base64_robust;

static std::string to_str(const std::vector<uint8_t>& v) {
  return std::string(v.begin(), v.end());
}

UT_TEST(base64_standard_with_padding) {
  auto r = decode_base64_robust("SGVsbG8=");
  UT_CHECK(r.has_value());
  UT_CHECK_EQ(to_str(*r), "Hello");
}

UT_TEST(base64_missing_padding_recovered) {
  // 无 padding → 补齐后成功（← universal_parser.rs:66-72）
  auto r = decode_base64_robust("SGVsbG8");
  UT_CHECK(r.has_value());
  UT_CHECK_EQ(to_str(*r), "Hello");
}

UT_TEST(base64_newlines_stripped) {
  // ← universal_parser.rs:1248-1253 test_decode_base64_robust_newlines
  auto r = decode_base64_robust("aGVsbG8gd29ybGQKaGVs\nbG8gd29ybGQ=");
  UT_CHECK(r.has_value());
  UT_CHECK_EQ(to_str(*r), "hello world\nhello world");
}

UT_TEST(base64_urlsafe_alphabet) {
  // 0xFB 0xEF 0xBE → 全部 6bit 组为 62：standard '+' / urlsafe '-'
  auto r1 = decode_base64_robust("++++");
  UT_CHECK(r1.has_value());
  UT_CHECK_EQ(to_str(*r1), std::string("\xFB\xEF\xBE", 3));

  auto r2 = decode_base64_robust("----");
  UT_CHECK(r2.has_value());
  UT_CHECK_EQ(to_str(*r2), std::string("\xFB\xEF\xBE", 3));

  // urlsafe 无 padding："-w"（0xFB）→ 补齐 "-w=="，canonical 通过
  auto r3 = decode_base64_robust("-w");
  UT_CHECK(r3.has_value());
  UT_CHECK_EQ(to_str(*r3), std::string("\xFB", 1));

  // 3 个数据字符（丢弃位非 0）补齐后仍非 canonical → None（对齐 base64 crate）
  UT_CHECK(!decode_base64_robust("---").has_value());
}

UT_TEST(base64_whitespace_all_stripped) {
  auto r = decode_base64_robust("  SGVs\r\nbG8= \t");
  UT_CHECK(r.has_value());
  UT_CHECK_EQ(to_str(*r), "Hello");
}

UT_TEST(base64_invalid_inputs) {
  // 空
  UT_CHECK(!decode_base64_robust("").has_value());
  UT_CHECK(!decode_base64_robust("   \n").has_value());
  // 非法字符
  UT_CHECK(!decode_base64_robust("!!!").has_value());
  // 长度 %4==1（补齐后含 3 个 '='）仍非法
  UT_CHECK(!decode_base64_robust("A").has_value());
  // 非法 padding："ab==" 末组丢弃位非 0（canonical 校验，对齐 base64 crate 默认）
  UT_CHECK(!decode_base64_robust("ab==").has_value());
  // 中间夹 '='
  UT_CHECK(!decode_base64_robust("ab=c").has_value());
}
