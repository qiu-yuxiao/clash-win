// 字符串工具测试（fix_dirty_url 样例域 ← prfitem.rs:856-880 注释与实现；
// 防双解码 ← scheme.rs:62-80）
#include "test_util.h"

#include "util/str_utils.h"

using namespace clashwin::util;

UT_TEST(str_trim_split_basic) {
  UT_CHECK_EQ(trim("  hello \t\n"), "hello");
  UT_CHECK_EQ(trim("noop"), "noop");

  auto parts = split("a;b;;c", ';');
  UT_CHECK_EQ(parts.size(), 4u);
  UT_CHECK_EQ(parts[0], "a");
  UT_CHECK_EQ(parts[1], "b");
  UT_CHECK_EQ(parts[2], "");
  UT_CHECK_EQ(parts[3], "c");

  auto ne = split_no_empty("a;b;;c", ';');
  UT_CHECK_EQ(ne.size(), 3u);

  UT_CHECK(starts_with("vmess://xxx", "vmess://"));
  UT_CHECK(ends_with("a.yaml", ".yaml"));
  UT_CHECK(!ends_with("a.yml", ".yaml"));
  UT_CHECK_EQ(to_lower("MiXeD CaSe 123"), "mixed case 123");
}

UT_TEST(str_percent_decode_lossy) {
  UT_CHECK_EQ(percent_decode("a%20b"), "a b");
  UT_CHECK_EQ(percent_decode("%E4%B8%AD%E6%96%87"), "中文");
  // 非法 % 序列原样保留
  UT_CHECK_EQ(percent_decode("100%"), "100%");
  UT_CHECK_EQ(percent_decode("%ZZ"), "%ZZ");
  // 非法 UTF-8 → U+FFFD（lossy）
  UT_CHECK_EQ(percent_decode("%FF"), "\xEF\xBF\xBD");
}

UT_TEST(str_percent_decode_max2_no_double_decode) {
  // %2520 一次 → "%20"，两次 → " "：防双解码 = 最多 2 次、稳定即停
  UT_CHECK_EQ(percent_decode_max2("%2520"), " ");
  // 已无转义：稳定即停
  UT_CHECK_EQ(percent_decode_max2("plain"), "plain");
  UT_CHECK_EQ(percent_decode_max2("a%20b%2Fc"), "a b/c");
}

UT_TEST(str_decode_subscription_url) {
  // ← scheme.rs：已是 URL 原样返回（不二次解码，保护嵌套 %2B 等）
  std::string url = "https://example.com/sub?token=a%2Bb";
  UT_CHECK_EQ(decode_subscription_url(url), url);
  // 编码 URL 解码一次即成 URL
  UT_CHECK_EQ(decode_subscription_url("https%3A%2F%2Fexample.com%2Fsub"),
              "https://example.com/sub");
  // 双层编码：解两次到 URL 即停
  UT_CHECK_EQ(decode_subscription_url("https%253A%252F%252Fexample.com"),
              "https://example.com");
}

UT_TEST(str_fix_dirty_url) {
  // ← prfitem.rs:856-858 示例
  auto r1 = fix_dirty_url("https://example.com/path&param1=value1");
  UT_CHECK(r1.is_ok());
  UT_CHECK_EQ(r1.value(), "https://example.com/path?param1=value1");

  auto r2 = fix_dirty_url("https://example.com/path&param1=value1&param2=v2");
  UT_CHECK(r2.is_ok());
  UT_CHECK_EQ(r2.value(),
              "https://example.com/path?param1=value1&param2=v2");

  // 干净 URL 原样（host 归一小写）
  auto r3 = fix_dirty_url("https://EXAMPLE.com/path?param1=value1");
  UT_CHECK(r3.is_ok());
  UT_CHECK_EQ(r3.value(), "https://example.com/path?param1=value1");

  // 无 query 无 '&'：原样
  auto r4 = fix_dirty_url("https://example.com/plainpath");
  UT_CHECK(r4.is_ok());
  UT_CHECK_EQ(r4.value(), "https://example.com/plainpath");

  // 非法 URL 报错
  auto r5 = fix_dirty_url("not a url");
  UT_CHECK(!r5.is_ok());
}
