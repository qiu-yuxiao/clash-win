// T25 P1 Finalization & E2E Suite Unit Tests
// 测试覆盖：
//   1. t25_service_mock_reinstall
//   2. t25_service_mock_repair
//   3. t25_service_mock_install_uninstall（含唯一服务名断言）
//   4. t25_service_available_command
//   5. t25_runtime_config_command
//   6. t25_resources_gfwlist_release
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
#include "system/resources.h"
#include "system/service_runner.h"
#include "test_util.h"
#include "ui/help_menu.h"
#include "ui/scenes/settings_drawer.h"
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

  // Case C: Uninstall → 与 Mini 一致：后端能力存在（UI 不暴露，由 t35 防误接用例守住）
  mock->installed = true;
  mock->log.clear();
  auto r3 = cmd::CommandService::instance().invoke("uninstall_service", {});
  UT_CHECK(r3.is_ok());
  std::vector<std::string> exp3 = {"uninstall", "is_installed"};
  UT_CHECK_EQ(mock->log, exp3);
  UT_CHECK(!mock->installed);

  // Case C2: 卸载器失败 → 报错且不假装成功（状态保持已安装）
  mock->installed = true;
  mock->uninstaller_fail = true;
  mock->log.clear();
  auto r3b = cmd::CommandService::instance().invoke("uninstall_service", {});
  UT_CHECK(!r3b.is_ok());
  UT_CHECK(mock->installed);
  mock->uninstaller_fail = false;

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

// 5. t25_runtime_config_command
UT_TEST(t25_runtime_config_command) {
  config::RuntimeState::instance().clear();

  enhance::EnhanceOutput out;
  out.config = YAML::Load("mixed-port: 7890\nmode: rule\nlog-level: info\n");
  out.exists_keys = {"dns", "proxies", "rules"};

  config::RuntimeState::instance().update(out);

  // get_runtime_config
  auto r_cfg = cmd::CommandService::instance().invoke("get_runtime_config", {});
  UT_CHECK(r_cfg.is_ok());
  UT_CHECK_EQ(r_cfg.value()["mixed-port"].get<int>(), 7890);
  UT_CHECK_EQ(r_cfg.value()["mode"].get<std::string>(), "rule");

  config::RuntimeState::instance().clear();
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
  ui::HelpMenu::execute_action(ui::HelpAction::AntivirusGuide);

  UT_CHECK_EQ(dispatched.size(), 7u);
  UT_CHECK_EQ(dispatched[0], ui::HelpAction::StarGithub);
  UT_CHECK_EQ(dispatched[1], ui::HelpAction::OpenWiki);
  UT_CHECK_EQ(dispatched[2], ui::HelpAction::ReviewGuide);
  UT_CHECK_EQ(dispatched[3], ui::HelpAction::ExportDiagnostics);
  UT_CHECK_EQ(dispatched[4], ui::HelpAction::CheckAppUpdate);
  UT_CHECK_EQ(dispatched[5], ui::HelpAction::CheckCoreUpdate);
  UT_CHECK_EQ(dispatched[6], ui::HelpAction::AntivirusGuide);

  // Menu handle verification
  HMENU menu = ui::HelpMenu::create_menu_handle();
  UT_CHECK(menu != nullptr);
  int item_count = GetMenuItemCount(menu);
  UT_CHECK(item_count >= 9);  // 9+ actions/separators
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
    UT_CHECK(j["components"].contains("antivirus") &&
             j["components"]["antivirus"].contains("menuLabel"));
    UT_CHECK(!j["components"]["antivirus"]["menuLabel"].get<std::string>().empty());
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

UT_TEST(t25_v330_basic_card_tun_sniffer_three_state) {
  ui::scene::SettingsDrawer drawer;
  std::vector<std::pair<std::string, nlohmann::json>> patches;
  drawer.on_patch_verge = [&](const std::string& key, const nlohmann::json& v) {
    patches.emplace_back(key, v);
  };
  drawer.wire_callbacks();

  auto* bc = drawer.basic_card();
  UT_CHECK(bc != nullptr && bc->sniffer_seg_ != nullptr);
  UT_CHECK_EQ(bc->sniffer_seg_->option_count(), size_t(3));

  bc->set_tun_enabled(false);
  UT_CHECK(!bc->sniffer_seg_->enabled());
  bc->set_tun_enabled(true);
  UT_CHECK(bc->sniffer_seg_->enabled());

  drawer.sync_basic(false, false, false, false, 7897, std::nullopt);
  UT_CHECK_EQ(bc->sniffer_seg_->selected(), 0);
  drawer.sync_basic(false, false, false, false, 7897, true);
  UT_CHECK_EQ(bc->sniffer_seg_->selected(), 1);
  drawer.sync_basic(false, false, false, false, 7897, false);
  UT_CHECK_EQ(bc->sniffer_seg_->selected(), 2);
  UT_CHECK(patches.empty());

  bc->sniffer_seg_->set_selected(1);
  bc->sniffer_seg_->set_selected(2);
  bc->sniffer_seg_->set_selected(0);

  UT_CHECK_EQ(patches.size(), size_t(3));
  UT_CHECK_EQ(patches[0].first, std::string("enable_tun_sniffer"));
  UT_CHECK(patches[0].second.is_boolean() && patches[0].second.get<bool>() == true);
  UT_CHECK_EQ(patches[1].first, std::string("enable_tun_sniffer"));
  UT_CHECK(patches[1].second.is_boolean() && patches[1].second.get<bool>() == false);
  UT_CHECK_EQ(patches[2].first, std::string("enable_tun_sniffer"));
  UT_CHECK(patches[2].second.is_null());
}
