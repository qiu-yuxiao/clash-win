// yaml_utils 测试：核心是"插入序不丢"的端到端证明（enhance 规则链顺序的命门）
#include "test_util.h"

#include <windows.h>

#include <fstream>
#include <string>

#include "util/yaml_utils.h"

using namespace clashwin::util;

namespace {
std::wstring temp_yaml_dir() {
  wchar_t buf[MAX_PATH];
  GetTempPathW(MAX_PATH, buf);
  return std::wstring(buf) + L"clashwin_test_yaml";
}
}  // namespace

UT_TEST(yaml_insertion_order_preserved) {
  // ① 加载保持文档顺序
  YAML::Node doc = YAML::Load("a: 1\nb: 2\nc: 3\n");
  std::string order1;
  for (const auto& kv : doc) order1 += kv.first.as<std::string>();
  UT_CHECK_EQ(order1, "abc");

  // ② 已有键赋值：原位覆盖不换位；新键：尾部追加
  doc["c"] = 30;   // 原位（第三）
  doc["d"] = 4;    // 追加到尾
  doc["b"] = 20;   // 仍在第二

  auto ser = emit_yaml(doc);
  UT_CHECK(ser.is_ok());
  UT_CHECK_EQ(ser.value(), "a: 1\nb: 20\nc: 30\nd: 4\n");
}

UT_TEST(yaml_order_survives_dump_reload_roundtrip) {
  YAML::Node doc = YAML::Load("rules:\n  - MATCH,DIRECT\nport: 7890\ndns:\n  enable: true\n");
  doc["new-top"] = 1;
  auto ser = emit_yaml(doc);
  UT_CHECK(ser.is_ok());

  YAML::Node back = YAML::Load(ser.value());
  std::string order;
  for (const auto& kv : back) order += kv.first.as<std::string>();
  // rules → port → dns → new-top：Dump 再 Load 一圈后顺序不变
  UT_CHECK_EQ(order, "rulesportdnsnew-top");
}

UT_TEST(yaml_deep_merge_order_and_recursion) {
  YAML::Node base = YAML::Load(
      "a: 1\nb:\n  x: 1\n  y: 2\nc: [1, 2]\n");
  YAML::Node overlay = YAML::Load(
      "b:\n  y: 9\n  z: 3\nc: [9]\nd: 4\n");
  deep_merge(base, overlay);

  // 顶层序：a(原位) b(原位) c(原位) d(追加)
  std::string top;
  for (const auto& kv : base) top += kv.first.as<std::string>();
  UT_CHECK_EQ(top, "abcd");

  // b 递归合并：x 保留原值，y 覆盖，z 追加
  YAML::Node b = base["b"];
  std::string bord;
  for (const auto& kv : b) bord += kv.first.as<std::string>();
  UT_CHECK_EQ(bord, "xyz");
  UT_CHECK_EQ(b["x"].as<int>(), 1);
  UT_CHECK_EQ(b["y"].as<int>(), 9);
  UT_CHECK_EQ(b["z"].as<int>(), 3);

  // 序列整体覆盖（flow 风格随载入保留）
  UT_CHECK_EQ(base["c"][0].as<int>(), 9);
  UT_CHECK_EQ(base["c"].size(), 1u);
}

UT_TEST(yaml_emit_scalar_only_exact_string) {
  // 纯标量场景的精确字符串断言（与块序列缩进风格无关）
  YAML::Node doc = YAML::Load("a: 1\nb: 2\n");
  doc["a"] = 10;
  doc["c"] = 3;
  auto ser = emit_yaml(doc);
  UT_CHECK(ser.is_ok());
  UT_CHECK_EQ(ser.value(), "a: 10\nb: 2\nc: 3\n");
}

UT_TEST(yaml_save_load_atomic_and_skip) {
  std::wstring dir = temp_yaml_dir();
  CreateDirectoryW(dir.c_str(), nullptr);
  std::wstring path = dir + L"\\t.yaml";
  DeleteFileW(path.c_str());

  YAML::Node doc;
  doc["port"] = 10801;
  doc["mode"] = "rule";

  auto r1 = save_yaml_file(path, doc, "# 头部注释");
  UT_CHECK(r1.is_ok());

  auto loaded = load_yaml_file(path);
  UT_CHECK(loaded.is_ok());
  UT_CHECK_EQ(loaded.value()["port"].as<int>(), 10801);
  UT_CHECK_EQ(loaded.value()["mode"].as<std::string>(), "rule");

  // 内容未变 → 第二次保存跳过写（mtime 不变）
  HANDLE h1 = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                          OPEN_EXISTING, 0, nullptr);
  FILETIME t1{};
  GetFileTime(h1, nullptr, nullptr, &t1);
  CloseHandle(h1);
  Sleep(60);
  auto r2 = save_yaml_file(path, doc, "# 头部注释");
  UT_CHECK(r2.is_ok());
  HANDLE h2 = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                          OPEN_EXISTING, 0, nullptr);
  FILETIME t2{};
  GetFileTime(h2, nullptr, nullptr, &t2);
  CloseHandle(h2);
  UT_CHECK(CompareFileTime(&t1, &t2) == 0);

  // 语法错误文件 → Error
  std::wstring bad = dir + L"\\bad.yaml";
  {
    std::ofstream f(bad, std::ios::binary);
    f << "a: [1, 2\n  b: {\n";
  }
  auto rb = load_yaml_file(bad);
  UT_CHECK(!rb.is_ok());

  DeleteFileW(path.c_str());
  DeleteFileW(bad.c_str());
}
