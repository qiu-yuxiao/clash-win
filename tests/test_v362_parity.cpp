// Clash Win — Clash Mini v3.6.0 ~ v3.6.2 (BUG-413..BUG-417) 专项对齐单测
// 1. BUG-413：诊断包 WireGuard/Reality 脱敏、CoreUpdater 官方 URL 白名单 / Fail-Closed SHA-256 / 体积上限、
//             IVerge::normalize_valid_clash_core 运行期白名单
// 2. BUG-414：Autostart 两档迁移标记 (done / pending_admin) 与任务目录存在性判断、
//             Singleton /commands/ping 探活与应答校验、CoreManager 手动切点抢占启动选点恢复
// 3. BUG-415：ProfileDialFailureTracker 建连失败 3-in-30s 突发阈值与 60s 冷却、read_incremental_log_lines
// 4. BUG-416：WER / CrashDumps 匹配、ProfileTimer 2min 快速重试计算、TUN 默认网卡丢失/切换与 300s/60s 解耦窗口
// 5. BUG-417：失真仅由活跃节点自身通过备用 URL 才豁免、首次观测不开伪宽限期、冷启动死节点预置第 1 次失败、
//             CoreManager::reconcile_running_mode_if_needed、ClientUpdater 代理端口解析/失败 3 次重试/验签不重下
#include "test_util.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/singleton.h"
#include "clashwin/config/models.h"
#include "clashwin/core/core_updater.h"
#include "clashwin/system/diagnostic_bundle.h"
#include "clashwin/update/client_updater.h"
#include "config/verge.h"
#include "core/autostart.h"
#include "core/core_manager.h"
#include "core/delay_manager.h"
#include "core/monitor.h"
#include "core/timer.h"
#include "minisign_vector.h"
#include "util/dirs.h"

using namespace clashwin;
using namespace clashwin::app;
using namespace clashwin::config;
using namespace clashwin::core;
using namespace clashwin::system;
using namespace clashwin::update;

namespace {

struct TempHomeGuard {
  std::filesystem::path dir;
  TempHomeGuard() {
    static std::atomic<int> seq{0};
    dir = std::filesystem::temp_directory_path() /
          ("clashwin_v362_" + std::to_string(::GetCurrentProcessId()) + "_" +
           std::to_string(seq.fetch_add(1)));
    std::filesystem::create_directories(dir);
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", dir.wstring().c_str());
  }
  ~TempHomeGuard() {
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
};

struct MonitorStateGuard {
  MonitorStateGuard() { CoreMonitor::instance().reset_state(); }
  ~MonitorStateGuard() {
    auto& m = CoreMonitor::instance();
    m.stop();
    m.set_hooks({});
    m.set_params({});
    m.reset_state();
  }
};

std::string make_minimal_pe64() {
  std::string img(5u * 1024u * 1024u, '\0');
  img[0] = 'M';
  img[1] = 'Z';
  const uint32_t pe_off = 0x80;
  std::memcpy(&img[0x3C], &pe_off, sizeof(pe_off));
  img[pe_off + 0] = 'P';
  img[pe_off + 1] = 'E';
  const uint16_t machine = 0x8664;
  const uint16_t num_sections = 1;
  const uint16_t opt_hdr_size = 240;
  const uint16_t characteristics = 0x0022;
  std::memcpy(&img[pe_off + 4], &machine, 2);
  std::memcpy(&img[pe_off + 6], &num_sections, 2);
  std::memcpy(&img[pe_off + 20], &opt_hdr_size, 2);
  std::memcpy(&img[pe_off + 22], &characteristics, 2);
  const uint16_t magic = 0x020B;
  std::memcpy(&img[pe_off + 24], &magic, 2);
  const size_t sec_off = pe_off + 24 + opt_hdr_size;
  std::memcpy(&img[sec_off], ".text\0\0\0", 8);
  return img;
}

}  // namespace

// ---------- 1. BUG-413: 诊断包 WireGuard / Reality 新字段脱敏 ----------
UT_TEST(v362_bug413_diagnostics_redaction_wg_reality) {
  const std::string yaml = R"(
proxies:
  - name: wg-01
    type: wireguard
    server: wg.example.com
    port: 51820
    private-key: top-secret-privkey
    private-key-passphrase: secret-passphrase
    public-key: peer-pubkey
    pre-shared-key: wg-top-psk-secret
    preshared-key: wg-top-preshared-secret
    peers:
      - server: wg2.example.com
        port: 51820
        public-key: peer-2-pubkey
        pre-shared-key: wg-peer-psk-secret
  - name: vless-reality-01
    type: vless
    server: reality.example.com
    port: 443
    uuid: 11111111-2222-3333-4444-555555555555
    obfs-param: secret-obfs-param
    protocol-param: secret-proto-param
    reality-opts:
      public-key: reality-pub
      short-id: abcd1234
)";
  const std::string out = DiagnosticBundle::redact_yaml_text(yaml);
  UT_CHECK(out.find("top-secret-privkey") == std::string::npos);
  UT_CHECK(out.find("secret-passphrase") == std::string::npos);
  UT_CHECK(out.find("wg-top-psk-secret") == std::string::npos);
  UT_CHECK(out.find("wg-top-preshared-secret") == std::string::npos);
  UT_CHECK(out.find("wg-peer-psk-secret") == std::string::npos);
  UT_CHECK(out.find("abcd1234") == std::string::npos);
  UT_CHECK(out.find("secret-obfs-param") == std::string::npos);
  UT_CHECK(out.find("secret-proto-param") == std::string::npos);
  UT_CHECK(out.find("wg.example.com") != std::string::npos);
  UT_CHECK(out.find("reality.example.com") != std::string::npos);

  const std::string log_out = DiagnosticBundle::redact_log_text(
      "wg init pre-shared-key=wg-log-psk preshared-key=wg-log-preshared psk: another-psk "
      "private-key=priv-123 private-key-passphrase=pass-123 short-id=sid-999 "
      "obfs-param=obfsp protocol-param=protop");
  UT_CHECK(log_out.find("wg-log-psk") == std::string::npos);
  UT_CHECK(log_out.find("wg-log-preshared") == std::string::npos);
  UT_CHECK(log_out.find("another-psk") == std::string::npos);
  UT_CHECK(log_out.find("priv-123") == std::string::npos);
  UT_CHECK(log_out.find("pass-123") == std::string::npos);
  UT_CHECK(log_out.find("sid-999") == std::string::npos);
  UT_CHECK(log_out.find("obfsp") == std::string::npos);
  UT_CHECK(log_out.find("protop") == std::string::npos);
}

// ---------- 2. BUG-413: CoreUpdater URL 白名单 / digest / Fail-Closed / IVerge 白名单 ----------
UT_TEST(v362_bug413_core_updater_url_digest_fail_closed_and_verge_whitelist) {
  TempHomeGuard th;

  const std::string valid_url =
      "https://github.com/MetaCubeX/mihomo/releases/download/v1.19.34/"
      "mihomo-windows-amd64-v2-v1.19.34.zip";
  UT_CHECK(CoreUpdater::validate_official_mihomo_asset_url(valid_url).is_ok());
  UT_CHECK(CoreUpdater::validate_official_mihomo_asset_url(
               "http://github.com/MetaCubeX/mihomo/releases/download/v1.0/a.zip")
               .is_err());
  UT_CHECK(CoreUpdater::validate_official_mihomo_asset_url(
               "https://evil.example.com/MetaCubeX/mihomo/releases/download/v1.0/a.zip")
               .is_err());
  UT_CHECK(CoreUpdater::validate_official_mihomo_asset_url(
               "https://github.com:8443/MetaCubeX/mihomo/releases/download/v1.0/a.zip")
               .is_err());
  UT_CHECK(CoreUpdater::validate_official_mihomo_asset_url(
               "https://user@github.com/MetaCubeX/mihomo/releases/download/v1.0/a.zip")
               .is_err());
  UT_CHECK(CoreUpdater::validate_official_mihomo_asset_url(
               "https://github.com/other/repo/releases/download/v1.0/a.zip")
               .is_err());
  UT_CHECK(CoreUpdater::validate_official_mihomo_asset_url(
               "https://github.com/MetaCubeX/mihomo/releases/download/../other/a.zip")
               .is_err());
  UT_CHECK(CoreUpdater::validate_official_mihomo_asset_url(
               "https://github.com/MetaCubeX/mihomo/releases/download/%2e%2e/other/a.zip")
               .is_err());

  const std::string upper_hex(64, 'A');
  const std::string lower_hex(64, 'a');
  const std::string bare_hex(64, 'b');
  auto p1 = CoreUpdater::parse_asset_sha256_digest("SHA256:" + upper_hex);
  UT_CHECK(p1.has_value() && *p1 == lower_hex);
  auto p2 = CoreUpdater::parse_asset_sha256_digest(bare_hex);
  UT_CHECK(p2.has_value() && *p2 == bare_hex);
  UT_CHECK(!CoreUpdater::parse_asset_sha256_digest("sha256:short").has_value());
  UT_CHECK(!CoreUpdater::parse_asset_sha256_digest("md5:1234").has_value());
  UT_CHECK(!CoreUpdater::parse_asset_sha256_digest(std::nullopt).has_value());

  // Fail-Closed：无 asset.digest 且无 checksum asset 时拒绝升级
  std::filesystem::path zip_path = th.dir / "core.zip";
  UT_CHECK(DiagnosticBundle::write_zip_file(
               zip_path.wstring(), {{"mini-mihomo.exe", make_minimal_pe64()}})
               .is_ok());

  struct SimpleDownloader : ICoreUpdateDownloader {
    std::wstring src;
    Result<std::string> fetch_latest_release_json() override {
      return Result<std::string>::err(Error{1, "unused"});
    }
    Result<std::string> fetch_checksum_content(const std::string&) override {
      return Result<std::string>::err(Error{1, "no checksum"});
    }
    Result<void> download_asset(
        const std::string&, const std::wstring& dst,
        std::function<void(uint64_t, uint64_t)>) override {
      std::error_code ec;
      std::filesystem::copy_file(
          src, dst, std::filesystem::copy_options::overwrite_existing, ec);
      return ec ? Result<void>::err(Error{2, ec.message()}) : Result<void>::ok();
    }
  };
  struct CoreUpdaterResetGuard {
    ~CoreUpdaterResetGuard() {
      CoreUpdater::instance().set_core_api_client(nullptr);
      CoreUpdater::instance().reset_lifecycle_ops();
    }
  } cu_guard;

  auto dl = std::make_shared<SimpleDownloader>();
  dl->src = zip_path.wstring();
  CoreUpdater::instance().set_downloader(dl);
  CoreOps ops;
  ops.stop = [] { return Result<void>::ok(); };
  ops.start = [] { return Result<void>::ok(); };
  CoreUpdater::instance().set_lifecycle_ops(ops);

  GithubRelease no_sha_rel;
  no_sha_rel.tag_name = "v1.19.34";
  no_sha_rel.assets = {{"mihomo-windows-amd64-v1.19.34.zip",
                        "https://github.com/MetaCubeX/mihomo/releases/download/v1.19.34/"
                        "mihomo-windows-amd64-v1.19.34.zip"}};
  auto res_no_sha = CoreUpdater::instance().start_core_upgrade(no_sha_rel);
  UT_CHECK(res_no_sha.is_err());

  // 声明 size 与实际下载体积不一致时拒绝升级
  GithubRelease size_mismatch_rel = no_sha_rel;
  size_mismatch_rel.assets[0].digest =
      "sha256:" + CoreUpdater::compute_file_sha256(zip_path.wstring()).value();
  size_mismatch_rel.assets[0].size = 12345ULL;
  auto res_size_bad = CoreUpdater::instance().start_core_upgrade(size_mismatch_rel);
  UT_CHECK(res_size_bad.is_err());

  // IVerge::clash_core 运行期白名单
  UT_CHECK_EQ(IVerge::normalize_valid_clash_core(std::nullopt), "win-mihomo");
  UT_CHECK_EQ(IVerge::normalize_valid_clash_core(std::string("")), "win-mihomo");
  UT_CHECK_EQ(IVerge::normalize_valid_clash_core(std::string("  win-mihomo  ")),
              "win-mihomo");
  UT_CHECK_EQ(IVerge::normalize_valid_clash_core(std::string("verge-mihomo-alpha")),
              "win-mihomo");
  UT_CHECK_EQ(IVerge::normalize_valid_clash_core(std::string("..\\..\\evil")),
              "win-mihomo");

  IVerge cfg;
  IVerge patch;
  patch.clash_core = "..\\attacker";
  cfg.patch_config(patch);
  UT_CHECK_EQ(cfg.get_valid_clash_core(), "win-mihomo");
  UT_CHECK(cfg.clash_core.has_value() && *cfg.clash_core == "win-mihomo");
}

// ---------- 3. BUG-414: Autostart 标记 / Singleton /commands/ping / 手动切点抢占 ----------
UT_TEST(v362_bug414_autostart_marker_and_singleton_ping_and_ensure_preempt) {
  TempHomeGuard th;
  MonitorStateGuard mg;

  // Autostart 两档标记
  UT_CHECK(!should_skip_cleanup_from_marker(std::nullopt, false));
  UT_CHECK(!should_skip_cleanup_from_marker(std::string("1"), false));
  UT_CHECK(should_skip_cleanup_from_marker(std::string(kMarkerDone), false));
  UT_CHECK(should_skip_cleanup_from_marker(std::string(kMarkerDone), true));
  UT_CHECK(should_skip_cleanup_from_marker(std::string(kMarkerPendingAdmin), false));
  UT_CHECK(!should_skip_cleanup_from_marker(std::string(kMarkerPendingAdmin), true));

  const auto task_dir = th.dir / "mock_tasks";
  std::filesystem::create_directories(task_dir);
  UT_CHECK(is_task_file_definitely_absent(task_dir, L"MissingTask"));
  {
    std::ofstream f(task_dir / L"ExistingTask", std::ios::binary);
    f << "<Task/>";
  }
  UT_CHECK(!is_task_file_definitely_absent(task_dir, L"ExistingTask"));
  UT_CHECK(!is_task_file_definitely_absent(std::nullopt, L"MissingTask"));

  // Singleton /commands/ping 与应答校验
  SingletonServer srv;
  const std::string ping_resp =
      srv.handle_http_request("GET /commands/ping HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
  UT_CHECK(ping_resp.find("HTTP/1.1 200 OK") == 0);
  UT_CHECK_EQ(SingletonClient::extract_http_body(ping_resp), "clash-win-ok");

  UT_CHECK(SingletonClient::resolve_singleton_probe_target(false, false) ==
           SingletonClient::ProbeTarget::Visible);
  UT_CHECK(SingletonClient::resolve_singleton_probe_target(false, true) ==
           SingletonClient::ProbeTarget::Ping);
  UT_CHECK(SingletonClient::resolve_singleton_probe_target(true, true) ==
           SingletonClient::ProbeTarget::Scheme);

  UT_CHECK(SingletonClient::is_valid_singleton_reply(false, 200, "ok\n"));
  UT_CHECK(!SingletonClient::is_valid_singleton_reply(false, 200, "other-app"));
  UT_CHECK(!SingletonClient::is_valid_singleton_reply(false, 404, "ok"));
  UT_CHECK(SingletonClient::is_valid_singleton_reply(true, 200, "clash-win-ok"));
  UT_CHECK(!SingletonClient::is_valid_singleton_reply(true, 200, "ok"));

  // CoreManager 手动切点抢占在途选点恢复
  struct MockEnsureApi : ICoreApiClient {
    int select_calls = 0;
    std::function<void()> on_first_base_cfg;
    bool fired = false;

    Result<mihomo::BaseConfig> get_base_config() override {
      if (!fired && on_first_base_cfg) {
        fired = true;
        on_first_base_cfg();
      }
      return Result<mihomo::BaseConfig>::ok(mihomo::BaseConfig{});
    }
    Result<mihomo::ProxyItem> get_group_by_name(const std::string&) override {
      mihomo::ProxyItem g;
      g.name = "PROXY";
      g.now = std::string("JP-1");
      g.all = std::vector<std::string>{"JP-1", "SG-1"};
      return Result<mihomo::ProxyItem>::ok(g);
    }
    Result<void> select_node_for_group(const std::string&, const std::string&) override {
      ++select_calls;
      return Result<void>::ok();
    }
    Result<void> reload_config(bool, const std::string&) override {
      return Result<void>::ok();
    }
  };

  CoreManager mgr;
  MockEnsureApi api;
  const uint64_t start_manual_epoch = CoreMonitor::instance().manual_switch_epoch();
  api.on_first_base_cfg = [] {
    CoreMonitor::instance().notify_manual_node_switched();
  };
  RestoreTimings timings;
  timings.api_ready_budget_ms = 500;
  timings.api_ready_poll_ms = 10;
  timings.group_fill_budget_ms = 500;
  timings.group_fill_poll_ms = 10;
  timings.select_budget_ms = 500;
  timings.select_retry_ms = 10;
  const bool ensure_ok = mgr.ensure_proxy_group_selection(
      api, std::string("SG-1"), timings,
      std::pair<std::optional<std::string>, std::string>{std::string("JP-1"), ""},
      0, nullptr, start_manual_epoch);
  UT_CHECK(!ensure_ok);
  UT_CHECK_EQ(api.select_calls, 0);
}

// ---------- 4. BUG-415 / BUG-416: Timer / WER / Dump / DialFail / TUN 信号 ----------
UT_TEST(v362_bug415_416_timer_wer_and_kernel_log_observer) {
  TempHomeGuard th;
  MonitorStateGuard mg;

  // ProfileTimer 2 分钟快速重试
  UT_CHECK_EQ(ProfileTimer::compute_next_schedule_minutes(1440, true), 1440ULL);
  UT_CHECK_EQ(ProfileTimer::compute_next_schedule_minutes(1440, false), 2ULL);
  UT_CHECK_EQ(ProfileTimer::compute_next_schedule_minutes(1, false), 1ULL);
  UT_CHECK_EQ(ProfileTimer::compute_next_schedule_minutes(2, false), 2ULL);

  // WER & CrashDumps 目标匹配
  UT_CHECK(DiagnosticBundle::is_clash_dump_target("clash-win.exe.42.dmp"));
  UT_CHECK(DiagnosticBundle::is_clash_dump_target("win-mihomo.exe.1234.dmp"));
  UT_CHECK(DiagnosticBundle::is_clash_dump_target("clash-verge-service.exe.99.dmp"));
  UT_CHECK(DiagnosticBundle::is_clash_dump_target("clash_verge_service.exe.1.dmp"));
  UT_CHECK(!DiagnosticBundle::is_clash_dump_target("clash win.exe.1.dmp"));
  UT_CHECK(!DiagnosticBundle::is_clash_dump_target("notepad.exe.42.dmp"));
  UT_CHECK(DiagnosticBundle::is_clash_wer_target("faulting application name: clash win"));
  UT_CHECK(DiagnosticBundle::is_clash_wer_target("faulting application name: win-mihomo.exe"));
  UT_CHECK(!DiagnosticBundle::is_clash_wer_target("faulting application name: calc.exe"));

  // 内核建连失败识别 + TUN 网卡信号分类
  UT_CHECK(CoreMonitor::is_proxy_dial_failure_log_line(
      "time=\"2026-11-20T21:28:56.383087000+08:00\" level=warning msg=\"[TCP] dial PROXY "
      "(match RuleSet/gfwlist) 198.18.0.1:8245 --> a.com:443 error: failed to create session: "
      "context deadline exceeded\""));
  UT_CHECK(!CoreMonitor::is_proxy_dial_failure_log_line(
      "time=\"2026-11-20T03:14:10Z\" level=warning msg=\"[TCP] dial DIRECT (match Match/) "
      "1.1.1.1:443 error: i/o timeout\""));
  UT_CHECK(!CoreMonitor::is_proxy_dial_failure_log_line(
      "time=\"2026-11-20T03:14:10Z\" level=warning msg=\"[TCP] dial PROXY__METRICS a.com:443 "
      "error: i/o timeout\""));
  UT_CHECK(!CoreMonitor::is_proxy_dial_failure_log_line(
      "time=\"2026-11-20T03:14:10Z\" level=warning msg=\"[TCP] dial PROXY a.com:443 "
      "error: operation was canceled\""));
  UT_CHECK(!CoreMonitor::is_proxy_dial_failure_log_line(
      "time=\"2026-11-20T03:14:10Z\" level=warning msg=\"[TCP] dial PROXY a.com:443 "
      "error: dns resolve failed: couldn't find ip\""));

  UT_CHECK(CoreMonitor::classify_kernel_log_event(
               "msg=\"[TUN] default interface lost by monitor\"") ==
           CoreMonitor::KernelLogEvent::TunInterfaceLost);
  UT_CHECK(CoreMonitor::classify_kernel_log_event(
               "msg=\"[TUN] default interface changed by monitor, Wi-Fi => Ethernet\"") ==
           CoreMonitor::KernelLogEvent::TunInterfaceChanged);
  UT_CHECK(!CoreMonitor::classify_kernel_log_event("level=info msg=\"Initial complete\"")
                .has_value());

  // ProxyDialFailureTracker 3-in-30s + 60s 冷却 + TUN 300s/60s 解耦窗口
  CoreMonitor::ProxyDialFailureTracker tracker;
  const int64_t base_utc = 1700000000000LL;
  const int64_t base_mono = 100000LL;
  tracker.on_node_checked("JP-1", base_utc);
  UT_CHECK(tracker.cursor.reanchor_to_end);

  const std::string dial_line =
      "[TCP] dial PROXY (match Match/) a.com:443 error: i/o timeout";
  tracker.ingest_log_line(dial_line, base_mono + 1000, base_utc + 1000);
  tracker.ingest_log_line(dial_line, base_mono + 2000, base_utc + 2000);
  UT_CHECK(tracker.evaluate_burst(base_mono + 2000).first == 2);
  UT_CHECK(!tracker.evaluate_burst(base_mono + 2000).second);
  tracker.ingest_log_line(dial_line, base_mono + 3000, base_utc + 3000);
  UT_CHECK(tracker.evaluate_burst(base_mono + 3000).second);
  tracker.consume_burst_for_probe(base_mono + 3000);
  UT_CHECK(!tracker.evaluate_reprobe_signal(base_mono + 10000).has_value());

  // TUN 切换信号 60s 冷却外触发，60s TTL 过期；300s 回溯窗独立于 30s
  CoreMonitor::record_flowing_traffic_at(base_mono + 70000);
  (void)CoreMonitor::take_force_online_recheck();
  tracker.ingest_log_line(
      "time=\"2023-11-14T22:14:40Z\" msg=\"[TUN] default interface changed by monitor\"",
      base_mono + 70000, base_utc + 105000);  // 45s old < 300s
  UT_CHECK(!CoreMonitor::is_recent_flowing_traffic_active(base_mono + 70000));
  UT_CHECK(CoreMonitor::take_force_online_recheck());
  auto sig = tracker.evaluate_reprobe_signal(base_mono + 70000);
  UT_CHECK(sig.has_value() &&
           sig->reason == CoreMonitor::KernelReprobeReason::TunInterfaceChanged);
  // TTL 60s 到期
  UT_CHECK(!tracker.evaluate_reprobe_signal(base_mono + 70000 + 60000).has_value());

  // read_incremental_log_lines：首锚定末尾、增量完整行、半行保留、轮转重置
  const std::wstring log_path = (th.dir / "service_latest.log").wstring();
  {
    std::ofstream f(log_path, std::ios::binary | std::ios::trunc);
    f << "old-1\nold-2\n";
  }
  CoreMonitor::IncrementalLogCursor cursor;
  cursor.reanchor_to_end = true;
  UT_CHECK(CoreMonitor::read_incremental_log_lines(log_path, cursor).empty());
  UT_CHECK(!cursor.reanchor_to_end);
  {
    std::ofstream f(log_path, std::ios::binary | std::ios::app);
    f << "new-1\npartial-no-nl";
  }
  auto inc1 = CoreMonitor::read_incremental_log_lines(log_path, cursor);
  UT_CHECK_EQ(inc1.size(), 1U);
  UT_CHECK_EQ(inc1[0], "new-1");
  {
    std::ofstream f(log_path, std::ios::binary | std::ios::app);
    f << "-completed\n";
  }
  auto inc2 = CoreMonitor::read_incremental_log_lines(log_path, cursor);
  UT_CHECK_EQ(inc2.size(), 1U);
  UT_CHECK_EQ(inc2[0], "partial-no-nl-completed");
  // 截断轮转
  {
    std::ofstream f(log_path, std::ios::binary | std::ios::trunc);
    f << "rot-1\n";
  }
  auto inc3 = CoreMonitor::read_incremental_log_lines(log_path, cursor);
  UT_CHECK_EQ(inc3.size(), 1U);
  UT_CHECK_EQ(inc3[0], "rot-1");
}

// ---------- 5. BUG-417: 失真豁免 / 首次宽限 / 启动失败预置 / 模式对账 / ClientUpdater ----------
UT_TEST(v362_bug417_distortion_excuse_boot_grace_and_client_updater) {
  MonitorStateGuard mg;

  // 主测速失真绝不能因其它节点通过而“替考免死”当前死节点
  std::map<std::string, uint32_t> non_real{
      {"DIRECT", 32}, {"剩余流量：100GB", 45}, {"SG-5", 0}, {"JP-1", 28}};
  UT_CHECK(!CoreMonitor::has_any_valid_real_probe_delay_with(
      non_real, [](const std::string& n) { return n == "SG-5"; }));

  std::map<std::string, uint32_t> peer_only{{"JP-1", 135}, {"SG-5", 0}};
  UT_CHECK(CoreMonitor::has_any_valid_real_probe_delay_with(
      peer_only, [](const std::string&) { return false; }));
  UT_CHECK(!CoreMonitor::is_active_excused_by_primary_distortion(true, "SG-5", peer_only));
  UT_CHECK(!CoreMonitor::is_active_excused_by_primary_distortion(true, "US-1", peer_only));
  peer_only["SG-5"] = 188;
  UT_CHECK(CoreMonitor::is_active_excused_by_primary_distortion(true, "SG-5", peer_only));
  UT_CHECK(!CoreMonitor::is_active_excused_by_primary_distortion(false, "SG-5", peer_only));

  // 首次观测严禁开启伪 20s 宽限期；冷启动死节点预置第 1 次失败
  UT_CHECK(!CoreMonitor::should_arm_switch_grace_on_node_change(
      std::nullopt, "SG-5", false));
  UT_CHECK(!CoreMonitor::should_arm_switch_grace_on_node_change(
      std::string("SG-5"), "SG-5", false));
  UT_CHECK(!CoreMonitor::should_arm_switch_grace_on_node_change(
      std::string("SG-5"), "JP-1", true));
  UT_CHECK(CoreMonitor::should_arm_switch_grace_on_node_change(
      std::string("SG-5"), "JP-1", false));

  UT_CHECK(CoreMonitor::should_seed_boot_probe_failure(
      std::string("SG-5"), false, true, {{"SG-5", 0}, {"JP-1", 140}}));
  UT_CHECK(!CoreMonitor::should_seed_boot_probe_failure(
      std::string("SG-5"), false, true, {{"SG-5", 1599}, {"JP-1", 90}}));
  UT_CHECK(!CoreMonitor::should_seed_boot_probe_failure(
      std::string("SG-5"), false, true, {{"SG-5", 0}, {"JP-1", 0}}));
  UT_CHECK(!CoreMonitor::should_seed_boot_probe_failure(
      std::string("SG-5"), false, false, {{"SG-5", 0}, {"JP-1", 120}}));

  // CoreManager::reconcile_running_mode_if_needed_with
  CoreManager mgr;
  UT_CHECK(mgr.running_mode() == RunningMode::NotRunning);
  CoreManager::ReconcileProbe p;
  p.service_ipc_present = [] { return true; };
  p.mihomo_pipe_present = [] { return true; };
  p.api_alive = [] { return false; };
  UT_CHECK(!mgr.reconcile_running_mode_if_needed_with(p));
  UT_CHECK(mgr.running_mode() == RunningMode::NotRunning);
  p.api_alive = [] { return true; };
  UT_CHECK(mgr.reconcile_running_mode_if_needed_with(p));
  UT_CHECK(mgr.running_mode() == RunningMode::Service);

  // ClientUpdater 本地端口解析
  UT_CHECK_EQ(ClientUpdater::resolve_local_mixed_proxy_url(std::nullopt, std::nullopt),
              "http://127.0.0.1:10801");
  UT_CHECK_EQ(ClientUpdater::resolve_local_mixed_proxy_url(7897, 7890),
              "http://127.0.0.1:7897");
  UT_CHECK_EQ(ClientUpdater::resolve_local_mixed_proxy_url(std::nullopt, 7890),
              "http://127.0.0.1:7890");
  UT_CHECK_EQ(ClientUpdater::resolve_local_mixed_proxy_url(0, 70000),
              "http://127.0.0.1:10801");

  // ClientUpdater 下载中断自动重试（1500/2500ms）与校验失败不重下
  struct UpdaterResetGuard {
    ~UpdaterResetGuard() {
      ClientUpdater::instance().set_sleep_fn(nullptr);
      set_verify_pubkey_for_test(nullptr);
    }
  } u_guard;

  struct FlakyDownloader : IAppUpdateDownloader {
    int fail_times = 0;
    int attempts = 0;
    bool write_valid = true;
    Result<std::string> fetch_manifest(const std::string&) override {
      return Result<std::string>::err(Error{1, "unused"});
    }
    Result<void> download_installer(
        const std::string&, const std::wstring& dst,
        std::function<void(size_t, size_t)> on_progress) override {
      ++attempts;
      if (attempts <= fail_times) {
        return Result<void>::err(Error{2, "transient socket reset"});
      }
      std::ofstream out(dst, std::ios::binary | std::ios::trunc);
      out.write(write_valid ? "SETUP_PAYLOAD" : "CORRUPTED_BIN", 13);
      out.close();
      if (on_progress) on_progress(13, 13);
      return Result<void>::ok();
    }
  };

  std::vector<uint32_t> sleeps;
  ClientUpdater::instance().set_sleep_fn(
      [&](uint32_t ms) { sleeps.push_back(ms); });
  set_verify_pubkey_for_test(kTestPubkeyFileB64);

  // (a) 前 2 次失败、第 3 次成功 → 记录 [1500, 2500] 退避且最终成功
  auto dl = std::make_shared<FlakyDownloader>();
  dl->fail_times = 2;
  ClientUpdater::instance().set_downloader(dl);
  auto ok_res = ClientUpdater::instance().download_app_update(
      "https://mock/setup.exe",
      "14f8d5227db0d72a23479944bd4263bfdcf77dceaa6b04ec77f7d0e5eb4bb457",
      kTestSetupPayloadSigB64);
  UT_CHECK(ok_res.is_ok());
  UT_CHECK_EQ(dl->attempts, 3);
  UT_CHECK(sleeps == std::vector<uint32_t>({1500, 2500}));
  if (ok_res.is_ok()) {
    std::error_code ec;
    std::filesystem::remove(ok_res.value(), ec);
  }

  // (b) 下载成功但 SHA-256 / 验签失败 → 仅执行 1 次，绝不自动重试
  sleeps.clear();
  auto bad_dl = std::make_shared<FlakyDownloader>();
  bad_dl->fail_times = 0;
  bad_dl->write_valid = false;
  ClientUpdater::instance().set_downloader(bad_dl);
  auto bad_res = ClientUpdater::instance().download_app_update(
      "https://mock/setup.exe",
      "14f8d5227db0d72a23479944bd4263bfdcf77dceaa6b04ec77f7d0e5eb4bb457",
      kTestSetupPayloadSigB64);
  UT_CHECK(bad_res.is_err());
  UT_CHECK_EQ(bad_dl->attempts, 1);
  UT_CHECK(sleeps.empty());
}
