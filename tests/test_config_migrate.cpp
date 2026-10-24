// T03 门禁测试③：版本迁移 detect/backup/迁移矩阵（对照 migrate.rs）
#include <windows.h>

#include <fstream>
#include <string>

#include "config/migrate.h"
#include "util/str_utils.h"
#include "config/verge.h"
#include "test_util.h"
#include "util/utf.h"
#include "config/yaml_serde.h"

using namespace clashwin;
using namespace clashwin::config;
using namespace clashwin::util;
using clashwin::utf::utf8_to_utf16;
using clashwin::utf::utf16_to_utf8;
using namespace clashwin::config::consts;

namespace {

std::wstring temp_root(const char* tag) {
  wchar_t buf[MAX_PATH];
  GetTempPathW(MAX_PATH, buf);
  std::wstring root = std::wstring(buf) + L"clashwin_t03\\" + utf::utf8_to_utf16(tag);
  CreateDirectoryW((std::wstring(buf) + L"clashwin_t03").c_str(), nullptr);
  CreateDirectoryW(root.c_str(), nullptr);
  return root;
}

void write_file(const std::wstring& path, const std::string& content) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f << content;
}

bool file_exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string read_backup_content(const std::wstring& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return "<missing>";
  return std::string((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
}

}  // namespace

UT_TEST(t03_migrate_detect_version) {
  YAML::Node empty(YAML::NodeType::Map);
  UT_CHECK_EQ(migrate::detect_version(empty), 1u);  // 缺失 → v1

  YAML::Node m(YAML::NodeType::Map);
  serde::set_u32(m, "config_version", 3);
  UT_CHECK_EQ(migrate::detect_version(m), 3u);
}

UT_TEST(t03_migrate_verge_v1_to_v2_and_v2_to_v3) {
  IVerge cfg;
  cfg.start_page = "/home";
  cfg.app_log_max_count = 8;
  uint32_t v = migrate::migrate_verge(cfg, 1);
  UT_CHECK_EQ(v, 3u);
  UT_CHECK(cfg.start_page && *cfg.start_page == "/");
  UT_CHECK(cfg.app_log_max_count && *cfg.app_log_max_count == 40);

  // 用户自定义日志数不被覆盖
  IVerge cfg2;
  cfg2.start_page = "/home";
  cfg2.app_log_max_count = 12;
  migrate::migrate_verge(cfg2, 2);
  UT_CHECK(cfg2.app_log_max_count && *cfg2.app_log_max_count == 12);
  UT_CHECK(cfg2.start_page && *cfg2.start_page == "/home");  // v2 起不再动 start_page

  // 非 /home 的 start_page 保持
  IVerge cfg3;
  cfg3.start_page = "/proxy";
  migrate::migrate_verge(cfg3, 1);
  UT_CHECK(cfg3.start_page && *cfg3.start_page == "/proxy");

  // 幂等
  IVerge cfg4;
  cfg4.start_page = "/home";
  migrate::migrate_verge(cfg4, 1);
  migrate::migrate_verge(cfg4, 3);
  UT_CHECK(cfg4.start_page && *cfg4.start_page == "/");
}

UT_TEST(t03_migrate_future_version_not_downgraded) {
  IVerge cfg;
  cfg.start_page = "/home";
  cfg.app_log_max_count = 8;
  uint32_t v = migrate::migrate_verge(cfg, 5);
  UT_CHECK_EQ(v, 5u);
  UT_CHECK(cfg.start_page && *cfg.start_page == "/home");  // 未被迁移改写
  UT_CHECK(cfg.app_log_max_count && *cfg.app_log_max_count == 8);
}

UT_TEST(t03_migrate_clash_v1_to_v2_stack) {
  YAML::Node m(YAML::NodeType::Map);
  YAML::Node tun(YAML::NodeType::Map);
  tun["stack"] = "system";
  m["tun"] = tun;
  uint32_t v = migrate::migrate_clash(m, 1);
  UT_CHECK_EQ(v, 2u);
  UT_CHECK_EQ(m["tun"]["stack"].Scalar(), std::string("gvisor"));

  // 幂等 + 高版本不降级
  YAML::Node m2(YAML::NodeType::Map);
  YAML::Node tun2(YAML::NodeType::Map);
  tun2["stack"] = "system";
  m2["tun"] = tun2;
  uint32_t v2 = migrate::migrate_clash(m2, 9);
  UT_CHECK_EQ(v2, 9u);
  UT_CHECK_EQ(m2["tun"]["stack"].Scalar(), std::string("system"));
}

UT_TEST(t03_migrate_backup_before_migration) {
  std::wstring root = temp_root("backup");
  std::wstring cfg = root + L"\\verge.yaml";
  write_file(cfg, "app_log_max_count: 8\nstart_page: /home\n");

  // 首次备份 → 生成 .bak.v2
  auto backup = migrate::backup_before_migration(cfg, 2);
  UT_CHECK(backup.has_value());
  UT_CHECK(file_exists(root + L"\\verge.yaml.bak.v2"));
  // 备份内容 = 原文件
  UT_CHECK_EQ(read_backup_content(root + L"\\verge.yaml.bak.v2"),
              std::string("app_log_max_count: 8\nstart_page: /home\n"));

  // 同版本重复备份 → 不覆盖（保留最早那份）
  write_file(cfg, "changed\n");
  auto backup2 = migrate::backup_before_migration(cfg, 2);
  UT_CHECK(backup2.has_value());
  UT_CHECK_EQ(read_backup_content(root + L"\\verge.yaml.bak.v2"),
              std::string("app_log_max_count: 8\nstart_page: /home\n"));
}

UT_TEST(t03_migrate_verge_yaml_end_to_end) {
  // 构造 v2 样例 yaml → 载入 → 迁移 → 备份存在 + 结果正确
  std::wstring root = temp_root("e2e");
  std::wstring cfg = root + L"\\verge.yaml";
  write_file(cfg, "config_version: 2\napp_log_max_count: 8\nstart_page: /home\n"
                  "theme_mode: light\n");
  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", root.c_str());
  _wputenv_s(L"CLASHWIN_PORTABLE_OVERRIDE", L"0");

  auto loaded = verge::load();
  UT_CHECK(loaded.is_ok());
  IVerge v = loaded.value();
  UT_CHECK_EQ(migrate::migrate_verge(v, v.config_version.value_or(1)), 3u);
  // v2→v3 生效
  UT_CHECK(v.app_log_max_count && *v.app_log_max_count == 40);
  // 用户字段保留
  UT_CHECK(v.theme_mode && *v.theme_mode == "light");
  UT_CHECK(file_exists(root + L"\\verge.yaml.bak.v2"));

  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
}
