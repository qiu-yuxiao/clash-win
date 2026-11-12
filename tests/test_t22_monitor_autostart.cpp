// T22 单元与集成测试：
//   - AutostartManager 开机自启对账、Run 注册表写入引号包裹及 --silent、幂等清理与 schtasks 迁移标记
//   - CommandService get_auto_launch_status / patch_verge_config 装配
//   - Port Guard 端口范围校验与 0 值回退默认（10811/10812/10814）
//   - Controller Conflict 控制器 9108 端口冲突自动上移探测与自身内核保留
//   - CoreMonitor 15s/3s/5s 节奏状态机、连续判死自愈、3 次重启/5 次警报升级阶梯
//   - 手杀内核后持久化 profiles.yaml 选定节点保底恢复
//
// 【铁律】所有测试必须使用 MockAutostartAccess、临时目录（CLASHWIN_HOME_OVERRIDE）与
// 本地回路模拟套接字，严格保证零真实主机注册表写、零真实计划任务变动、零真实系统状态污染。

#include "test_util.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "cmd/command_service.h"
#include "config/clash.h"
#include "config/config.h"
#include "config/prfitem.h"
#include "core/autostart.h"
#include "core/core_manager.h"
#include "core/monitor.h"
#include "util/dirs.h"
#include "util/utf.h"

#pragma comment(lib, "ws2_32.lib")

using namespace clashwin;
using namespace clashwin::core;
using namespace clashwin::config;

namespace {

// ---------- MockAutostartAccess ----------
struct MockAutostartAccess : IAutostartAccess {
  bool enabled = false;
  std::wstring last_enable_cmd;
  int disable_call_count = 0;
  std::vector<std::wstring> deleted_tasks;
  std::wstring exe_path = L"C:\\Program Files\\ClashWin\\clashwin.exe";

  Result<bool> is_enabled() override { return Result<bool>::ok(enabled); }
  Result<void> enable(const std::wstring& cmd) override {
    enabled = true;
    last_enable_cmd = cmd;
    return Result<void>::ok();
  }
  Result<void> disable() override {
    enabled = false;
    ++disable_call_count;
    return Result<void>::ok();
  }
  Result<bool> delete_scheduled_task(const std::wstring& task_name) override {
    deleted_tasks.push_back(task_name);
    return Result<bool>::ok(true);
  }
  std::wstring current_exe_path() override { return exe_path; }
};

// ---------- RAII 隔离卫士 ----------
struct AutostartGuard {
  ~AutostartGuard() {
    AutostartManager::instance().reset_state();
  }
};

struct MonitorGuard {
  ~MonitorGuard() {
    CoreMonitor::instance().stop();
    CoreMonitor::instance().set_hooks({});
    CoreMonitor::instance().reset_state();
  }
};

struct TempHomeGuard {
  std::filesystem::path dir;
  TempHomeGuard() {
    auto base = std::filesystem::temp_directory_path();
    dir = base / ("clashwin_t22_" + std::to_string(GetTickCount64()) + "_" +
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

}  // namespace

// 1. 开机自启启用命令引号包裹与 --silent 格式
UT_TEST(t22_autostart_enable_silent_format) {
  AutostartGuard ag;
  MockAutostartAccess mock;
  AutostartManager::instance().set_access(&mock);

  // 情况 A：路径中含有空格且未加引号
  mock.exe_path = L"C:\\Program Files\\ClashWin\\clashwin.exe";
  auto r1 = AutostartManager::instance().update_launch(true);
  UT_CHECK(r1.is_ok());
  UT_CHECK(mock.enabled);
  UT_CHECK_EQ(mock.last_enable_cmd,
              L"\"C:\\Program Files\\ClashWin\\clashwin.exe\" --silent");

  // 情况 B：路径已经包含首尾双引号
  mock.exe_path = L"\"D:\\Tools\\ClashWin\\clashwin.exe\"";
  auto r2 = AutostartManager::instance().update_launch(true);
  UT_CHECK(r2.is_ok());
  UT_CHECK(mock.enabled);
  UT_CHECK_EQ(mock.last_enable_cmd,
              L"\"D:\\Tools\\ClashWin\\clashwin.exe\" --silent");
}

// 2. 开机自启禁用操作的幂等性
UT_TEST(t22_autostart_disable_idempotent) {
  AutostartGuard ag;
  MockAutostartAccess mock;
  mock.enabled = true;
  mock.disable_call_count = 0;
  AutostartManager::instance().set_access(&mock);

  // 第一次禁用：由开启变更为关闭
  auto r1 = AutostartManager::instance().update_launch(false);
  UT_CHECK(r1.is_ok());
  UT_CHECK(!mock.enabled);
  UT_CHECK_EQ(mock.disable_call_count, 1);

  // 第二次禁用：已处于关闭状态，不再重复调用 disable()
  auto r2 = AutostartManager::instance().update_launch(false);
  UT_CHECK(r2.is_ok());
  UT_CHECK(!mock.enabled);
  UT_CHECK_EQ(mock.disable_call_count, 1);
}

// 3. 历史计划任务清理及 .autostart_task_migrated 标记文件
UT_TEST(t22_autostart_legacy_schtasks_cleanup_and_marker) {
  TempHomeGuard th;
  AutostartGuard ag;
  MockAutostartAccess mock;
  AutostartManager::instance().set_access(&mock);

  std::filesystem::path marker = th.dir / kAutostartMigrationMarker;
  UT_CHECK(!std::filesystem::exists(marker));

  // 首次运行清理
  AutostartManager::instance().cleanup_legacy_scheduled_tasks();
  UT_CHECK_EQ(mock.deleted_tasks.size(), 2);
  UT_CHECK_EQ(mock.deleted_tasks[0], L"Clash Win");
  UT_CHECK_EQ(mock.deleted_tasks[1], L"Clash Win (Admin)");
  UT_CHECK(std::filesystem::exists(marker));

  // 验证标记内容为 "1"
  std::ifstream f(marker, std::ios::binary);
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
  UT_CHECK_EQ(content, "1");

  // 第二次运行清理：标记已存在，应直接跳过
  mock.deleted_tasks.clear();
  AutostartManager::instance().cleanup_legacy_scheduled_tasks();
  UT_CHECK_EQ(mock.deleted_tasks.size(), 0);
}

// 4. 启动时对账：配置为开启自启
UT_TEST(t22_autostart_reconcile_on_startup_enabled) {
  TempHomeGuard th;
  AutostartGuard ag;
  MockAutostartAccess mock;
  mock.enabled = false;
  AutostartManager::instance().set_access(&mock);

  Config::instance().verge().edit_draft([](IVerge& v) {
    v.enable_auto_launch = true;
  });
  Config::instance().verge().apply();

  auto r = AutostartManager::instance().reconcile_on_startup();
  UT_CHECK(r.is_ok());
  UT_CHECK(mock.enabled);
  UT_CHECK_EQ(mock.deleted_tasks.size(), 2);
  UT_CHECK(std::filesystem::exists(th.dir / kAutostartMigrationMarker));
}

// 5. 启动时对账：配置为关闭自启
UT_TEST(t22_autostart_reconcile_on_startup_disabled) {
  TempHomeGuard th;
  AutostartGuard ag;
  MockAutostartAccess mock;
  mock.enabled = true;
  AutostartManager::instance().set_access(&mock);

  Config::instance().verge().edit_draft([](IVerge& v) {
    v.enable_auto_launch = false;
  });
  Config::instance().verge().apply();

  auto r = AutostartManager::instance().reconcile_on_startup();
  UT_CHECK(r.is_ok());
  UT_CHECK(!mock.enabled);
  UT_CHECK_EQ(mock.deleted_tasks.size(), 2);
  UT_CHECK(std::filesystem::exists(th.dir / kAutostartMigrationMarker));
}

// 6. CommandService 自启命令与服务装配
UT_TEST(t22_autostart_command_service_wiring) {
  TempHomeGuard th;
  AutostartGuard ag;
  MockAutostartAccess mock;
  mock.enabled = false;
  AutostartManager::instance().set_access(&mock);
  cmd::CommandService::instance().install_all();

  // 查询初始状态
  auto r1 = cmd::CommandService::instance().invoke("get_auto_launch_status", {});
  UT_CHECK(r1.is_ok());
  UT_CHECK_EQ(r1.value().get<bool>(), false);

  // 触发补丁修改为 true
  auto r2 = cmd::CommandService::instance().invoke(
      "patch_verge_config", cmd::Json{{"enable_auto_launch", true}});
  UT_CHECK(r2.is_ok());
  UT_CHECK(mock.enabled);

  // 再次查询
  auto r3 = cmd::CommandService::instance().invoke("get_auto_launch_status", {});
  UT_CHECK(r3.is_ok());
  UT_CHECK_EQ(r3.value().get<bool>(), true);

  // 触发补丁修改为 false
  auto r4 = cmd::CommandService::instance().invoke(
      "patch_verge_config", cmd::Json{{"enable_auto_launch", false}});
  UT_CHECK(r4.is_ok());
  UT_CHECK(!mock.enabled);
}

// 7. 端口卫士零值与边界截断
UT_TEST(t22_port_guard_zero_and_boundary_clamping) {
  YAML::Node n(YAML::NodeType::Map);
  n["mixed-port"] = 0;
  n["socks-port"] = 0;
  n["port"] = 0;

  UT_CHECK_EQ(IClashTemp::guard_mixed_port(n), 10801);
  UT_CHECK_EQ(IClashTemp::guard_socks_port(n), 10802);
  UT_CHECK_EQ(IClashTemp::guard_port(n), 7899);

  // Rust as u16 截断行为验证 (65537 -> 1)
  n["mixed-port"] = "65537";
  UT_CHECK_EQ(IClashTemp::guard_mixed_port(n), 1);

  // 非法非数字字面量回退默认值
  n["mixed-port"] = "invalid_port";
  UT_CHECK_EQ(IClashTemp::guard_mixed_port(n), 10801);

  // 正常端口保留
  n["mixed-port"] = 20800;
  UT_CHECK_EQ(IClashTemp::guard_mixed_port(n), 20800);
}

// 8. 外部控制器冲突探测：外部进程占用端口时自动上移
UT_TEST(t22_controller_conflict_foreign_app_shifts_port) {
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);

  // 在回路地址上临时绑定一个测试端口，不响应 HTTP 请求
  SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
  UT_CHECK(s != INVALID_SOCKET);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;  // 自动分配一个空闲端口
  int br = bind(s, (sockaddr*)&addr, sizeof(addr));
  UT_CHECK_EQ(br, 0);
  listen(s, 1);

  sockaddr_in bound_addr{};
  int len = sizeof(bound_addr);
  getsockname(s, (sockaddr*)&bound_addr, &len);
  uint16_t test_port = ntohs(bound_addr.sin_port);

  // 验证 is_port_conflict 在端口被占且无 HTTP 2xx 响应时判定为冲突
  bool conflict = IClashTemp::is_port_conflict(test_port, "some_secret");
  UT_CHECK(conflict);

  // find_free_controller_port 必须避开该端口
  uint16_t next_free = IClashTemp::find_free_controller_port(test_port);
  UT_CHECK(next_free > test_port);

  closesocket(s);
}

// 9. 外部控制器探测：自身内核返回 200 OK 时保留原端口
UT_TEST(t22_controller_conflict_own_core_retains_port) {
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);

  // 建立模拟自身内核的轻量 HTTP 服务器
  SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
  UT_CHECK(s != INVALID_SOCKET);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  int br = bind(s, (sockaddr*)&addr, sizeof(addr));
  UT_CHECK_EQ(br, 0);
  listen(s, 1);

  sockaddr_in bound_addr{};
  int len = sizeof(bound_addr);
  getsockname(s, (sockaddr*)&bound_addr, &len);
  uint16_t test_port = ntohs(bound_addr.sin_port);

  std::atomic<bool> server_stop{false};
  std::thread server_thread([s, &server_stop] {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    timeval tv{1, 0};
    if (select(0, &fds, nullptr, nullptr, &tv) > 0) {
      SOCKET c = accept(s, nullptr, nullptr);
      if (c != INVALID_SOCKET) {
        char buf[512] = {};
        recv(c, buf, sizeof(buf), 0);
        const char* resp = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";
        send(c, resp, static_cast<int>(strlen(resp)), 0);
        closesocket(c);
      }
    }
  });

  // 自身内核应判定为非冲突（false）
  bool conflict = IClashTemp::is_port_conflict(test_port, "my_secret");
  UT_CHECK(!conflict);

  server_thread.join();
  closesocket(s);
}

// 10. 后台守护节奏与间隔切换（正常 15s / 重试 3s / 离线 5s）
UT_TEST(t22_monitor_rhythm_and_intervals) {
  MonitorGuard mg;
  auto& m = CoreMonitor::instance();

  bool online = true;
  bool node_ok = true;
  bool api_ok = true;

  CoreMonitor::Hooks h;
  h.network_online = [&] { return online; };
  h.node_healthy = [&] { return node_ok; };
  h.api_alive = [&] { return api_ok; };
  m.set_hooks(h);
  m.reset_state();

  // 初始全正常状态：15s 正常间隔
  UT_CHECK_EQ(m.current_interval_secs(), 15);

  // 节点异常探测 1 次：进入重试模式 3s 间隔
  node_ok = false;
  m.tick_once();
  UT_CHECK_EQ(m.node_fail_count(), 1);
  UT_CHECK_EQ(m.current_interval_secs(), 3);

  // 节点恢复，API 异常探测 1 次：同样为 3s 间隔
  node_ok = true;
  api_ok = false;
  m.tick_once();
  UT_CHECK_EQ(m.api_fail_count(), 1);
  UT_CHECK_EQ(m.current_interval_secs(), 3);

  // 物理网络离线：优先进入 5s 离线等待间隔
  online = false;
  UT_CHECK_EQ(m.current_interval_secs(), 5);
}

// 11. 活跃节点连续 2 次判死触发自愈
UT_TEST(t22_monitor_node_dead_two_consecutive_heals) {
  MonitorGuard mg;
  auto& m = CoreMonitor::instance();

  int heals = 0;
  CoreMonitor::Hooks h;
  h.core_alive = [] { return true; };
  h.network_online = [] { return true; };
  h.api_alive = [] { return true; };
  h.node_healthy = [] { return false; };  // 节点持续判死
  h.current_uid = [] { return "profile_uid_1"; };
  h.auto_select_trigger = [&]() -> Result<bool> {
    ++heals;
    return Result<bool>::ok(true);
  };
  m.set_hooks(h);
  m.reset_state();

  // 第 1 次判死：进入重试模式，不触发自愈
  auto a1 = m.tick_once();
  UT_CHECK(a1 == CoreMonitor::Action::Idle);
  UT_CHECK_EQ(m.node_fail_count(), 1);
  UT_CHECK_EQ(heals, 0);

  // 第 2 次判死：达到阈值，触发自愈，计数复位
  auto a2 = m.tick_once();
  UT_CHECK(a2 == CoreMonitor::Action::Healed);
  UT_CHECK_EQ(m.node_fail_count(), 0);
  UT_CHECK_EQ(heals, 1);
}

// 12. 守护升级梯（BUG-318 双计数解耦状态机）：
//     - API 假死连续 3 次自愈失败 → 重启内核（保留告警计数 auto_select_fail_count=3），
//       若重启后仍继续失败，至第 5 次触发系统警报并重置双计数；
//     - API 可达（节点侧故障）→ 第 3 次跳过内核重启但保留告警计数，至第 5 次触发系统警报并归零；
//     - API 不可达且重启内核自身返回 Err → 第 3 次立即升级触发终态系统警报并重置双计数。
UT_TEST(t22_monitor_escalation_ladder_3fail_reset_then_alert) {
  MonitorGuard mg;
  auto& m = CoreMonitor::instance();

  // ---------- 阶段 A：3 次自愈失败且 API 不可达 → restart_core（保留告警计数=3）→ 第 5 次弹窗 ----------
  {
    int restarts = 0, alerts = 0;
    int64_t clock = 1000;
    CoreMonitor::Hooks h;
    h.core_alive = [] { return true; };
    h.network_online = [] { return true; };
    h.api_alive = [] { return false; };  // API 持续不可达
    h.current_uid = [] { return "uid_1"; };
    h.auto_select_trigger = []() -> Result<bool> {
      return Result<bool>::err(Error{-1, "auto select failed"});
    };
    h.restart_core = [&] {
      ++restarts;
      return Result<void>::ok();
    };
    h.alert = [&](const std::wstring&, const std::wstring&) { ++alerts; };
    h.now_ms = [&] { return clock; };
    m.set_hooks(h);
    m.reset_state();

    // 失败第 1 轮（3 次 API 不可达触发 1 次自愈）
    m.tick_once(); m.tick_once();
    auto a1 = m.tick_once();
    UT_CHECK(a1 == CoreMonitor::Action::Idle);
    UT_CHECK_EQ(m.heal_fail_count(), 1);

    // 跨过冷却，失败第 2 轮
    clock += 70000;
    m.tick_once(); m.tick_once();
    auto a2 = m.tick_once();
    UT_CHECK(a2 == CoreMonitor::Action::Idle);
    UT_CHECK_EQ(m.heal_fail_count(), 2);

    // 跨过冷却，失败第 3 轮：触发 restart_core，保留 auto_select_fail_count=3
    clock += 70000;
    m.tick_once(); m.tick_once();
    auto a3 = m.tick_once();
    UT_CHECK(a3 == CoreMonitor::Action::RestartedOnHealFail);
    UT_CHECK_EQ(restarts, 1);
    UT_CHECK_EQ(alerts, 0);
    UT_CHECK_EQ(m.heal_fail_count(), 3);
    UT_CHECK_EQ(m.api_dead_heal_fail_count(), 0);

    // 跨过冷却，失败第 4 轮
    clock += 70000;
    m.tick_once(); m.tick_once();
    auto a4 = m.tick_once();
    UT_CHECK(a4 == CoreMonitor::Action::Idle);
    UT_CHECK_EQ(m.heal_fail_count(), 4);
    UT_CHECK_EQ(alerts, 0);

    // 跨过冷却，失败第 5 轮：触发终态网络警报并重置双计数
    clock += 70000;
    m.tick_once(); m.tick_once();
    auto a5 = m.tick_once();
    UT_CHECK(a5 == CoreMonitor::Action::Alerted);
    UT_CHECK_EQ(restarts, 1);
    UT_CHECK_EQ(alerts, 1);
    UT_CHECK_EQ(m.heal_fail_count(), 0);
    UT_CHECK_EQ(m.api_dead_heal_fail_count(), 0);
  }

  // ---------- 阶段 B：API 可达（节点侧故障）→ 跳过内核重启，累计至第 5 次触发网络警报 ----------
  {
    m.reset_state();
    int restarts = 0, alerts = 0;
    int64_t clock = 1000;

    CoreMonitor::Hooks h;
    h.core_alive = [] { return true; };
    h.network_online = [] { return true; };
    h.api_alive = [] { return true; };      // 内核 API 存活（节点侧问题）
    h.node_healthy = [] { return false; };  // 节点死
    h.current_uid = [] { return "uid_1"; };
    h.auto_select_trigger = []() -> Result<bool> {
      return Result<bool>::err(Error{-1, "auto select failed"});
    };
    h.restart_core = [&] {
      ++restarts;
      return Result<void>::ok();
    };
    h.alert = [&](const std::wstring&, const std::wstring&) { ++alerts; };
    h.now_ms = [&] { return clock; };
    m.set_hooks(h);

    for (int i = 1; i <= 4; ++i) {
      clock += 70000;
      m.tick_once();
      auto a = m.tick_once();
      UT_CHECK(a == CoreMonitor::Action::Idle);
      UT_CHECK_EQ(m.heal_fail_count(), i);
      UT_CHECK_EQ(restarts, 0);
      UT_CHECK_EQ(alerts, 0);
    }

    // 第 5 次失败：触发网络警报并归零
    clock += 70000;
    m.tick_once();
    auto a5 = m.tick_once();
    UT_CHECK(a5 == CoreMonitor::Action::Alerted);
    UT_CHECK_EQ(m.heal_fail_count(), 0);
    UT_CHECK_EQ(restarts, 0);
    UT_CHECK_EQ(alerts, 1);
  }

  // ---------- 阶段 C：API 不可达且重启内核失败 → 第 3 次失败立即升级弹窗 ----------
  {
    m.reset_state();
    int restarts = 0, alerts = 0;
    int64_t clock = 1000;

    CoreMonitor::Hooks h;
    h.core_alive = [] { return true; };
    h.network_online = [] { return true; };
    h.api_alive = [] { return false; };
    h.current_uid = [] { return "uid_1"; };
    h.auto_select_trigger = []() -> Result<bool> {
      return Result<bool>::err(Error{-1, "auto select failed"});
    };
    h.restart_core = [&] {
      ++restarts;
      return Result<void>::err(Error{-1, "restart failed"});
    };
    h.alert = [&](const std::wstring&, const std::wstring&) { ++alerts; };
    h.now_ms = [&] { return clock; };
    m.set_hooks(h);

    for (int i = 1; i <= 2; ++i) {
      clock += 70000;
      m.tick_once();
      m.tick_once();
      auto a = m.tick_once();
      UT_CHECK(a == CoreMonitor::Action::Idle);
      UT_CHECK_EQ(alerts, 0);
    }

    clock += 70000;
    m.tick_once();
    m.tick_once();
    auto a3 = m.tick_once();
    UT_CHECK(a3 == CoreMonitor::Action::Alerted);
    UT_CHECK_EQ(restarts, 1);
    UT_CHECK_EQ(alerts, 1);
    UT_CHECK_EQ(m.heal_fail_count(), 0);
    UT_CHECK_EQ(m.api_dead_heal_fail_count(), 0);
  }
}

// 13. 手杀内核后重启：实时快照失败时保底读取 profiles.yaml 恢复节点
UT_TEST(t22_monitor_node_restore_after_killed_core) {
  TempHomeGuard th;

  // 构造并激活 profile
  Config::instance().profiles().edit_draft([](IProfiles& p) {
    p.current = "p_mock_1";
    PrfItem item;
    item.uid = "p_mock_1";
    item.name = "Test Profile";
    item.selected = std::vector<PrfSelected>{PrfSelected{"PROXY", "My_Saved_HK_Node"}};
    p.items = std::vector<PrfItem>{item};
  });
  Config::instance().profiles().apply();

  // Mock API：快照阶段失败（模拟手杀进程），重启后恢复成功
  struct MockKilledCoreApi : ICoreApiClient {
    bool killed = true;
    std::string restored_node;

    Result<mihomo::BaseConfig> get_base_config() override {
      return Result<mihomo::BaseConfig>::ok(mihomo::BaseConfig{});
    }

    Result<mihomo::ProxyItem> get_group_by_name(const std::string& name) override {
      if (killed) {
        return Result<mihomo::ProxyItem>::err({-1, "process terminated / connection refused"});
      }
      mihomo::ProxyItem pi;
      pi.name = name;
      pi.now = "DIRECT";
      pi.all = std::vector<std::string>{"DIRECT", "REJECT", "My_Saved_HK_Node"};
      return Result<mihomo::ProxyItem>::ok(pi);
    }

    Result<void> select_node_for_group(const std::string&, const std::string& node) override {
      restored_node = node;
      return Result<void>::ok();
    }

    Result<void> reload_config(bool, const std::string&) override {
      return Result<void>::ok();
    }
  } api;

  CoreOps ops;
  ops.stop = [&] { return Result<void>::ok(); };
  ops.start = [&] {
    api.killed = false;  // 内核重启完成
    return Result<void>::ok();
  };

  RestoreTimings timings;
  timings.api_ready_budget_ms = 50;
  timings.api_ready_poll_ms = 1;
  timings.group_fill_budget_ms = 50;
  timings.group_fill_poll_ms = 1;
  timings.select_budget_ms = 50;
  timings.select_retry_ms = 1;

  auto r = CoreManager::instance().restart_with_restore(api, ops, timings);
  UT_CHECK(r.is_ok());
  UT_CHECK_EQ(api.restored_node, "My_Saved_HK_Node");
}
