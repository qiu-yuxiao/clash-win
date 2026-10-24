// uid 生成测试（← help.rs::get_uid：前缀 + nanoid!(11, ALPHABET62)）
#include "test_util.h"

#include <set>
#include <string>

#include "util/uuid.h"

using clashwin::util::gen_uid;

static bool in_alphabet(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z');
}

UT_TEST(uuid_format_matches_nanoid11) {
  std::string uid = gen_uid("R");
  // PrfItem uid 规则：前缀(R/L/m/s/r/p/g) + 11 位字母数字
  UT_CHECK_EQ(uid.size(), 12u);
  UT_CHECK_EQ(uid[0], 'R');
  for (size_t i = 1; i < uid.size(); ++i) UT_CHECK(in_alphabet(uid[i]));
}

UT_TEST(uuid_prefix_variants) {
  const char* prefixes[] = {"L", "m", "s", "r", "p", ""};
  for (const char* p : prefixes) {
    std::string uid = gen_uid(p);
    UT_CHECK_EQ(uid.size(), std::string(p).size() + 11u);
  }
}

UT_TEST(uuid_uniqueness_smoke) {
  std::set<std::string> seen;
  for (int i = 0; i < 1000; ++i) seen.insert(gen_uid(""));
  // 62^11 空间下 1000 次碰撞概率≈0
  UT_CHECK_EQ(seen.size(), 1000u);
}
