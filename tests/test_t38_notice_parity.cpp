// T38 回归测试：切换/操作通知完整性对齐（notice_handler 映射、LongOperationGuard 抑制、
// TitleBar 投影与 SnackbarW 层序防污染、ProfileImportCard / FirstRunGuideDialog 异步导入）。
#include <windows.h>

#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cmd/command_service.h"
#include "events/event_bus.h"
#include "events/event_names.h"
#include "events/notice.h"
#include "i18n/i18n.h"
#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/container.h"
#include "ui/notice_handler.h"
#include "ui/scenes/first_run_guide_dialog.h"
#include "ui/scenes/node_data.h"
#include "ui/scenes/node_list.h"
#include "ui/scenes/profile_import_card.h"
#include "ui/snackbar_w.h"
#include "ui/theme/theme.h"
#include "ui/title_bar.h"
#include "ui/ui_postbox.h"
#include "util/dirs.h"

using namespace clashwin;
using namespace clashwin::ui;
using namespace ui_test;

namespace {

void clear_notices(events::NoticeService& ns) {
  for (const auto& it : ns.snapshot()) {
    ns.hide(it.id);
  }
}

}  // namespace

// 1. notice_handler 新增映射与服务文案去重回退规则
UT_TEST(t38_notice_handler_service_and_warning_mappings) {
  auto& i18n = i18n::I18n::instance();
  if (!i18n.inited()) i18n.init(util::dirs::exe_dir() + L"\\locales", "zh");
  i18n.set_language("zh");
  auto& ns = events::NoticeService::instance();
  clear_notices(ns);

  // install_service::info / ok：传入完整中文句子时直接使用原文，不重复拼接前缀
  UT_CHECK(dispatch_notice("install_service::info",
                           "正在安装 Clash Mini 服务，请在弹出的授权窗口中确认..."));
  auto snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("info"));
  UT_CHECK_EQ(snap[0].message,
              std::string("正在安装 Clash Mini 服务，请在弹出的授权窗口中确认..."));

  // 空消息时回退 i18n 键（兼容单测空参语义）
  clear_notices(ns);
  UT_CHECK(dispatch_notice("install_service::ok", ""));
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("success"));
  UT_CHECK(!snap[0].message.empty());

  // reinstall_service::ok / reinstall_service::error
  clear_notices(ns);
  UT_CHECK(dispatch_notice("reinstall_service::ok", "服务重装完成并已重启"));
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("success"));
  UT_CHECK_EQ(snap[0].message, std::string("服务重装完成并已重启"));

  clear_notices(ns);
  UT_CHECK(dispatch_notice("reinstall_service::error", "UAC 取消"));
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("error"));
  UT_CHECK(snap[0].message.find("UAC 取消") != std::string::npos);

  // config_validate::timeout 与 warning（← lifecycle.rs:237）
  clear_notices(ns);
  UT_CHECK(dispatch_notice("config_validate::timeout", "5s"));
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("error"));

  clear_notices(ns);
  UT_CHECK(dispatch_notice("warning", "发现内核配置回退"));
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("warning"));
  UT_CHECK_EQ(snap[0].message, std::string("发现内核配置回退"));

  clear_notices(ns);
}

// 2. CommandService::LongOperationGuard 抑制重操作期间的 ipc_timeout 误报，退出守卫后恢复通知
UT_TEST(t38_long_operation_guard_suppresses_ipc_timeout_toast) {
  auto& svc = cmd::CommandService::instance();
  auto& bus = events::EventBus::instance();
  const bool prev_lw = bus.lightweight_filter();
  bus.set_lightweight_filter(false);

  int timeout_events = 0;
  const uint64_t sub = bus.subscribe(
      events::names::kNoticeMessage, [&](const events::Payload& p) {
        if (p.is_object() && p.value("status", std::string()) == "ipc_timeout") {
          ++timeout_events;
        }
      });

  svc.register_command(
      "t38_slow_probe",
      {[](const cmd::Json&) -> cmd::CmdResult {
         Sleep(140);
         return cmd::CmdResult::ok(true);
       },
       50});

  UT_CHECK(!svc.is_long_operation_running());
  {
    cmd::LongOperationGuard guard;
    UT_CHECK(svc.is_long_operation_running());
    auto r = svc.invoke("t38_slow_probe");
    UT_CHECK(!r.is_ok());
    // 守护期内静默，不触发 ipc_timeout 事件，也不污染节流表
    UT_CHECK_EQ(timeout_events, 0);
  }
  UT_CHECK(!svc.is_long_operation_running());

  // 退出守卫后再次超时：立即正常发出 ipc_timeout 事件（证明守卫内未污染 5s 节流表）
  auto r2 = svc.invoke("t38_slow_probe");
  UT_CHECK(!r2.is_ok());
  UT_CHECK_EQ(timeout_events, 1);

  bus.unsubscribe(sub);
  bus.set_lightweight_filter(prev_lw);
  Sleep(160);  // 等 detached 工作线程自然收尾
}

// 3. TitleBar 投影位于 TitleBar(z=10) 层，绝不污染顶层 SnackbarW(z=1500) 像素
UT_TEST(t38_snackbar_not_polluted_by_titlebar_shadow) {
  auto& ts = theme::ThemeService::instance();
  ts.init(theme::SkinId::Retro3d, theme::ThemeMode::Light);

  make_offscreen(271, 120, 96.0f);
  Stack root;
  auto flow = std::make_unique<Column>();
  auto tb = std::make_unique<win::TitleBar>(nullptr);
  tb->set_z_order(10);
  tb->set_version(L"0.2.7");
  flow->add_child(std::move(tb));
  auto body = std::make_unique<Widget>();
  body->flex_weight = 1.0f;
  flow->add_child(std::move(body));
  root.add_child(std::move(flow));

  auto snack = std::make_unique<SnackbarW>();
  SnackbarW* snack_ptr = snack.get();
  snack_ptr->set_position("top-right");
  events::NoticeItem item;
  item.id = 1;
  item.type = "success";
  item.message = "系统代理已开启";
  snack_ptr->set_items({item});
  root.add_child(std::move(snack));

  root.layout_tree({0, 0, 271, 120});
  UT_CHECK_EQ(snack_ptr->item_rects().size(), 1u);
  const RectF alert_rc = snack_ptr->item_rects()[0];
  // 通知条从 y=20 开始，跨越 y=30..33（原 TitleBar 投影所在行）
  UT_CHECK(alert_rc.y <= 20.5f && alert_rc.bottom() >= 40.0f);

  rt().begin_frame();
  rt().clear(ts.vars().theme_bg);
  root.paint_all(rt());
  rt().end_frame();

  // 取通知条左侧内边距纯绿色底区域 x = alert_rc.x + 8，比较 y=25 与 y=31
  const int sample_x = static_cast<int>(alert_rc.x + 8.0f);
  const Px p_above = pixel(sample_x, 25);
  const Px p_shadow_row = pixel(sample_x, 31);
  UT_CHECK(std::abs(static_cast<int>(p_above.r) - static_cast<int>(p_shadow_row.r)) <= 1);
  UT_CHECK(std::abs(static_cast<int>(p_above.g) - static_cast<int>(p_shadow_row.g)) <= 1);
  UT_CHECK(std::abs(static_cast<int>(p_above.b) - static_cast<int>(p_shadow_row.b)) <= 1);
}

// 4. ProfileImportCard 与 FirstRunGuideDialog 异步导入回调状态机验证
UT_TEST(t38_async_import_callbacks_non_blocking_state) {
  make_offscreen(360, 480, 96.0f);

  // ProfileImportCard::on_import_async（child(2) 为导入按钮）
  scene::ProfileImportCard card;
  card.layout_tree({0, 0, 231, 200});
  card.url_field()->set_text(L"https://example.com/sub.yaml");
  UT_CHECK(card.child_count() >= 3u);
  Widget* import_btn = card.child(2);

  std::function<void(bool)> resume_card;
  std::string captured_url;
  card.on_import_async = [&](const std::string& url, const std::string&,
                             std::function<void(bool)> done) {
    captured_url = url;
    resume_card = std::move(done);
  };

  card.url_field()->on_submit();
  UT_CHECK_EQ(captured_url, std::string("https://example.com/sub.yaml"));
  // 异步挂起期间：输入框与按钮处于禁用态，文本尚未清空
  UT_CHECK(!card.url_field()->enabled());
  UT_CHECK(!import_btn->enabled());
  UT_CHECK(!card.url().empty());

  // 模拟导入失败恢复：恢复可交互且保留原输入供用户修改
  UT_CHECK(static_cast<bool>(resume_card));
  resume_card(false);
  UT_CHECK(card.url_field()->enabled());
  UT_CHECK(import_btn->enabled());
  UT_CHECK_EQ(card.url(), std::string("https://example.com/sub.yaml"));

  // 再次提交并模拟成功：清空输入并恢复按钮可用
  card.url_field()->on_submit();
  UT_CHECK(static_cast<bool>(resume_card));
  resume_card(true);
  UT_CHECK(card.url_field()->enabled());
  UT_CHECK(import_btn->enabled());
  UT_CHECK(card.url().empty());

  // FirstRunGuideDialog::on_import_async
  scene::FirstRunGuideDialog dlg;
  dlg.layout_tree({0, 0, 360, 480});
  dlg.open();
  dlg.set_url("https://example.com/guide.yaml");

  std::function<void(bool, const std::string&)> resume_dlg;
  dlg.on_import_async = [&](const std::string&,
                            std::function<void(bool, const std::string&)> done) {
    resume_dlg = std::move(done);
  };
  dlg.on_patch_system_proxy = [](bool) {};

  KeyEvent enter{};
  enter.type = KeyEventType::Down;
  enter.key = VK_RETURN;
  dlg.on_key(enter);

  // 异步挂起期间：处于 is_importing() 态，保持在 Input 步渲染进度条
  UT_CHECK(dlg.is_importing());
  UT_CHECK(dlg.step() == scene::FirstRunStep::Input);
  UT_CHECK(static_cast<bool>(resume_dlg));

  // 模拟失败：退出加载态并显示错误提示
  resume_dlg(false, "DNS error");
  UT_CHECK(!dlg.is_importing());
  UT_CHECK(dlg.step() == scene::FirstRunStep::Input);
  UT_CHECK(dlg.error_message().find("DNS error") != std::string::npos);

  // 再次提交并模拟成功：进入 Ready 步
  dlg.on_key(enter);
  UT_CHECK(dlg.is_importing());
  resume_dlg(true, "");
  UT_CHECK(!dlg.is_importing());
  UT_CHECK(dlg.step() == scene::FirstRunStep::Ready);
}

// 5. 特约增强 1 & 2：代理方式三段切换与基础设置 4 个小开关具名提示（告别静默与含糊「已更新」）
UT_TEST(t38_enhanced_verge_switches_and_routing_notices) {
  auto& i18n = i18n::I18n::instance();
  if (!i18n.inited()) i18n.init(util::dirs::exe_dir() + L"\\locales", "zh");
  i18n.set_language("zh");

  // 代理分流模式三段切换
  UT_CHECK_EQ(resolve_verge_patch_success_key("rule_fallback", "proxy"),
              std::string("layout.notices.switchedToGlobalProxy"));
  UT_CHECK_EQ(resolve_verge_patch_success_key("rule_fallback", "direct"),
              std::string("layout.notices.switchedToGfwList"));
  UT_CHECK_EQ(resolve_verge_patch_success_key("rule_fallback", "addurl"),
              std::string(""));

  // 基础设置 4 个小开关（开/关共 8 态）
  struct Case {
    const char* field;
    bool val;
    const char* expected_key;
    const char* zh_substr;
  };
  const Case cases[] = {
      {"enable_auto_launch", true, "layout.notices.autoLaunchEnabled", "开机自启已开启"},
      {"enable_auto_launch", false, "layout.notices.autoLaunchDisabled", "开机自启已关闭"},
      {"enable_silent_start", true, "layout.notices.silentStartEnabled", "静默启动已开启"},
      {"enable_silent_start", false, "layout.notices.silentStartDisabled", "静默启动已关闭"},
      {"clash_allow_lan", true, "layout.notices.allowLanEnabled", "Allow LAN"},
      {"clash_allow_lan", false, "layout.notices.allowLanDisabled", "Allow LAN"},
      {"clash_ipv6", true, "layout.notices.ipv6Enabled", "IPv6 已开启"},
      {"clash_ipv6", false, "layout.notices.ipv6Disabled", "IPv6 已关闭"},
  };
  for (const auto& c : cases) {
    const std::string key = resolve_verge_patch_success_key(c.field, c.val);
    UT_CHECK_EQ(key, std::string(c.expected_key));
    const std::string zh_text = i18n.t(key);
    UT_CHECK(zh_text != key);
    UT_CHECK(zh_text != "已更新");
    UT_CHECK(zh_text.find(c.zh_substr) != std::string::npos);
  }

  // 外观/皮肤/语言保持即时视觉生效、不弹冗余成功 toast
  UT_CHECK(resolve_verge_patch_success_key("theme_mode", "dark").empty());
  UT_CHECK(resolve_verge_patch_success_key("clash_mini_control_skin", "modern").empty());
  UT_CHECK(resolve_verge_patch_success_key("language", "en").empty());

  // 英文词条同步完整可用
  i18n.set_language("en");
  UT_CHECK_EQ(i18n.t("layout.notices.switchedToGlobalProxy"),
              std::string("Switched to Global Proxy"));
  UT_CHECK_EQ(i18n.t("layout.notices.autoLaunchEnabled"),
              std::string("Auto Launch enabled"));
  i18n.set_language("zh");
}

// 6. 特约增强 3 / 4 / 5：节点切换、⚡ 批量测速择优、窗口置顶与断开全部连接通知闭环
UT_TEST(t38_enhanced_node_switch_batch_test_pin_and_conn_notices) {
  auto& i18n = i18n::I18n::instance();
  if (!i18n.inited()) i18n.init(util::dirs::exe_dir() + L"\\locales", "zh");
  i18n.set_language("zh");
  auto& ns = events::NoticeService::instance();
  clear_notices(ns);

  // (3a) 节点切换成功（节点实际变化 + 自动剥离六位重名去重后缀）
  dispatch_node_switch_notice(true, true, "HK IEPL (123456)", "");
  auto snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("success"));
  UT_CHECK_EQ(snap[0].message, std::string("已切换节点: HK IEPL"));

  // (3b) 点选当前已选中节点（changed=false）：静默不刷屏
  clear_notices(ns);
  dispatch_node_switch_notice(true, false, "HK IEPL (123456)", "");
  UT_CHECK_EQ(ns.snapshot().size(), 0u);

  // (3c) 节点切换失败：弹出 error 明细
  dispatch_node_switch_notice(false, true, "HK IEPL", "HTTP 502");
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("error"));
  UT_CHECK(snap[0].message.find("HTTP 502") != std::string::npos);

  // (4a) 批量测速择优：常驻 info → 择优成功（隐藏 wait_id 并展示节点名与延迟）
  clear_notices(ns);
  uint64_t wait_id = begin_batch_test_notice();
  UT_CHECK(wait_id != 0u);
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("info"));
  UT_CHECK_EQ(snap[0].duration_ms, 0);

  scene::BatchTestSummary sum_ok;
  sum_ok.executed = true;
  sum_ok.selected = true;
  sum_ok.selected_node = "Tokyo 01 (654321)";
  sum_ok.selected_delay_ms = 42;
  finish_batch_test_notice(wait_id, sum_ok);
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("success"));
  UT_CHECK_EQ(snap[0].message,
              std::string("测速完成，已优选最快节点：Tokyo 01 (42ms)"));

  // (4b) 批量测速全员超时 → warning
  clear_notices(ns);
  wait_id = begin_batch_test_notice();
  scene::BatchTestSummary sum_timeout;
  sum_timeout.executed = true;
  sum_timeout.selected = false;
  finish_batch_test_notice(wait_id, sum_timeout);
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("warning"));
  UT_CHECK_EQ(snap[0].message, std::string("所有候选节点测速均超时"));

  // (4c) 批量测速繁忙重入 → info；探针未执行 → error
  clear_notices(ns);
  scene::BatchTestSummary sum_busy;
  sum_busy.busy = true;
  finish_batch_test_notice(0, sum_busy);
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("info"));

  clear_notices(ns);
  scene::BatchTestSummary sum_not_exec;
  finish_batch_test_notice(0, sum_not_exec);
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("error"));

  // (4d) NodeList ⚡ 点击端到端联动 on_batch_test_begin / on_batch_test_end
  clear_notices(ns);
  scene::DataHooks hooks;
  hooks.fetch_group = [](std::vector<std::string>* all, std::string* now) {
    *all = {"SG Fast (111222)", "JP Slow"};
    *now = "JP Slow";
    return true;
  };
  hooks.fetch_proxies = [](std::map<std::string, mihomo::ProxyItem>* items) {
    mihomo::ProxyItem a;
    a.name = "SG Fast (111222)";
    a.type = "TROJAN";
    a.alive = true;
    mihomo::ProxyItem b;
    b.name = "JP Slow";
    b.type = "TROJAN";
    b.alive = true;
    (*items)[a.name] = a;
    (*items)[b.name] = b;
    return true;
  };
  hooks.select_node = [](const std::string&) { return true; };
  hooks.test_group = [](const std::string&) { return true; };
  hooks.test_and_select = [](const std::vector<std::string>&) -> std::string {
    return "SG Fast (111222)";
  };
  hooks.last_batch_summary = [] {
    scene::BatchTestSummary s;
    s.executed = true;
    s.selected = true;
    s.selected_node = "SG Fast (111222)";
    s.selected_delay_ms = 28;
    return s;
  };
  hooks.now_ms = [] { return int64_t{1000}; };

  scene::NodeData data(std::move(hooks));
  UT_CHECK(data.load());
  scene::NodeList list;
  list.bind(&data);
  list.layout_tree({0, 0, 271, 300});
  std::atomic<bool> end_fired{false};
  list.on_batch_test_begin = [] { return begin_batch_test_notice(); };
  list.on_batch_test_end = [&](uint64_t wid, const scene::BatchTestSummary& s) {
    finish_batch_test_notice(wid, s);
    end_fired.store(true);
  };
  UT_CHECK(list.bolt_button() != nullptr);
  list.bolt_button()->on_click();
  for (int i = 0; i < 100 && !end_fired.load(); ++i) {
    UiPostbox::pump();
    ::Sleep(10);
  }
  UiPostbox::pump();
  UT_CHECK(end_fired.load());
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("success"));
  UT_CHECK_EQ(snap[0].message,
              std::string("测速完成，已优选最快节点：SG Fast (28ms)"));

  // (5) 窗口置顶与断开全部连接通知
  clear_notices(ns);
  dispatch_pin_window_notice(true);
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].message, std::string("窗口置顶已开启"));

  clear_notices(ns);
  dispatch_pin_window_notice(false);
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].message, std::string("窗口置顶已取消"));

  clear_notices(ns);
  dispatch_close_all_connections_notice(true, "");
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("success"));
  UT_CHECK_EQ(snap[0].message, std::string("已断开全部活动连接"));

  clear_notices(ns);
  dispatch_close_all_connections_notice(false, "API unreachable");
  snap = ns.snapshot();
  UT_CHECK_EQ(snap.size(), 1u);
  UT_CHECK_EQ(snap[0].type, std::string("error"));
  UT_CHECK_EQ(snap[0].message, std::string("API unreachable"));

  clear_notices(ns);
}
