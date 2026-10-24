// T03：Draft<T> 草稿-提交语义（对照 crates/clash-verge-draft）
#include "config/draft.h"

#include <string>

#include "test_util.h"

using namespace clashwin;
using clashwin::config::Draft;

UT_TEST(t03_draft_committed_isolation) {
  Draft<std::string> d(std::string("v1"));
  // 无草稿：latest == data
  UT_CHECK_EQ(*d.latest_arc(), std::string("v1"));
  UT_CHECK(!d.has_draft());

  // edit_draft 不影响已提交
  d.edit_draft([](std::string& s) { s = "v2-draft"; });
  UT_CHECK(d.has_draft());
  UT_CHECK_EQ(*d.data_arc(), std::string("v1"));     // committed 不变
  UT_CHECK_EQ(*d.latest_arc(), std::string("v2-draft"));  // latest 反映草稿

  // discard → 回滚
  d.discard();
  UT_CHECK(!d.has_draft());
  UT_CHECK_EQ(*d.latest_arc(), std::string("v1"));
}

UT_TEST(t03_draft_apply_commits) {
  Draft<std::string> d(std::string("v1"));
  d.edit_draft([](std::string& s) { s = "v2"; });
  d.apply();
  UT_CHECK(!d.has_draft());
  UT_CHECK_EQ(*d.data_arc(), std::string("v2"));
  UT_CHECK_EQ(*d.latest_arc(), std::string("v2"));

  // 无草稿 apply → no-op
  d.apply();
  UT_CHECK_EQ(*d.data_arc(), std::string("v2"));
}

UT_TEST(t03_draft_edit_chain_and_snapshot_stability) {
  Draft<std::string> d(std::string("base"));
  auto before = d.data_arc();
  d.edit_draft([](std::string& s) { s += "-1"; });
  d.edit_draft([](std::string& s) { s += "-2"; });  // 在同一份草稿上继续
  UT_CHECK_EQ(*before, std::string("base"));        // 旧快照不被影响
  d.apply();
  UT_CHECK_EQ(*d.data_arc(), std::string("base-1-2"));
}

UT_TEST(t03_draft_with_data_modify_optimistic_lock) {
  Draft<int> d(1);
  auto r = d.with_data_modify([](int& v) -> clashwin::Result<void> {
    v = 42;
    return clashwin::Result<void>::ok();
  });
  UT_CHECK(r.is_ok());
  UT_CHECK_EQ(*d.data_arc(), 42);

  // modify 失败 → 不落盘
  auto r2 = d.with_data_modify([](int& v) -> clashwin::Result<void> {
    v = 99;
    clashwin::Error e;
    e.code = 1;
    e.message = "boom";
    return clashwin::Result<void>::err(std::move(e));
  });
  UT_CHECK(!r2.is_ok());
  UT_CHECK_EQ(*d.data_arc(), 42);
}
