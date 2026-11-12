// BUG-317 ~ BUG-362 后端全量确定性单元与契约测试（Shards 1 & 2）
//
// 覆盖矩阵（100% 离线、零真实 SCM/UAC、零外网依赖）：
//   - BUG-319 + BUG-346：x64 PE32+ 二进制完整性校验、三态判定（IoUnavailable 保锁文件 vs
//     Corrupted 自动清理）、.staging 原子替换、异常包不覆盖原核、wait_cores_exit(3000)
//   - BUG-324 + BUG-329 + BUG-330 + BUG-351：Mihomo/PipeHttp 超时契约（5000ms / 2000ms /
//     100ms / 80ms）、锁外 open_pipe 失败正确归还 permit、陈旧池化连接 GET 单次透明重试 vs POST 不重试
//   - BUG-325 + BUG-347：PipeWebSocket::close(force_timeout_ms) 有界超时防卡死、
//     compute_ws_reconnect_delay_ms 指数退避+±20%抖动（<=15000ms）、kRefreshClash 清零退避
//   - BUG-328 + BUG-352：service_operation_mutex 递归可重入与跨线程互斥、
//     extract_service_exe_from_image_path 规范化、TempTunFallbackSnapshot 内存临时降级不覆写磁盘
//     enable_tun_mode、start_auto 三态 SCM 注册分支（InstalledAndEnabled 临时降级 vs DisabledOrBroken 永久降级）
//   - BUG-326 + BUG-331 + BUG-334 + BUG-335：Lifecycle 退出默认零 stop_service UAC、
//     无锁服务优先停核绕开阻塞 lifecycle_、模式漂移停核兜底、ProfileTimer 异常安全 RAII 锁释放、
//     read_persisted_proxy_selection 过滤伪节点跨 apply_config / restart_with_restore 保底恢复
//   - BUG-317 + BUG-345：probe_physical_network_with 2s 下限 + 600ms 并发竞速上限保底 >=1400ms DNS 预算、
//     tcp_race_connect_ipv4 本地回环非阻塞 select 竞速实测
//   - BUG-318：自愈双计数器（3 次 API 假死重启保留告警计数 -> 5 次系统告警；重启报错立即升级告警；
//     AUTO_SELECT_BUSY 零惩罚回退；"Clash Win - 网络警报" 品牌不变量）
//   - BUG-321 + BUG-322 + BUG-344 + BUG-349：backend_delay_results fromProbe 载荷契约、
//     0ms 超时节点排序置底（排在 4800ms 慢节点之后）+ saved_sort_type 解析、
//     <30s 新鲜度门控缓存复用 vs 未执行严禁伪造 0ms
//   - BUG-336 + BUG-353 + BUG-359 + BUG-362：notify_node_switched 命令与三处宽限期检查点吸收、
//     run_supervised_daemon 异常捕获自愈重启、emit_network_health_notice 去重状态广播、
//     PrfOption::merge_opt 12 字段全量合并无丢失
#include "test_util.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "app/lifecycle.h"
#include "clashwin/config/models.h"
#include "clashwin/core/core_updater.h"
#include "cmd/command_service.h"
#include "config/config.h"
#include "core/auto_select.h"
#include "core/core_manager.h"
#include "core/delay_manager.h"
#include "core/monitor.h"
#include "core/service_mode.h"
#include "core/switching_lock.h"
#include "core/timer.h"
#include "events/event_bus.h"
#include "events/event_names.h"
#include "mihomo/mihomo_client.h"
#include "mihomo/pipe_http.h"
#include "mihomo/pipe_ws.h"
#include "mihomo/ws_manager.h"
#include "mock_pipe_server.h"
#include "system/diagnostic_bundle.h"
#include "system/service_runner.h"
#include "util/dirs.h"
#include "yaml-cpp/yaml.h"

namespace {

using namespace clashwin;

struct TempHomeScope {
  std::filesystem::path dir;
  TempHomeScope() {
    static std::atomic<int> s_seq{1};
    const auto base = std::filesystem::temp_directory_path();
    dir = base / ("clashwin_bug317_362_" + std::to_string(::GetTickCount64()) +
                  "_" + std::to_string(s_seq.fetch_add(1)));
    std::filesystem::create_directories(dir);
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", dir.wstring().c_str());
  }
  ~TempHomeScope() {
    config::IVerge::clear_temp_tun_fallback();
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
};

struct MonitorScope {
  core::CoreMonitor& m = core::CoreMonitor::instance();
  MonitorScope() {
    m.set_hooks({});
    m.set_params({});
    m.reset_state();
  }
  ~MonitorScope() {
    m.set_hooks({});
    m.set_params({});
    m.reset_state();
  }
};

std::wstring unique_test_pipe(const char* tag) {
  static std::atomic<int> s_seq{1};
  wchar_t buf[160];
  swprintf_s(buf, L"\\\\.\\pipe\\clashwin-b317-%hs-%lu-%d", tag,
             ::GetCurrentProcessId(), s_seq.fetch_add(1));
  return buf;
}

std::string build_pe64_image(size_t total_bytes = 5u * 1024u * 1024u,
                             uint16_t machine = 0x8664,
                             uint16_t characteristics = 0x0022,
                             uint16_t opt_magic = 0x020B,
                             uint16_t num_sections = 1) {
  std::string img(total_bytes, '\0');
  if (total_bytes < 512) return img;
  img[0] = 'M';
  img[1] = 'Z';
  const uint32_t pe_off = 0x80;
  std::memcpy(&img[0x3C], &pe_off, sizeof(pe_off));
  img[pe_off + 0] = 'P';
  img[pe_off + 1] = 'E';
  img[pe_off + 2] = '\0';
  img[pe_off + 3] = '\0';
  const uint16_t opt_hdr_size = 240;
  std::memcpy(&img[pe_off + 4], &machine, 2);
  std::memcpy(&img[pe_off + 6], &num_sections, 2);
  std::memcpy(&img[pe_off + 20], &opt_hdr_size, 2);
  std::memcpy(&img[pe_off + 22], &characteristics, 2);
  std::memcpy(&img[pe_off + 24], &opt_magic, 2);
  const size_t sec_off = pe_off + 24 + opt_hdr_size;
  if (sec_off + 40 <= img.size()) {
    std::memcpy(&img[sec_off], ".text\0\0\0", 8);
  }
  return img;
}

void write_binary_file(const std::filesystem::path& p, const std::string& data) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

}  // namespace

// ============================================================================
// 1. BUG-319: PE64 完整性校验器 + IoUnavailable 保锁文件 vs Corrupted 清理
// ============================================================================
UT_TEST(bug319_pe64_validator_and_custom_core_inspection) {
  TempHomeScope home;
  using core::CoreBinaryValidationKind;
  using core::CoreUpdater;

  // 缺失文件 -> IoUnavailable（对齐 Mini fs::metadata 失败语义）
  const auto missing = CoreUpdater::inspect_mihomo_pe_binary(
      (home.dir / "missing.exe").wstring());
  UT_CHECK(missing.kind == CoreBinaryValidationKind::IoUnavailable);

  // 目录路径（非普通文件）-> Corrupted
  const auto dir_as_exe = home.dir / "dir_not_file.exe";
  std::filesystem::create_directories(dir_as_exe);
  UT_CHECK(CoreUpdater::inspect_mihomo_pe_binary(dir_as_exe.wstring()).kind ==
           CoreBinaryValidationKind::Corrupted);

  // < 5 MiB 截断文件 -> Corrupted
  const auto truncated_path = home.dir / "small.exe";
  write_binary_file(truncated_path, build_pe64_image(1024 * 1024));
  UT_CHECK(CoreUpdater::inspect_mihomo_pe_binary(truncated_path.wstring()).kind ==
           CoreBinaryValidationKind::Corrupted);

  // 非 MZ 头 -> Corrupted
  const auto bad_mz_path = home.dir / "bad_mz.exe";
  {
    std::string bad = build_pe64_image();
    bad[0] = 'P';
    bad[1] = 'K';
    write_binary_file(bad_mz_path, bad);
  }
  UT_CHECK(CoreUpdater::inspect_mihomo_pe_binary(bad_mz_path.wstring()).kind ==
           CoreBinaryValidationKind::Corrupted);

  // x86 (0x014c) / 非可执行特征位 / PE32 (0x010B) -> 全部 Corrupted
  const auto x86_path = home.dir / "x86.exe";
  write_binary_file(x86_path, build_pe64_image(5u * 1024u * 1024u, 0x014C));
  UT_CHECK(CoreUpdater::inspect_mihomo_pe_binary(x86_path.wstring()).kind ==
           CoreBinaryValidationKind::Corrupted);

  const auto noexec_path = home.dir / "noexec.exe";
  write_binary_file(noexec_path,
                    build_pe64_image(5u * 1024u * 1024u, 0x8664, 0x2000));
  UT_CHECK(CoreUpdater::inspect_mihomo_pe_binary(noexec_path.wstring()).kind ==
           CoreBinaryValidationKind::Corrupted);

  const auto pe32_path = home.dir / "pe32.exe";
  write_binary_file(pe32_path,
                    build_pe64_image(5u * 1024u * 1024u, 0x8664, 0x0022, 0x010B));
  UT_CHECK(CoreUpdater::inspect_mihomo_pe_binary(pe32_path.wstring()).kind ==
           CoreBinaryValidationKind::Corrupted);

  // 合法 5 MiB x64 PE32+ -> Valid
  const auto cores_dir = home.dir / "cores";
  const auto custom_core = cores_dir / "win-mihomo.exe";
  write_binary_file(custom_core, build_pe64_image());
  const auto valid_res =
      CoreUpdater::inspect_mihomo_pe_binary(custom_core.wstring());
  UT_CHECK(valid_res.is_valid());
  const auto resolved_ok =
      CoreUpdater::resolve_usable_custom_core(cores_dir.wstring());
  UT_CHECK(resolved_ok.has_value());

  // 独占锁占用 -> IoUnavailable：resolve_usable_custom_core 回退空但不删除文件！
  {
    HANDLE hLock = ::CreateFileW(custom_core.c_str(), GENERIC_READ | GENERIC_WRITE,
                                 /*dwShareMode=*/0, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    UT_CHECK(hLock != INVALID_HANDLE_VALUE);
    if (hLock != INVALID_HANDLE_VALUE) {
      const auto locked_res =
          CoreUpdater::inspect_mihomo_pe_binary(custom_core.wstring());
      UT_CHECK(locked_res.kind == CoreBinaryValidationKind::IoUnavailable);
      const auto resolved_locked =
          CoreUpdater::resolve_usable_custom_core(cores_dir.wstring());
      UT_CHECK(!resolved_locked.has_value());
      ::CloseHandle(hLock);
    }
  }
  UT_CHECK(std::filesystem::exists(custom_core));

  // 损坏自定义内核 -> resolve_usable_custom_core 自动清理删除损坏文件
  write_binary_file(custom_core, "CORRUPT_STUB");
  const auto resolved_corrupt =
      CoreUpdater::resolve_usable_custom_core(cores_dir.wstring());
  UT_CHECK(!resolved_corrupt.has_value());
  UT_CHECK(!std::filesystem::exists(custom_core));
}

// ============================================================================
// 2. BUG-346: .staging 原子升级 + 损坏包保护原核 + wait_cores_exit(3000)
// ============================================================================
UT_TEST(bug346_atomic_staging_upgrade_and_wait_cores_exit) {
  TempHomeScope home;
  using core::CoreOps;
  using core::CoreUpdater;
  using core::GithubRelease;
  using core::ICoreUpdateDownloader;
  using system::DiagnosticBundle;

  struct LocalZipDownloader : ICoreUpdateDownloader {
    std::wstring zip_file;
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
      std::filesystem::copy_file(
          zip_file, dest_path,
          std::filesystem::copy_options::overwrite_existing, ec);
      return ec ? Result<void>::err(Error{2, ec.message()})
                : Result<void>::ok();
    }
  };

  struct UpdaterResetGuard {
    UpdaterResetGuard() {
      CoreUpdater::instance().set_core_api_client(nullptr);
      CoreUpdater::instance().reset_lifecycle_ops();
    }
    ~UpdaterResetGuard() {
      CoreUpdater::instance().set_core_api_client(nullptr);
      CoreUpdater::instance().reset_lifecycle_ops();
    }
  } reset_guard;

  const std::filesystem::path target_core =
      std::filesystem::path(util::dirs::cores_dir()) / L"win-mihomo.exe";
  const std::filesystem::path staging_core =
      std::filesystem::path(util::dirs::cores_dir()) / L"win-mihomo.exe.staging";
  write_binary_file(target_core, build_pe64_image(5u * 1024u * 1024u + 4096u));

  // (A) 压缩包内二进制不足 5 MiB -> 校验拦截，staging 清理，原核完好保留
  const auto bad_zip = home.dir / "bad_core.zip";
  UT_CHECK(DiagnosticBundle::write_zip_file(
               bad_zip.wstring(), {{"mini-mihomo.exe", "TINY_INVALID_EXE"}})
               .is_ok());
  auto dl = std::make_shared<LocalZipDownloader>();
  dl->zip_file = bad_zip.wstring();
  CoreUpdater::instance().set_downloader(dl);

  uint32_t wait_arg_seen = 0;
  CoreOps ops;
  ops.stop = [] { return Result<void>::ok(); };
  ops.start = [] { return Result<void>::ok(); };
  ops.wait_cores_exit = [&](uint32_t ms) {
    wait_arg_seen = ms;
    return true;
  };
  CoreUpdater::instance().set_lifecycle_ops(ops);

  GithubRelease rel;
  rel.tag_name = "v1.19.0";
  rel.assets = {{"mihomo-windows-amd64-v1.19.0.zip", "https://mock/core.zip"}};

  auto bad_res = CoreUpdater::instance().start_core_upgrade(rel);
  UT_CHECK(bad_res.is_err());
  UT_CHECK(!std::filesystem::exists(staging_core));
  UT_CHECK(CoreUpdater::inspect_mihomo_pe_binary(target_core.wstring()).is_valid());
  UT_CHECK_EQ(std::filesystem::file_size(target_core),
              5u * 1024u * 1024u + 4096u);

  // (B) 合法 6 MiB PE64 压缩包 -> wait_cores_exit(3000) 调用 + 原子替换成功
  wait_arg_seen = 0;
  const auto good_zip = home.dir / "good_core.zip";
  UT_CHECK(DiagnosticBundle::write_zip_file(
               good_zip.wstring(),
               {{"mini-mihomo.exe", build_pe64_image(6u * 1024u * 1024u)}})
               .is_ok());
  dl->zip_file = good_zip.wstring();
  auto good_res = CoreUpdater::instance().start_core_upgrade(rel);
  UT_CHECK(good_res.is_ok());
  UT_CHECK_EQ(wait_arg_seen, 3000u);
  UT_CHECK(!std::filesystem::exists(staging_core));
  UT_CHECK(CoreUpdater::inspect_mihomo_pe_binary(target_core.wstring()).is_valid());
  UT_CHECK_EQ(std::filesystem::file_size(target_core), 6u * 1024u * 1024u);
}

// ============================================================================
// 3. BUG-324 + BUG-329 + BUG-330 + BUG-351: PipeHttp 超时、锁外开管与陈旧连接重试
// ============================================================================
UT_TEST(bug324_329_330_351_pipe_http_timeouts_lockfree_open_and_stale_retry) {
  using mihomo::HttpErrc;
  using mihomo::MihomoClient;
  using mihomo::PipeHttpClient;
  using mihomo::PipeHttpConfig;

  static_assert(MihomoClient::kDefaultTimeout == 5000);
  const PipeHttpConfig defaults{};
  UT_CHECK_EQ(defaults.pool_wait_timeout_ms, 2000);
  UT_CHECK_EQ(defaults.default_timeout_ms, 5000);
  UT_CHECK_EQ(defaults.pipe_wait_ms, 100);
  UT_CHECK_EQ(defaults.busy_retry_delay_ms, 80);
  UT_CHECK_EQ(defaults.busy_retry_count, 24);

  // 锁外 open_pipe 失败时 PermitGuard 必归还 in_use_（max_connections=1 连续两次均报 ConnectFailed 而非 PoolFull）
  {
    PipeHttpConfig cfg;
    cfg.max_connections = 1;
    cfg.open_retry_count = 1;
    cfg.busy_retry_count = 1;
    cfg.busy_retry_delay_ms = 5;
    cfg.pool_wait_timeout_ms = 80;
    PipeHttpClient c(cfg);
    UT_CHECK(c.connect(unique_test_pipe("missing")).is_ok());
    auto r1 = c.send("GET", "/version", {}, "", 200);
    auto r2 = c.send("GET", "/version", {}, "", 200);
    UT_CHECK(r1.is_err());
    UT_CHECK(r2.is_err());
    UT_CHECK_EQ(r1.error().code, static_cast<int>(HttpErrc::PipeNotFound));
    UT_CHECK_EQ(r2.error().code, static_cast<int>(HttpErrc::PipeNotFound));
  }

  // 陈旧池化连接在服务端读取请求头后立即断开（PeekNamedPipe 放行、ReadFile 断管）：
  // 幂等 GET 自动开新管透明单次重试成功；非幂等 POST 绝不自动重试。
  {
    const std::wstring pipe = unique_test_pipe("stale-retry");
    std::atomic<bool> drop_next_second_req_on_conn{true};
    std::atomic<bool> stop_srv{false};

    auto serve_conn = [&](HANDLE hPipe) {
      mockpipe::Request req1;
      if (mockpipe::mo_read_request(hPipe, &req1)) {
        const std::string resp =
            "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
        mockpipe::mo_write(hPipe, resp.data(), static_cast<DWORD>(resp.size()),
                           2000);
      }
      // 同一池化连接上的第 2 个请求：模拟内核重启/对端半关导致断管无响应
      mockpipe::Request req2;
      if (mockpipe::mo_read_request(hPipe, &req2)) {
        if (drop_next_second_req_on_conn.load()) {
          ::DisconnectNamedPipe(hPipe);
          ::CloseHandle(hPipe);
          return;
        }
        const std::string resp2 =
            "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nfresh";
        mockpipe::mo_write(hPipe, resp2.data(),
                           static_cast<DWORD>(resp2.size()), 2000);
      }
      ::DisconnectNamedPipe(hPipe);
      ::CloseHandle(hPipe);
    };

    // 预创建首个监听实例避免竞态
    auto make_instance = [&]() -> HANDLE {
      return ::CreateNamedPipeW(
          pipe.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 8, 16384, 16384, 0,
          nullptr);
    };
    HANDLE hFirst = make_instance();
    UT_CHECK(hFirst != INVALID_HANDLE_VALUE);

    std::thread accept_thread([&] {
      std::vector<std::thread> workers;
      HANDLE hCur = hFirst;
      while (!stop_srv.load() && hCur != INVALID_HANDLE_VALUE) {
        OVERLAPPED ov{};
        ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL ok = ::ConnectNamedPipe(hCur, &ov);
        DWORD gle = ::GetLastError();
        bool connected = false;
        if (ok || gle == ERROR_PIPE_CONNECTED) {
          connected = true;
        } else if (gle == ERROR_IO_PENDING) {
          while (!stop_srv.load()) {
            if (::WaitForSingleObject(ov.hEvent, 30) == WAIT_OBJECT_0) {
              connected = true;
              break;
            }
          }
          if (!connected) ::CancelIoEx(hCur, &ov);
        }
        ::CloseHandle(ov.hEvent);
        if (!connected) {
          ::CloseHandle(hCur);
          break;
        }
        HANDLE hAccepted = hCur;
        hCur = stop_srv.load() ? INVALID_HANDLE_VALUE : make_instance();
        workers.emplace_back(serve_conn, hAccepted);
      }
      if (hCur != INVALID_HANDLE_VALUE) ::CloseHandle(hCur);
      for (auto& t : workers) {
        if (t.joinable()) t.join();
      }
    });

    PipeHttpConfig cfg;
    cfg.busy_retry_delay_ms = 10;
    PipeHttpClient client(cfg);
    UT_CHECK(client.connect(pipe).is_ok());

    // 第 1 次 GET：建立连接 #1 并归还空闲池
    auto g1 = client.send("GET", "/version", {}, "", 2000);
    UT_CHECK(g1.is_ok());
    UT_CHECK_EQ(client.idle_count(), 1u);

    // 第 2 次 GET：复用连接 #1 遭遇对端断管 → 自动单次重连连接 #2 成功！
    auto g2 = client.send("GET", "/version", {}, "", 2000);
    UT_CHECK(g2.is_ok());
    if (g2.is_ok()) {
      UT_CHECK_EQ(g2.value().status, 200);
    }
    UT_CHECK_EQ(client.connections_created(), 2u);

    // 第 3 次 POST：复用连接 #2 再次遭遇对端断管 → 非幂等方法严禁自动重发
    auto p3 = client.send("POST", "/configs", {}, "{}", 2000);
    UT_CHECK(p3.is_err());
    UT_CHECK_EQ(client.connections_created(), 2u);

    stop_srv.store(true);
    client.clear_pool();
    if (accept_thread.joinable()) accept_thread.join();
  }
}

// ============================================================================
// 4. BUG-325 + BUG-347: PipeWebSocket 有界 close 超时 + WS 退避 + kRefreshClash
// ============================================================================
UT_TEST(bug325_347_pipe_ws_bounded_close_backoff_and_refresh_clash) {
  using mihomo::PipeWebSocket;
  using mihomo::PipeWsCallbacks;
  using mihomo::WsManager;
  using mihomo::compute_ws_reconnect_delay_ms;

  // (A) 退避公式边界与抖动不变量（1s..15s，上界恒 <= 15000ms）
  UT_CHECK_EQ(compute_ws_reconnect_delay_ms(0, 0.5), 1000u);
  UT_CHECK_EQ(compute_ws_reconnect_delay_ms(0, 0.0), 800u);
  UT_CHECK_EQ(compute_ws_reconnect_delay_ms(0, 1.0), 1200u);
  UT_CHECK_EQ(compute_ws_reconnect_delay_ms(3, 0.5), 8000u);
  UT_CHECK_EQ(compute_ws_reconnect_delay_ms(4, 1.0), 15000u);
  UT_CHECK_EQ(compute_ws_reconnect_delay_ms(99, 9.9), 15000u);
  UT_CHECK_EQ(compute_ws_reconnect_delay_ms(0, -5.0), 800u);

  // (B) PipeWebSocket::close(500u) 在小缓冲区写满阻塞管道上有界返回不挂死
  {
    const std::wstring pipe = unique_test_pipe("ws-wedged");
    // Out/In buffer 设为极小并由客户端先塞满发送缓冲区，模拟服务端假死不读管道
    HANDLE hSrv = ::CreateNamedPipeW(
        pipe.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 64, 64, 0, nullptr);
    UT_CHECK(hSrv != INVALID_HANDLE_VALUE);

    std::atomic<bool> release_srv{false};
    std::thread srv_thread([&] {
      OVERLAPPED ov{};
      ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
      BOOL ok = ::ConnectNamedPipe(hSrv, &ov);
      if (!ok && ::GetLastError() == ERROR_IO_PENDING) {
        ::WaitForSingleObject(ov.hEvent, 3000);
      }
      ::CloseHandle(ov.hEvent);

      mockpipe::Request req;
      if (mockpipe::mo_read_request(hSrv, &req)) {
        auto it = req.headers.find("sec-websocket-key");
        const std::string accept =
            mihomo::ws_compute_accept(it != req.headers.end() ? it->second : "");
        const std::string hs =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " +
            accept + "\r\n\r\n";
        mockpipe::mo_write(hSrv, hs.data(), static_cast<DWORD>(hs.size()), 2000);
      }
      while (!release_srv.load()) {
        ::Sleep(20);
      }
      ::DisconnectNamedPipe(hSrv);
      ::CloseHandle(hSrv);
    });

    PipeWebSocket ws;
    PipeWsCallbacks cbs;
    UT_CHECK(ws.connect(pipe, "/traffic", "", cbs, 2000).is_ok());
    const auto t0 = std::chrono::steady_clock::now();
    ws.close(500u);
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0)
            .count();
    UT_CHECK(elapsed_ms < 2000);
    UT_CHECK(!ws.is_open());

    release_srv.store(true);
    if (srv_thread.joinable()) srv_thread.join();
  }

  // (C) WsManager 订阅 kRefreshClash：重连退避累计后收到 refresh_clash 立即归零
  {
    WsManager mgr;
    mgr.configure(unique_test_pipe("ws-missing"), "");
    uint64_t sub = mgr.subscribe_traffic([](const std::string&) {});
    for (int i = 0;
         i < 60 &&
         mgr.channel_reconnect_attempt_for_test(WsManager::kTraffic) == 0;
         ++i) {
      ::Sleep(20);
    }
    UT_CHECK(mgr.channel_reconnect_attempt_for_test(WsManager::kTraffic) >= 1u);

    events::EventBus::refresh_clash();
    events::EventBus::instance().advance_for_test(500);
    events::EventBus::instance().flush_pending();
    // flush_reconnect_backoff 同步将 reconnect_attempt 置 0
    mgr.flush_reconnect_backoff();
    UT_CHECK_EQ(mgr.channel_reconnect_attempt_for_test(WsManager::kTraffic), 0u);
    mgr.unsubscribe(sub);
  }
}

// ============================================================================
// 5. BUG-328 + BUG-352: 服务互斥锁、ImagePath 解析与临时 TUN 降级快照保护
// ============================================================================
UT_TEST(bug328_352_service_mutex_image_path_and_temp_tun_fallback) {
  TempHomeScope home;
  using core::CoreManager;
  using core::service_mode::ScmServiceRegistration;
  using core::service_mode::extract_service_exe_from_image_path;
  using core::service_mode::service_operation_mutex;

  // (A) ImagePath 解析：兼容 \??\ 前缀、双引号带参、无引号带参
  UT_CHECK_EQ(extract_service_exe_from_image_path(
                  L"\\??\\\"C:\\Program Files\\Clash Win\\clash-verge-service.exe\" --run"),
              std::wstring(L"C:\\Program Files\\Clash Win\\clash-verge-service.exe"));
  UT_CHECK_EQ(extract_service_exe_from_image_path(
                  L"C:\\ClashWin\\clash-verge-service.exe --service"),
              std::wstring(L"C:\\ClashWin\\clash-verge-service.exe"));

  // (B) service_operation_mutex：同线程递归重入 + 跨线程互斥串行化
  {
    std::unique_lock<std::recursive_mutex> lk1(service_operation_mutex());
    std::unique_lock<std::recursive_mutex> lk2(service_operation_mutex());
    UT_CHECK(lk1.owns_lock() && lk2.owns_lock());

    std::atomic<bool> other_acquired{false};
    std::thread contender([&] {
      std::lock_guard<std::recursive_mutex> g(service_operation_mutex());
      other_acquired.store(true);
    });
    ::Sleep(30);
    UT_CHECK(!other_acquired.load());
    lk2.unlock();
    lk1.unlock();
    contender.join();
    UT_CHECK(other_acquired.load());
  }

  // (C) TempTunFallbackSnapshot：临时内存降级期间任意 save_file() 绝不篡改磁盘 enable_tun_mode: true
  auto& verge_draft = config::Config::instance().verge();
  verge_draft.edit_draft([](config::IVerge& v) {
    v.enable_tun_mode = true;
    v.enable_system_proxy = false;
  });
  verge_draft.apply();
  UT_CHECK(verge_draft.data_arc()->save_file().is_ok());

  CoreManager::instance().fallback_to_system_proxy(/*persist_to_disk=*/false);
  UT_CHECK(config::IVerge::has_temp_tun_fallback());
  UT_CHECK_EQ(verge_draft.latest_arc()->enable_tun_mode.value_or(true), false);
  UT_CHECK_EQ(verge_draft.latest_arc()->enable_system_proxy.value_or(false), true);

  // 触发无关配置写盘（如切换主题或窗口设置）
  verge_draft.edit_draft([](config::IVerge& v) { v.enable_always_on_top = true; });
  verge_draft.apply();
  UT_CHECK(verge_draft.data_arc()->save_file().is_ok());

  const YAML::Node disk_after_temp =
      YAML::LoadFile(std::filesystem::path(util::dirs::verge_path()).string());
  UT_CHECK(disk_after_temp["enable_tun_mode"].as<bool>(false) == true);
  UT_CHECK(disk_after_temp["enable_system_proxy"].as<bool>(true) == false);

  // 用户显式切换 TUN / 系统代理 → clear_temp_tun_fallback 并落盘真实新值
  config::IVerge::clear_temp_tun_fallback();
  UT_CHECK(!config::IVerge::has_temp_tun_fallback());

  // (D) CoreManager::start_auto 三态 SCM 注册分支：
  //     InstalledAndEnabled 瞬态未就绪 -> 临时降级（盘上仍 true）
  //     DisabledOrBroken -> 永久降级（盘上写回 false）
  struct MockSvcRunner : system::IServiceCommandRunner {
    ScmServiceRegistration reg = ScmServiceRegistration::InstalledAndEnabled;
    int installer_calls = 0;
    Result<void> run_installer(bool) override {
      ++installer_calls;
      return Result<void>::err(Error{1, "uac cancelled"});
    }
    bool is_installed() override {
      return reg != ScmServiceRegistration::NotInstalled;
    }
    ScmServiceRegistration inspect_registration() override { return reg; }
    Result<void> start_service() override {
      return Result<void>::err(Error{2, "transient start timeout"});
    }
    Result<void> stop_service() override { return Result<void>::ok(); }
    bool wait_for_ready(int) override { return false; }
    bool is_available() override { return false; }
  };

  auto prev_runner = system::get_service_runner();
  auto mock_runner = std::make_shared<MockSvcRunner>();
  system::set_service_runner(mock_runner);

  CoreManager::instance().stop();
  int sidecar_starts = 0;
  CoreManager::StartAutoTestHooks hooks;
  hooks.is_admin_fn = [] { return false; };
  hooks.service_ready_wait_ms = 20;
  hooks.start_sidecar_fn = [&](const std::wstring&, const std::wstring&) {
    ++sidecar_starts;
    return Result<void>::ok();
  };
  CoreManager::instance().set_start_auto_test_hooks(hooks);

  struct RunnerGuard {
    std::shared_ptr<system::IServiceCommandRunner> prev;
    ~RunnerGuard() {
      CoreManager::instance().stop();
      CoreManager::instance().reset_start_auto_test_hooks();
      system::set_service_runner(prev);
      config::IVerge::clear_temp_tun_fallback();
    }
  } rg{prev_runner};

  // Case 1: InstalledAndEnabled -> 不弹安装器，走临时内存降级，盘上保持 enable_tun_mode=true
  verge_draft.edit_draft([](config::IVerge& v) {
    v.enable_tun_mode = true;
    v.enable_system_proxy = false;
  });
  verge_draft.apply();
  UT_CHECK(verge_draft.data_arc()->save_file().is_ok());

  mock_runner->reg = ScmServiceRegistration::InstalledAndEnabled;
  UT_CHECK(CoreManager::instance().start_auto(L"clash-win.yaml").is_ok());
  UT_CHECK_EQ(mock_runner->installer_calls, 0);
  UT_CHECK_EQ(sidecar_starts, 1);
  UT_CHECK(config::IVerge::has_temp_tun_fallback());
  {
    const YAML::Node on_disk =
        YAML::LoadFile(std::filesystem::path(util::dirs::verge_path()).string());
    UT_CHECK(on_disk["enable_tun_mode"].as<bool>(false) == true);
  }

  // Case 2: DisabledOrBroken -> 弹修复安装失败后走永久降级，盘上写回 enable_tun_mode=false
  CoreManager::instance().stop();
  config::IVerge::clear_temp_tun_fallback();
  verge_draft.edit_draft([](config::IVerge& v) {
    v.enable_tun_mode = true;
    v.enable_system_proxy = false;
  });
  verge_draft.apply();
  UT_CHECK(verge_draft.data_arc()->save_file().is_ok());

  mock_runner->reg = ScmServiceRegistration::DisabledOrBroken;
  UT_CHECK(CoreManager::instance().start_auto(L"clash-win.yaml").is_ok());
  UT_CHECK_EQ(mock_runner->installer_calls, 1);
  UT_CHECK_EQ(sidecar_starts, 2);
  UT_CHECK(!config::IVerge::has_temp_tun_fallback());
  {
    const YAML::Node on_disk =
        YAML::LoadFile(std::filesystem::path(util::dirs::verge_path()).string());
    UT_CHECK(on_disk["enable_tun_mode"].as<bool>(true) == false);
  }
}

// ============================================================================
// 6. BUG-331 + BUG-334 + BUG-335: 无锁服务优先停核、Timer RAII 与伪节点过滤恢复
// ============================================================================
UT_TEST(bug331_334_335_lifecycle_service_quit_timer_raii_and_dummy_restore) {
  TempHomeScope home;

  // (A) BUG-331: reset_hooks 默认绝不设置 stop_service（退出零 UAC 弹窗）
  auto& lc = app::Lifecycle::instance();
  lc.reset();
  UT_CHECK(lc.hooks().stop_service == nullptr);
  UT_CHECK(lc.hooks().is_service_or_tun_mode != nullptr);
  UT_CHECK(lc.hooks().stop_core_by_service_direct != nullptr);

  // 服务优先停核绕开阻塞 stop_core
  {
    app::QuitHooks qh;
    int stop_core_calls = 0;
    int direct_svc_calls = 0;
    int cleanup_calls = 0;
    qh.reset_sysproxy = [] { return Result<void>::ok(); };
    qh.is_service_or_tun_mode = [] { return true; };
    qh.stop_core_by_service_direct = [&] {
      ++direct_svc_calls;
      return Result<void>::ok();
    };
    qh.wait_cores_exit = [](uint32_t) { return true; };
    qh.cleanup_after_service_stop = [&] { ++cleanup_calls; };
    qh.stop_core = [&] {
      ++stop_core_calls;
      return Result<void>::err(Error{1, "should not be called"});
    };
    qh.kill_cores = [] { return std::vector<uint32_t>{}; };
    lc.set_hooks(qh);
    UT_CHECK(lc.clean_async());
    UT_CHECK_EQ(direct_svc_calls, 1);
    UT_CHECK_EQ(cleanup_calls, 1);
    UT_CHECK_EQ(stop_core_calls, 0);
  }

  // 模式漂移兜底：Sidecar 停完后仍有残留服务内核 -> 自动补发 stop_core_by_service_direct
  {
    lc.reset();
    app::QuitHooks qh;
    int direct_svc_calls = 0;
    qh.reset_sysproxy = [] { return Result<void>::ok(); };
    qh.is_service_or_tun_mode = [] { return false; };
    qh.stop_core = [] { return Result<void>::ok(); };
    qh.are_cores_running = [] { return true; };
    qh.is_service_installed = [] { return true; };
    qh.stop_core_by_service_direct = [&] {
      ++direct_svc_calls;
      return Result<void>::ok();
    };
    qh.wait_cores_exit = [](uint32_t) { return true; };
    qh.kill_cores = [] { return std::vector<uint32_t>{}; };
    lc.set_hooks(qh);
    UT_CHECK(lc.clean_async());
    UT_CHECK_EQ(direct_svc_calls, 1);
    lc.reset();
  }

  // (B) BUG-334: ProfileTimer update_fn / event_emitter 抛异常时必释放 switching_lock 与 running
  {
    auto& timer = core::ProfileTimer::instance();
    timer.reset_for_test();
    core::profile_switching_lock().store(false);
    int64_t fake_now = 10000;
    timer.set_now_ms([&] { return fake_now; });
    timer.set_intervals_provider([] {
      return std::map<std::string, std::pair<uint64_t, int64_t>>{
          {"uid_throw", {60, 0}}};
    });
    timer.set_update_fn([](const std::string&) -> bool {
      throw std::runtime_error("network parser exploded");
    });
    timer.set_event_emitter([](const std::string&, bool is_start) {
      if (!is_start) throw std::runtime_error("emitter exploded");
    });
    UT_CHECK(!timer.run_now("uid_throw"));
    UT_CHECK(!core::profile_switching_lock().load());
    UT_CHECK(!timer.running("uid_throw"));
    timer.reset_for_test();
  }

  // (C) BUG-335: 实时快照为伪节点时过滤丢弃，回退读取 profiles.yaml 持久化真实节点
  {
    auto& profiles = config::Config::instance().profiles();
    profiles.edit_draft([](config::IProfiles& d) {
      d.current = "p_real";
      config::PrfItem it;
      it.uid = "p_real";
      it.selected = std::vector<config::PrfSelected>{
          config::PrfSelected{"PROXY", "HK-Real-01"}};
      d.items = std::vector<config::PrfItem>{it};
    });
    profiles.apply();

    struct DummySnapshotApi : core::ICoreApiClient {
      std::string current_now = "剩余流量：88.8 GB";
      std::string restored;
      Result<mihomo::BaseConfig> get_base_config() override {
        return Result<mihomo::BaseConfig>::ok({});
      }
      Result<mihomo::ProxyItem> get_group_by_name(const std::string& g) override {
        mihomo::ProxyItem item;
        item.name = g;
        item.now = current_now;
        item.all = {"剩余流量：88.8 GB", "HK-Real-01", "JP-Real-02"};
        return Result<mihomo::ProxyItem>::ok(item);
      }
      Result<void> select_node_for_group(const std::string&,
                                         const std::string& node) override {
        restored = node;
        current_now = node;
        return Result<void>::ok();
      }
      Result<void> reload_config(bool, const std::string&) override {
        return Result<void>::ok();
      }
    } api;

    auto& mgr = core::CoreManager::instance();
    UT_CHECK(!mgr.snapshot_proxy_group_now(api).has_value());
    const auto persisted = mgr.read_persisted_proxy_selection("PROXY");
    UT_CHECK(persisted.has_value() && *persisted == "HK-Real-01");

    core::CoreOps ops;
    ops.stop = [] { return Result<void>::ok(); };
    ops.start = [] { return Result<void>::ok(); };
    core::RestoreTimings rt{50, 5, 50, 5, 50, 5};
    UT_CHECK(mgr.apply_config(api, "clash-win.yaml", ops, rt).is_ok());
    UT_CHECK_EQ(api.restored, "HK-Real-01");

    // 若持久化节点也是伪节点，则过滤为 nullopt
    profiles.edit_draft([](config::IProfiles& d) {
      if (d.items && !d.items->empty()) {
        (*d.items)[0].selected = std::vector<config::PrfSelected>{
            config::PrfSelected{"PROXY", "套餐到期：2099-01-01"}};
      }
    });
    profiles.apply();
    UT_CHECK(!mgr.read_persisted_proxy_selection("PROXY").has_value());
  }
}

// ============================================================================
// 7. BUG-317 + BUG-345: 3 路 TCP:53 并发竞速 600ms 封顶 + >=1400ms DNS 预算
// ============================================================================
UT_TEST(bug317_345_parallel_tcp53_race_and_dns_budget) {
  using core::CoreMonitor;

  // (A) timeout_secs=1 强制抬升至 2s 总预算；TCP:53 竞速封顶 600ms，留给 DNS >= 1400ms
  {
    int64_t clock = 0;
    int captured_race_ms = 0;
    int captured_dns_ms = 0;
    size_t captured_ip_count = 0;
    CoreMonitor::PhysicalNetProbeOps ops;
    ops.now_ms = [&] { return clock; };
    ops.tcp_race_connect = [&](const std::vector<const char*>& ips,
                               uint16_t port, int timeout_ms) {
      UT_CHECK_EQ(port, 53);
      captured_ip_count = ips.size();
      captured_race_ms = timeout_ms;
      clock += timeout_ms;  // 模拟 3 路 53 端口被防火墙静默丢包耗尽竞速窗口
      return false;
    };
    ops.dns_lookup = [&](const std::string& host, uint16_t port, int rem_ms) {
      UT_CHECK_EQ(host, "cp.cloudflare.com");
      UT_CHECK_EQ(port, 443);
      captured_dns_ms = rem_ms;
      return true;
    };

    UT_CHECK(CoreMonitor::probe_physical_network_with(
        /*timeout_secs=*/1, "https://cp.cloudflare.com/generate_204", ops));
    UT_CHECK_EQ(captured_ip_count, 3u);
    UT_CHECK_EQ(captured_race_ms, 600);
    UT_CHECK(captured_dns_ms >= 1400);
  }

  // (B) 真实本地回环非阻塞 select() 多路竞速：1 路监听成功立即胜出，关闭端口立即失败
  {
    WSADATA wsa{};
    (void)::WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET lsock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    UT_CHECK(lsock != INVALID_SOCKET);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    UT_CHECK(::bind(lsock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    int namelen = sizeof(addr);
    UT_CHECK(::getsockname(lsock, reinterpret_cast<sockaddr*>(&addr), &namelen) == 0);
    const uint16_t bound_port = ntohs(addr.sin_port);
    UT_CHECK(::listen(lsock, 4) == 0);

    std::thread accepter([lsock] {
      SOCKET cli = ::accept(lsock, nullptr, nullptr);
      if (cli != INVALID_SOCKET) ::closesocket(cli);
    });

    const auto t0 = std::chrono::steady_clock::now();
    const bool won =
        CoreMonitor::tcp_race_connect_ipv4({"127.0.0.1"}, bound_port, 600);
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0)
            .count();
    UT_CHECK(won);
    UT_CHECK(elapsed_ms < 500);

    accepter.join();
    ::closesocket(lsock);

    // 监听关闭后对同一回环端口竞速应迅速感知连接拒绝并返回 false
    UT_CHECK(!CoreMonitor::tcp_race_connect_ipv4({"127.0.0.1"}, bound_port, 150));
  }
}

// ============================================================================
// 8. BUG-318: 双计数器纯函数矩阵 + AUTO_SELECT_BUSY 零惩罚回退 + 品牌不变量
// ============================================================================
UT_TEST(bug318_dual_self_heal_counters_and_busy_rollback) {
  MonitorScope ms;
  using core::CoreMonitor;

  // 纯函数校验：3 次 API 死重启保留 auto_select_fail_count=3；第 5 次 FireAlert 归零
  int auto_c = 3, api_c = 3;
  UT_CHECK(CoreMonitor::evaluate_self_heal_counters(auto_c, api_c, false) ==
           CoreMonitor::SelfHealAction::RestartCore);
  UT_CHECK_EQ(auto_c, 3);
  UT_CHECK_EQ(api_c, 0);
  UT_CHECK(CoreMonitor::complete_restart_core_attempt(true, auto_c, api_c) ==
           CoreMonitor::SelfHealAction::None);
  UT_CHECK_EQ(auto_c, 3);

  auto_c = 5;
  api_c = 2;
  UT_CHECK(CoreMonitor::evaluate_self_heal_counters(auto_c, api_c, false) ==
           CoreMonitor::SelfHealAction::FireAlert);
  UT_CHECK_EQ(auto_c, 0);
  UT_CHECK_EQ(api_c, 0);

  // AUTO_SELECT_BUSY 错误返回：不计失败、回退冷却时间戳允许下一轮立即重试
  int64_t clock = 10000;
  bool return_busy = true;
  std::wstring alert_title, alert_body;
  CoreMonitor::Hooks h;
  h.core_alive = [] { return true; };
  h.network_online = [] { return true; };
  h.api_alive = [] { return false; };
  h.current_uid = [] { return "u1"; };
  h.now_ms = [&] { return clock; };
  h.auto_select_trigger = [&]() -> Result<bool> {
    if (return_busy) return Result<bool>::err(Error{-2, "AUTO_SELECT_BUSY"});
    return Result<bool>::err(Error{-1, "all dead"});
  };
  h.restart_core = [] { return Result<void>::err(Error{-1, "restart boom"}); };
  h.alert = [&](const std::wstring& title, const std::wstring& body) {
    alert_title = title;
    alert_body = body;
  };
  ms.m.set_hooks(h);

  ms.m.tick_once();
  ms.m.tick_once();
  UT_CHECK(ms.m.tick_once() == CoreMonitor::Action::Idle);
  UT_CHECK_EQ(ms.m.auto_select_fail_count(), 0);
  UT_CHECK_EQ(ms.m.last_heal_ms(), 0);  // 冷却已回退！

  // 仅过 1s（远小于 25s 冷却）即可重试，连续 3 轮触发重启失败终态警报，验证 "Clash Win" 品牌文案
  return_busy = false;
  for (int round = 1; round <= 3; ++round) {
    clock += (round == 1 ? 1000 : 30000);
    ms.m.tick_once();
    ms.m.tick_once();
    const auto act = ms.m.tick_once();
    if (round < 3) {
      UT_CHECK(act == CoreMonitor::Action::Idle);
    } else {
      UT_CHECK(act == CoreMonitor::Action::Alerted);
    }
  }
  UT_CHECK_EQ(alert_title, std::wstring(L"Clash Win - 网络警报"));
  UT_CHECK(alert_body.find(L"Clash Win") != std::wstring::npos);
  UT_CHECK(alert_body.find(L"Clash Mini") == std::wstring::npos);
}

// ============================================================================
// 9. BUG-321 + BUG-322 + BUG-344 + BUG-349: fromProbe 契约 + 0ms 置底排序 + 新鲜缓存复用
// ============================================================================
UT_TEST(bug321_322_344_349_delay_source_from_probe_and_zero_bottom_sort) {
  using core::AutoSelect;
  using core::AutoSelectHooks;
  using core::DelayManager;
  using core::DelayProbeHooks;
  using core::DelayResultSource;

  // (A) EventBus::backend_delay_results 载荷必须包含 fromProbe 字段
  {
    std::vector<bool> flags;
    uint64_t sub = events::EventBus::instance().subscribe(
        events::names::kBackendDelayResults, [&](const nlohmann::json& p) {
          if (p.contains("fromProbe") && p["fromProbe"].is_boolean()) {
            flags.push_back(p["fromProbe"].get<bool>());
          }
        });
    events::EventBus::backend_delay_results("PROXY", {{"A", 120}}, false);
    events::EventBus::backend_delay_results("PROXY", {{"A", 95}}, true);
    events::EventBus::instance().unsubscribe(sub);
    UT_CHECK_EQ(flags.size(), 2u);
    UT_CHECK(!flags[0]);
    UT_CHECK(flags[1]);
  }

  // (B) BUG-322 / BUG-349: 0ms 超时节点必须排在 4800ms 慢节点之后，绝不可霸占榜首
  {
    std::vector<std::pair<std::string, uint32_t>> rows = {
        {"Dead-A", 0}, {"Slow-HK", 4800}, {"Fast-TW", 85}, {"Dead-B", 0}};
    AutoSelect::sort_display_delays(rows, 1);
    UT_CHECK_EQ(rows[0].first, "Fast-TW");
    UT_CHECK_EQ(rows[1].first, "Slow-HK");
    UT_CHECK_EQ(rows[2].first, "Dead-A");
    UT_CHECK_EQ(rows[2].second, 0u);
    UT_CHECK_EQ(rows[3].first, "Dead-B");
    UT_CHECK_EQ(rows[3].second, 0u);

    AutoSelect::sort_display_delays(rows, 2);
    UT_CHECK_EQ(rows[0].first, "Dead-A");
    UT_CHECK_EQ(rows[1].first, "Dead-B");
    UT_CHECK_EQ(rows[2].first, "Fast-TW");
    UT_CHECK_EQ(rows[3].first, "Slow-HK");
  }

  // (C) BUG-344 + sort_type=0 解析：多节点前端子集命中 <30s 新鲜缓存 -> FreshMetricsCache (fromProbe=false)
  {
    int live_probes = 0;
    bool last_from_probe = true;
    DelayProbeHooks dh;
    dh.group_probe = [&](const std::string&) {
      ++live_probes;
      return std::optional{
          std::map<std::string, uint32_t>{{"Z-Node", 90}, {"A-Node", 220}}};
    };
    dh.emit_results = [&](const std::string&,
                          const std::vector<std::pair<std::string, uint32_t>>&,
                          bool from_probe) { last_from_probe = from_probe; };
    DelayManager::instance().set_hooks(std::move(dh));

    AutoSelectHooks ah;
    ah.is_dummy = [](const std::string&) { return false; };
    ah.saved_sort_type = [](const std::string&) { return std::optional<int>(2); };
    ah.fresh_metrics_subset =
        [](const std::vector<std::string>&)
        -> std::optional<std::vector<std::pair<std::string, uint32_t>>> {
      return std::vector<std::pair<std::string, uint32_t>>{{"Z-Node", 0},
                                                           {"A-Node", 180}};
    };
    AutoSelect::instance().set_hooks(std::move(ah));

    auto cached_out = AutoSelect::instance().trigger(
        "u1", std::vector<std::string>{"Z-Node", "A-Node"},
        /*sort_type=*/0, /*select=*/false, /*skip_wait_ready=*/true);
    UT_CHECK_EQ(live_probes, 0);
    UT_CHECK(cached_out.source == DelayResultSource::FreshMetricsCache);
    UT_CHECK(!last_from_probe);
    // sort_type=0 解析出 saved_sort_type=2（按名称排序）：A-Node 在 Z-Node 前
    UT_CHECK_EQ(cached_out.display[0].first, "A-Node");
    UT_CHECK_EQ(cached_out.display[1].first, "Z-Node");

    // 单节点子集测速强制穿透缓存实时拨测（fromProbe=true）
    auto single_out = AutoSelect::instance().trigger(
        "u1", std::vector<std::string>{"Z-Node"},
        /*sort_type=*/1, /*select=*/false, /*skip_wait_ready=*/true);
    UT_CHECK_EQ(live_probes, 1);
    UT_CHECK(single_out.source == DelayResultSource::LiveProbe);
    UT_CHECK(last_from_probe);
  }
}

// ============================================================================
// 10. BUG-336 + BUG-353 + BUG-359 + BUG-362: 手动切换三检查点、监管循环、健康广播与订阅合并
// ============================================================================
UT_TEST(bug336_353_359_362_manual_switch_supervisor_health_and_prf_merge) {
  MonitorScope ms;
  using core::CoreMonitor;

  // (A) BUG-336: notify_node_switched 命令 + 离线提前返回吸收 + 精确点击时间戳保留 + 拨测中途吸收
  cmd::CommandService::instance().install_all();
  int64_t clock = 5000;
  bool online = false;
  std::string active = "Node-1";
  bool node_ok = false;
  bool mid_probe_switch = false;

  CoreMonitor::Hooks h;
  h.core_alive = [] { return true; };
  h.network_online = [&] { return online; };
  h.api_alive = [] { return true; };
  h.active_node_name = [&] { return active; };
  h.probe_metrics_with_fallback = [&] {
    if (mid_probe_switch) {
      clock += 2000;  // 拨测耗时 2s 期间用户手动切了节点
      ms.m.notify_manual_node_switched();
    }
    return true;
  };
  h.node_healthy = [&] { return node_ok; };
  h.current_uid = [] { return "u1"; };
  h.auto_select_trigger = [] {
    return Result<bool>::err(Error{-1, "all dead"});
  };
  h.now_ms = [&] { return clock; };
  ms.m.set_hooks(h);

  // Checkpoint #1：离线态点击切换节点 -> 离线提前返回的 tick 依然吸收真实点击时间戳 5000
  UT_CHECK(cmd::CommandService::instance().invoke("notify_node_switched", {}).is_ok());
  ms.m.tick_once();
  UT_CHECK_EQ(ms.m.last_node_change_ms(), 5000);
  UT_CHECK(ms.m.in_switch_grace());

  // Checkpoint #2：t=12000 恢复在线并观测到 active 变为 Node-2 -> 保留手动点击时间戳 5000 不二次顺延！
  online = true;
  node_ok = true;
  ms.m.reset_state();
  clock = 5000;
  ms.m.notify_manual_node_switched();
  clock = 12000;
  active = "Node-2";
  ms.m.tick_once();
  UT_CHECK_EQ(ms.m.last_node_change_ms(), 5000);

  // Checkpoint #3：宽限期已过（t=40000），慢速拨测期间（t=42000）用户切节点 -> 不计入连续失败！
  clock = 40000;
  node_ok = false;
  mid_probe_switch = true;
  ms.m.tick_once();
  UT_CHECK_EQ(ms.m.last_node_change_ms(), 42000);
  UT_CHECK_EQ(ms.m.node_fail_count(), 0);
  mid_probe_switch = false;

  // (B) BUG-353: run_supervised_daemon 捕获未预期异常、调用重置并 2000ms 退避重启
  {
    int runs = 0;
    int resets = 0;
    std::vector<uint32_t> sleeps;
    bool stop = false;
    CoreMonitor::run_supervised_daemon(
        "TestDaemon", [&] { return stop; },
        [&](uint32_t ms_delay) { sleeps.push_back(ms_delay); },
        [&] { ++resets; },
        [&] {
          if (++runs == 1) throw std::runtime_error("simulated panic");
          stop = true;
        });
    UT_CHECK_EQ(runs, 2);
    UT_CHECK_EQ(resets, 1);
    UT_CHECK_EQ(sleeps.size(), 1u);
    UT_CHECK_EQ(sleeps[0], CoreMonitor::kSupervisorRestartDelayMs);
  }

  // (C) BUG-359: 网络健康状态去重广播序列（offline -> offline 去重 -> all_dead -> ok -> ok 去重）
  {
    ms.m.reset_state();
    std::vector<std::pair<std::string, std::string>> notices;
    uint64_t sub = events::EventBus::instance().subscribe(
        events::names::kNoticeMessage, [&](const nlohmann::json& p) {
          const std::string msg = p.value("message", "");
          if (msg.rfind("network_health::", 0) == 0) {
            notices.emplace_back(p.value("status", ""), msg);
          }
        });

    online = false;
    ms.m.tick_once();
    ms.m.tick_once();  // 连续离线去重
    UT_CHECK_EQ(notices.size(), 1u);
    UT_CHECK_EQ(notices[0].first, "warning");
    UT_CHECK_EQ(notices[0].second, "network_health::offline");

    // 恢复在线触发 heal 失败 -> network_health::all_dead
    online = true;
    node_ok = false;
    clock = 100000;
    ms.m.tick_once();
    UT_CHECK_EQ(notices.size(), 2u);
    UT_CHECK_EQ(notices[1].first, "warning");
    UT_CHECK_EQ(notices[1].second, "network_health::all_dead");

    // 节点恢复健康 -> network_health::ok（连续健康去重）
    node_ok = true;
    clock += 30000;
    ms.m.tick_once();
    ms.m.tick_once();
    events::EventBus::instance().unsubscribe(sub);
    UT_CHECK_EQ(notices.size(), 3u);
    UT_CHECK_EQ(notices[2].first, "info");
    UT_CHECK_EQ(notices[2].second, "network_health::ok");
  }

  // (D) BUG-362: PrfOption::merge_opt 全量保留全部 12 个字段（含关联子项与代理拉取配置）
  {
    config::PrfOption old_opt;
    old_opt.user_agent = "clash-verge/v2.0";
    old_opt.with_proxy = true;
    old_opt.self_proxy = false;
    old_opt.update_interval = 720;
    old_opt.timeout_seconds = 25;
    old_opt.danger_accept_invalid_certs = true;
    old_opt.allow_auto_update = false;
    old_opt.merge = "m_uid_1";
    old_opt.script = "s_uid_1";
    old_opt.rules = "r_uid_1";
    old_opt.proxies = "p_uid_1";
    old_opt.groups = "g_uid_1";

    config::PrfOption override_opt;
    override_opt.timeout_seconds = 45;  // 仅覆盖单项

    const auto merged =
        config::PrfOption::merge_opt(old_opt, override_opt)
            .value_or(config::PrfOption{});
    UT_CHECK_EQ(merged.user_agent.value_or(""), "clash-verge/v2.0");
    UT_CHECK_EQ(merged.with_proxy.value_or(false), true);
    UT_CHECK_EQ(merged.self_proxy.value_or(true), false);
    UT_CHECK_EQ(merged.update_interval.value_or(0), 720u);
    UT_CHECK_EQ(merged.timeout_seconds.value_or(0), 45u);
    UT_CHECK_EQ(merged.danger_accept_invalid_certs.value_or(false), true);
    UT_CHECK_EQ(merged.allow_auto_update.value_or(true), false);
    UT_CHECK_EQ(merged.merge.value_or(""), "m_uid_1");
    UT_CHECK_EQ(merged.script.value_or(""), "s_uid_1");
    UT_CHECK_EQ(merged.rules.value_or(""), "r_uid_1");
    UT_CHECK_EQ(merged.proxies.value_or(""), "p_uid_1");
    UT_CHECK_EQ(merged.groups.value_or(""), "g_uid_1");
  }
}
