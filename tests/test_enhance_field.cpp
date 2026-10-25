// T08 单测：field.rs 移植（use_lowercase / use_sort / use_keys）
#include <string>
#include <vector>

#include "config/yaml_serde.h"
#include "enhance/field.h"
#include "test_util.h"

using namespace clashwin;
using namespace clashwin::enhance::field;

UT_TEST(t08_field_use_lowercase) {
  YAML::Node cfg = YAML::Load("Mode: rule\nMIXED-Port: 10801\nkeep-lower: v\n");
  YAML::Node out = use_lowercase(cfg);
  UT_CHECK(out["mode"].IsDefined());
  UT_CHECK(out["mixed-port"].IsDefined());
  UT_CHECK(out["keep-lower"].IsDefined());
  UT_CHECK(!out["Mode"].IsDefined());
  UT_CHECK_EQ(out["mode"].Scalar(), "rule");

  // 数字键丢弃（serde Value::as_str None）
  YAML::Node numeric_key = YAML::Load("123: v\nname: x\n");
  YAML::Node out2 = use_lowercase(numeric_key);
  UT_CHECK(!out2["123"].IsDefined());
  UT_CHECK(out2["name"].IsDefined());
}

UT_TEST(t08_field_use_sort) {
  // 输入插入序：订阅键序（对照实机 R 文件）
  YAML::Node cfg = YAML::Load(
      "mixed-port: 7892\n"
      "allow-lan: false\n"
      "bind-address: '*'\n"
      "mode: rule\n"
      "log-level: info\n"
      "ipv6: false\n"
      "find-process-mode: strict\n"
      "external-controller: '127.0.0.1:9090'\n"
      "profile:\n  store-selected: true\n"
      "unified-delay: true\n"
      "tcp-concurrent: true\n"
      "ntp:\n  enable: true\n"
      "sniffer:\n  enable: true\n"
      "dns:\n  enable: true\n"
      "tun:\n  enable: false\n"
      "proxies:\n- name: a\n"
      "proxy-groups:\n- name: g\n"
      "rules:\n- MATCH,DIRECT\n"
      "rule-providers:\n  rp:\n    type: http\n");
  YAML::Node out = use_sort(cfg);

  // HANDLE_FIELDS 段（按表序）
  std::vector<std::string> keys;
  for (const auto& kv : out) keys.push_back(kv.first.Scalar());
  std::vector<std::string> expect_head = {
      "mode", "mixed-port", "allow-lan", "log-level", "ipv6",
      "external-controller", "secret", "unified-delay",
  };
  // secret 不存在，前 6 个应依次是 mode..external-controller + unified-delay
  // （unified-delay 属 HANDLE 段第 12 位，仍在中间段前）
  std::vector<std::string> got_head(keys.begin(), keys.begin() + 7);
  UT_CHECK(got_head[0] == "mode");
  UT_CHECK(got_head[1] == "mixed-port");
  UT_CHECK(got_head[2] == "allow-lan");
  UT_CHECK(got_head[3] == "log-level");
  UT_CHECK(got_head[4] == "ipv6");
  UT_CHECK(got_head[5] == "external-controller");
  UT_CHECK(got_head[6] == "unified-delay");

  // 尾段 DEFAULT_FIELDS（实机对齐 4 项；本输入含 proxies/proxy-groups/rule-providers）
  UT_CHECK(keys[keys.size() - 3] == "proxies");
  UT_CHECK(keys[keys.size() - 2] == "proxy-groups");
  UT_CHECK(keys[keys.size() - 1] == "rule-providers");

  // 中间段含 rules（实机对齐）+ 输入插入序
  std::vector<std::string> middle;
  for (size_t i = 7; i + 3 < keys.size(); ++i) middle.push_back(keys[i]);
  auto find_in = [](const std::vector<std::string>& v, const std::string& k) {
    for (size_t i = 0; i < v.size(); ++i) {
      if (v[i] == k) return static_cast<int>(i);
    }
    return -1;
  };
  UT_CHECK(find_in(middle, "rules") >= 0);
  UT_CHECK(find_in(middle, "tun") >= 0);
  UT_CHECK(find_in(middle, "profile") >= 0);
  UT_CHECK(find_in(middle, "sniffer") >= 0);
}

UT_TEST(t08_field_use_keys) {
  YAML::Node cfg = YAML::Load("Mode: 1\nmIxEd-port: 2\nplain: 3\n");
  auto keys = use_keys(cfg);
  UT_CHECK_EQ(keys.size(), size_t(3));
  bool has_mode = false, has_mixed = false, has_plain = false;
  for (const auto& k : keys) {
    if (k == "mode") has_mode = true;
    if (k == "mixed-port") has_mixed = true;
    if (k == "plain") has_plain = true;
  }
  UT_CHECK(has_mode && has_mixed && has_plain);
}

UT_TEST(t08_yaml_serde_quote_style) {
  // G3 引号形态：单引号优先、字符串语义数字文本加引号、空串 ''
  YAML::Node m(YAML::NodeType::Map);
  YAML::Node empty("");
  empty.SetTag("!");
  m["a"] = empty;
  YAML::Node star("*");
  star.SetTag("!");
  m["b"] = star;
  YAML::Node one("1");
  one.SetTag("!");
  m["c"] = one;
  m["d"] = YAML::Node("10801");   // 程序构造数字 → plain
  YAML::Node host("2400:3200::1");
  host.SetTag("!");
  m["e"] = host;
  YAML::Node lan("*.lan");
  lan.SetTag("!");
  m["f"] = lan;
  std::string text = clashwin::config::serde::dump(m);
  UT_CHECK(text.find("a: ''") != std::string::npos);
  UT_CHECK(text.find("b: '*'") != std::string::npos);
  UT_CHECK(text.find("c: '1'") != std::string::npos);
  UT_CHECK(text.find("d: 10801") != std::string::npos);
  UT_CHECK(text.find("e: 2400:3200::1") != std::string::npos);
  UT_CHECK(text.find("f: '*.lan'") != std::string::npos);
  // 引号加载（tag !）
  YAML::Node loaded = YAML::Load("a: '1'\nb: 10086\nc: x\n");
  std::string t2 = clashwin::config::serde::dump(loaded);
  UT_CHECK(t2.find("a: '1'") != std::string::npos);
  UT_CHECK(t2.find("b: 10086") != std::string::npos);
  UT_CHECK(t2.find("c: x") != std::string::npos);
}
