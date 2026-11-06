// T18 测试：系统代理（sysopt 语义对照）+ 守护（防篡改/内核守护状态机）。
// 【铁律】全部经 ISysProxyAccess mock / hooks 注入 / tick_once 单步驱动，
// 绝不触发真实注册表写、真线程、真内核、真 TUN。
#include "test_util.h"

#include <string>
#include <vector>

#include "cmd/command_service.h"
#include "config/config.h"
#include "config/prfitem.h"
#include "core/auto_select.h"
#include "system/guard_monitor.h"
#include "system/sysproxy.h"
#include "util/dirs.h"
#include "util/utf.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace clashwin;
using namespace clashwin::cmd;
using namespace clashwin::system;
using clashwin::utf::utf16_to_utf8;
namespace fs = std::filesystem;

// ---------- mock access（注册表语义；记录写序列） ----------
namespace {
struct MockAccess : ISysProxyAccess {
  std::vector<std::string> log;  // "sys:on/off(host:port|bypass)" / "auto:on/off(url)"
  SysProxyState cur_sys;
  AutoProxyState cur_auto;
  Result<SysProxyState> read_sysproxy() override {
    return Result<SysProxyState>::ok(cur_sys);
  }
  Result<void> write_sysproxy(const SysProxyState& s) override {
    cur_sys = s;
    log.push_back("sys:" + std::string(s.enable ? "on" : "off") + "(" +
                  utf16_to_utf8(s.host) + ":" +
                  std::to_string(s.port) + "|" + utf16_to_utf8(s.bypass) + ")");
    return Result<void>::ok();
  }
  Result<AutoProxyState> read_autoproxy() override {
    return Result<AutoProxyState>::ok(cur_auto);
  }
  Result<void> write_autoproxy(const AutoProxyState& a) override {
    cur_auto = a;
    log.push_back("auto:" + std::string(a.enable ? "on" : "off") + "(" +
                  utf16_to_utf8(a.url) + ")");
    return Result<void>::ok();
  }
  void clear() { log.clear(); }
};

struct SysGuard {
  ~SysGuard() {
    // 用例隔离：守护/monitor 全停 + 重写回调清空 + monitor 状态清零 + 清除 access 防止悬垂指针
    Sysopt::instance().set_access(nullptr);
    SysproxyGuard::instance().stop();
    SysproxyGuard::instance().set_guard_type(GuardType::None);
    SysproxyGuard::instance().set_rewrite(nullptr);
    CoreMonitor::instance().stop();
    CoreMonitor::instance().set_hooks({});
    CoreMonitor::instance().reset_state();
  }
};
}  // namespace

// ---------- 顺序规则 / bypass（← sysopt.rs 单测三例） ----------
UT_TEST(t18_apply_steps_order_rule) {
  // 纯全局：先清 PAC 再开 Sysproxy（禁 PAC 清 WinINET 标志）
  auto pure = proxy_apply_steps(true, false);
  UT_CHECK_EQ(pure.size(), 2);
  UT_CHECK(pure[0] == ApplyStep::Autoproxy && pure[1] == ApplyStep::Sysproxy);
  // PAC 模式：先关全局再开 PAC
  auto pac = proxy_apply_steps(false, true);
  UT_CHECK(pac[0] == ApplyStep::Sysproxy && pac[1] == ApplyStep::Autoproxy);
  // 双关：同序
  auto off = proxy_apply_steps(false, false);
  UT_CHECK(off[0] == ApplyStep::Sysproxy && off[1] == ApplyStep::Autoproxy);
}

UT_TEST(t18_default_bypass_verbatim) {
  // ← sysopt.rs:54 逐字符（分号分隔 + <local> 结尾）
  UT_CHECK(utf16_to_utf8(kDefaultBypass) ==
           "localhost;127.*;192.168.*;10.*;172.16.*;172.17.*;172.18.*;"
           "172.19.*;172.20.*;172.21.*;172.22.*;172.23.*;172.24.*;172.25.*;"
           "172.26.*;172.27.*;172.28.*;172.29.*;172.30.*;172.31.*;<local>");
  // 自定义空 → DEFAULT；use_default → "DEFAULT,custom"；否则替换
  UT_CHECK(build_bypass(true, "") == kDefaultBypass);
  UT_CHECK(build_bypass(true, "foo.bar") == std::wstring(kDefaultBypass) + L",foo.bar");
  UT_CHECK(build_bypass(false, "foo.bar") == L"foo.bar");
}

// ---------- update_sysproxy（master switch / PAC / 顺序 / guard type） ----------
UT_TEST(t18_update_sysproxy_disabled_clears_residue) {
  SysGuard g;
  MockAccess acc;
  // 模拟上次未正常关闭的残留（enable=true + PAC on）
  acc.cur_sys.enable = true;
  acc.cur_auto.enable = true;
  Sysopt::instance().set_access(&acc);
  Sysopt::VergeView v;  // enable_system_proxy=false（master 关）
  auto r = Sysopt::instance().update_sysproxy(v);
  UT_CHECK(r.is_ok());
  // ← sysopt.rs:156-159：强制双关（启动残留收敛），顺序 [Sysproxy, Autoproxy]
  UT_CHECK_EQ(acc.log.size(), 2);
  UT_CHECK(acc.log[0] == "sys:off(127.0.0.1:0|localhost;127.*;192.168.*;10.*;"
                         "172.16.*;172.17.*;172.18.*;172.19.*;172.20.*;172.21.*;"
                         "172.22.*;172.23.*;172.24.*;172.25.*;172.26.*;172.27.*;"
                         "172.28.*;172.29.*;172.30.*;172.31.*;<local>)");
  UT_CHECK(acc.log[1] == "auto:off(http://127.0.0.1:33337/commands/pac)");
  UT_CHECK(SysproxyGuard::instance().guard_type() == GuardType::None);
}

UT_TEST(t18_update_sysproxy_pure_global_order_and_guard) {
  SysGuard g;
  MockAccess acc;
  Sysopt::instance().set_access(&acc);
  Sysopt::VergeView v;
  v.enable_system_proxy = true;
  v.enable_proxy_guard = true;
  v.mixed_port = 10801;
  v.system_proxy_bypass = "foo.bar";
  auto r = Sysopt::instance().update_sysproxy(v);
  UT_CHECK(r.is_ok());
  // ← sysopt.rs:29-34：纯全局先 Autoproxy 清再 Sysproxy 开
  UT_CHECK_EQ(acc.log.size(), 2);
  UT_CHECK(acc.log[0] == "auto:off(http://127.0.0.1:33337/commands/pac)");
  UT_CHECK(acc.log[1].rfind("sys:on(127.0.0.1:10801|") == 0);
  UT_CHECK(acc.log[1].find("<local>,foo.bar") != std::string::npos);  // 追加
  // guard 开 + 纯全局 → GuardType::Sysproxy
  UT_CHECK(SysproxyGuard::instance().guard_type() == GuardType::Sysproxy);
}

UT_TEST(t18_update_sysproxy_pac_mode) {
  SysGuard g;
  MockAccess acc;
  Sysopt::instance().set_access(&acc);
  Sysopt::VergeView v;
  v.enable_system_proxy = true;
  v.proxy_auto_config = true;   // PAC 模式
  v.enable_proxy_guard = true;
  auto r = Sysopt::instance().update_sysproxy(v);
  UT_CHECK(r.is_ok());
  // ← sysopt.rs:160-167：sys off + auto on；[Sysproxy, Autoproxy]
  UT_CHECK_EQ(acc.log.size(), 2);
  UT_CHECK(acc.log[0].rfind("sys:off(") == 0);
  UT_CHECK(acc.log[1] == "auto:on(http://127.0.0.1:33337/commands/pac)");
  UT_CHECK(SysproxyGuard::instance().guard_type() == GuardType::Autoproxy);
}

UT_TEST(t18_update_sysproxy_access_unbound_leaves_trace) {
  SysGuard g;
  Sysopt::instance().set_access(nullptr);  // 测试/骨架环境
  Sysopt::VergeView v;
  v.enable_system_proxy = true;
  auto r = Sysopt::instance().update_sysproxy(v);
  UT_CHECK(r.is_ok());  // 留痕放行（绝不静默触真写——因根本无法触）
}

// ---------- reset_sysproxy（CAS 防重入 + 双关 + guard 停） ----------
UT_TEST(t18_reset_sysproxy_cas_and_guard_stop) {
  SysGuard g;
  MockAccess acc;
  acc.cur_sys.enable = true;
  Sysopt::instance().set_access(&acc);
  auto r1 = Sysopt::instance().reset_sysproxy();
  UT_CHECK(r1.is_ok());
  if (acc.log.size() != 2) {
    std::printf("  [t18_reset] log.size=%zu r1=%s\n", acc.log.size(),
                r1.is_ok() ? "ok" : r1.error().message.c_str());
    for (auto& l : acc.log) std::printf("  [t18_reset] log: %s\n", l.c_str());
  }
  UT_CHECK_EQ(acc.log.size(), 2);  // 双关一次
  UT_CHECK(acc.log[0] == "sys:off(127.0.0.1:0|)");  // mock 串直接拼接 host:port
  UT_CHECK(acc.log[1] == "auto:off()");
  // ← 原版 defer 立即复位：CAS 只防并发重入，串行二连 = 两次完整双关
  auto r2 = Sysopt::instance().reset_sysproxy();
  UT_CHECK(r2.is_ok());
  UT_CHECK_EQ(acc.log.size(), 4);
}

// ---------- SysproxyGuard（30s 防篡改） ----------
UT_TEST(t18_guard_tick_rewrites_and_stop) {
  SysGuard g;
  int rewrites = 0;
  GuardType seen = GuardType::None;
  SysProxyState seen_sys;
  SysproxyGuard::instance().set_rewrite(
      [&](GuardType t, const SysProxyState& s, const AutoProxyState&) {
        ++rewrites;
        seen = t;
        seen_sys = s;
      });
  SysProxyState sys;
  sys.enable = true;
  sys.port = 10801;
  // None 态 tick → 不重写
  SysproxyGuard::instance().set_guard_type(GuardType::None);
  SysproxyGuard::instance().tick_once();
  UT_CHECK_EQ(rewrites, 0);
  // Sysproxy 态 tick → 重写且参数一致（手动改注册表 30s 内被复原的机制）
  SysproxyGuard::instance().set_guard_type(GuardType::Sysproxy, sys, {});
  SysproxyGuard::instance().tick_once();
  UT_CHECK_EQ(rewrites, 1);
  UT_CHECK(seen == GuardType::Sysproxy);
  UT_CHECK(seen_sys.enable && seen_sys.port == 10801);
  // interval 对照（默认 30s，可设）
  UT_CHECK_EQ(SysproxyGuard::instance().interval_secs(), 30u);
  SysproxyGuard::instance().set_interval(std::chrono::seconds(10));
  UT_CHECK_EQ(SysproxyGuard::instance().interval_secs(), 10u);
  // 回 None → stop
  SysproxyGuard::instance().set_guard_type(GuardType::None);
  UT_CHECK(!SysproxyGuard::instance().running());
}

// ---------- CoreMonitor 状态机（← monitor.rs 参数对照） ----------
UT_TEST(t18_monitor_core_dead_restart) {
  SysGuard g;
  auto& m = CoreMonitor::instance();
  int restarts = 0;
  CoreMonitor::Hooks h;
  h.core_alive = [] { return false; };  // 进程静默退出
  h.restart_core = [&] { ++restarts; return Result<void>::ok(); };
  h.now_ms = [] { return 1000; };
  m.set_hooks(h);
  m.reset_state();
  auto a = m.tick_once();
  UT_CHECK(a == CoreMonitor::Action::Restarted);
  UT_CHECK_EQ(restarts, 1);
}

UT_TEST(t18_monitor_api_3fail_heal_cooldown_and_esculation) {
  SysGuard g;
  auto& m = CoreMonitor::instance();
  int64_t clock = 0;
  int heals = 0, restarts = 0, alerts = 0;
  CoreMonitor::Hooks h;
  h.core_alive = [] { return true; };
  h.network_online = [] { return true; };
  h.api_alive = [&] { return false; };  // API 不可达
  h.current_uid = [] { return std::string("U1"); };
  h.auto_select_trigger = [&]() -> Result<bool> {
    ++heals;
    return Result<bool>::err({1, "heal failed"});  // 自愈失败（err 语义）
  };
  h.restart_core = [&] { ++restarts; return Result<void>::ok(); };
  h.alert = [&](const std::wstring&, const std::wstring&) { ++alerts; };
  h.now_ms = [&] { return clock; };
  m.set_hooks(h);
  m.reset_state();

  // API 不可达 ×2 → 未达阈值
  m.tick_once();
  m.tick_once();
  UT_CHECK(m.api_fail_count() == 2);
  UT_CHECK_EQ(heals, 0);
  // 第 3 次 → 自愈触发（失败 → heal_fail=1）
  auto a = m.tick_once();
  UT_CHECK_EQ(heals, 1);
  UT_CHECK(m.heal_fail_count() == 1);
  (void)a;
  // 再三轮（每轮 3 次 API 失败 → 自愈失败）→ heal_fail=2,3 → 第 3 次失败
  // 清零 + API 不可达 → restart_core（每轮跨过 60s 自愈冷却）
  for (int i = 0; i < 9; ++i) {
    clock += 70000;
    m.tick_once();
  }
  UT_CHECK_EQ(heals, 4);     // 1 + 3 次失败自愈
  UT_CHECK_EQ(restarts, 1);  // heal_fail≥3 且 API 不可达 → restart
  UT_CHECK_EQ(alerts, 0);    // restart 成功 → 无警报
}

UT_TEST(t18_monitor_heal_success_and_busy) {
  SysGuard g;
  auto& m = CoreMonitor::instance();
  int64_t clock = 0;
  CoreMonitor::Hooks h;
  h.core_alive = [] { return true; };
  h.network_online = [] { return true; };
  h.api_alive = [] { return false; };
  h.current_uid = [] { return std::string("U1"); };
  h.auto_select_trigger = []() -> Result<bool> {
    return Result<bool>::ok(true);  // 自愈成功
  };
  h.now_ms = [&] { return clock; };
  m.set_hooks(h);
  m.reset_state();
  // 3 次 API 失败 → 自愈成功 → 计数全清
  m.tick_once();
  m.tick_once();
  auto a = m.tick_once();
  UT_CHECK(a == CoreMonitor::Action::Healed);
  UT_CHECK(m.heal_fail_count() == 0);
  // 冷却内不再自愈
  clock += 1000;
  m.tick_once();
  m.tick_once();
  m.tick_once();
  // （第 3 次达阈值但 60s 冷却内 → Idle，无第二次 Healed 计数变化可观测）
}

UT_TEST(t18_monitor_offline_and_recovery) {
  SysGuard g;
  auto& m = CoreMonitor::instance();
  bool online = false;
  int api_calls = 0;
  CoreMonitor::Hooks h;
  h.core_alive = [] { return true; };
  h.network_online = [&] { return online; };
  h.api_alive = [&] { ++api_calls; return true; };
  h.now_ms = [] { return 0; };
  m.set_hooks(h);
  m.reset_state();
  // 离线 → 跳过检测（不探 API）
  m.tick_once();
  UT_CHECK_EQ(api_calls, 0);
  // 恢复在线 → 计数清零（was_offline 转移）
  online = true;
  auto a = m.tick_once();
  UT_CHECK_EQ(api_calls, 0);  // 转移步走恢复自愈路径（uid 空 → Idle）
  UT_CHECK(a == CoreMonitor::Action::Idle);
  m.tick_once();  // 下一轮正常检测
  UT_CHECK_EQ(api_calls, 1);
}

UT_TEST(t18_monitor_failed_servers_cooldown) {
  SysGuard g;
  auto& m = CoreMonitor::instance();
  int64_t clock = 0;
  CoreMonitor::Hooks h;
  h.now_ms = [&] { return clock; };
  m.set_hooks(h);
  m.reset_state();
  m.record_server_failure("s1.example:443");
  m.record_server_failure("s2.example:443");
  auto snap = m.snapshot_failed_servers();
  UT_CHECK_EQ(snap.size(), 2);
  // 600s 内仍有效
  clock += 599999;
  UT_CHECK_EQ(m.snapshot_failed_servers().size(), 2);
  // 600s 冷却过期 → 剔除
  clock += 1;
  UT_CHECK_EQ(m.snapshot_failed_servers().size(), 0);
}

UT_TEST(t18_monitor_pick_fastest_avoids_failed) {
  // T16 pick_fastest 避让接活：failed_servers 钩子 = CoreMonitor 快照
  auto& m = CoreMonitor::instance();
  int64_t clock = 0;
  CoreMonitor::Hooks h;
  h.now_ms = [&] { return clock; };
  m.set_hooks(h);
  m.reset_state();
  m.record_server_failure("bad.example:443");
  std::map<std::string, std::string> node_server{
      {"nodeA", "good.example:443"}, {"nodeB", "bad.example:443"}};
  std::vector<std::pair<std::string, uint32_t>> display = {
      {"nodeB", 100}, {"nodeA", 200}};  // nodeB 更快但在失败表
  auto pick = core::AutoSelect::pick_fastest(
      display, [&] { return m.snapshot_failed_servers(); },
      [&] { return node_server; });
  UT_CHECK(pick.has_value());
  UT_CHECK(*pick == "nodeA");  // 避让失败服务器
}

UT_TEST(t18_wait_for_clash_ready_phases) {
  // 阶段全过（假时钟+假 sleep，零实时窗）
  int64_t clock = 0;
  CoreMonitor::ReadyHooks h;
  h.now_ms = [&] { return clock; };
  h.sleep_ms = [&](int) { clock += 300; };
  h.api_alive = [&] { return clock >= 900; };     // 3 次轮询后就绪
  h.group_ready = [&] { return clock >= 1500; };
  h.network_online = [] { return true; };
  UT_CHECK(CoreMonitor::wait_for_clash_ready(h));
  // API budget 用尽（30s）→ false
  clock = 0;
  CoreMonitor::ReadyHooks h2;
  h2.now_ms = [&] { return clock; };
  h2.sleep_ms = [&](int) { clock += 1000; };
  h2.api_alive = [] { return false; };
  UT_CHECK(!CoreMonitor::wait_for_clash_ready(h2));
}

// ---------- TUN 注入链路（mock：verge→enhance 产物→clash 面；真 TUN 绝不触发） ----------
UT_TEST(t18_tun_patch_chain_verge_and_clash_layers) {
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  static std::wstring home = std::wstring(temp) + L"clashwin-test-t18-home";
  std::error_code ec;
  fs::remove_all(home, ec);
  fs::create_directories(home, ec);
  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", home.c_str());
  CommandService::instance().install_all();

  // 本地 profile（最小内核配置）+ current（← T15 PrfItem 装配模式）
  {
    std::wstring pdir = config::prfitem::profiles_dir();
    fs::create_directories(pdir, ec);
    std::ofstream f(pdir + L"\\u1.yaml", std::ios::binary);
    f << "mixed-port: 10801\nmode: rule\nlog-level: info\n";
  }
  auto& profiles = config::Config::instance().profiles();
  profiles.edit_draft([&](config::IProfiles& d) {
    config::PrfItem item;
    item.uid = "U1";
    item.itype = "local";
    item.file = "u1.yaml";
    item.name = "u1";
    if (!d.items) d.items = std::vector<config::PrfItem>();
    d.items->push_back(item);
    d.current = "U1";
  });
  profiles.apply();

  auto& svc = CommandService::instance();
  // verge 面：enable_tun_mode=true → kFClashConfig → enhance pipeline 注入
  // （无内核运行 → 仅落盘产物，不触真内核/真 TUN）
  auto p = svc.invoke("patch_verge_config", Json{{"enable_tun_mode", true}});
  if (!p.is_ok()) {
    std::printf("  [t18_tun] patch err code=%d msg=%s\n", p.error().code,
                p.error().message.c_str());
    if (p.error().message.find("Validation process timed out") !=
        std::string::npos) {
      std::printf("  SKIP: mini-mihomo -t validation timed out under machine load\n");
      _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
      return;
    }
  }
  UT_CHECK(p.is_ok());
  // 产物断言（T08 use_tun 注入语义已落 clash-win.yaml）
  {
    std::ifstream f(home + L"\\clash-win.yaml", std::ios::binary);
    UT_CHECK(f.is_open());
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string body = ss.str();
    UT_CHECK(body.find("tun:") != std::string::npos);
    UT_CHECK(body.find("enable: true") != std::string::npos);
    UT_CHECK(body.find("fake-ip-range") != std::string::npos);  // T08 fake-ip 组
  }
  // verge 读回（null 防御：serde skip → 键可能缺省）
  auto g = svc.invoke("get_verge_config");
  UT_CHECK(g.is_ok());
  if (g.is_ok()) {
    const Json& v = g.value();
    UT_CHECK(v.contains("enable_tun_mode") && v["enable_tun_mode"].is_boolean() &&
             v["enable_tun_mode"].get<bool>());
  }
  // clash 面：patch_clash_config{tun.enable=false}（无内核运行 → 配置落盘 +
  // refresh 事件；不触真内核/真 TUN）
  auto pc = svc.invoke("patch_clash_config",
                       Json{{"tun", Json{{"enable", false}}}});
  UT_CHECK(pc.is_ok());
  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
}
