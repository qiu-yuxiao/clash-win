// T03：proxy_head_state.json 与 unlock_cache 读写
// （proxy_head_state 形态对照实机 %APPDATA%\io.github.clash-win.clash-win
//   /proxy_head_state.json 只读样本；unlock 对照 src/pages/unlock.tsx 缓存字段）
#include <windows.h>

#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "config/proxy_head_state.h"
#include "config/unlock_cache.h"
#include "test_util.h"
#include "util/dirs.h"
#include "util/utf.h"

using namespace clashwin;
using namespace clashwin::config;
using namespace clashwin::util;
using clashwin::utf::utf8_to_utf16;
using clashwin::utf::utf16_to_utf8;

namespace {

std::wstring fresh_home(const char* tag) {
  wchar_t buf[MAX_PATH];
  GetTempPathW(MAX_PATH, buf);
  CreateDirectoryW((std::wstring(buf) + L"clashwin_t03").c_str(), nullptr);
  std::wstring root = std::wstring(buf) + L"clashwin_t03\\" + utf::utf8_to_utf16(tag);
  CreateDirectoryW(root.c_str(), nullptr);
  DeleteFileW((root + L"\\proxy_head_state.json").c_str());
  DeleteFileW((root + L"\\unlock_cache.json").c_str());
  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", root.c_str());
  return root;
}

std::string read_text(const std::wstring& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return "<missing>";
  return std::string((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
}

}  // namespace

UT_TEST(t03_proxy_head_state_roundtrip_and_defaults) {
  fresh_home("phs");
  // 文件不存在 → 空对象（← cmd/proxy.rs:108-116）
  UT_CHECK(proxy_head_state::load().is_object());
  UT_CHECK(proxy_head_state::load().empty());

  // 实机形态：{uid: {group: {filterText, open, showType, sortType}}}
  nlohmann::json state = nlohmann::json::object();
  proxy_head_state::GroupHeadState gs;
  gs.filter_text = "";
  gs.open = false;
  gs.show_type = true;
  gs.sort_type = 1;
  state = proxy_head_state::set_group_state(state, "RdzAUK1VxD4t", "PROXY", gs);
  UT_CHECK(proxy_head_state::save(state).is_ok());

  auto loaded = proxy_head_state::load();
  auto back = proxy_head_state::get_group_state(loaded, "RdzAUK1VxD4t", "PROXY");
  UT_CHECK(back.has_value());
  if (back) {
    UT_CHECK_EQ(back->filter_text, std::string(""));
    UT_CHECK(!back->open);
    UT_CHECK(back->show_type);
    UT_CHECK_EQ(back->sort_type, 1);
  }
  // 未写入的组 → nullopt
  UT_CHECK(!proxy_head_state::get_group_state(loaded, "Rother", "PROXY").has_value());

  // pretty(2) 形态（serde_json::to_string_pretty 对齐）
  std::string text = read_text(proxy_head_state::state_path());
  UT_CHECK(text.find("{\n  \"RdzAUK1VxD4t\": {\n    \"PROXY\": {\n      \"filterText\": \"\",\n") == 0);
  // 无尾换行（serde_json 不补）
  UT_CHECK(!text.empty() && text.back() != '\n');

  // 撕裂文件 → 静默退化为默认空对象
  {
    std::ofstream f(proxy_head_state::state_path(), std::ios::binary | std::ios::trunc);
    f << "{\"Rdz\": {\"PRO\": {\"filt";  // 截断
  }
  UT_CHECK(proxy_head_state::load().is_object());
}

UT_TEST(t03_unlock_cache_roundtrip_and_dedupe) {
  fresh_home("uc");
  // 缺失 → nullopt
  UT_CHECK(!unlock_cache::load().has_value());

  std::vector<unlock_cache::UnlockItem> items = {
      {"Netflix", "Yes", std::string("US"), std::string("2026-10-25T10:00:00Z")},
      {"Disney+", "No", std::nullopt, std::nullopt},
  };
  UT_CHECK(unlock_cache::save(items, std::string("2026-10-25T10:00:05Z")).is_ok());

  auto c = unlock_cache::load();
  UT_CHECK(c.has_value());
  if (c) {
    UT_CHECK_EQ(c->items.size(), 2u);
    UT_CHECK(c->time && *c->time == "2026-10-25T10:00:05Z");
    if (c->items.size() == 2) {
      UT_CHECK_EQ(c->items[0].name, std::string("Netflix"));
      UT_CHECK_EQ(c->items[0].status, std::string("Yes"));
      UT_CHECK(c->items[0].region && *c->items[0].region == "US");
      UT_CHECK(c->items[1].status == "No");
      UT_CHECK(!c->items[1].region.has_value());  // null round-trip
    }
  }

  // time 为空 → 落 null（前端 time 键未写入语义）
  UT_CHECK(unlock_cache::save(items, std::nullopt).is_ok());
  auto c2 = unlock_cache::load();
  UT_CHECK(c2.has_value() && !c2->time.has_value());
}

UT_TEST(t03_unlock_cache_dedupe_priority) {
  // ← unlock.tsx::dedupeUnlockItems / getStatusPriority：
  // Pending=0、其余=1，且「itemPriority > existingPriority」才替换 ——
  // 即非 Pending（1）胜过 Pending（0），Pending 优先级最低
  std::vector<unlock_cache::UnlockItem> items = {
      {"Netflix", "Yes", std::string("US"), std::nullopt},
      {"  netflix ", "Pending", std::nullopt, std::string("t1")},  // 同名归一
      {"Disney+", "Pending", std::nullopt, std::nullopt},
      {"disney+", "Yes", std::string("CA"), std::string("t2")},
  };
  auto out = unlock_cache::dedupe_items(items);
  UT_CHECK_EQ(out.size(), 2u);
  for (auto& it : out) {
    // netflix：新项 Pending(0) < 旧项 Yes(1) → 旧项胜出并合并新项可选字段
    if (it.name == "Netflix") {
      UT_CHECK_EQ(it.status, std::string("Yes"));
      UT_CHECK(it.region && *it.region == "US");
      UT_CHECK(it.check_time && *it.check_time == "t1");  // fallback 补入
    }
    // disney+：平级（同为非 Pending）→ 新项 preferred + 旧项可选字段
    if (it.name == "disney+") {
      UT_CHECK_EQ(it.status, std::string("Yes"));
      UT_CHECK(it.region && *it.region == "CA");
      UT_CHECK(it.check_time && *it.check_time == "t2");
    }
  }
}
