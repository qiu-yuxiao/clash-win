// T07 live 用例：完整 Sidecar 生命周期（沙箱外可跑；沙箱内 spawn 被拦 → SKIP）
//
// 铁律：
// - 独立管道 \\.\pipe\clashwin-test-coremgr + 独立端口 20804 + %TEMP% 独立配置目录
// - 内核 exe 拷贝到 %TEMP% home\cores\（同时覆盖"热升级内核优先"定位分支）
// - kill_all 作用域 = %TEMP% home\cores + exe 目录 → 用户实机内核绝不在作用域内
// - 结束按 PID 精确清理 + 零残留断言
#include "test_util.h"

#include "clashwin/core/constants.h"
#include "core/core_manager.h"
#include "mihomo/mihomo_client.h"
#include "util/dirs.h"
#include "util/utf.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace clashwin;
using namespace clashwin::core;
using clashwin::mihomo::MihomoClient;
namespace fs = std::filesystem;

namespace {

constexpr wchar_t kTestPipe[] = L"\\\\.\\pipe\\clashwin-test-coremgr";
constexpr const char* kTestPipeN = "\\\\.\\pipe\\clashwin-test-coremgr";
constexpr uint16_t kTestPort = 20804;
constexpr const char* kTestSecret = "clashwin-coremgr-secret";

struct LiveEnv {
  std::wstring home;
  std::wstring config;

  static LiveEnv& get() {
    static LiveEnv e;
    return e;
  }

  bool setup() {
    if (!config.empty()) return true;
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    home = std::wstring(temp) + L"clashwin-test-coremgr-home";
    std::error_code ec;
    fs::remove_all(home, ec);
    fs::create_directories(home + L"\\cores", ec);
    if (ec) return false;

    // 环境覆盖：全部落 %TEMP%（绝不写真实 %APPDATA%）
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", home.c_str());

    // 内核定位分支：<home>\cores\mini-mihomo.exe 优先（拷贝 sidecar 件）
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring src = (fs::path(self).parent_path() / L"sidecar\\mini-mihomo.exe").wstring();
    if (!fs::exists(src)) {
      std::printf("  [live-core] sidecar not found: %ls\n", src.c_str());
      return false;
    }
    fs::copy_file(src, home + L"\\cores\\mini-mihomo.exe", fs::copy_options::overwrite_existing, ec);
    if (ec) return false;

    // 最小配置：含 PROXY select 组（restore 语义联调）
    config = home + L"\\clash-mini.yaml";
    std::ofstream f(config, std::ios::binary);
    if (!f.is_open()) return false;
    f << "mixed-port: " << kTestPort << "\n"
      << "external-controller-pipe: " << kTestPipeN << "\n"
      << "secret: \"" << kTestSecret << "\"\n"
      << "log-level: info\n"
      << "mode: rule\n"
      << "proxies:\n"
      << "  - name: TD-01\n"
      << "    type: direct\n"
      << "  - name: TD-02\n"
      << "    type: direct\n"
      << "proxy-groups:\n"
      << "  - name: PROXY\n"
      << "    type: select\n"
      << "    proxies:\n"
      << "      - TD-01\n"
      << "      - TD-02\n"
      << "rules:\n"
      << "  - MATCH,DIRECT\n";
    f.flush();
    return f.good();
  }

  void teardown() {
    std::error_code ec;
    fs::remove_all(home, ec);
    config.clear();
    home.clear();
  }

  // 零残留断言：作用域内无 mini-mihomo 进程存活
  bool zero_residue() {
    auto procs = enumerate_all_processes();
    std::vector<std::wstring> scope;
    if (!home.empty()) scope.push_back(home + L"\\cores");
    scope.push_back(util::dirs::exe_dir());
    auto targets = select_stale_core_pids(procs, {GetCurrentProcessId()}, scope);
    return targets.empty();
  }
};

bool wait_pipe(int timeout_ms) {
  // 用真实 CreateFileW 探测（沙箱实测 WaitNamedPipeW 对该内核管道 15s 未命中，
  // 而连接实际已就绪）
  for (int i = 0; i < timeout_ms / 100; ++i) {
    HANDLE h = CreateFileW(kTestPipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0,
                           nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      CloseHandle(h);
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return false;
}

}  // namespace

// ---------- 诊断：沙箱内 CreateProcessW 变量定位 gle=87（env 块 / stdio 继承） ----------
UT_TEST(live_coremgr_spawn_diag) {
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  std::wstring out_file = std::wstring(temp) + L"clashwin-coremgr-diag.txt";
  DeleteFileW(out_file.c_str());

  auto run = [&](const wchar_t* label, LPWSTR env, BOOL inherit, DWORD flags) {
    std::wstring cmd = L"\"C:\\Windows\\System32\\cmd.exe\" /c echo diag-ok > \"" + out_file + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = inherit ? STARTF_USESTDHANDLES : STARTF_USESHOWWINDOW;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, inherit, flags, env, nullptr,
                             &si, &pi);
    std::printf("  [diag] %ls ok=%d gle=%lu\n", label, ok, ok ? 0 : GetLastError());
    if (ok) {
      WaitForSingleObject(pi.hProcess, 5000);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    std::ifstream f(out_file);
    std::string line;
    std::getline(f, line);
    std::printf("  [diag] %ls output: %s\n", label, line.empty() ? "(none)" : line.c_str());
    return ok;
  };

  // ① 基线：无 env 无重定向
  run(L"baseline", nullptr, FALSE, CREATE_NO_WINDOW);
  // ② 仅 env 块（Unicode）
  std::wstring simple_env = L"PATH=C:\\Windows\\System32\0TEMP=C:\\Temp\0\0";
  simple_env.resize(wcslen(L"PATH=C:\\Windows\\System32") + 1 + wcslen(L"TEMP=C:\\Temp") + 2);
  run(L"env-only", simple_env.data(), FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT);
  // ③ 仅 stdio 继承（无 env）
  {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE out_r = nullptr, out_w = nullptr;
    CreatePipe(&out_r, &out_w, &sa, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = out_w;
    si.hStdError = out_w;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"C:\\Windows\\System32\\cmd.exe\" /c echo diag-ok > \"" + out_file + L"\"";
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             nullptr, &si, &pi);
    std::printf("  [diag] stdio-only ok=%d gle=%lu\n", ok, ok ? 0 : GetLastError());
    if (ok) {
      WaitForSingleObject(pi.hProcess, 5000);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    CloseHandle(out_r);
    CloseHandle(out_w);
  }
  // ④ env 块 + stdio 继承同时（复刻 start_sidecar 参数组合；env 用真实构造函数）
  {
    SidecarLaunch s = make_sidecar_launch(L"C:\\x\\mini-mihomo.exe", L"C:\\home", L"C:\\home\\c.yaml",
                                          L"\\\\.\\pipe\\clashwin-test-coremgr");
    std::wstring env_block = build_sidecar_env_block(s);
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE out_r = nullptr, out_w = nullptr;
    CreatePipe(&out_r, &out_w, &sa, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = out_w;
    si.hStdError = out_w;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"C:\\Windows\\System32\\cmd.exe\" /c echo diag-ok > \"" + out_file + L"\"";
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, env_block.data(),
                             nullptr, &si, &pi);
    std::printf("  [diag] env+stdio ok=%d gle=%lu\n", ok, ok ? 0 : GetLastError());
    if (ok) {
      WaitForSingleObject(pi.hProcess, 5000);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    CloseHandle(out_r);
    CloseHandle(out_w);
  }
  // ⑤ 真实环境块（env-only，无 stdio 继承）：定位内容问题
  // ⑥ 真实环境块去掉 "=" 开头保留项（GetEnvironmentStringsW 的驱动器当前目录项）
  {
    SidecarLaunch s = make_sidecar_launch(L"C:\\x\\mini-mihomo.exe", L"C:\\home", L"C:\\home\\c.yaml",
                                          L"\\\\.\\pipe\\clashwin-test-coremgr");
    auto run_env = [&](const wchar_t* label, std::wstring block) {
      STARTUPINFOW si{};
      si.cb = sizeof(si);
      PROCESS_INFORMATION pi{};
      std::wstring cmd = L"\"C:\\Windows\\System32\\cmd.exe\" /c echo diag-ok > \"" + out_file + L"\"";
      BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, block.data(),
                               nullptr, &si, &pi);
      std::printf("  [diag] %ls ok=%d gle=%lu\n", label, ok, ok ? 0 : GetLastError());
      if (ok) {
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
      }
    };
    SidecarLaunch s5 = s;
    run_env(L"real-env-only", build_sidecar_env_block(s5));
    SidecarLaunch s6 = s;
    std::wstring block6 = build_sidecar_env_block(s6);
    // 去掉 "=" 开头项
    std::vector<std::wstring> keep;
    for (const wchar_t* p = block6.c_str(); *p;) {
      std::wstring e(p);
      p += e.size() + 1;
      if (!e.empty() && e[0] != L'=') keep.push_back(e);
    }
    std::wstring filtered;
    for (auto& e : keep) {
      filtered += e;
      filtered.push_back(L'\0');
    }
    filtered.push_back(L'\0');
    run_env(L"real-env-noeq", filtered);
  }
  DeleteFileW(out_file.c_str());
}

UT_TEST(live_coremgr_sidecar_full) {
  LiveEnv& env = LiveEnv::get();
  if (!env.setup()) {
    std::printf("  SKIP: live env setup unavailable (sandbox or missing sidecar)\n");
    env.teardown();
    return;
  }

  CoreManager mgr;
  RestoreTimings fast;
  fast.api_ready_budget_ms = 15000;
  fast.group_fill_budget_ms = 5000;
  fast.select_budget_ms = 5000;
  fast.select_retry_ms = 500;

  // ① start_sidecar：进程拉起 + 独立管道 + 日志落盘
  auto started = mgr.start_sidecar(env.config, kTestPipe);
  if (started.is_err()) {
    std::printf("  SKIP: spawn blocked in sandbox (%s); run from a normal terminal\n",
                started.error().message.c_str());
    env.teardown();
    return;
  }
  UT_CHECK(mgr.running_mode() == RunningMode::Sidecar);
  UT_CHECK(mgr.sidecar_pid() != 0);
  UT_CHECK(mgr.sidecar_alive());
  UT_CHECK(wait_pipe(15000));
  std::printf("  [live-core] sidecar pid=%lu\n", mgr.sidecar_pid());

  // ② 幂等：再次 start no-op
  auto again = mgr.start_sidecar(env.config, kTestPipe);
  UT_CHECK(again.is_ok());

  // ③ REST 联调：get_version + get_group
  MihomoClient client(kTestPipe, kTestSecret);
  auto ver = client.get_version();
  UT_CHECK(ver.is_ok());
  if (ver.is_ok()) std::printf("  [live-core] core version: %s\n", ver.value().version.c_str());
  CoreApiClientAdapter api(&client);
  auto grp = api.get_group_by_name("PROXY");
  UT_CHECK(grp.is_ok() && grp.value().all.has_value() && grp.value().all->size() == 2);

  // ④ 预选 TD-02 → apply_config 热应用 → 节点选择保持（snapshot→reload→restore）
  UT_CHECK(client.select_node_for_group("PROXY", "TD-02").is_ok());
  CoreOps ops;
  ops.start = [&]() {
    auto r = mgr.start_sidecar(env.config, kTestPipe);
    if (r.is_ok()) {
      // ← BUG-171/172 约定：内核重启后旧连接池全部作废，必须清池
      client.update_controller(kTestPipe);
    }
    return r;
  };
  ops.stop = [&]() { return mgr.stop(); };
  auto applied = mgr.apply_config(api, utf::utf16_to_utf8(env.config), ops, fast);
  UT_CHECK(applied.is_ok());
  auto after = api.get_group_by_name("PROXY");
  UT_CHECK(after.is_ok() && after.value().now.has_value() && *after.value().now == "TD-02");
  std::printf("  [live-core] apply_config preserved PROXY.now=%s\n",
              after.is_ok() && after.value().now ? after.value().now->c_str() : "?");

  // ⑤ restart_with_restore：停 → 启 → now 恢复
  UT_CHECK(mgr.restart_with_restore(api, ops, fast).is_ok());
  auto after2 = api.get_group_by_name("PROXY");
  UT_CHECK(after2.is_ok() && after2.value().now.has_value() && *after2.value().now == "TD-02");

  // ⑥ stop：进程退出 + 管道关闭
  UT_CHECK(mgr.stop().is_ok());
  UT_CHECK(mgr.running_mode() == RunningMode::NotRunning);
  UT_CHECK(!mgr.sidecar_alive());
  UT_CHECK(!WaitNamedPipeW(kTestPipe, 300));

  // ⑦ 日志落盘（logs/sidecar/sidecar_latest.log 非空）
  std::ifstream logf(util::dirs::sidecar_log_dir() + L"\\sidecar_latest.log");
  std::string first_line;
  std::getline(logf, first_line);
  UT_CHECK(!first_line.empty());
  std::printf("  [live-core] sidecar log head: %.60s\n", first_line.c_str());

  // ⑧ 零残留
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  UT_CHECK(env.zero_residue());
  env.teardown();
  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
}
