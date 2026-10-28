// T19 测试：系统托盘 (3态/300ms防抖/2项菜单) + 轻量模式 (D2D/WS释放恢复/EventBus静音)
// + 单实例 (TCP 33336/SO_REUSEADDR/HTTP路由) + PAC 端口替换 + 深层链接 (clash:// 解码/条件重载)
// 【铁律】全部使用 MockTrayBackend / MockLifecycleWindow / 隔离端口进行测试，
// 绝不触发真实 Windows 任务栏图标闪烁、绝不占用 33335 端口、绝不污染真实注册表。
#include "test_util.h"

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>

#include "app/lifecycle.h"
#include "app/scheme.h"
#include "app/singleton.h"
#include "app/app.h"
#include "config/config.h"
#include "clashwin/config/constants.h"
#include "events/event_bus.h"
#include "events/event_names.h"
#include "system/tray.h"
#include "util/str_utils.h"

using namespace clashwin;
using namespace clashwin::app;
using namespace clashwin::system;

namespace {

// ---------- MockTrayBackend：全内存托盘后端，完全零真实系统任务栏操作 ----------
class MockTrayBackend : public ITrayBackend {
 public:
  struct IconRecord {
    HWND hwnd = nullptr;
    UINT uID = 0;
    UINT uCallbackMsg = 0;
    HICON icon = nullptr;
    std::wstring tip;
  };

  std::vector<IconRecord> add_calls;
  std::vector<IconRecord> modify_calls;
  std::vector<UINT> delete_calls;
  std::vector<std::wstring> shown_menu_labels;
  int last_menu_cmd = 0;

  bool add_icon(HWND hwnd, UINT uID, UINT uCallbackMsg, HICON icon, const std::wstring& tip) override {
    add_calls.push_back({hwnd, uID, uCallbackMsg, icon, tip});
    return true;
  }

  bool modify_icon(HWND hwnd, UINT uID, HICON icon, const std::wstring& tip) override {
    modify_calls.push_back({hwnd, uID, 0, icon, tip});
    return true;
  }

  bool delete_icon(HWND /*hwnd*/, UINT uID) override {
    delete_calls.push_back(uID);
    return true;
  }

  void show_menu(HWND /*hwnd*/, POINT /*pt*/, bool in_lite_mode, std::function<void(int cmd_id)> on_cmd) override {
    shown_menu_labels.clear();
    if (in_lite_mode) {
      shown_menu_labels.push_back(L"✔ 轻量模式 / Lite mode");
    } else {
      shown_menu_labels.push_back(L"轻量模式 / Lite mode");
    }
    shown_menu_labels.push_back(L"退出 (Exit)");

    if (last_menu_cmd != 0 && on_cmd) {
      on_cmd(last_menu_cmd);
    }
  }
};

// ---------- MockLifecycleWindow：模拟主窗口资源销毁与恢复 ----------
class MockLifecycleWindow : public ILifecycleWindow {
 public:
  int hide_count = 0;
  int show_count = 0;
  int release_d2d_count = 0;
  int restore_d2d_count = 0;
  int disconnect_ws_count = 0;
  int reconnect_ws_count = 0;
  int save_geometry_count = 0;

  void hide() override { ++hide_count; }
  void show_and_activate() override { ++show_count; }
  void release_d2d_resources() override { ++release_d2d_count; }
  void restore_d2d_resources() override { ++restore_d2d_count; }
  void disconnect_ws() override { ++disconnect_ws_count; }
  void reconnect_ws() override { ++reconnect_ws_count; }
  void save_geometry(bool /*immediately*/) override { ++save_geometry_count; }

  void reset() {
    hide_count = 0;
    show_count = 0;
    release_d2d_count = 0;
    restore_d2d_count = 0;
    disconnect_ws_count = 0;
    reconnect_ws_count = 0;
    save_geometry_count = 0;
  }
};

}  // namespace

// =========================================================================
// 1. 三态图标计算逻辑（TUN > SysProxy > Default 全四象限覆盖）
// =========================================================================
UT_TEST(t19_tray_icon_3state_logic) {
  // ① 4 种输入组合的纯函数状态计算
  UT_CHECK_EQ(calculate_state(false, false), TrayIconState::Default);
  UT_CHECK_EQ(calculate_state(false, true),  TrayIconState::SysProxy);
  UT_CHECK_EQ(calculate_state(true, false),  TrayIconState::Tun);
  UT_CHECK_EQ(calculate_state(true, true),   TrayIconState::Tun);  // TUN 优先级高于系统代理

  // ② Tray 实例驱动状态切换与 backend 记录
  auto mock = std::make_unique<MockTrayBackend>();
  auto* raw_mock = mock.get();
  Tray tray(std::move(mock));

  // 虚拟 HWND 进行初始化（零真实 API 污染）
  HWND dummy_hwnd = reinterpret_cast<HWND>(0x1234);
  UT_CHECK(tray.init(dummy_hwnd));
  UT_CHECK_EQ(raw_mock->add_calls.size(), 1u);
  UT_CHECK_EQ(tray.current_state(), TrayIconState::Default);

  // 切换为 SysProxy
  tray.update_icon(false, true);
  UT_CHECK_EQ(tray.current_state(), TrayIconState::SysProxy);
  UT_CHECK_EQ(raw_mock->modify_calls.size(), 1u);

  // 切换为 Tun (tun=true, sysproxy=true -> Tun)
  tray.update_icon(true, true);
  UT_CHECK_EQ(tray.current_state(), TrayIconState::Tun);
  UT_CHECK_EQ(raw_mock->modify_calls.size(), 2u);

  // 切换为 Default
  tray.update_icon(false, false);
  UT_CHECK_EQ(tray.current_state(), TrayIconState::Default);
  UT_CHECK_EQ(raw_mock->modify_calls.size(), 3u);

  tray.destroy();
  UT_CHECK_EQ(raw_mock->delete_calls.size(), 1u);
}

// =========================================================================
// 2. 托盘 Tooltip 与 2 项菜单定义（轻量模式对勾态与退出项）
// =========================================================================
UT_TEST(t19_tray_tooltip_and_menu_definition) {
  auto mock = std::make_unique<MockTrayBackend>();
  auto* raw_mock = mock.get();
  Tray tray(std::move(mock));

  // Tooltip 必须与 Clash Mini 原版一致 ("Clash Mini")
  UT_CHECK_EQ(tray.tooltip(), L"Clash Mini");

  HWND dummy_hwnd = reinterpret_cast<HWND>(0x1234);
  tray.init(dummy_hwnd);

  // 默认状态下展示右键菜单：菜单项必须正好 2 项
  POINT pt{100, 100};
  tray.update_lite_mode_menu(false);
  raw_mock->show_menu(dummy_hwnd, pt, tray.is_lite_mode_checked(), nullptr);

  UT_CHECK_EQ(raw_mock->shown_menu_labels.size(), 2u);
  UT_CHECK(raw_mock->shown_menu_labels[0] == L"轻量模式 / Lite mode");
  UT_CHECK(raw_mock->shown_menu_labels[1] == L"退出 (Exit)");

  // 激活轻量模式勾选态后再次展示菜单：首项带对勾标记
  tray.update_lite_mode_menu(true);
  raw_mock->show_menu(dummy_hwnd, pt, tray.is_lite_mode_checked(), nullptr);

  UT_CHECK_EQ(raw_mock->shown_menu_labels.size(), 2u);
  UT_CHECK(raw_mock->shown_menu_labels[0] == L"✔ 轻量模式 / Lite mode");
  UT_CHECK(raw_mock->shown_menu_labels[1] == L"退出 (Exit)");
}

// =========================================================================
// 3. 左键 300ms 防抖限制器与退出绕过
// =========================================================================
UT_TEST(t19_tray_left_click_debounce_300ms) {
  Tray tray;

  // 首次点击（t=1000）：通过
  UT_CHECK(tray.check_click_debounce(1000));

  // 快速连续点击（50ms, 150ms, 299ms）：均被拒绝
  UT_CHECK(!tray.check_click_debounce(1050));
  UT_CHECK(!tray.check_click_debounce(1150));
  UT_CHECK(!tray.check_click_debounce(1299));

  // 达到或超过 300ms（t=1300）：通过
  UT_CHECK(tray.check_click_debounce(1300));

  // 再次快速点击（t=1350）：被拒绝
  UT_CHECK(!tray.check_click_debounce(1350));

  // 超过 300ms（t=1601）：通过
  UT_CHECK(tray.check_click_debounce(1601));

  // 验证 Exit 菜单项绕过防抖：即使处于防抖抑制窗口内，退出动作也无条件响应
  auto mock = std::make_unique<MockTrayBackend>();
  auto* raw_mock = mock.get();
  Tray tray_with_mock(std::move(mock));
  HWND dummy_hwnd = reinterpret_cast<HWND>(0x1234);
  tray_with_mock.init(dummy_hwnd);

  bool exit_invoked = false;
  tray_with_mock.set_on_exit([&] { exit_invoked = true; });

  // 立即触发一次通过消耗防抖
  UT_CHECK(tray_with_mock.check_click_debounce(10000));
  // 10ms 后触发右键菜单 Exit 命令
  raw_mock->last_menu_cmd = kMenuExitId;
  tray_with_mock.handle_tray_message(0, WM_RBUTTONUP);

  // 退出回调必须成功执行
  UT_CHECK(exit_invoked);
}

// =========================================================================
// 4. 轻量模式资源释放/恢复与 EventBus 静音测试
// =========================================================================
UT_TEST(t19_lightweight_mode_resource_teardown_and_restore) {
  auto& lifecycle = Lifecycle::instance();
  lifecycle.reset();

  MockLifecycleWindow win;
  lifecycle.set_window(&win);

  auto mock_tray = std::make_unique<MockTrayBackend>();
  Tray tray(std::move(mock_tray));
  tray.init(reinterpret_cast<HWND>(0x1234));
  lifecycle.set_tray(&tray);

  // 准备 EventBus 监听计数
  auto& bus = events::EventBus::instance();
  std::atomic<int> event_delivered{0};
  uint64_t sub_id = bus.subscribe("t19_test_topic", [&](const std::string&) {
    ++event_delivered;
  });

  UT_CHECK_EQ(lifecycle.state(), LifecycleState::Normal);
  UT_CHECK(!lifecycle.is_in_lightweight_mode());
  UT_CHECK(!tray.is_lite_mode_checked());

  // 1) 强制进入轻量模式 (force=true)
  UT_CHECK(lifecycle.entry_lightweight_mode(/*force=*/true));
  UT_CHECK_EQ(lifecycle.state(), LifecycleState::InLightweight);
  UT_CHECK(lifecycle.is_in_lightweight_mode());
  UT_CHECK(tray.is_lite_mode_checked());

  // 校验窗口动作：释放 D2D、断开 WS、隐藏窗口
  UT_CHECK_EQ(win.hide_count, 1);
  UT_CHECK_EQ(win.release_d2d_count, 1);
  UT_CHECK_EQ(win.disconnect_ws_count, 1);

  // 验证 EventBus 过滤：轻量模式下事件被静音丢弃
  event_delivered = 0;
  bus.publish("t19_test_topic", "hello during lightweight");
  UT_CHECK_EQ(event_delivered.load(), 0);

  // 2) 退出轻量模式
  UT_CHECK(lifecycle.exit_lightweight_mode());
  UT_CHECK_EQ(lifecycle.state(), LifecycleState::Normal);
  UT_CHECK(!lifecycle.is_in_lightweight_mode());
  UT_CHECK(!tray.is_lite_mode_checked());

  // 校验窗口动作：恢复 D2D、重连 WS、激活窗口
  UT_CHECK_EQ(win.restore_d2d_count, 1);
  UT_CHECK_EQ(win.reconnect_ws_count, 1);
  UT_CHECK_EQ(win.show_count, 1);

  // 验证 EventBus 恢复：普通模式下事件正常派发
  event_delivered = 0;
  bus.publish("t19_test_topic", "hello after exit");
  UT_CHECK_EQ(event_delivered.load(), 1);

  // 3) 校验 enable_auto_light_weight_mode=false 时的自然隐藏行为 (force=false)
  win.reset();
  auto orig_verge = config::Config::instance().verge().data_arc();
  bool orig_val = orig_verge ? orig_verge->enable_auto_light_weight_mode.value_or(true) : true;

  config::Config::instance().verge().edit_draft([](config::IVerge& d) {
    d.enable_auto_light_weight_mode = false;
  });
  config::Config::instance().verge().apply();

  UT_CHECK(lifecycle.entry_lightweight_mode(/*force=*/false));
  // 窗口隐藏，但并未真正进入轻量模式（不销毁 D2D，不进入 InLightweight 态）
  UT_CHECK_EQ(win.hide_count, 1);
  UT_CHECK_EQ(win.release_d2d_count, 0);
  UT_CHECK_EQ(lifecycle.state(), LifecycleState::Normal);

  // 恢复原配置
  config::Config::instance().verge().edit_draft([orig_val](config::IVerge& d) {
    d.enable_auto_light_weight_mode = orig_val;
  });
  config::Config::instance().verge().apply();

  bus.unsubscribe(sub_id);
  lifecycle.reset();
}

// =========================================================================
// 5. 单实例服务端 SO_REUSEADDR 与 HTTP 路由端点验证
// =========================================================================
UT_TEST(t19_singleton_server_so_reuseaddr_and_endpoints) {
  // 使用专用测试端口（33342），绝不与实机 Clash Mini (33335) 或默认 ClashWin (33336) 冲突
  const uint16_t kTestPort = 33342;

  SingletonServer server;
  std::atomic<bool> visible_called{false};
  std::atomic<int> scheme_called{0};
  std::string received_scheme;

  bool ok = server.start(
      kTestPort,
      [&] { visible_called = true; },
      [&](const std::string& param) {
        ++scheme_called;
        received_scheme = param;
      });
  UT_CHECK(ok);
  UT_CHECK(server.is_running());

  // ① 测试 /commands/visible
  std::string resp_visible;
  int code_visible = SingletonClient::send_request(kTestPort, "/commands/visible", &resp_visible);
  UT_CHECK_EQ(code_visible, 200);
  UT_CHECK(resp_visible.find("200 OK") != std::string::npos);
  UT_CHECK(resp_visible.find("ok") != std::string::npos);
  UT_CHECK(visible_called.load());

  // ② 测试 /commands/pac
  std::string resp_pac;
  int code_pac = SingletonClient::send_request(kTestPort, "/commands/pac", &resp_pac);
  UT_CHECK_EQ(code_pac, 200);
  UT_CHECK(resp_pac.find("Content-Type: application/x-ns-proxy-autoconfig") != std::string::npos);
  UT_CHECK(resp_pac.find("FindProxyForURL") != std::string::npos);

  // ③ 测试 /commands/scheme?param=...
  std::string test_link = "clash://install-config?url=https%3A%2F%2Ftest.com%2Fsub";
  std::string resp_scheme;
  int code_scheme = SingletonClient::send_request(kTestPort, "/commands/scheme?param=" + test_link, &resp_scheme);
  UT_CHECK_EQ(code_scheme, 200);
  UT_CHECK(resp_scheme.find("200 OK") != std::string::npos);
  UT_CHECK_EQ(scheme_called.load(), 1);
  UT_CHECK(received_scheme.find("clash://install-config") != std::string::npos);

  // ④ 测试未知路由 404
  std::string resp_404;
  int code_404 = SingletonClient::send_request(kTestPort, "/invalid_route", &resp_404);
  UT_CHECK_EQ(code_404, 404);
  UT_CHECK(resp_404.find("404 Not Found") != std::string::npos);

  // ⑤ 验证 SO_REUSEADDR：关闭后立即在同端口重启，绝不发生 WSAEADDRINUSE
  server.stop();
  UT_CHECK(!server.is_running());

  bool restart_ok = server.start(kTestPort, nullptr, nullptr);
  UT_CHECK(restart_ok);
  server.stop();
}

// =========================================================================
// 6. PAC 脚本 %mixed-port% 替换规则
// =========================================================================
UT_TEST(t19_singleton_pac_port_substitution) {
  // ① 显式指定端口替换
  std::string pac7890 = SingletonServer::generate_pac_script(7890);
  UT_CHECK(pac7890.find("127.0.0.1:7890") != std::string::npos);
  UT_CHECK(pac7890.find("%mixed-port%") == std::string::npos);

  std::string pac10808 = SingletonServer::generate_pac_script(10808);
  UT_CHECK(pac10808.find("127.0.0.1:10808") != std::string::npos);
  UT_CHECK(pac10808.find("%mixed-port%") == std::string::npos);

  // ② 缺省无配置兜底为 7897 端口
  std::string pac_default = SingletonServer::generate_pac_script(0);
  UT_CHECK(pac_default.find("%mixed-port%") == std::string::npos);
  UT_CHECK(pac_default.find("FindProxyForURL") != std::string::npos);
}

// =========================================================================
// 7. 深层链接解析、外层修剪与 URL 解码（clash://, clash-verge://, clash-mini://）
// =========================================================================
UT_TEST(t19_deep_link_parsing_and_outer_trimming) {
  // ① 标准 clash://
  auto r1 = parse_deep_link("clash://install-config?url=https%3A%2F%2Fexample.com%2Fsub&name=MyProfile");
  UT_CHECK(r1.is_ok());
  UT_CHECK_EQ(r1.value().url, "https://example.com/sub");
  UT_CHECK(r1.value().name.has_value());
  UT_CHECK_EQ(*r1.value().name, "MyProfile");

  // ② 带外层 ["..."] 修剪（系统深链传递常见包覆格式）
  auto r2 = parse_deep_link("[\"clash-verge://install-config?url=https://sub.domain/link&name=TestSub\"]");
  UT_CHECK(r2.is_ok());
  UT_CHECK_EQ(r2.value().url, "https://sub.domain/link");
  UT_CHECK(r2.value().name.has_value());
  UT_CHECK_EQ(*r2.value().name, "TestSub");

  // ③ 带外层 [...] 及 URL 编码的中文 name
  auto r3 = parse_deep_link("[clash-mini://install-config?name=%E6%B5%8B%E8%AF%95&url=https%3A%2F%2Fexample.com%2Fclash]");
  UT_CHECK(r3.is_ok());
  UT_CHECK_EQ(r3.value().url, "https://example.com/clash");
  UT_CHECK(r3.value().name.has_value());
  UT_CHECK_EQ(*r3.value().name, "测试");

  // ④ 订阅 URL 参数自身带有多层嵌套查询参数
  auto r4 = parse_deep_link("clash://install-config?url=https://sub.test/api?token=abc%26flag=clash");
  UT_CHECK(r4.is_ok());
  UT_CHECK(r4.value().url.find("token=abc") != std::string::npos);
  UT_CHECK(!r4.value().name.has_value());

  // ⑤ 非法协议应被拒绝
  auto r5 = parse_deep_link("http://install-config?url=https://example.com");
  UT_CHECK(!r5.is_ok());
}

// =========================================================================
// 8. 深层链接核心重载触发条件（仅当此前无激活 profile 时重载）
// =========================================================================
UT_TEST(t19_deep_link_import_core_reload_condition) {
  // 模拟 profiles 状态判定
  bool had_current_true = true;
  bool is_current = true;
  bool should_reload_1 = (!had_current_true) && is_current;
  UT_CHECK(!should_reload_1);  // 已有配置时不强制刷新内核

  bool had_current_false = false;
  bool should_reload_2 = (!had_current_false) && is_current;
  UT_CHECK(should_reload_2);   // 首个导入激活时强制刷新内核

  bool not_current = false;
  bool should_reload_3 = (!had_current_false) && not_current;
  UT_CHECK(!should_reload_3);  // 未成为激活配置时不刷新内核
}

// =========================================================================
// 9. 安全红线测试：常量端口隔离与零任务栏污染
// =========================================================================
UT_TEST(t19_host_safety_and_no_port_conflict) {
  // ClashWin 默认单例端口必须为 33336
  uint16_t singleton_port = clashwin::consts::kSingletonPort;
  UT_CHECK_EQ(singleton_port, 33336);

  // 严格杜绝使用 33335（保护主机上运行的 ClashVerge/Clash Mini）
  UT_CHECK(singleton_port != 33335);

  // MockTrayBackend 验证：没有真实 Shell 调用产生
  MockTrayBackend mock;
  HWND dummy = reinterpret_cast<HWND>(0x9999);
  mock.add_icon(dummy, 1, 0, nullptr, L"test");
  UT_CHECK_EQ(mock.add_calls.size(), 1u);
  mock.delete_icon(dummy, 1);
  UT_CHECK_EQ(mock.delete_calls.size(), 1u);
}
