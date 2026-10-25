// T08 单测：merge.rs / seq.rs 移植（use_merge 深合并；use_seq 增删与 selector 联动）
#include <string>
#include <vector>

#include "enhance/merge.h"
#include "enhance/seq.h"
#include "test_util.h"

using namespace clashwin;
using namespace clashwin::enhance;

UT_TEST(t08_merge_deep) {
  // ← merge.rs::use_merge：深合并 + 值覆盖
  YAML::Node config = YAML::Load(
      "rules:\n- aaaaa\nscript1: test\ntun:\n  enable: false\n  stack: system\n");
  YAML::Node merge = YAML::Load(
      "prepend-rules:\n- prepend\n- 1123123\n"
      "append-rules:\n- append\n"
      "rules:\n- replace\n"
      "tun:\n  enable: true\n"
      "dns:\n  enable: true\n"
      "UPPER-KEY: v\n");
  YAML::Node out = use_merge(merge, config);

  // 深合并：tun.enable 覆盖、tun.stack 保留
  UT_CHECK(out["tun"]["enable"].Scalar() == "true");
  UT_CHECK(out["tun"]["stack"].Scalar() == "system");
  // 标量/列表整体覆盖
  UT_CHECK(out["rules"][0].Scalar() == "replace");
  UT_CHECK(out["rules"].size() == 1);
  // 新键追加 + 键名小写化（use_lowercase 应用于 merge）
  UT_CHECK(out["upper-key"].Scalar() == "v");
  UT_CHECK(!out["UPPER-KEY"].IsDefined());
  // 原有键保留
  UT_CHECK(out["script1"].Scalar() == "test");
}

UT_TEST(t08_seq_delete_proxy_and_references) {
  // ← seq.rs::test_delete_proxy_and_references
  YAML::Node config = YAML::Load(
      "proxies:\n"
      "- name: proxy1\n  type: ss\n"
      "- name: proxy2\n  type: vmess\n"
      "proxy-groups:\n"
      "- name: group1\n  type: select\n  proxies:\n  - proxy1\n  - proxy2\n"
      "- name: group2\n  type: select\n  proxies:\n  - proxy1\n");
  SeqMap seq = SeqMap::empty();
  seq.delete_ = {"proxy1"};
  YAML::Node out = use_seq(seq, config, "proxies");

  UT_CHECK_EQ(out["proxies"].size(), size_t(1));
  UT_CHECK(out["proxies"][0]["name"].Scalar() == "proxy2");
  UT_CHECK_EQ(out["proxy-groups"][0]["proxies"].size(), size_t(1));
  UT_CHECK(out["proxy-groups"][0]["proxies"][0].Scalar() == "proxy2");
  UT_CHECK_EQ(out["proxy-groups"][1]["proxies"].size(), size_t(0));
}

UT_TEST(t08_seq_add_new_proxies_to_first_selector) {
  // ← seq.rs::test_add_new_proxies_to_first_selector_group
  YAML::Node config = YAML::Load(
      "proxies:\n"
      "- name: proxy1\n  type: ss\n"
      "proxy-groups:\n"
      "- name: group1\n  type: select\n  proxies:\n  - proxy1\n"
      "- name: group2\n  type: select\n  proxies:\n  - proxy1\n");
  SeqMap seq = SeqMap::empty();
  YAML::Node pre = YAML::Load("- name: proxy3\n  type: ss\n");
  YAML::Node app = YAML::Load("- name: proxy4\n  type: vmess\n");
  seq.prepend = pre;
  seq.append = app;
  YAML::Node out = use_seq(seq, config, "proxies");

  UT_CHECK_EQ(out["proxies"].size(), size_t(3));
  UT_CHECK(out["proxies"][0]["name"].Scalar() == "proxy3");
  UT_CHECK(out["proxies"][2]["name"].Scalar() == "proxy4");

  // 仅首个 selector 组注入；新增名在既有成员前
  auto names_of = [](const YAML::Node& g) {
    std::vector<std::string> v;
    for (const auto& p : g["proxies"]) v.push_back(p.Scalar());
    return v;
  };
  auto n1 = names_of(out["proxy-groups"][0]);
  UT_CHECK_EQ(n1.size(), size_t(3));
  UT_CHECK(n1[0] == "proxy3" && n1[1] == "proxy4" && n1[2] == "proxy1");
  auto n2 = names_of(out["proxy-groups"][1]);
  UT_CHECK_EQ(n2.size(), size_t(1));
  UT_CHECK(n2[0] == "proxy1");
}

UT_TEST(t08_seq_rules_field) {
  // rules 字段：prepend / append / delete + 无 proxy-groups 联动
  YAML::Node config = YAML::Load(
      "rules:\n- A\n- B\n- C\nproxy-groups:\n- name: g\n  type: select\n");
  SeqMap seq = SeqMap::empty();
  seq.prepend = YAML::Load("- PRE\n");
  seq.append = YAML::Load("- APP\n");
  seq.delete_ = {"B"};
  YAML::Node out = use_seq(seq, config, "rules");
  UT_CHECK_EQ(out["rules"].size(), size_t(4));
  UT_CHECK(out["rules"][0].Scalar() == "PRE");
  UT_CHECK(out["rules"][1].Scalar() == "A");
  UT_CHECK(out["rules"][2].Scalar() == "C");
  UT_CHECK(out["rules"][3].Scalar() == "APP");
  // 无 selector 注入（rules 字段）
  UT_CHECK(!out["proxy-groups"][0]["proxies"].IsDefined());
}

UT_TEST(t08_seq_read_seq_map) {
  // read_seq_map：模板形态（带注释 + 空数组）
  std::string text =
      "# Profile Enhancement Rules Template for Clash Verge\n\n"
      "prepend: []\n\nappend: []\n\ndelete: []\n";
  auto r = read_seq_map_text(text);
  UT_CHECK(r.ok);
  UT_CHECK_EQ(r.map.delete_.size(), size_t(0));
  UT_CHECK_EQ(r.map.prepend.size(), size_t(0));

  std::string text2 = "prepend:\n- X\nappend:\n- Y\ndelete:\n- Z\n";
  auto r2 = read_seq_map_text(text2);
  UT_CHECK(r2.ok);
  UT_CHECK_EQ(r2.map.prepend.size(), size_t(1));
  UT_CHECK_EQ(r2.map.append.size(), size_t(1));
  UT_CHECK_EQ(r2.map.delete_.size(), size_t(1));
  UT_CHECK(r2.map.delete_[0] == "Z");
}
