// T25 P1 Finalization & E2E Suite Unit Tests
// 测试覆盖：
//   1. t25_service_mock_reinstall
//   2. t25_service_mock_repair
//   3. t25_service_mock_install_uninstall（含唯一服务名断言）
//   4. t25_service_available_command
//   5. t25_network_mock_provider
//   6. t25_network_interfaces_info_schema
//   7. t25_runtime_yaml_exists_logs
//   8. t25_icon_cache_name_and_ssrf_validation
//   9. t25_icon_cache_download_dedup
//   10. t25_icon_cache_content_filter
//   11. t25_icon_cache_copy_and_cleanup
//   12. t25_resources_gfwlist_release
//   13. t25_dirs_portable_config_detection
//   14. t25_help_menu_action_dispatch
//   15. t25_i18n_13_locales_help_menu_sync

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "config/runtime_state.h"
#include "core/service_mode.h"
#include "enhance/enhance.h"
#include "system/icon_cache.h"
#include "system/network_info.h"
#include "system/resources.h"
#include "system/service_runner.h"
#include "test_util.h"
#include "ui/help_menu.h"
#include "util/dirs.h"
#include "util/utf.h"

using namespace clashwin;
using json = nlohmann::json;

namespace {

namespace errs = clashwin::cmd::errs;

// 静态装配：使 t25_* 用例可单独过滤运行（不依赖 test_cmd_registry 先执行）
struct T25RegistryFixture {
  T25RegistryFixture() { cmd::CommandService::instance().install_all(); }
};
T25RegistryFixture g_t25_registry_fixture;

// ================= Mock Service Command Runner =================
class MockServiceRunner : public system::IServiceCommandRunner {
 public:
  bool installed = false;
  bool available = true;
  bool binary_valid = true;
  bool image_own = true;
  bool installer_fail = false;
  bool uninstaller_fail = false;
  bool start_fail = false;
  bool stop_fail = false;
  bool wait_ready_fail = false;

  std::vector<std::string> log;

  Result<void> run_installer(bool install) override {
    log.push_back(install ? "install" : "uninstall");
    if (install && installer_fail) {
      return Result<void>::err({errs::kIo, "mock installer failure"});
    }
    if (!install && uninstaller_fail) {
      return Result<void>::err({errs::kIo, "mock uninstaller failure"});
    }
    installed = install;
    return Result<void>::ok();
  }

  bool is_installed() override {
    log.push_back("is_installed");
    return installed;
  }

  bool is_binary_valid() override { return binary_valid; }
  bool is_image_own() override { return image_own; }

  Result<void> start_service() override {
    log.push_back("start");
    if (start_fail) {
      return Result<void>::err({errs::kIo, "mock start failure"});
    }
    return Result<void>::ok();
  }

  Result<void> stop_service() override {
    log.push_back("stop");
    if (stop_fail) {
      return Result<void>::err({errs::kIo, "mock stop failure"});
    }
    return Result<void>::ok();
  }

  bool wait_for_ready(int /*timeout_ms*/) override {
    log.push_back("wait_ready");
    return !wait_ready_fail;
  }

  bool is_available() override {
    log.push_back("is_available");
    return available;
  }
};

// ================= Mock Network Info Provider =================
class MockNetworkProvider : public system::INetworkInfoProvider {
 public:
  std::string hostname = "mock-clashwin-desktop";
  std::vector<std::string> interfaces{"Ethernet0", "Wi-Fi"};
  std::vector<system::NetworkInterfaceInfo> interfaces_info{
      {"Ethernet0", "00:1A:2B:3C:4D:5E", {"192.168.1.100", "fe80::100"}},
      {"Wi-Fi", "AA:BB:CC:DD:EE:FF", {"192.168.1.101"}}};

  std::string get_hostname() override { return hostname; }
  std::vector<std::string> list_interfaces() override { return interfaces; }
  std::vector<system::NetworkInterfaceInfo> list_interfaces_info() override {
    return interfaces_info;
  }
};

// ================= Mock Icon Cache Fetcher =================
class MockIconFetcher : public system::IIconCacheFetcher {
 public:
  int fetch_count = 0;
  std::vector<uint8_t> payload;
  bool fail = false;

  Result<std::vector<uint8_t>> fetch(const std::string& /*url*/) override {
    fetch_count++;
    if (fail) {
      return Result<std::vector<uint8_t>>::err({errs::kIo, "fetch failed"});
    }
    return Result<std::vector<uint8_t>>::ok(payload);
  }
};

// RAII Temp Directory helper
struct ScopedTempDir {
  std::filesystem::path path;
  ScopedTempDir(const std::string& prefix) {
    path = std::filesystem::temp_directory_path() /
           (prefix + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
  }
  ~ScopedTempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

}  // namespace

// 1. t25_service_mock_reinstall
//    用户 2026-11-09 二次裁决（D2 = 选 B）：Mini 可以那么 Win 应该也可以。
//    旧期望（拒绝且 runner 零调用）对应该裁决之前的"接受无自愈路径"方案；
//    现行语义 ← Mini: src-tauri/src/core/service.rs:193-217：卸载 → 安装 → 就绪确认。
UT_TEST(t25_service_mock_reinstall) {
  auto mock = std::make_shared<MockServiceRunner>();
  system::set_service_runner(mock);

  auto res = cmd::CommandService::instance().invoke("reinstall_service", {});
  UT_CHECK(res.is_ok());
  std::vector<std::string> exp = {"uninstall", "install", "is_installed", "is_available"};
  UT_CHECK_EQ(mock->log, exp);
  UT_CHECK(mock->installed);

  system::set_service_runner(nullptr);
}

// 2. t25_service_mock_repair
//    同上裁决；repair 与 reinstall 走同一序列（Mini: force_reinstall_service 复用重装路径）。
UT_TEST(t25_service_mock_repair) {
  auto mock = std::make_shared<MockServiceRunner>();
  system::set_service_runner(mock);

  auto res = cmd::CommandService::instance().invoke("repair_service", {});
  UT_CHECK(res.is_ok());
  std::vector<std::string> exp = {"uninstall", "install", "is_installed", "is_available"};
  UT_CHECK_EQ(mock->log, exp);
  UT_CHECK(mock->installed);

  system::set_service_runner(nullptr);
}

// 2b. t25_service_reinstall_uninstall_step_tolerated
//     ← Mini: src-tauri/src/core/service.rs:196-199：卸载失败只告警、不阻断重装；
//     但安装失败必须报错（:201-207），不得静默成功。
UT_TEST(t25_service_reinstall_uninstall_step_tolerated) {
  auto mock = std::make_shared<MockServiceRunner>();
  system::set_service_runner(mock);

  mock->uninstaller_fail = true;
  auto res = cmd::CommandService::instance().invoke("reinstall_service", {});
  UT_CHECK(res.is_ok());
  std::vector<std::string> exp = {"uninstall", "install", "is_installed", "is_available"};
  UT_CHECK_EQ(mock->log, exp);

  mock->log.clear();
  mock->installer_fail = true;
  auto bad = cmd::CommandService::instance().invoke("reinstall_service", {});
  UT_CHECK(!bad.is_ok());

  system::set_service_runner(nullptr);
}

// 3. t25_service_mock_install_uninstall（共存条件式：缺失可安装同名服务；
//    已存在绝不触发安装；uninstall 禁用；无第二服务名）
//    注：v0.2.2 的 install_service 增加了两项真实 SCM 判定（ImagePath 有效性 / 归属），
//    直接打真实注册表会让本用例随机器状态漂移（有服务能过、没服务必挂）。
//    现通过 runner seam 注入，恢复"已注册且就绪 → 只查询"的原断言意图。
UT_TEST(t25_service_mock_install_uninstall) {
  auto mock = std::make_shared<MockServiceRunner>();
  system::set_service_runner(mock);

  // Case A: 缺失 → 安装成功 → 重新查询 → 启动
  mock->installed = false;
  mock->log.clear();
  auto r1 = cmd::CommandService::instance().invoke("install_service", {});
  UT_CHECK(r1.is_ok());
  std::vector<std::string> exp1 = {"is_installed", "install", "is_installed", "start", "wait_ready"};
  UT_CHECK_EQ(mock->log, exp1);
  UT_CHECK(mock->installed);  // 安装的是同名服务

  // Case A2: 缺失 → 安装失败（UAC 取消/安装器错误）→ 不启动
  mock->installed = false;
  mock->installer_fail = true;
  mock->log.clear();
  auto r1b = cmd::CommandService::instance().invoke("install_service", {});
  UT_CHECK(!r1b.is_ok());
  std::vector<std::string> exp1b = {"is_installed", "install"};
  UT_CHECK_EQ(mock->log, exp1b);
  mock->installer_fail = false;

  // Case B: 已存在且就绪 → 只查询，绝不触发安装
  mock->installed = true;
  mock->available = true;
  mock->log.clear();
  auto r2 = cmd::CommandService::instance().invoke("install_service", {});
  UT_CHECK(r2.is_ok());
  std::vector<std::string> exp2 = {"is_installed", "is_available"};
  UT_CHECK_EQ(mock->log, exp2);

  // Case B2: 已存在但未就绪 → 查询后仅启动，绝不触发安装
  mock->available = false;
  mock->log.clear();
  auto r2b = cmd::CommandService::instance().invoke("install_service", {});
  UT_CHECK(r2b.is_ok());
  std::vector<std::string> exp2b = {"is_installed", "is_available", "start", "wait_ready"};
  UT_CHECK_EQ(mock->log, exp2b);

  // Case C: Uninstall → 共存禁用，runner 零调用
  mock->log.clear();
  auto r3 = cmd::CommandService::instance().invoke("uninstall_service", {});
  UT_CHECK(!r3.is_ok());
  UT_CHECK(mock->log.empty());

  // 共存铁律：唯一服务名 clash_verge_service，无第二名称；
  // 真实 runner 卸载器路径恒拒绝（校验先于任何文件系统/进程操作，测试环境安全）
  UT_CHECK(std::wstring_view(core::service_mode::kServiceNameW) == L"clash_verge_service");
  UT_CHECK(std::string(core::service_mode::kServiceName) == "clash_verge_service");
  system::DefaultServiceCommandRunner real_runner;
  UT_CHECK(real_runner.run_installer(false).is_err());

  system::set_service_runner(nullptr);
}

// 4. t25_service_available_command
UT_TEST(t25_service_available_command) {
  auto mock = std::make_shared<MockServiceRunner>();
  system::set_service_runner(mock);

  mock->available = true;
  auto r1 = cmd::CommandService::instance().invoke("is_service_available", {});
  UT_CHECK(r1.is_ok());
  UT_CHECK_EQ(r1.value().get<bool>(), true);

  mock->available = false;
  auto r2 = cmd::CommandService::instance().invoke("is_service_available", {});
  UT_CHECK(!r2.is_ok());

  // Test stop_service command
  auto r3 = cmd::CommandService::instance().invoke("stop_service", {});
  UT_CHECK(r3.is_ok());
  UT_CHECK(std::find(mock->log.begin(), mock->log.end(), "stop") != mock->log.end());

  system::set_service_runner(nullptr);
}

// 5. t25_network_mock_provider
UT_TEST(t25_network_mock_provider) {
  auto mock = std::make_shared<MockNetworkProvider>();
  system::set_network_info_provider(mock);

  // get_system_hostname
  auto r1 = cmd::CommandService::instance().invoke("get_system_hostname", {});
  UT_CHECK(r1.is_ok());
  UT_CHECK_EQ(r1.value().get<std::string>(), "mock-clashwin-desktop");

  // hostname alias
  auto r2 = cmd::CommandService::instance().invoke("hostname", {});
  UT_CHECK(r2.is_ok());
  UT_CHECK_EQ(r2.value().get<std::string>(), "mock-clashwin-desktop");

  system::set_network_info_provider(nullptr);
}

// 6. t25_network_interfaces_info_schema
UT_TEST(t25_network_interfaces_info_schema) {
  auto mock = std::make_shared<MockNetworkProvider>();
  system::set_network_info_provider(mock);

  // get_network_interfaces
  auto r1 = cmd::CommandService::instance().invoke("get_network_interfaces", {});
  UT_CHECK(r1.is_ok());
  UT_CHECK(r1.value().is_array());
  UT_CHECK_EQ(r1.value().size(), 2u);
  UT_CHECK_EQ(r1.value()[0].get<std::string>(), "Ethernet0");
  UT_CHECK_EQ(r1.value()[1].get<std::string>(), "Wi-Fi");

  // get_network_interfaces_info
  auto r2 = cmd::CommandService::instance().invoke("get_network_interfaces_info", {});
  UT_CHECK(r2.is_ok());
  UT_CHECK(r2.value().is_array());
  UT_CHECK_EQ(r2.value().size(), 2u);

  const auto& item0 = r2.value()[0];
  UT_CHECK_EQ(item0["name"].get<std::string>(), "Ethernet0");
  UT_CHECK_EQ(item0["mac"].get<std::string>(), "00:1A:2B:3C:4D:5E");
  UT_CHECK(item0["addresses"].is_array());
  UT_CHECK_EQ(item0["addresses"].size(), 2u);
  UT_CHECK_EQ(item0["addresses"][0].get<std::string>(), "192.168.1.100");

  system::set_network_info_provider(nullptr);
}

// 7. t25_runtime_yaml_exists_logs
UT_TEST(t25_runtime_yaml_exists_logs) {
  config::RuntimeState::instance().clear();

  enhance::EnhanceOutput out;
  out.config = YAML::Load("mixed-port: 7890\nmode: rule\nlog-level: info\n");
  out.exists_keys = {"dns", "proxies", "rules"};
  out.script_logs = {
      {"Script", {{"info", "initialized rules"}, {"debug", "parsed 10 nodes"}}},
      {"Merge", {{"warn", "port overwritten"}}}};

  config::RuntimeState::instance().update(out);

  // get_runtime_yaml
  auto r_yaml = cmd::CommandService::instance().invoke("get_runtime_yaml", {});
  UT_CHECK(r_yaml.is_ok());
  std::string yaml_str = r_yaml.value().get<std::string>();
  UT_CHECK(yaml_str.find("mixed-port: 7890") != std::string::npos);
  UT_CHECK(yaml_str.find("mode: rule") != std::string::npos);

  // get_runtime_exists
  auto r_exists = cmd::CommandService::instance().invoke("get_runtime_exists", {});
  UT_CHECK(r_exists.is_ok());
  UT_CHECK(r_exists.value().is_array());
  std::vector<std::string> exists = r_exists.value().get<std::vector<std::string>>();
  UT_CHECK_EQ(exists.size(), 3u);
  UT_CHECK_EQ(exists[0], "dns");
  UT_CHECK_EQ(exists[1], "proxies");
  UT_CHECK_EQ(exists[2], "rules");

  // get_runtime_logs
  auto r_logs = cmd::CommandService::instance().invoke("get_runtime_logs", {});
  UT_CHECK(r_logs.is_ok());
  UT_CHECK(r_logs.value().is_object());
  UT_CHECK(r_logs.value().contains("Script"));
  UT_CHECK(r_logs.value().contains("Merge"));
  UT_CHECK_EQ(r_logs.value()["Script"].size(), 2u);
  UT_CHECK_EQ(r_logs.value()["Script"][0][0].get<std::string>(), "info");
  UT_CHECK_EQ(r_logs.value()["Script"][0][1].get<std::string>(), "initialized rules");

  // get_runtime_config
  auto r_cfg = cmd::CommandService::instance().invoke("get_runtime_config", {});
  UT_CHECK(r_cfg.is_ok());
  UT_CHECK_EQ(r_cfg.value()["mixed-port"].get<int>(), 7890);
  UT_CHECK_EQ(r_cfg.value()["mode"].get<std::string>(), "rule");

  config::RuntimeState::instance().clear();
}

// 8. t25_icon_cache_name_and_ssrf_validation
UT_TEST(t25_icon_cache_name_and_ssrf_validation) {
  // Name validation
  UT_CHECK(!system::IconCache::is_valid_icon_name("../evil.png"));
  UT_CHECK(!system::IconCache::is_valid_icon_name("a/b.png"));
  UT_CHECK(!system::IconCache::is_valid_icon_name("a\\b.png"));
  UT_CHECK(!system::IconCache::is_valid_icon_name(""));
  UT_CHECK(!system::IconCache::is_valid_icon_name("   "));
  UT_CHECK(!system::IconCache::is_valid_icon_name("file*name.png"));
  UT_CHECK(!system::IconCache::is_valid_icon_name("file:name.png"));
  UT_CHECK(system::IconCache::is_valid_icon_name("valid_icon.png"));
  UT_CHECK(system::IconCache::is_valid_icon_name("profile-1234.ico"));

  // SSRF validation
  UT_CHECK(!system::IconCache::is_ssrf_safe_url("http://127.0.0.1/icon.png"));
  UT_CHECK(!system::IconCache::is_ssrf_safe_url("http://localhost/icon.png"));
  UT_CHECK(!system::IconCache::is_ssrf_safe_url("http://0.0.0.0:8080/icon.png"));
  UT_CHECK(!system::IconCache::is_ssrf_safe_url("http://192.168.1.1/icon.png"));
  UT_CHECK(!system::IconCache::is_ssrf_safe_url("http://10.0.0.1/icon.png"));
  UT_CHECK(!system::IconCache::is_ssrf_safe_url("http://172.16.0.1/icon.png"));
  UT_CHECK(!system::IconCache::is_ssrf_safe_url("ftp://example.com/icon.png"));
  UT_CHECK(!system::IconCache::is_ssrf_safe_url("file:///c:/icon.png"));
  UT_CHECK(system::IconCache::is_ssrf_safe_url("https://example.com/icon.png"));
  UT_CHECK(system::IconCache::is_ssrf_safe_url("http://93.184.216.34/icon.png"));
}

// 9. t25_icon_cache_download_dedup
UT_TEST(t25_icon_cache_download_dedup) {
  ScopedTempDir temp("t25_icon_dedup");
  // 必须用 _wputenv_s：CRT 的 _dupenv_s 不读取 SetEnvironmentVariableW 的改动
  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", temp.path.wstring().c_str());

  auto fetcher = std::make_shared<MockIconFetcher>();
  // Valid PNG magic bytes + dummy data
  fetcher->payload = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x01, 0x02};
  system::IconCache::instance().set_fetcher(fetcher);

  // Call 1: Download icon
  auto r1 = system::IconCache::instance().download_icon_cache("https://example.com/icon.png", "dedup_test.png");
  UT_CHECK(r1.is_ok());
  if (!r1.is_ok()) std::printf("  icon download error: %s\n", r1.error().message.c_str());
  UT_CHECK_EQ(fetcher->fetch_count, 1);
  UT_CHECK(std::filesystem::exists(r1.value()));

  // Call 2: Same URL and name -> Deduplication, returns existing file without invoking fetcher!
  auto r2 = system::IconCache::instance().download_icon_cache("https://example.com/icon.png", "dedup_test.png");
  UT_CHECK(r2.is_ok());
  UT_CHECK_EQ(fetcher->fetch_count, 1);  // fetch_count remains 1!
  UT_CHECK_EQ(r1.value(), r2.value());

  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
  system::IconCache::instance().set_fetcher(std::make_shared<system::DefaultIconCacheFetcher>());
}

// 10. t25_icon_cache_content_filter
UT_TEST(t25_icon_cache_content_filter) {
  // HTML Error payload rejected
  std::string html = "<!DOCTYPE html><html><head><title>404 Not Found</title></head><body>error</body></html>";
  std::vector<uint8_t> html_bytes(html.begin(), html.end());
  UT_CHECK(!system::IconCache::is_supported_icon_content(html_bytes));

  // Plain HTML fragment rejected
  std::string html2 = "<html lang=\"en\"><body>Not Found</body></html>";
  std::vector<uint8_t> html2_bytes(html2.begin(), html2.end());
  UT_CHECK(!system::IconCache::is_supported_icon_content(html2_bytes));

  // SVG accepted
  std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"32\" height=\"32\"></svg>";
  std::vector<uint8_t> svg_bytes(svg.begin(), svg.end());
  UT_CHECK(system::IconCache::is_supported_icon_content(svg_bytes));

  // XML + SVG accepted
  std::string xml_svg = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><svg width=\"16\" height=\"16\"></svg>";
  std::vector<uint8_t> xml_svg_bytes(xml_svg.begin(), xml_svg.end());
  UT_CHECK(system::IconCache::is_supported_icon_content(xml_svg_bytes));

  // PNG accepted
  std::vector<uint8_t> png_bytes = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x01};
  UT_CHECK(system::IconCache::is_supported_icon_content(png_bytes));

  // ICO accepted
  std::vector<uint8_t> ico_bytes = {0x00, 0x00, 0x01, 0x00, 0x01, 0x00};
  UT_CHECK(system::IconCache::is_supported_icon_content(ico_bytes));

  // Arbitrary text rejected
  std::string plain = "Random non-image plain text string";
  std::vector<uint8_t> plain_bytes(plain.begin(), plain.end());
  UT_CHECK(!system::IconCache::is_supported_icon_content(plain_bytes));
}

// 11. t25_icon_cache_copy_and_cleanup
UT_TEST(t25_icon_cache_copy_and_cleanup) {
  ScopedTempDir temp("t25_icon_copy_cleanup");
  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", temp.path.wstring().c_str());

  // Setup sample source file
  auto src_file = temp.path / "sample.ico";
  {
    std::ofstream ofs(src_file, std::ios::binary);
    ofs << "dummy-ico-data";
  }

  // Pre-create previous version
  auto icons_dir = temp.path / "icons";
  std::filesystem::create_directories(icons_dir);
  auto prev_png = icons_dir / "profileA-1001.png";
  auto prev_ico = icons_dir / "profileA-1001.ico";
  {
    std::ofstream ofs1(prev_png); ofs1 << "prev1";
    std::ofstream ofs2(prev_ico); ofs2 << "prev2";
  }
  UT_CHECK(std::filesystem::exists(prev_png));
  UT_CHECK(std::filesystem::exists(prev_ico));

  // Copy icon with version rotation
  system::IconInfo info{"profileA", "1002", "1001"};
  auto res = system::IconCache::instance().copy_icon_file(utf::utf16_to_utf8(src_file.wstring()), info);
  UT_CHECK(res.is_ok());
  if (!res.is_ok()) std::printf("  icon copy error: %s\n", res.error().message.c_str());

  auto dest_file = icons_dir / "profileA-1002.ico";
  UT_CHECK(std::filesystem::exists(dest_file));
  UT_CHECK(!std::filesystem::exists(prev_png));  // Old versions purged!
  UT_CHECK(!std::filesystem::exists(prev_ico));

  // Cleanup test: Create 5 cache files and prune down to max 3
  auto cache_dir = icons_dir / "cache";
  std::filesystem::create_directories(cache_dir);
  for (int i = 1; i <= 5; ++i) {
    auto f = cache_dir / ("cache_" + std::to_string(i) + ".dat");
    std::ofstream ofs(f);
    ofs << "cache content " << i;
    // Stagger timestamps
    std::filesystem::last_write_time(f, std::filesystem::file_time_type(std::chrono::seconds(1000 * i)));
  }

  auto clean_res = system::IconCache::instance().cleanup_icon_cache(3);
  UT_CHECK(clean_res.is_ok());

  // Count remaining files
  int count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(cache_dir)) {
    if (entry.is_regular_file()) count++;
  }
  UT_CHECK_EQ(count, 3);
  // Oldest files (1 and 2) must be deleted, 3, 4, 5 remain
  UT_CHECK(!std::filesystem::exists(cache_dir / "cache_1.dat"));
  UT_CHECK(!std::filesystem::exists(cache_dir / "cache_2.dat"));
  UT_CHECK(std::filesystem::exists(cache_dir / "cache_5.dat"));

  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
}

// 12. t25_resources_gfwlist_release
UT_TEST(t25_resources_gfwlist_release) {
  ScopedTempDir temp_home("t25_res_home");
  ScopedTempDir temp_res("t25_res_pkg");

  // Create fake bundled resources
  auto src_mmdb = temp_res.path / "Country.mmdb";
  auto src_geosite = temp_res.path / "geosite.dat";
  auto src_gfwlist = temp_res.path / "gfwlist.yaml";

  {
    std::ofstream(src_mmdb) << "fake-mmdb-content";
    std::ofstream(src_geosite) << "fake-geosite-content";
    std::ofstream(src_gfwlist) << "payload:\n  - DOMAIN,google.com\n";
  }

  // First release: Cold start 0ms routing
  auto r1 = system::init_resources(temp_home.path.wstring(), temp_res.path.wstring());
  UT_CHECK(r1.is_ok());

  auto dst_mmdb = temp_home.path / "Country.mmdb";
  auto dst_geosite = temp_home.path / "geosite.dat";
  auto dst_gfwlist = temp_home.path / "ruleset" / "gfwlist.yaml";

  UT_CHECK(std::filesystem::exists(dst_mmdb));
  UT_CHECK(std::filesystem::exists(dst_geosite));
  UT_CHECK(std::filesystem::exists(dst_gfwlist));

  // Modify destination gfwlist and set newer timestamp (mimicking Mihomo runtime update)
  {
    std::ofstream ofs(dst_gfwlist);
    ofs << "payload:\n  - DOMAIN,google.com\n  - DOMAIN,youtube.com\n";
  }
  auto future_time = std::filesystem::last_write_time(src_gfwlist) + std::chrono::hours(24);
  std::filesystem::last_write_time(dst_gfwlist, future_time);

  // Second release: Should preserve newer destination file!
  auto r2 = system::init_resources(temp_home.path.wstring(), temp_res.path.wstring());
  UT_CHECK(r2.is_ok());

  std::ifstream check_stream(dst_gfwlist);
  std::string content((std::istreambuf_iterator<char>(check_stream)), std::istreambuf_iterator<char>());
  UT_CHECK(content.find("youtube.com") != std::string::npos);  // Kept newer version!
}

// 13. t25_dirs_portable_config_detection
UT_TEST(t25_dirs_portable_config_detection) {
  std::wstring exe = util::dirs::exe_dir();
  std::wstring expected = exe + L"\\.config\\io.github.clash-win.clash-win";
  UT_CHECK_EQ(util::dirs::portable_home_for(exe), expected);

  // 环境变量必须走 CRT（_wputenv_s）；SetEnvironmentVariableW 不会更新 _dupenv_s 视图
  _wputenv_s(L"CLASHWIN_PORTABLE_OVERRIDE", L"1");
  UT_CHECK(util::dirs::is_portable());

  _wputenv_s(L"CLASHWIN_PORTABLE_OVERRIDE", L"0");
  UT_CHECK(!util::dirs::is_portable());

  _wputenv_s(L"CLASHWIN_PORTABLE_OVERRIDE", L"");
  util::dirs::reset_portable_cache_for_testing();
}

// 14. t25_help_menu_action_dispatch
UT_TEST(t25_help_menu_action_dispatch) {
  std::vector<ui::HelpAction> dispatched;
  ui::HelpMenu::set_action_handler_for_testing([&](ui::HelpAction act, HWND /*hwnd*/) {
    dispatched.push_back(act);
  });

  ui::HelpMenu::execute_action(ui::HelpAction::StarGithub);
  ui::HelpMenu::execute_action(ui::HelpAction::OpenWiki);
  ui::HelpMenu::execute_action(ui::HelpAction::ReviewGuide);
  ui::HelpMenu::execute_action(ui::HelpAction::ExportDiagnostics);
  ui::HelpMenu::execute_action(ui::HelpAction::CheckAppUpdate);
  ui::HelpMenu::execute_action(ui::HelpAction::CheckCoreUpdate);
  ui::HelpMenu::execute_action(ui::HelpAction::OpenConfigDir);
  ui::HelpMenu::execute_action(ui::HelpAction::OpenLogsDir);

  UT_CHECK_EQ(dispatched.size(), 8u);
  UT_CHECK_EQ(dispatched[0], ui::HelpAction::StarGithub);
  UT_CHECK_EQ(dispatched[1], ui::HelpAction::OpenWiki);
  UT_CHECK_EQ(dispatched[2], ui::HelpAction::ReviewGuide);
  UT_CHECK_EQ(dispatched[3], ui::HelpAction::ExportDiagnostics);
  UT_CHECK_EQ(dispatched[4], ui::HelpAction::CheckAppUpdate);
  UT_CHECK_EQ(dispatched[5], ui::HelpAction::CheckCoreUpdate);
  UT_CHECK_EQ(dispatched[6], ui::HelpAction::OpenConfigDir);
  UT_CHECK_EQ(dispatched[7], ui::HelpAction::OpenLogsDir);

  // Menu handle verification
  HMENU menu = ui::HelpMenu::create_menu_handle();
  UT_CHECK(menu != nullptr);
  int item_count = GetMenuItemCount(menu);
  UT_CHECK(item_count >= 8);  // 8 actions + separators
  DestroyMenu(menu);

  ui::HelpMenu::set_action_handler_for_testing(nullptr);
}

// 15. t25_i18n_13_locales_help_menu_sync
UT_TEST(t25_i18n_13_locales_help_menu_sync) {
  const std::vector<std::string> locales = {
      "ar", "de", "en", "es", "fa", "id", "jp", "ko", "ru", "tr", "tt", "zh", "zhtw"};

  const std::vector<std::string> required_keys = {
      "github", "wiki", "firstRunGuide", "exportDiagnostics",
      "generatingDiagnostics", "diagnosticsExported",
      "checkAppUpdate", "checkCoreUpdate", "buttonAriaLabel"};

  std::filesystem::path locales_base("assets/locales");
  if (!std::filesystem::exists(locales_base)) {
    // If running from build output dir, look up parent dirs
    locales_base = "../../../assets/locales";
    if (!std::filesystem::exists(locales_base)) {
      locales_base = "../../assets/locales";
    }
  }

  for (const auto& loc : locales) {
    auto layout_file = locales_base / loc / "layout.json";
    UT_CHECK(std::filesystem::exists(layout_file));
    if (!std::filesystem::exists(layout_file)) continue;

    std::ifstream f(layout_file);
    UT_CHECK(f.is_open());
    json j;
    f >> j;

    UT_CHECK(j.contains("components"));
    UT_CHECK(j["components"].contains("helpMenu"));

    const auto& hm = j["components"]["helpMenu"];
    for (const auto& key : required_keys) {
      UT_CHECK_MSG(hm.contains(key), ("Locale " + loc + " missing helpMenu key: " + key).c_str());
      if (hm.contains(key)) {
        UT_CHECK(!hm[key].get<std::string>().empty());
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 服务状态五态（可注入探测；不依赖 SCM/真实管道）
// ---------------------------------------------------------------------------
namespace {
class MockServiceProbe : public core::service_mode::IServiceProbe {
 public:
  bool inst = true;  // installed
  bool bin = true;   // ImagePath 文件存在
  bool run = true;   // SCM STATE == RUNNING
  bool pipe = true;  // 管道存在 + 握手成功
  bool installed() override { return inst; }
  bool binary_valid() override { return bin; }
  bool running() override { return run; }
  bool pipe_ready() override { return pipe; }
};
}  // namespace

// ① 未安装 → Missing（应走 UAC 安装）
UT_TEST(t25_service_state_missing) {
  MockServiceProbe probe;
  probe.inst = false;
  const auto st = core::service_mode::evaluate_state(probe);
  UT_CHECK(st == core::service_mode::ServiceState::Missing);
  UT_CHECK(core::service_mode::state_requires_install(st));
  UT_CHECK(!core::service_mode::state_requires_start(st));
}

// ② 已安装但 ImagePath 文件不存在（Mini 卸载残壳）→ BinaryInvalid（应重装）
UT_TEST(t25_service_state_binary_invalid) {
  MockServiceProbe probe;
  probe.bin = false;
  const auto st = core::service_mode::evaluate_state(probe);
  UT_CHECK(st == core::service_mode::ServiceState::BinaryInvalid);
  UT_CHECK(core::service_mode::state_requires_install(st));
}

// ③ STOPPED → Stopped（可启动；调用方应执行 start_service）
UT_TEST(t25_service_state_stopped_startable) {
  MockServiceProbe probe;
  probe.run = false;
  probe.pipe = false;
  const auto st = core::service_mode::evaluate_state(probe);
  UT_CHECK(st == core::service_mode::ServiceState::Stopped);
  UT_CHECK(core::service_mode::state_requires_start(st));
  UT_CHECK(!core::service_mode::state_requires_install(st));
}

// ④ RUNNING + 管道就绪 → Ready
UT_TEST(t25_service_state_ready) {
  MockServiceProbe probe;
  const auto st = core::service_mode::evaluate_state(probe);
  UT_CHECK(st == core::service_mode::ServiceState::Ready);
}

// ⑤ RUNNING 但管道未就绪 → NotReady（可等待/启动，不得判 Ready）
UT_TEST(t25_service_state_running_but_pipe_not_ready) {
  MockServiceProbe probe;
  probe.pipe = false;
  const auto st = core::service_mode::evaluate_state(probe);
  UT_CHECK(st == core::service_mode::ServiceState::NotReady);
  UT_CHECK(core::service_mode::state_requires_start(st));
}

// 回归：管道在但服务 STOPPED → 必须判 Stopped，绝不能判 Ready（原缺陷）
UT_TEST(t25_service_state_pipe_present_but_stopped_not_ready) {
  MockServiceProbe probe;
  probe.run = false;   // SCM STOPPED
  probe.pipe = true;   // 残留管道仍在
  const auto st = core::service_mode::evaluate_state(probe);
  UT_CHECK(st == core::service_mode::ServiceState::Stopped);
  UT_CHECK(st != core::service_mode::ServiceState::Ready);
  UT_CHECK(core::service_mode::state_requires_start(st));
}
