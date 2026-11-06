// T24 单元与集成测试：内核热升级 + 客户端自更新 + 诊断包导出与脱敏
#include "test_util.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "cmd/command_service.h"
#include "core/core_manager.h"
#include "core/core_updater.h"
#include "events/event_bus.h"
#include "mihomo/mihomo_client.h"
#include "minisign_vector.h"
#include "util/ed25519.h"
#include "system/diagnostic_bundle.h"
#include "update/client_updater.h"
#include "util/dirs.h"
#include "util/str_utils.h"
#include "util/utf.h"

namespace {

using namespace clashwin;
using namespace clashwin::core;
using namespace clashwin::mihomo;
using namespace clashwin::update;
using namespace clashwin::system;

struct TempHomeGuard {
  std::filesystem::path dir;
  TempHomeGuard() {
    auto base = std::filesystem::temp_directory_path();
    dir = base / ("clashwin_t24_" + std::to_string(GetTickCount64()) + "_" +
                  std::to_string(rand()));
    std::filesystem::create_directories(dir);
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", dir.wstring().c_str());
  }
  ~TempHomeGuard() {
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
};

struct MockApiClient : ICoreApiClient {
  std::string active_node = "HongKong01";
  std::string last_selected;
  int select_calls = 0;

  Result<BaseConfig> get_base_config() override {
    return Result<BaseConfig>::ok(BaseConfig{});
  }
  Result<ProxyItem> get_group_by_name(const std::string& name) override {
    ProxyItem item;
    item.name = name;
    item.now = active_node;
    item.all = {"HongKong01", "Tokyo01", "Singapore01", "Japan-Tokyo-01"};
    return Result<ProxyItem>::ok(item);
  }
  Result<void> select_node_for_group(const std::string&, const std::string& node) override {
    ++select_calls;
    last_selected = node;
    active_node = node;
    return Result<void>::ok();
  }
  Result<void> reload_config(bool, const std::string&) override {
    return Result<void>::ok();
  }
};

}  // namespace

// Test 1: Core Updater Target Asset Matching
UT_TEST(t24_core_updater_match_target_asset) {
  GithubRelease release;
  release.tag_name = "v1.19.28";
  release.assets = {
      {"mihomo-linux-amd64-v1.19.28.tar.gz", "https://dl/linux.tar.gz"},
      {"mihomo-darwin-arm64-v1.19.28.tar.gz", "https://dl/darwin.tar.gz"},
      {"mihomo-windows-amd64-v1.19.28.zip", "https://dl/windows.zip"},
      {"mihomo-windows-arm64-v1.19.28.zip", "https://dl/arm64.zip"},
  };

  auto opt = CoreUpdater::match_target_asset(release);
  UT_CHECK(opt.has_value());
  UT_CHECK_EQ(opt->name, "mihomo-windows-amd64-v1.19.28.zip");
  UT_CHECK_EQ(opt->browser_download_url, "https://dl/windows.zip");

  // Fallback to exact .gz
  GithubRelease release_gz;
  release_gz.tag_name = "v1.19.28";
  release_gz.assets = {
      {"mihomo-linux-amd64-v1.19.28.tar.gz", "https://dl/linux.tar.gz"},
      {"mihomo-windows-amd64.exe.gz", "https://dl/windows.gz"},
  };
  auto opt_gz = CoreUpdater::match_target_asset(release_gz);
  UT_CHECK(opt_gz.has_value());
  UT_CHECK_EQ(opt_gz->name, "mihomo-windows-amd64.exe.gz");

  // Non-matching release
  GithubRelease release_none;
  release_none.tag_name = "v1.19.28";
  release_none.assets = {
      {"mihomo-linux-amd64-v1.19.28.tar.gz", "https://dl/linux.tar.gz"},
      {"mihomo-darwin-amd64-v1.19.28.tar.gz", "https://dl/darwin.tar.gz"},
  };
  auto opt_none = CoreUpdater::match_target_asset(release_none);
  UT_CHECK(!opt_none.has_value());
}

// Test 2: Core Updater Checksum Parsing
UT_TEST(t24_core_updater_parse_checksum) {
  const std::string text =
      "# Mihomo Release Checksums\n"
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  mihomo-linux-amd64.tar.gz\n"
      "f2ca1bb6c7e907d06dafe4687e579fce76b37e4e93b7605022da52e6ccc26fd2 *mihomo-windows-amd64-v1.0.0.zip\n"
      "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef  dist/mihomo-windows-amd64.exe.gz\n";

  auto cs1 = CoreUpdater::parse_checksum_for_asset(text, "mihomo-windows-amd64-v1.0.0.zip");
  UT_CHECK(cs1.has_value());
  UT_CHECK_EQ(*cs1, "f2ca1bb6c7e907d06dafe4687e579fce76b37e4e93b7605022da52e6ccc26fd2");

  // Case-insensitivity check
  auto cs2 = CoreUpdater::parse_checksum_for_asset(text, "MIHOMO-WINDOWS-AMD64-V1.0.0.ZIP");
  UT_CHECK(cs2.has_value());
  UT_CHECK_EQ(*cs2, "f2ca1bb6c7e907d06dafe4687e579fce76b37e4e93b7605022da52e6ccc26fd2");

  // Directory path stripping
  auto cs3 = CoreUpdater::parse_checksum_for_asset(text, "mihomo-windows-amd64.exe.gz");
  UT_CHECK(cs3.has_value());
  UT_CHECK_EQ(*cs3, "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");

  // Non-existent asset
  auto cs4 = CoreUpdater::parse_checksum_for_asset(text, "unknown-file.zip");
  UT_CHECK(!cs4.has_value());

  // Check find_checksum_url
  GithubRelease release;
  release.assets = {
      {"mihomo.zip", "https://dl/zip"},
      {"sha256sums.txt", "https://dl/sha256sums.txt"},
  };
  auto cs_url = CoreUpdater::find_checksum_url(release);
  UT_CHECK(cs_url.has_value());
  UT_CHECK_EQ(*cs_url, "https://dl/sha256sums.txt");
}

// Test 3: Core Updater SHA-256 Computation
UT_TEST(t24_core_updater_sha256_computation) {
  TempHomeGuard th;
  std::filesystem::path test_file = th.dir / "hello.txt";

  // Known SHA256 of "Hello, World!" is dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f
  std::ofstream out(test_file, std::ios::binary);
  out.write("Hello, World!", 13);
  out.close();

  auto sha_res = CoreUpdater::compute_file_sha256(test_file.wstring());
  UT_CHECK(sha_res.is_ok());
  UT_CHECK_EQ(sha_res.value(), "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f");

  // Empty file SHA256: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
  std::filesystem::path empty_file = th.dir / "empty.txt";
  std::ofstream out_empty(empty_file, std::ios::binary);
  out_empty.close();

  auto empty_sha_res = CoreUpdater::compute_file_sha256(empty_file.wstring());
  UT_CHECK(empty_sha_res.is_ok());
  UT_CHECK_EQ(empty_sha_res.value(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

// Test 4: Core Updater Progress Event Stream
UT_TEST(t24_core_updater_progress_event_stream) {
  TempHomeGuard th;

  // Prepare a valid STORE PKZIP with mini-mihomo.exe
  std::filesystem::path zip_path = th.dir / "mock_archive.zip";
  std::vector<DiagnosticBundle::ZipEntry> zip_entries = {
      {"mini-mihomo.exe", "MZ_MOCK_MIHOMO_EXECUTABLE_BINARY"}};
  auto wz = DiagnosticBundle::write_zip_file(zip_path.wstring(), zip_entries);
  UT_CHECK(wz.is_ok());

  auto archive_sha = CoreUpdater::compute_file_sha256(zip_path.wstring());
  UT_CHECK(archive_sha.is_ok());

  // Mock downloader
  struct MockDownloader : ICoreUpdateDownloader {
    std::wstring source_zip;
    std::string sha;
    Result<std::string> fetch_latest_release_json() override {
      return Result<std::string>::ok(
          R"({"tag_name":"v1.0.0","assets":[{"name":"mihomo-windows-amd64-v1.0.0.zip","browser_download_url":"https://mock/dl.zip"},{"name":"checksums.txt","browser_download_url":"https://mock/cs.txt"}]})");
    }
    Result<std::string> fetch_checksum_content(const std::string&) override {
      return Result<std::string>::ok(sha + "  mihomo-windows-amd64-v1.0.0.zip\n");
    }
    Result<void> download_asset(
        const std::string&, const std::wstring& dest_path,
        std::function<void(uint64_t downloaded, uint64_t total)> on_progress) override {
      std::error_code ec;
      std::filesystem::copy_file(source_zip, dest_path,
                                 std::filesystem::copy_options::overwrite_existing, ec);
      if (on_progress) {
        on_progress(50, 100);
        on_progress(100, 100);
      }
      return Result<void>::ok();
    }
  };

  auto dl = std::make_shared<MockDownloader>();
  dl->source_zip = zip_path.wstring();
  dl->sha = archive_sha.value();
  CoreUpdater::instance().set_downloader(dl);

  bool stopped = false, started = false;
  CoreOps ops;
  ops.stop = [&]() -> Result<void> {
    stopped = true;
    return Result<void>::ok();
  };
  ops.start = [&]() -> Result<void> {
    started = true;
    return Result<void>::ok();
  };
  CoreUpdater::instance().set_lifecycle_ops(ops);

  std::vector<CoreUpgradeProgress> captured_events;
  uint64_t sub_id = events::EventBus::instance().subscribe(
      "core-upgrade-progress", [&](const nlohmann::json& payload) {
        CoreUpgradeProgress p;
        p.status = payload.value("status", "");
        p.progress = payload.value("progress", 0u);
        p.message = payload.value("message", "");
        captured_events.push_back(p);
      });

  GithubRelease rel;
  rel.tag_name = "v1.0.0";
  rel.assets = {
      {"mihomo-windows-amd64-v1.0.0.zip", "https://mock/dl.zip"},
      {"checksums.txt", "https://mock/cs.txt"},
  };

  auto upgrade_res = CoreUpdater::instance().start_core_upgrade(rel);
  UT_CHECK(upgrade_res.is_ok());
  UT_CHECK(stopped);
  UT_CHECK(started);

  events::EventBus::instance().unsubscribe(sub_id);

  // Validate ordered progression of statuses
  std::vector<std::string> statuses;
  for (const auto& ev : captured_events) {
    if (statuses.empty() || statuses.back() != ev.status) {
      statuses.push_back(ev.status);
    }
  }

  UT_CHECK(std::find(statuses.begin(), statuses.end(), "checking") != statuses.end());
  UT_CHECK(std::find(statuses.begin(), statuses.end(), "downloading") != statuses.end());
  UT_CHECK(std::find(statuses.begin(), statuses.end(), "verifying") != statuses.end());
  UT_CHECK(std::find(statuses.begin(), statuses.end(), "extracting") != statuses.end());
  UT_CHECK(std::find(statuses.begin(), statuses.end(), "done") != statuses.end());

  // 进度断言：Mini 真实事件流并非全局单调（core_updater.rs:269 下载末事件
  // pct=10+floor(1.0*80)=90，随后 verifying 固定 85）→ 按「同状态内单调」
  // 校验，并显式校验 checking→downloading→verifying→extracting→done 顺序。
  {
    uint32_t prev_in_status = 0;
    std::string prev_status;
    for (const auto& ev : captured_events) {
      if (ev.status == prev_status) {
        UT_CHECK_MSG(ev.progress >= prev_in_status, "progress must be monotonic within status");
      }
      prev_in_status = ev.progress;
      prev_status = ev.status;
    }
    const std::vector<std::string> expect_order = {"checking", "downloading",
                                                   "verifying", "extracting", "done"};
    UT_CHECK(statuses == expect_order);
  }
}

// Test 5: Core Updater SHA-256 Mismatch Rejection
UT_TEST(t24_core_updater_sha256_mismatch_rejection) {
  TempHomeGuard th;

  std::filesystem::path zip_path = th.dir / "corrupted_archive.zip";
  std::vector<DiagnosticBundle::ZipEntry> zip_entries = {
      {"mini-mihomo.exe", "CORRUPTED_PAYLOAD"}};
  DiagnosticBundle::write_zip_file(zip_path.wstring(), zip_entries);

  struct MismatchDownloader : ICoreUpdateDownloader {
    std::wstring source_zip;
    Result<std::string> fetch_latest_release_json() override {
      return Result<std::string>::err(Error{1, "unused"});
    }
    Result<std::string> fetch_checksum_content(const std::string&) override {
      // Wrong SHA256 declared
      return Result<std::string>::ok(
          "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff  mihomo-windows-amd64-v1.0.0.zip\n");
    }
    Result<void> download_asset(
        const std::string&, const std::wstring& dest_path,
        std::function<void(uint64_t, uint64_t)>) override {
      std::error_code ec;
      std::filesystem::copy_file(source_zip, dest_path,
                                 std::filesystem::copy_options::overwrite_existing, ec);
      return Result<void>::ok();
    }
  };

  auto dl = std::make_shared<MismatchDownloader>();
  dl->source_zip = zip_path.wstring();
  CoreUpdater::instance().set_downloader(dl);

  std::vector<CoreUpgradeProgress> captured_events;
  uint64_t sub_id = events::EventBus::instance().subscribe(
      "core-upgrade-progress", [&](const nlohmann::json& payload) {
        CoreUpgradeProgress p;
        p.status = payload.value("status", "");
        p.progress = payload.value("progress", 0u);
        p.message = payload.value("message", "");
        captured_events.push_back(p);
      });

  GithubRelease rel;
  rel.tag_name = "v1.0.0";
  rel.assets = {
      {"mihomo-windows-amd64-v1.0.0.zip", "https://mock/dl.zip"},
      {"checksums.txt", "https://mock/cs.txt"},
  };

  auto res = CoreUpdater::instance().start_core_upgrade(rel);
  UT_CHECK(!res.is_ok());

  events::EventBus::instance().unsubscribe(sub_id);

  // Temporary download file must be cleaned up
  std::wstring temp_path = util::dirs::cores_dir() + L"\\mini-mihomo.download.tmp";
  UT_CHECK(!std::filesystem::exists(temp_path));

  // Last event emitted should be error(0)
  UT_CHECK(!captured_events.empty());
  UT_CHECK_EQ(captured_events.back().status, "error");
  UT_CHECK_EQ(captured_events.back().progress, 0u);
}

// Test 6: Core Updater Proxy Group Snapshot and Restore
UT_TEST(t24_core_updater_proxy_group_snapshot_restore) {
  TempHomeGuard th;

  std::filesystem::path zip_path = th.dir / "valid.zip";
  std::vector<DiagnosticBundle::ZipEntry> zip_entries = {
      {"mini-mihomo.exe", "MIHOMO_EXECUTABLE"}};
  DiagnosticBundle::write_zip_file(zip_path.wstring(), zip_entries);

  struct FastDownloader : ICoreUpdateDownloader {
    std::wstring source_zip;
    Result<std::string> fetch_latest_release_json() override {
      return Result<std::string>::err(Error{1, "unused"});
    }
    Result<std::string> fetch_checksum_content(const std::string&) override {
      return Result<std::string>::err(Error{1, "no checksum"});
    }
    Result<void> download_asset(
        const std::string&, const std::wstring& dest_path,
        std::function<void(uint64_t, uint64_t)>) override {
      std::error_code ec;
      std::filesystem::copy_file(source_zip, dest_path,
                                 std::filesystem::copy_options::overwrite_existing, ec);
      return Result<void>::ok();
    }
  };

  auto dl = std::make_shared<FastDownloader>();
  dl->source_zip = zip_path.wstring();
  CoreUpdater::instance().set_downloader(dl);

  MockApiClient mock_api;
  mock_api.active_node = "Japan-Tokyo-01";
  CoreUpdater::instance().set_core_api_client(&mock_api);

  CoreOps ops;
  ops.stop = []() -> Result<void> { return Result<void>::ok(); };
  ops.start = []() -> Result<void> { return Result<void>::ok(); };
  CoreUpdater::instance().set_lifecycle_ops(ops);

  GithubRelease rel;
  rel.tag_name = "v1.0.0";
  rel.assets = {{"mihomo-windows-amd64-v1.0.0.zip", "https://mock/dl.zip"}};

  auto res = CoreUpdater::instance().start_core_upgrade(rel);
  UT_CHECK(res.is_ok());
  UT_CHECK(mock_api.select_calls > 0);
  UT_CHECK_EQ(mock_api.last_selected, "Japan-Tokyo-01");
}

// Test 7: Client Updater Version Comparison
UT_TEST(t24_client_updater_version_comparison) {
  UT_CHECK(ClientUpdater::is_version_newer("v3.2.2", "v3.2.1"));
  UT_CHECK(!ClientUpdater::is_version_newer("3.2.1", "3.2.1"));
  UT_CHECK(!ClientUpdater::is_version_newer("v3.2.0", "v3.2.1"));
  UT_CHECK(ClientUpdater::is_version_newer("3.3.0", "3.2.9"));
  UT_CHECK(ClientUpdater::is_version_newer("v4.0.0", "v3.10.5"));
  UT_CHECK(!ClientUpdater::is_version_newer("v3.2.1-beta.1", "v3.2.1"));
  UT_CHECK(ClientUpdater::is_version_newer("3.2.2-rc.1", "3.2.1"));
}

// Test 8: Client Updater Manifest Parsing
UT_TEST(t24_client_updater_manifest_parsing) {
  const std::string manifest_json = R"({
    "version": "v3.2.3",
    "notes": "Feature additions and stability improvements",
    "pub_date": "2026-10-25T12:00:00Z",
    "platforms": {
      "windows-x86_64": {
        "signature": "mock_sig_base64_string",
        "url": "https://github.com/qiu-yuxiao/clash-mini/releases/download/v3.2.3/Clash.Mini_3.2.3_x64-setup.exe"
      }
    }
  })";

  auto res = ClientUpdater::parse_manifest(manifest_json, "v3.2.2");
  UT_CHECK(res.is_ok());
  auto info = res.value();
  UT_CHECK(info.has_update);
  UT_CHECK_EQ(info.latest_version, "v3.2.3");
  UT_CHECK_EQ(info.notes, "Feature additions and stability improvements");
  UT_CHECK_EQ(info.pub_date, "2026-10-25T12:00:00Z");
  UT_CHECK_EQ(info.download_url,
              "https://github.com/qiu-yuxiao/clash-mini/releases/download/v3.2.3/Clash.Mini_3.2.3_x64-setup.exe");
  UT_CHECK_EQ(info.signature, "mock_sig_base64_string");

  // When current version is equal or newer
  auto res_no_update = ClientUpdater::parse_manifest(manifest_json, "v3.2.3");
  UT_CHECK(res_no_update.is_ok());
  UT_CHECK(!res_no_update.value().has_update);
}

// Test 8b: Client Updater Download Progress Events
UT_TEST(t24_client_updater_download_progress_events) {
  // ← use-client-update.ts:73-105 Started/Progress/Finished 三段事件
  struct MockAppDownloader : IAppUpdateDownloader {
    Result<std::string> fetch_manifest(const std::string&) override {
      return Result<std::string>::err(Error{1, "unused"});
    }
    Result<void> download_installer(
        const std::string&, const std::wstring& dest_path,
        std::function<void(size_t, size_t)> on_progress) override {
      std::ofstream out(dest_path, std::ios::binary | std::ios::trunc);
      out.write("SETUP_PAYLOAD", 13);
      out.close();
      if (on_progress) {
        on_progress(100, 1000);
        on_progress(1000, 1000);
      }
      return Result<void>::ok();
    }
  };

  auto dl = std::make_shared<MockAppDownloader>();
  ClientUpdater::instance().set_downloader(dl);
  // B-D44 收敛后验签走 minisign（config 公钥 = Mini tauri.conf.json:32）；
  // mock 载荷 SETUP_PAYLOAD 配测试密钥对签名（tests/minisign_vector.h）
  clashwin::update::set_verify_pubkey_for_test(kTestPubkeyFileB64);

  std::vector<std::pair<std::string, int>> captured;
  std::string last_message;
  uint64_t sub_id = events::EventBus::instance().subscribe(
      "client-update-progress", [&](const nlohmann::json& payload) {
        captured.emplace_back(payload.value("status", ""), payload.value("progress", 0));
        last_message = payload.value("message", "");
      });

  auto res = ClientUpdater::instance().download_app_update(
      "https://mock/setup.exe",
      "14f8d5227db0d72a23479944bd4263bfdcf77dceaa6b04ec77f7d0e5eb4bb457",
      kTestSetupPayloadSigB64);
  clashwin::update::set_verify_pubkey_for_test(nullptr);
  events::EventBus::instance().unsubscribe(sub_id);

  UT_CHECK(res.is_ok());
  UT_CHECK(std::filesystem::exists(res.value()));
  UT_CHECK(!captured.empty());

  // 首事件 downloading/0（开始下载软件更新包...），中间有 downloading/10
  UT_CHECK_EQ(captured.front().first, "downloading");
  UT_CHECK_EQ(captured.front().second, 0);
  bool saw_ten = false;
  for (const auto& ev : captured) {
    if (ev.first == "downloading" && ev.second == 10) saw_ten = true;
  }
  UT_CHECK(saw_ten);

  // 末事件 done/100 + 完成文案
  UT_CHECK_EQ(captured.back().first, "done");
  UT_CHECK_EQ(captured.back().second, 100);
  UT_CHECK(last_message.find("下载完成") != std::string::npos);

  std::error_code ec;
  std::filesystem::remove(res.value(), ec);
}

// Test 8c: Client Updater Download Failure Emits Error Event
UT_TEST(t24_client_updater_download_failure_emits_error) {
  struct FailingDownloader : IAppUpdateDownloader {
    Result<std::string> fetch_manifest(const std::string&) override {
      return Result<std::string>::err(Error{1, "unused"});
    }
    Result<void> download_installer(const std::string&, const std::wstring&,
                                    std::function<void(size_t, size_t)>) override {
      return Result<void>::err(Error{2, "mock boom"});
    }
  };

  ClientUpdater::instance().set_downloader(std::make_shared<FailingDownloader>());

  std::string last_status;
  std::string last_message;
  uint64_t sub_id = events::EventBus::instance().subscribe(
      "client-update-progress", [&](const nlohmann::json& payload) {
        last_status = payload.value("status", "");
        last_message = payload.value("message", "");
      });

  auto res = ClientUpdater::instance().download_app_update("https://mock/setup.exe");
  events::EventBus::instance().unsubscribe(sub_id);

  UT_CHECK(!res.is_ok());
  UT_CHECK_EQ(last_status, "error");
  UT_CHECK(last_message.find("更新失败") == 0);
  UT_CHECK(last_message.find("mock boom") != std::string::npos);
}

// Test 8d: Client Updater SHA-256 校验（已知向量 + manifest 解析 + 不匹配拒绝）
UT_TEST(t24_client_updater_sha256_verify) {
  // "abc" 的 SHA-256 标准向量
  const auto tmp = std::filesystem::temp_directory_path() / "cw_sha_abc.bin";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write("abc", 3);
  }
  UT_CHECK_EQ(ClientUpdater::compute_sha256_hex(tmp.wstring()),
              std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  std::error_code ec;
  std::filesystem::remove(tmp, ec);

  // manifest 平台节点 sha256（统一转小写）
  auto info = ClientUpdater::parse_manifest(
      R"({"version":"9.9.9","platforms":{"windows-x86_64":{"url":"https://x/setup.exe","sha256":"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"}}})",
      "1.0.0");
  UT_CHECK(info.is_ok());
  UT_CHECK_EQ(info.value().sha256,
              std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

  // 不匹配 → 删除文件 + error 事件（← Mini 验签失败即中止）
  struct PayloadDownloader : IAppUpdateDownloader {
    Result<std::string> fetch_manifest(const std::string&) override {
      return Result<std::string>::err(Error{1, "unused"});
    }
    Result<void> download_installer(const std::string&, const std::wstring& dest_path,
                                    std::function<void(size_t, size_t)> on_progress) override {
      std::ofstream out(dest_path, std::ios::binary | std::ios::trunc);
      out.write("NOT_ABC", 7);
      out.close();
      if (on_progress) on_progress(7, 7);
      return Result<void>::ok();
    }
  };
  ClientUpdater::instance().set_downloader(std::make_shared<PayloadDownloader>());

  std::string last_status, last_message;
  uint64_t sub_id = events::EventBus::instance().subscribe(
      "client-update-progress", [&](const nlohmann::json& payload) {
        last_status = payload.value("status", "");
        last_message = payload.value("message", "");
      });
  auto res = ClientUpdater::instance().download_app_update(
      "https://mock/setup.exe",
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  events::EventBus::instance().unsubscribe(sub_id);

  UT_CHECK(!res.is_ok());
  UT_CHECK_EQ(last_status, "error");
  UT_CHECK(last_message.find("校验失败") != std::string::npos);
}

// Test 8e: Ed25519 验签（RFC 8032 test vector + 篡改拒绝）
UT_TEST(t24_ed25519_verify_rfc8032) {
  const uint8_t pk[32] = {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7,
                          0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
                          0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25,
                          0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};
  const uint8_t sig[64] = {0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72,
                           0x90, 0x86, 0xe2, 0xcc, 0x80, 0x6e, 0x82, 0x8a,
                           0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5, 0xd9, 0x74,
                           0xd8, 0x73, 0xe0, 0x65, 0x22, 0x49, 0x01, 0x55,
                           0x5f, 0xb8, 0x82, 0x15, 0x90, 0xa3, 0x3b, 0xac,
                           0xc6, 0x1e, 0x39, 0x70, 0x1c, 0xf9, 0xb4, 0x6b,
                           0xd2, 0x5b, 0xf5, 0xf0, 0x59, 0x5b, 0xbe, 0x24,
                           0x65, 0x51, 0x41, 0x43, 0x8e, 0x7a, 0x10, 0x0b};
  // 空消息
  UT_CHECK(util::ed25519_verify(nullptr, 0, sig, pk));
  // 篡改消息 → 拒绝
  const uint8_t bad[3] = {'a', 'b', 'c'};
  UT_CHECK(!util::ed25519_verify(bad, 3, sig, pk));
}

// Test 9: Diagnostic Bundle YAML Secret Redaction
UT_TEST(t24_diag_redact_yaml_masks_secrets) {
  const std::string raw_yaml = R"(
proxies:
  - name: "HK-01"
    type: ss
    server: 1.2.3.4
    port: 443
    password: my-super-secret-password
    uuid: 12345678-abcd-1234-cdef-123456789abc
    psk: my-preshared-key
secret: clash-secret-token-xyz
proxy-providers:
  sub1:
    url: https://airport.example/sub?token=very-confidential-token
)";

  std::string redacted = DiagnosticBundle::redact_yaml_text(raw_yaml);

  // Assert secret strings are scrubbed
  UT_CHECK(redacted.find("my-super-secret-password") == std::string::npos);
  UT_CHECK(redacted.find("12345678-abcd-1234-cdef-123456789abc") == std::string::npos);
  UT_CHECK(redacted.find("my-preshared-key") == std::string::npos);
  UT_CHECK(redacted.find("clash-secret-token-xyz") == std::string::npos);
  UT_CHECK(redacted.find("very-confidential-token") == std::string::npos);

  // Assert replacement token is present
  UT_CHECK(redacted.find("***REDACTED***") != std::string::npos);

  // Assert non-sensitive fields are preserved
  UT_CHECK(redacted.find("HK-01") != std::string::npos);
  UT_CHECK(redacted.find("1.2.3.4") != std::string::npos);
  UT_CHECK(redacted.find("443") != std::string::npos);
  UT_CHECK(redacted.find("ss") != std::string::npos);
}

// Test 10: Unparsable YAML Never Leaks Content
UT_TEST(t24_diag_unparsable_yaml_never_leaks) {
  const std::string dirty_yaml =
      "proxies:\n"
      "  - name: broken\n"
      "    password: leak-me-password-12345\n"
      "  corrupted_syntax::: [[[[ [ unclosed tab\t\n";

  std::string output = DiagnosticBundle::redact_yaml_text(dirty_yaml);

  // Strict invariant: password token must never appear in output!
  UT_CHECK(output.find("leak-me-password-12345") == std::string::npos);

  // Explanatory warning must lead the output
  UT_CHECK(util::starts_with(output, "# 配置解析失败，为避免泄露原始内容，本文件不含配置正文:"));
}

// Test 11: Log Text Redaction for Authorization Header and Bearer Tokens
UT_TEST(t24_diag_redact_log_auth_header_and_bearer) {
  const std::string log =
      "2026-10-28 12:00:00 [INFO] Request headers: Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.secret\n"
      "2026-10-28 12:00:01 [DEBUG] Handshake token: Bearer my-standalone-bearer-token-12345\n"
      "2026-10-28 12:00:02 [DEBUG] authorization=Bearer token-in-equal-line\n";

  std::string redacted = DiagnosticBundle::redact_log_text(log);

  UT_CHECK(redacted.find("eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.secret") == std::string::npos);
  UT_CHECK(redacted.find("my-standalone-bearer-token-12345") == std::string::npos);
  UT_CHECK(redacted.find("token-in-equal-line") == std::string::npos);

  UT_CHECK(redacted.find("Authorization: ***REDACTED***") != std::string::npos);
  UT_CHECK(redacted.find("Bearer ***REDACTED***") != std::string::npos);
}

// Test 12: Log Text Redaction for Key-Value Secrets and URL Userinfo
UT_TEST(t24_diag_redact_log_kv_and_userinfo) {
  const std::string log =
      "Connecting to proxy url=socks5://admin:supersecret@192.168.1.1:1080\n"
      "Profile sync failed password=MyPassword999 token=AbCdEfGh123 uuid=deadbeef-0000\n"
      "Checking benign url: https://www.google.com/generate_204 status 204\n";

  std::string redacted = DiagnosticBundle::redact_log_text(log);

  // Userinfo scrubbed
  UT_CHECK(redacted.find("admin:supersecret@") == std::string::npos);
  UT_CHECK(redacted.find("***REDACTED***@") != std::string::npos);

  // Key-value secrets scrubbed
  UT_CHECK(redacted.find("MyPassword999") == std::string::npos);
  UT_CHECK(redacted.find("AbCdEfGh123") == std::string::npos);
  UT_CHECK(redacted.find("deadbeef-0000") == std::string::npos);

  // Benign URL intact
  UT_CHECK(redacted.find("https://www.google.com/generate_204") != std::string::npos);
}

// Test 13: Log Tail Truncation
UT_TEST(t24_diag_log_tail_cap) {
  std::string big_log;
  big_log.reserve(2500000);
  for (int i = 0; i < 50000; ++i) {
    big_log += "2026-10-28 12:00:00 [DEBUG] Log record number " + std::to_string(i) + "\n";
  }

  size_t cap = 2 * 1024 * 1024;
  std::string truncated = DiagnosticBundle::tail_truncate_log(big_log, cap);

  UT_CHECK(truncated.size() <= cap + 256);
  UT_CHECK(util::starts_with(truncated, "[... 日志文件较大（总计"));
  UT_CHECK(truncated.find("Log record number") != std::string::npos);
}

// Test 14: Diagnostic Bundle ZIP Creation and Contents
UT_TEST(t24_diag_bundle_zip_creation_and_contents) {
  TempHomeGuard th;

  // Prepare directories and sample files
  std::filesystem::path logs_dir = th.dir / "logs";
  std::filesystem::path sidecar_logs = logs_dir / "sidecar";
  std::filesystem::create_directories(sidecar_logs);

  std::ofstream(logs_dir / "app.log") << "2026-10-28 10:00:00 Authorization: Bearer secret_app_tok\n";
  std::ofstream(sidecar_logs / "core.log") << "2026-10-28 10:00:01 password=secret_core_pwd\n";

  std::ofstream(th.dir / "clash-win.yaml") << "proxies:\n  - name: P1\n    password: clash_secret_pwd\n";
  std::ofstream(th.dir / "verge.yaml") << "secret: verge_secret_val\n";
  std::ofstream(th.dir / "profiles.yaml") << "token: prof_secret_val\n";

  auto res = DiagnosticBundle::export_diagnostic_bundle();
  UT_CHECK(res.is_ok());

  std::filesystem::path zip_path(res.value());
  UT_CHECK(std::filesystem::exists(zip_path));

  // Inspect the generated zip binary
  std::ifstream zf(zip_path, std::ios::binary);
  UT_CHECK(zf.is_open());
  std::string zip_content((std::istreambuf_iterator<char>(zf)), std::istreambuf_iterator<char>());
  zf.close();

  // PKZIP signature 0x04034b50
  uint32_t sig = *reinterpret_cast<const uint32_t*>(zip_content.data());
  UT_CHECK_EQ(sig, 0x04034b50u);

  // Check required entries in zip table
  UT_CHECK(zip_content.find("logs/app.log") != std::string::npos);
  UT_CHECK(zip_content.find("logs/sidecar/core.log") != std::string::npos);
  UT_CHECK(zip_content.find("runtime-config.redacted.yaml") != std::string::npos);
  UT_CHECK(zip_content.find("verge.yaml.redacted.yaml") != std::string::npos);
  UT_CHECK(zip_content.find("profiles.yaml.redacted.yaml") != std::string::npos);
  UT_CHECK(zip_content.find("system-info.txt") != std::string::npos);
  UT_CHECK(zip_content.find("README.txt") != std::string::npos);

  // Assert all raw secrets are completely scrubbed from zip contents!
  UT_CHECK(zip_content.find("secret_app_tok") == std::string::npos);
  UT_CHECK(zip_content.find("secret_core_pwd") == std::string::npos);
  UT_CHECK(zip_content.find("clash_secret_pwd") == std::string::npos);
  UT_CHECK(zip_content.find("verge_secret_val") == std::string::npos);
  UT_CHECK(zip_content.find("prof_secret_val") == std::string::npos);
  UT_CHECK(zip_content.find("***REDACTED***") != std::string::npos);
}
