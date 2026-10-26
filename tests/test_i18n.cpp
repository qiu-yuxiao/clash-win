// T10 i18n 测试：13 语言全加载、fallback 链、zh-cn/zh-tw 归一、插值、热切换。
#include <windows.h>

#include <filesystem>
#include <fstream>

#include "test_util.h"
#include "i18n/i18n.h"
#include "util/utf.h"

using namespace clashwin::i18n;
namespace fs = std::filesystem;

// 测试 exe 输出目录（构建后 add_clashwin_copy_locales 拷贝 locales/）
static std::wstring exe_locales_dir() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring dir(path);
  size_t pos = dir.find_last_of(L"\\/");
  return (pos == std::wstring::npos ? L"." : dir.substr(0, pos)) + L"\\locales";
}

UT_TEST(i18n_resolve_language_normalization) {
  // i18n.ts resolveLanguage 逐条对照
  UT_CHECK_EQ(I18n::resolve_language("zh-cn"), "zh");
  UT_CHECK_EQ(I18n::resolve_language("zh-CN"), "zh");
  UT_CHECK_EQ(I18n::resolve_language("zh_tw"), "zhtw");   // _ 归一 → -
  UT_CHECK_EQ(I18n::resolve_language("zh-TW"), "zhtw");
  UT_CHECK_EQ(I18n::resolve_language("en-US"), "en");     // base 命中
  UT_CHECK_EQ(I18n::resolve_language("EN"), "en");
  UT_CHECK_EQ(I18n::resolve_language("zhtw"), "zhtw");
  UT_CHECK_EQ(I18n::resolve_language("jp"), "jp");
  UT_CHECK_EQ(I18n::resolve_language("pt-BR"), "zh");     // 不支持 → fallback
  UT_CHECK_EQ(I18n::resolve_language(""), "zh");
  UT_CHECK_EQ(I18n::resolve_language("xx_YY"), "zh");
  UT_CHECK_EQ(I18n::supported_languages().size(), 13u);
}

UT_TEST(i18n_load_13_languages) {
  UT_CHECK(I18n::instance().init(exe_locales_dir(), "zh"));
  UT_CHECK_EQ(I18n::instance().loaded_language_count(), 13u);
  // 全语言 × 10 命名空间（settings/layout/connections/proxies 共性抽查）
  for (const std::string& lang : I18n::supported_languages()) {
    UT_CHECK_MSG(I18n::instance().has_bundle(lang, "settings"),
                 ("missing settings for " + lang).c_str());
    UT_CHECK_MSG(I18n::instance().has_bundle(lang, "layout"),
                 ("missing layout for " + lang).c_str());
  }
  UT_CHECK_EQ(I18n::instance().language(), "zh");
}

UT_TEST(i18n_t_and_translation) {
  auto& i = I18n::instance();
  i.set_language("zh");
  UT_CHECK_EQ(i.t("settings.page.title"), "设置");
  i.set_language("en");
  UT_CHECK_EQ(i.t("settings.page.title"), "Settings");
  i.set_language("jp");
  UT_CHECK_EQ(i.t("settings.page.title"), "設定");
}

UT_TEST(i18n_interpolation) {
  auto& i = I18n::instance();
  i.set_language("zh");
  // connections.notices.copied = "已复制: {{address}}"
  std::string s = i.t("connections.notices.copied",
                      {{"address", "127.0.0.1:9090"}});
  UT_CHECK_MSG(s.find("127.0.0.1:9090") != std::string::npos,
               s.c_str());
  UT_CHECK_MSG(s.find("{{") == std::string::npos,
               ("placeholder left: " + s).c_str());
}

UT_TEST(i18n_missing_key_fallback_chain) {
  auto& i = I18n::instance();
  // 语言级 + key 级双缺失 → key 原文（i18next 缺省）
  i.set_language("en");
  UT_CHECK_EQ(i.t("settings.no.such.key"), "settings.no.such.key");
}

UT_TEST(i18n_fallback_to_zh_when_lang_missing_key) {
  // 构造仅 zh 全量 + en 缺 key 的临时目录（写 %TEMP%，不碰真实数据）
  wchar_t temp_dir[MAX_PATH];
  GetTempPathW(MAX_PATH, temp_dir);
  fs::path root = fs::path(temp_dir) / L"clashwin_i18n_fb_test";
  fs::create_directories(root / L"zh");
  fs::create_directories(root / L"en");
  auto write = [](const fs::path& p, const std::string& content) {
    std::ofstream f(p, std::ios::binary);
    f << content;
  };
  write(root / L"zh" / L"settings.json",
        R"({"page":{"title":"设置","onlyZh":"中文专属"}})");
  write(root / L"en" / L"settings.json", R"({"page":{"title":"Settings"}})");

  UT_CHECK(I18n::instance().init(root.wstring(), "en") ||
           I18n::instance().inited());  // 部分语言目录缺失允许 init 返回 false
  UT_CHECK_EQ(I18n::instance().loaded_language_count(), 2u);
  auto& i = I18n::instance();
  UT_CHECK_EQ(i.t("settings.page.title"), "Settings");   // 当前语言命中
  UT_CHECK_EQ(i.t("settings.page.onlyZh"), "中文专属");    // en 缺 → zh
  UT_CHECK_EQ(i.t("settings.none.what"), "settings.none.what");  // 两级缺 → key
  // 清理
  std::error_code ec;
  fs::remove_all(root, ec);
}

UT_TEST(i18n_hot_switch_and_listener) {
  auto& i = I18n::instance();
  // 重新载全量（上面用例换了资源集）
  UT_CHECK(I18n::instance().init(exe_locales_dir(), "zh"));
  std::vector<std::string> seen;
  uint64_t id = i.subscribe([&](const std::string& lang) { seen.push_back(lang); });

  UT_CHECK(i.set_language("zh-TW"));   // 归一 zhtw
  UT_CHECK_EQ(i.language(), "zhtw");
  UT_CHECK(i.set_language("en"));
  UT_CHECK_EQ(i.language(), "en");
  UT_CHECK_EQ(seen.size(), 2u);
  UT_CHECK_EQ(seen[0], "zhtw");
  UT_CHECK_EQ(seen[1], "en");
  // 相同语言不触发
  UT_CHECK(i.set_language("en"));
  UT_CHECK_EQ(seen.size(), 2u);
  // 退订后不触发
  i.unsubscribe(id);
  i.set_language("zh");
  UT_CHECK_EQ(seen.size(), 2u);
  // 不支持语言：归一 fallback 但返回 false
  UT_CHECK(!i.set_language("klingon"));
  UT_CHECK_EQ(i.language(), "zh");
}
