// json_utils 测试
#include "test_util.h"

#include <nlohmann/json.hpp>

#include "util/json_utils.h"

using namespace clashwin::util;

UT_TEST(json_get_or) {
  nlohmann::json j = {{"port", 10801},
                      {"name", "节点A"},
                      {"flag", true},
                      {"str_num", "42"}};

  UT_CHECK_EQ(get_or_int(j, "port", 0), 10801);
  UT_CHECK_EQ(get_or_int(j, "missing", 999), 999);
  UT_CHECK_EQ(get_or_int(j, "name", 7), 7);          // 类型不符回退
  UT_CHECK_EQ(get_or_int(j, "str_num", 7), 7);       // 字符串数字不取

  UT_CHECK_EQ(get_or_string(j, "name", "x"), "节点A");
  UT_CHECK_EQ(get_or_string(j, "missing", "def"), "def");
  UT_CHECK_EQ(get_or_string(j, "port", "def"), "def");

  UT_CHECK(get_or_bool(j, "flag", false));
  UT_CHECK(!get_or_bool(j, "missing", false));
}

UT_TEST(json_try_get) {
  nlohmann::json j = {{"port", 9098}, {"name", "x"}};
  auto p = try_get_int(j, "port");
  UT_CHECK(p.has_value());
  UT_CHECK_EQ(*p, 9098);
  UT_CHECK(!try_get_int(j, "missing").has_value());
  UT_CHECK(!try_get_int(j, "name").has_value());

  auto s = try_get_string(j, "name");
  UT_CHECK(s.has_value());
  UT_CHECK_EQ(*s, "x");
  UT_CHECK(!try_get_string(j, "port").has_value());
}

UT_TEST(json_wstring_boundary) {
  nlohmann::json j = {{"title", "流量面板"}, {"empty", ""}, {"num", 1}};
  std::wstring t = get_or_wstring(j, "title", L"def");
  UT_CHECK_EQ(t, L"流量面板");
  // 合法空串返回空（仅缺失/类型不符才回退）
  UT_CHECK(get_or_wstring(j, "empty", L"def").empty());
  UT_CHECK_EQ(get_or_wstring(j, "missing", L"def"), L"def");
  UT_CHECK_EQ(get_or_wstring(j, "num", L"def"), L"def");
}
