// T03：proxy_head_state.json 读写
// （proxy_head_state 形态对照实机 %APPDATA%\io.github.clash-win.clash-win
//   /proxy_head_state.json 只读样本）
#include <windows.h>

#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "config/proxy_head_state.h"
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
