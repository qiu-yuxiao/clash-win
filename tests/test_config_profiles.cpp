// T03 门禁测试④：profiles 增删/切换 current/文件名规则/保护件/清理
#include <windows.h>

#include <fstream>
#include <set>
#include <string>

#include "config/profiles.h"
#include "clashwin/config/constants.h"
#include "cmd/cmd_install.h"
#include "config/prfitem.h"
#include "config/verge.h"
#include "test_util.h"
#include "util/dirs.h"
#include "util/utf.h"

using namespace clashwin;
using namespace clashwin::config;
using namespace clashwin::util;
using clashwin::utf::utf8_to_utf16;
using clashwin::utf::utf16_to_utf8;

namespace {

std::wstring fresh_profiles_home(const char* tag) {
  wchar_t buf[MAX_PATH];
  GetTempPathW(MAX_PATH, buf);
  std::wstring root = std::wstring(buf) + L"clashwin_t03\\" + utf::utf8_to_utf16(tag);
  CreateDirectoryW((std::wstring(buf) + L"clashwin_t03").c_str(), nullptr);
  CreateDirectoryW(root.c_str(), nullptr);
  CreateDirectoryW((root + L"\\profiles").c_str(), nullptr);
  DeleteFileW((root + L"\\profiles.yaml").c_str());
  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", root.c_str());
  return root;
}

bool file_exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

UT_TEST(t03_profiles_filename_rules) {
  // ← profiles.rs::is_profile_file 四条正则
  UT_CHECK(profiles::is_profile_file("R123abcXYZ.yaml"));
  // [a-zA-Z0-9]+ 不含下划线（与原版正则一致）
  UT_CHECK(!profiles::is_profile_file("L_Direct_Imports.yaml"));
  UT_CHECK(profiles::is_profile_file("m2CFYn8m9YMw.yaml"));
  UT_CHECK(profiles::is_profile_file("sOIpbav0QpMd.js"));
  UT_CHECK(profiles::is_profile_file("rwWMH8DGFCbm.yaml"));
  UT_CHECK(profiles::is_profile_file("pGlaepWR90Hn.yaml"));
  UT_CHECK(profiles::is_profile_file("gAbCdEfGhIjK.yaml"));
  UT_CHECK(!profiles::is_profile_file("x12345678.yaml"));    // 非法前缀
  UT_CHECK(!profiles::is_profile_file("R12345678.js"));      // 错扩展名
  UT_CHECK(!profiles::is_profile_file("s12345678.yaml"));    // 错扩展名
  UT_CHECK(!profiles::is_profile_file("Merge.yaml"));        // 无前缀数字
  UT_CHECK(!profiles::is_profile_file("R.yaml"));            // 空 uid 体
  UT_CHECK(profiles::is_protected_global_file("Merge.yaml"));
  UT_CHECK(profiles::is_protected_global_file("Script.js"));
  UT_CHECK(!profiles::is_protected_global_file("R12345678.yaml"));
}

UT_TEST(t03_profiles_from_local_appends_with_subitems) {
  fresh_profiles_home("local");
  profiles::set_profiles_instance(nullptr);  // 清除其它用例残留

  IProfiles p = profiles::load();

  // 注册实例供 ensure_enhance_items 追加子项
  profiles::set_profiles_instance(&p);
  auto item = PrfItem::from_local("My Local", "desc", std::nullopt, std::nullopt);
  profiles::set_profiles_instance(nullptr);
  UT_CHECK(item.is_ok());
  PrfItem local = item.value();

  // 文件名规则：L + uid + .yaml
  UT_CHECK(local.uid && local.uid->rfind("L", 0) == 0);
  UT_CHECK(local.file && *local.file == *local.uid + ".yaml");
  // 子项自动挂载 merge/script/rules/proxies（groups 不创建）
  UT_CHECK(local.option.has_value());
  UT_CHECK(local.option->merge.has_value());
  UT_CHECK(local.option->script.has_value());
  UT_CHECK(local.option->rules.has_value());
  UT_CHECK(local.option->proxies.has_value());
  UT_CHECK(!local.option->groups.has_value());
  // 默认模板内容
  UT_CHECK(local.file_data.has_value());
  UT_CHECK_EQ(*local.file_data, std::string(tmpl::kItemLocal));

  // 追加：current 自动激活（首个 local/remote 项）。
  // 注意：ensure_enhance_items 已先向 p 追加 m/s/r/p 四个子项（与 Rust 全局
  // profiles 行为一致），故 items 共 5 项。
  UT_CHECK(p.append_item(local).is_ok());
  UT_CHECK(p.current.has_value() && *p.current == *local.uid);
  UT_CHECK(p.items && p.items->size() == 5);
  // 内容落盘
  UT_CHECK(file_exists(util::dirs::app_profiles_dir() + L"\\" +
                       utf::utf8_to_utf16(*local.file)));

  // 子项文件也已由 ensure_enhance_items 写出
  UT_CHECK(file_exists(util::dirs::app_profiles_dir() + L"\\" +
                       utf::utf8_to_utf16(*local.option->merge + ".yaml")));
}

UT_TEST(t03_profiles_append_remote_same_url_dedup) {
  fresh_profiles_home("dedup");
  IProfiles p = profiles::load();

  PrfItem a;
  a.uid = "Raaa";
  a.set_type(PrfType::Remote);
  a.url = "https://example.com/sub";
  a.file = "Raaa.yaml";
  UT_CHECK(p.append_item(a).is_ok());
  UT_CHECK(p.current && *p.current == "Raaa");

  // 同 url remote → 复用既有 uid/file
  PrfItem b;
  b.uid = "Rbbb";
  b.set_type(PrfType::Remote);
  b.url = "https://example.com/sub";
  b.file = "Rbbb.yaml";
  UT_CHECK(p.append_item(b).is_ok());
  UT_CHECK(b.uid && *b.uid == "Raaa");  // 被改写为既有 uid
  UT_CHECK(p.items && p.items->size() == 1);
}

UT_TEST(t03_profiles_patch_item_and_switch_current) {
  fresh_profiles_home("patch");
  IProfiles p = profiles::load();

  PrfItem a;
  a.uid = "Raaa";
  a.set_type(PrfType::Remote);
  a.url = "https://a.example";
  a.file = "Raaa.yaml";
  a.name = "First";
  PrfItem b;
  b.uid = "Lbbb";
  b.set_type(PrfType::Local);
  b.file = "Lbbb.yaml";
  b.name = "Second";
  UT_CHECK(p.append_item(a).is_ok());
  UT_CHECK(p.append_item(b).is_ok());
  UT_CHECK(p.current && *p.current == "Raaa");  // 首个 remote

  // patch_config 仅修 current，且 uid 必须存在
  IProfiles patch;
  patch.current = "Lbbb";
  p.patch_config(patch);
  UT_CHECK(p.current && *p.current == "Lbbb");

  patch.current = "Rnonexistent";
  p.patch_config(patch);
  UT_CHECK(p.current && *p.current == "Lbbb");  // 非法 uid 不改

  // patch_item：name 可改、file 禁改（路径穿越防护）
  PrfItem patch_item;
  patch_item.name = "Renamed";
  patch_item.file = "../../evil.yaml";
  UT_CHECK(p.patch_item("Raaa", patch_item).is_ok());
  const PrfItem* got = p.get_item("Raaa");
  UT_CHECK(got && got->name && *got->name == "Renamed");
  UT_CHECK(got && got->file && *got->file == "Raaa.yaml");
  // 不存在的 uid → 错误
  UT_CHECK(!p.patch_item("Rmissing", patch_item).is_ok());
}

UT_TEST(t03_profiles_delete_item_cascades) {
  fresh_profiles_home("delete");
  IProfiles p = profiles::load();

  // 构造 remote + 关联子项（merge/script/rules/proxies uid）
  PrfItem main;
  main.uid = "Raaa";
  main.set_type(PrfType::Remote);
  main.url = "https://a.example";
  main.file = "Raaa.yaml";
  main.option = PrfOption{};
  main.option->merge = "m111";
  main.option->script = "s111";
  main.option->rules = "r111";
  main.option->proxies = "p111";
  p.items = std::vector<PrfItem>{main};
  p.current = "Raaa";

  // 建立文件
  auto touch = [&](const std::string& name) {
    std::ofstream f(util::dirs::app_profiles_dir() + L"\\" +
                        utf::utf8_to_utf16(name),
                    std::ios::binary | std::ios::trunc);
    f << "x";
  };
  touch("Raaa.yaml"); touch("m111.yaml"); touch("s111.js");
  touch("r111.yaml"); touch("p111.yaml"); touch("Lkeep.yaml");

  // 子项也挂在 items 上（真实 profiles 形态；take_item_file_by_uid 才能摘除）
  auto sub_item = [](const char* uid, const char* file, const char* type) {
    PrfItem it;
    it.uid = uid;
    it.itype = type;
    it.file = file;
    return it;
  };
  p.items->push_back(sub_item("m111", "m111.yaml", "merge"));
  p.items->push_back(sub_item("s111", "s111.js", "script"));
  p.items->push_back(sub_item("r111", "r111.yaml", "rules"));
  p.items->push_back(sub_item("p111", "p111.yaml", "proxies"));

  PrfItem keeper;
  keeper.uid = "Lkeep";
  keeper.set_type(PrfType::Local);
  keeper.file = "Lkeep.yaml";
  p.items->push_back(keeper);

  auto r = p.delete_item("Raaa");
  UT_CHECK(r.is_ok());
  UT_CHECK(r.value());  // 删的是 current → true
  // 主项 + 关联子项文件全删
  UT_CHECK(!file_exists(util::dirs::app_profiles_dir() + L"\\Raaa.yaml"));
  UT_CHECK(!file_exists(util::dirs::app_profiles_dir() + L"\\m111.yaml"));
  UT_CHECK(!file_exists(util::dirs::app_profiles_dir() + L"\\s111.js"));
  UT_CHECK(!file_exists(util::dirs::app_profiles_dir() + L"\\r111.yaml"));
  UT_CHECK(!file_exists(util::dirs::app_profiles_dir() + L"\\p111.yaml"));
  UT_CHECK(file_exists(util::dirs::app_profiles_dir() + L"\\Lkeep.yaml"));
  // current 顺延到首个 remote/local
  UT_CHECK(p.current && *p.current == "Lkeep");
  UT_CHECK(p.items && p.items->size() == 1);  // 主项+4 子项全摘除，仅剩 keeper
}

UT_TEST(t03_profiles_reorder_and_cleanup_orphans) {
  fresh_profiles_home("cleanup");
  IProfiles p = profiles::load();

  PrfItem a;
  a.uid = "Raaa"; a.set_type(PrfType::Remote); a.url = "https://a"; a.file = "Raaa.yaml";
  PrfItem b;
  b.uid = "Lbbb"; b.set_type(PrfType::Local); b.file = "Lbbb.yaml";
  p.items = std::vector<PrfItem>{a, b};

  // reorder：aaa 移到 bbb 之后
  UT_CHECK(p.reorder("Raaa", "Lbbb").is_ok());
  UT_CHECK(p.items && p.items->size() == 2 &&
           (*p.items)[0].uid == "Lbbb" && (*p.items)[1].uid == "Raaa");

  // 孤立文件清理：orphan 按规则命名但不在 items；保护件豁免
  auto touch = [&](const std::string& name) {
    std::ofstream f(util::dirs::app_profiles_dir() + L"\\" +
                        utf::utf8_to_utf16(name),
                    std::ios::binary | std::ios::trunc);
    f << "x";
  };
  touch("Raaa.yaml"); touch("Lbbb.yaml"); touch("Rorphan.yaml");
  touch("Merge.yaml"); touch("Script.js");
  UT_CHECK(p.cleanup_orphaned_files().is_ok());
  UT_CHECK(file_exists(util::dirs::app_profiles_dir() + L"\\Raaa.yaml"));
  UT_CHECK(file_exists(util::dirs::app_profiles_dir() + L"\\Lbbb.yaml"));
  UT_CHECK(file_exists(util::dirs::app_profiles_dir() + L"\\Merge.yaml"));
  UT_CHECK(file_exists(util::dirs::app_profiles_dir() + L"\\Script.js"));
  UT_CHECK(!file_exists(util::dirs::app_profiles_dir() + L"\\Rorphan.yaml"));
}

UT_TEST(t03_prfitem_read_and_save_file) {
  fresh_profiles_home("rw");
  PrfItem item;
  item.file = "Lreadwrite.yaml";
  // 初始写
  UT_CHECK(item.save_file_data("key: value\r\nlist:\r\n  - a\r\n").is_ok());
  auto content = item.read_file();
  UT_CHECK(content.is_ok());
  // CRLF 归一后一致 → 跳过写（内容不变）
  UT_CHECK(item.save_file_data("key: value\nlist:\n  - a\n").is_ok());
  auto content2 = item.read_file();
  UT_CHECK(content2.is_ok());
  // 内容确实还是最初那份（未重写）
  UT_CHECK_EQ(content2.value(), std::string("key: value\r\nlist:\r\n  - a\r\n"));
  // 真正不同 → 覆盖
  UT_CHECK(item.save_file_data("changed: true").is_ok());
  auto content3 = item.read_file();
  UT_CHECK(content3.is_ok() && content3.value() == "changed: true");
  // 无 file 字段 → 错误
  PrfItem nofile;
  UT_CHECK(!nofile.read_file().is_ok());
  UT_CHECK(!nofile.save_file_data("x").is_ok());
}

UT_TEST(t03_prfitem_from_url_placeholder) {
  // T03 占位：网络下载属 T06/T15，必须显式报错而不是静默成功
  auto r = PrfItem::from_url("https://example.com/sub", std::nullopt,
                             std::nullopt, std::nullopt);
  UT_CHECK(!r.is_ok());
}

UT_TEST(t03_prfitem_option_merge_semantics) {
  // ← prfitem.rs::PrfOption::merge
  PrfOption base;
  base.user_agent = "UA/base";
  base.timeout_seconds = 30;
  base.merge = "m1";
  PrfOption patch;
  patch.user_agent = "UA/new";
  patch.allow_auto_update = false;
  auto merged = PrfOption::merge_opt(base, patch);
  UT_CHECK(merged.has_value());
  UT_CHECK(merged->user_agent && *merged->user_agent == "UA/new");
  UT_CHECK(merged->timeout_seconds && *merged->timeout_seconds == 30);  // 保留
  UT_CHECK(merged->merge && *merged->merge == "m1");                    // 保留
  UT_CHECK(merged->allow_auto_update.has_value() &&
           !*merged->allow_auto_update);

  // one 空 → other；both 空 → 空
  UT_CHECK(PrfOption::merge_opt(std::nullopt, base).has_value());
  UT_CHECK(!PrfOption::merge_opt(std::nullopt, std::nullopt).has_value());
}

UT_TEST(t03_profiles_save_load_roundtrip) {
  fresh_profiles_home("roundtrip");
  IProfiles p = profiles::load();
  PrfItem item;
  item.uid = "Rxyz";
  item.set_type(PrfType::Remote);
  item.url = "https://roundtrip";
  item.file = "Rxyz.yaml";
  item.name = "RT";
  item.option = PrfOption{};
  item.option->allow_auto_update = true;
  item.option->merge = "m2";
  item.updated = 1700000000;
  item.selected = std::vector<PrfSelected>{PrfSelected{"PROXY", std::string("HK-1")}};
  item.extra = PrfExtra{1, 2, 3, 4};
  UT_CHECK(p.append_item(item).is_ok());
  UT_CHECK(p.save_file().is_ok());

  IProfiles p2 = profiles::load();
  UT_CHECK(p2.current && *p2.current == "Rxyz");
  const PrfItem* got = p2.get_item("Rxyz");
  UT_CHECK(got != nullptr);
  UT_CHECK(got->name && *got->name == "RT");
  UT_CHECK(got->updated && *got->updated == 1700000000);
  UT_CHECK(got->option && got->option->allow_auto_update.has_value());
  UT_CHECK(got->option->merge && *got->option->merge == "m2");
  UT_CHECK(got->selected && got->selected->size() == 1 &&
           (*got->selected)[0].now && *(*got->selected)[0].now == "HK-1");
  UT_CHECK(got->extra && got->extra->upload == 1 && got->extra->expire == 4);
  // groups 为 null 时保留 null 语义
  UT_CHECK(!got->option->groups.has_value());
}

UT_TEST(t_bug362_prf_option_custom_user_agent_normalization_and_merge) {
  // 1) normalize_custom_user_agent
  UT_CHECK(!prfitem::normalize_custom_user_agent(std::nullopt).has_value());
  UT_CHECK(!prfitem::normalize_custom_user_agent("").has_value());
  UT_CHECK(!prfitem::normalize_custom_user_agent("   \t  ").has_value());
  auto norm = prfitem::normalize_custom_user_agent("  ClashForWindows/0.20.39  ");
  UT_CHECK(norm.has_value() && *norm == "ClashForWindows/0.20.39");

  // 2) merge_opt：other.user_agent 为 nullopt 时保留并 trim 原有自定义 UA
  PrfOption base;
  base.user_agent = "  OldCustomUA/1.0  ";
  base.update_interval = 60;
  PrfOption patch_keep;
  patch_keep.update_interval = 120;
  auto m1 = PrfOption::merge_opt(base, patch_keep);
  UT_CHECK(m1.has_value());
  UT_CHECK(m1->user_agent.has_value() && *m1->user_agent == "OldCustomUA/1.0");
  UT_CHECK(m1->update_interval.has_value() && *m1->update_interval == 120);

  // 3) merge_opt：other.user_agent 为空串或纯空白时，显式清空已有自定义 UA（回退默认兼容 UA）
  PrfOption patch_clear;
  patch_clear.user_agent = "   ";
  auto m2 = PrfOption::merge_opt(base, patch_clear);
  UT_CHECK(m2.has_value());
  UT_CHECK(!m2->user_agent.has_value());

  // 4) IProfiles::patch_item 编辑保存空白 UA 时同样归一为 nullopt
  fresh_profiles_home("ua_patch");
  IProfiles p = profiles::load();
  PrfItem item;
  item.uid = "Rua1";
  item.set_type(PrfType::Remote);
  item.url = "https://example.com/sub";
  item.file = "Rua1.yaml";
  item.option = PrfOption{};
  item.option->user_agent = "CustomUA/1.0";
  UT_CHECK(p.append_item(item).is_ok());

  PrfItem edit;
  edit.option = PrfOption{};
  edit.option->user_agent = "   ";
  UT_CHECK(p.patch_item("Rua1", edit).is_ok());

  IProfiles p_reloaded = profiles::load();
  const PrfItem* got = p_reloaded.get_item("Rua1");
  UT_CHECK(got != nullptr && got->option.has_value());
  if (got && got->option) {
    UT_CHECK(!got->option->user_agent.has_value());
  }
}

UT_TEST(t_bug333_copy_clash_env_default_mixed_port_constant) {
  _putenv_s("CLASH_VERGE_REV_IP", "");
  IVerge v_default;
  auto ps = cmd::build_clash_env_text(v_default);
  UT_CHECK(ps.is_ok());
  if (ps.is_ok()) {
    const std::string expected_port = std::to_string(consts::kDefaultMixed);
    UT_CHECK(ps.value().find("127.0.0.1:" + expected_port) != std::string::npos);
    UT_CHECK(ps.value().find("7897") == std::string::npos);
  }

  IVerge v_custom;
  v_custom.proxy_host = "192.168.50.2";
  v_custom.verge_mixed_port = 10899;
  v_custom.env_type = "sh";
  auto sh = cmd::build_clash_env_text(v_custom);
  UT_CHECK(sh.is_ok());
  if (sh.is_ok()) {
    UT_CHECK(sh.value().find("http://192.168.50.2:10899") != std::string::npos);
    UT_CHECK(sh.value().find("socks5://192.168.50.2:10899") != std::string::npos);
  }
}
