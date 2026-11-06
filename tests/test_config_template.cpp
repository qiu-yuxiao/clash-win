// T03 门禁测试①：空环境首启产物与 tmpl.rs / serde 形态逐字节 diff 为空
// 期望值 = tmpl.rs 模板常量 + serde_yaml_ng 序列化形态（以实机
// %APPDATA%\io.github.clash-win.clash-win 三份 yaml 为对照基准只读抄录）
#include <windows.h>

#include <fstream>
#include <string>

#include "config/clash.h"
#include "config/config.h"
#include "clashwin/config/constants.h"
#include "config/migrate.h"
#include "config/profiles.h"
#include "config/verge.h"
#include "test_util.h"
#include "util/dirs.h"
#include "util/utf.h"
#include "config/yaml_serde.h"

using namespace clashwin;
using namespace clashwin::config;
using namespace clashwin::util;
using clashwin::utf::utf8_to_utf16;
using clashwin::utf::utf16_to_utf8;
using namespace clashwin::config::consts;

namespace {

// 每个用例独立临时 home（绝不写真实 %APPDATA%）
std::wstring fresh_home(const char* tag) {
  wchar_t buf[MAX_PATH];
  DWORD n = GetTempPathW(MAX_PATH, buf);
  std::wstring root = std::wstring(buf) + L"clashwin_t03\\" + utf::utf8_to_utf16(tag);
  RemoveDirectoryW((root + L"\\profiles").c_str());
  DeleteFileW((root + L"\\verge.yaml").c_str());
  DeleteFileW((root + L"\\config.yaml").c_str());
  DeleteFileW((root + L"\\profiles.yaml").c_str());
  RemoveDirectoryW(root.c_str());
  std::wstring parent = std::wstring(buf) + L"clashwin_t03";
  BOOL c1 = CreateDirectoryW(parent.c_str(), nullptr);
  DWORD e1 = GetLastError();
  BOOL c2 = CreateDirectoryW(root.c_str(), nullptr);
  DWORD e2 = GetLastError();
  std::printf("  [diag] GetTempPathW=%u temp=%S c1=%d(err=%lu) c2=%d(err=%lu)\n",
              n, buf, c1, e1, c2, e2);
  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", root.c_str());
  _wputenv_s(L"CLASHWIN_PORTABLE_OVERRIDE", L"0");
  wchar_t ov[MAX_PATH] = {};
  GetEnvironmentVariableW(L"CLASHWIN_HOME_OVERRIDE", ov, MAX_PATH);
  std::printf("  [diag] env-echo=%S verge_path=%S\n", ov,
              util::dirs::verge_path().c_str());
  return root;
}

std::string read_text(const std::wstring& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return "<missing>";
  return std::string((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
}

}  // namespace

UT_TEST(t03_first_launch_verge_yaml_diff_empty) {
  fresh_home("verge");
  IVerge v = verge::make_template();
  auto r = verge::save_verge(v);
  if (!r.is_ok())
    std::printf("  [diag] save_verge err code=%d msg=%s\n", r.error().code,
                r.error().message.c_str());
  UT_CHECK(r.is_ok());
  // 首启语言：安装器所选语言优先，取不到才回落系统语言（用户指令 2026-11-07）
  std::string lang = verge::installer_language();
  if (lang.empty()) lang = verge::system_language();

  // 期望文本：serde_yaml_ng 对 IVerge::template() 的序列化产物
  // （字段顺序 = verge.rs:23-192 声明序；None→null；skip 字段 None 省键；
  //   pac_file_content 多行 → literal 块）
  std::string expected =
      "# Clash Verge Config\n"
      "\n"
      "config_version: 3\n"
      "app_log_level: null\n"
      "app_log_max_size: 128\n"
      "app_log_max_count: 40\n"
      "language: " + lang + "\n"
      "theme_mode: dark\n"
      "env_type: powershell\n"
      "start_page: /\n"
      "startup_script: null\n"
      "traffic_graph: true\n"
      "enable_memory_usage: true\n"
      "enable_group_icon: true\n"
      "pause_render_traffic_stats_on_blur: true\n"
      "menu_icon: monochrome\n"
      "notice_position: top-right\n"
      "collapse_navbar: false\n"
      "enable_tun_mode: null\n"
      "enable_auto_launch: false\n"
      "enable_silent_start: false\n"
      "enable_system_proxy: true\n"
      "enable_proxy_guard: true\n"
      "enable_bypass_check: true\n"
      "use_default_bypass: true\n"
      "system_proxy_bypass: null\n"
      "proxy_guard_duration: 30\n"
      "proxy_auto_config: false\n"
      "pac_file_content: |\n"
      "  function FindProxyForURL(url, host) {\n"
      "    return \"PROXY 127.0.0.1:%mixed-port%; SOCKS5 127.0.0.1:%mixed-port%; "
      "DIRECT;\";\n"
      "  }\n"
      "proxy_host: 127.0.0.1\n"
      "theme_setting: null\n"
      "web_ui_list: null\n"
      "clash_core: mini-mihomo\n"
      "enable_always_on_top: false\n"
      "home_cards: null\n"
      "auto_close_connection: false\n"
      "auto_check_update: false\n"
      "default_latency_test: null\n"
      "enable_auto_delay_detection: null\n"
      "auto_delay_detection_interval_minutes: null\n"
      "enable_builtin_enhanced: true\n"
      "proxy_layout_column: null\n"
      "test_list: null\n"
      "auto_log_clean: 2\n"
      "verge_mixed_port: 10811\n"
      "verge_socks_port: 10812\n"
      "verge_socks_enabled: false\n"
      "verge_port: 10813\n"
      "verge_http_enabled: false\n"
      "enable_auto_light_weight_mode: true\n"
      "auto_light_weight_minutes: 5\n"
      "enable_hover_jump_navigator: true\n"
      "hover_jump_navigator_delay: 280\n"
      "enable_external_controller: false\n"
      "rule_fallback: direct\n";

  std::string actual = read_text(util::dirs::verge_path());
  if (actual != expected) {
    std::printf("  verge diff:\n--- expected ---\n%s\n--- actual ---\n%s\n",
                expected.c_str(), actual.c_str());
  }
  UT_CHECK_EQ(actual, expected);
}

UT_TEST(t03_first_launch_clash_yaml_diff_empty) {
  fresh_home("clash");
  IClashTemp c = clash::load();  // 空环境 → 模板（9108 冲突时自动寻位）
  auto r = clash::save_clash(c);
  UT_CHECK(r.is_ok());

  // 期望文本：clash.rs::template() 的 serde_yaml_ng 产物（实机 config.yaml
  // 首启形态；allow-lan 出厂 false）
  std::string expected =
      "# Generated by Clash Verge\n"
      "\n"
      "config_version: 2\n"
      "mixed-port: 10811\n"
      "socks-port: 10812\n"
      "port: 10813\n"
      "log-level: warning\n"
      "allow-lan: false\n"
      "ipv6: false\n"
      "mode: rule\n"
      "tcp-concurrent: false\n"
      "dns:\n"
      "  enable: true\n"
      "  cache-size: 512\n"
      "profile:\n"
      "  store-selected: true\n"
      "  store-fake-ip: false\n"
      "external-controller: 127.0.0.1:9108\n"
      "external-controller-pipe: \\\\.\\pipe\\mini-mihomo-win\n"
      "tun:\n"
      "  enable: false\n"
      "  stack: gvisor\n"
      "  auto-route: true\n"
      "  strict-route: false\n"
      "  auto-detect-interface: true\n"
      "  dns-hijack:\n"
      "  - any:53\n"
      "  - tcp://any:53\n"
      "secret: adapted-by-clash-win\n"
      "external-controller-cors:\n"
      "  allow-private-network: true\n"
      "  allow-origins:\n"
      "  - tauri://localhost\n"
      "  - http://tauri.localhost\n"
      "  - https://yacd.metacubex.one\n"
      "  - https://metacubex.github.io\n"
      "  - https://board.zash.run.place\n"
      "unified-delay: true\n";

  std::string actual = read_text(util::dirs::clash_path());
  if (actual != expected) {
    std::printf("  clash diff:\n--- expected ---\n%s\n--- actual ---\n%s\n",
                expected.c_str(), actual.c_str());
  }
  UT_CHECK_EQ(actual, expected);
}

UT_TEST(t03_first_launch_profiles_and_protection_files) {
  fresh_home("profiles");
  CreateDirectoryW(util::dirs::app_profiles_dir().c_str(), nullptr);

  IProfiles p = profiles::load();
  UT_CHECK(p.config_version == 1u);
  auto r = profiles::ensure_default_profile_items(p);
  if (!r.is_ok())
    std::printf("  [diag] ensure_default err code=%d msg=%s\n", r.error().code,
                r.error().message.c_str());
  UT_CHECK(r.is_ok());
  UT_CHECK(p.items && p.items->size() == 2);
  UT_CHECK(!p.current.has_value());  // merge/script 不触发自动激活
  if (!p.items || p.items->size() < 2) return;  // 防崩：前置失败则跳过解引用

  // items[0] = Merge 保护件（uid 固定 "Merge"、file Merge.yaml）
  const PrfItem& m = (*p.items)[0];
  UT_CHECK(m.uid && *m.uid == "Merge");
  UT_CHECK(m.itype && *m.itype == "merge");
  UT_CHECK(!m.name.has_value());  // serde: name: null
  UT_CHECK(m.file && *m.file == "Merge.yaml");
  UT_CHECK(m.updated.has_value());
  // items[1] = Script 保护件
  const PrfItem& s = (*p.items)[1];
  UT_CHECK(s.uid && *s.uid == "Script");
  UT_CHECK(s.itype && *s.itype == "script");
  UT_CHECK(s.file && *s.file == "Script.js");

  UT_CHECK(p.save_file().is_ok());

  // 保护件内容与 tmpl.rs 模板逐字节一致
  UT_CHECK_EQ(read_text(util::dirs::app_profiles_dir() + L"\\Merge.yaml"),
              std::string(tmpl::kItemMerge));
  UT_CHECK_EQ(read_text(util::dirs::app_profiles_dir() + L"\\Script.js"),
              std::string(tmpl::kItemScript));

  // profiles.yaml 头部形态（config_version/current 固定，items 结构核对）
  std::string actual = read_text(util::dirs::profiles_path());
  std::string head =
      "# Profiles Config for Clash Verge\n"
      "\n"
      "config_version: 1\n"
      "current: null\n"
      "items:\n";
  UT_CHECK(actual.rfind(head, 0) == 0);
  // Merge 项 serde 形态：- uid: Merge / type / name: null / file / updated
  size_t pos = actual.find("- uid: Merge\n");
  UT_CHECK(pos != std::string::npos);
  if (pos != std::string::npos) {
    std::string item_head =
        "- uid: Merge\n"
        "  type: merge\n"
        "  name: null\n"
        "  file: Merge.yaml\n"
        "  updated: ";
    UT_CHECK(actual.compare(pos, item_head.size(), item_head) == 0);
  }
  UT_CHECK(actual.find("- uid: Script\n  type: script\n  name: null\n"
                       "  file: Script.js\n  updated: ") !=
           std::string::npos);
}

UT_TEST(t03_config_init_generates_all_three) {
  fresh_home("init");
  auto r = Config::instance().init(true);
  UT_CHECK(r.is_ok());
  UT_CHECK(GetFileAttributesW(util::dirs::verge_path().c_str()) !=
           INVALID_FILE_ATTRIBUTES);
  UT_CHECK(GetFileAttributesW(util::dirs::clash_path().c_str()) !=
           INVALID_FILE_ATTRIBUTES);
  UT_CHECK(GetFileAttributesW(util::dirs::profiles_path().c_str()) !=
           INVALID_FILE_ATTRIBUTES);
  UT_CHECK(GetFileAttributesW(
               (util::dirs::app_profiles_dir() + L"\\Merge.yaml").c_str()) !=
           INVALID_FILE_ATTRIBUTES);
  UT_CHECK(GetFileAttributesW(
               (util::dirs::app_profiles_dir() + L"\\Script.js").c_str()) !=
           INVALID_FILE_ATTRIBUTES);

  // 装配后的门面数据可用：verge 语言非空、clash 端口 guard 生效
  auto& cfg = Config::instance();
  UT_CHECK(cfg.verge().latest_arc()->language.has_value());
  UT_CHECK_EQ(cfg.clash().latest_arc()->get_mixed_port(), 10811);
  UT_CHECK_EQ(cfg.clash().latest_arc()->get_client_info().server,
              std::string("127.0.0.1:9108"));
  UT_CHECK(cfg.profiles().latest_arc()->get_item("Merge") != nullptr);
  UT_CHECK(cfg.profiles().latest_arc()->get_item("Script") != nullptr);
}

// ← 安装器语言选择（clashwin.nsi .onInit MUI_LANGDLL_DISPLAY）落注册表
//   HKCU\Software\Clash Win\ClashWin\"Installer Language" = 语言 ID。
//   应用首次生成 verge.yaml 时采用它，避免「安装器选了中文、应用却是日文」。
UT_TEST(t03_installer_language_mapping) {
  using namespace clashwin::config::verge;
  UT_CHECK(language_from_installer_id(2052) == "zh");  // LANG_SIMPCHINESE
  UT_CHECK(language_from_installer_id(1033) == "en");  // LANG_ENGLISH
  UT_CHECK(language_from_installer_id(1049) == "ru");  // LANG_RUSSIAN
  // 其余语言安装器不提供：返回空串 → 调用方回落系统语言（不猜）
  UT_CHECK(language_from_installer_id(0).empty());
  UT_CHECK(language_from_installer_id(1041).empty());  // ja-JP，安装器无此选项
  UT_CHECK(language_from_installer_id(2057).empty());

  // 注册表读取：只做「不抛、返回合法值」的健全性断言（值随本机安装历史变化）
  const std::string got = installer_language();
  UT_CHECK(got.empty() || got == "zh" || got == "en" || got == "ru");
}
