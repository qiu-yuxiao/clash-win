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
#include "ui/scenes/settings_drawer.h"
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
  snack_ptr->set_top_offset(0.0f);  // 隐藏标题栏场景：从 y=4 起跨过 y=30..33 投影带
  events::NoticeItem item;
  item.id = 1;
  item.type = "success";
  item.message = "系统代理已开启";
  snack_ptr->set_items({item});
  root.add_child(std::move(snack));

  root.layout_tree({0, 0, 271, 120});
  UT_CHECK_EQ(snack_ptr->item_rects().size(), 1u);
  const RectF alert_rc = snack_ptr->item_rects()[0];
  // 通知条从 y=4 开始，跨越 y=30..33（原 TitleBar 投影所在行）
  UT_CHECK(alert_rc.y <= 4.5f && alert_rc.bottom() >= 34.0f);

  rt().begin_frame();
  rt().clear(ts.vars().theme_bg);
  root.paint_all(rt());
  rt().end_frame();

  // 取通知条左侧纯绿色底区域 x = alert_rc.x + 8，比较 y=25 与 y=31
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
  // 异步挂起期间：url_field 保持可编辑以对齐 Mini，按钮禁用，文本尚未清空
  UT_CHECK(card.is_importing());
  UT_CHECK(card.url_field()->enabled());
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

  // 模拟失败：退出加载态并显示统一本地化 errImport 错误提示（← first-run-guide-dialog.tsx:412,416）
  resume_dlg(false, "DNS error");
  UT_CHECK(!dlg.is_importing());
  UT_CHECK(dlg.step() == scene::FirstRunStep::Input);
  UT_CHECK_EQ(dlg.error_message(), tr8("layout.components.firstRun.errImport"));

  // 再次提交并模拟成功：进入 Ready 步
  dlg.on_key(enter);
  UT_CHECK(dlg.is_importing());
  resume_dlg(true, "");
  UT_CHECK(!dlg.is_importing());
  UT_CHECK(dlg.step() == scene::FirstRunStep::Ready);
}

// 5. 特约增强 1 & 2：代理方式三段切换与基础设置 4 个小开关具名提示（告别静默与含糊「已更新」）
// 6. 特约增强 3 / 4 / 5：节点切换、⚡ 批量测速择优、窗口置顶与断开全部连接通知闭环
// 7. 致命死锁回归：UI 线程在 SnackbarW::attach 后直接调 NoticeService::push / hide
//    必须同步刷新 items_ 并真实渲染出 Alert 像素（此前持非递归锁回调 refresh->snapshot 抛异常被吞）
UT_TEST(t38_live_snackbar_attach_ui_thread_push_hide_and_d2d_pixels) {
  auto& i18n = i18n::I18n::instance();
  if (!i18n.inited()) i18n.init(util::dirs::exe_dir() + L"\\locales", "zh");
  i18n.set_language("zh");

  UiPostbox::instance().mark_ui_thread();
  UT_CHECK(UiPostbox::instance().on_ui_thread());

  auto& ts = theme::ThemeService::instance();
  ts.init(theme::SkinId::Retro3d, theme::ThemeMode::Dark);

  auto& ns = events::NoticeService::instance();
  clear_notices(ns);

  // 复刻用户真实窗口参数：逻辑 271×680 @ 144 DPI (150% 缩放 = 物理 407×1020)
  make_offscreen(407, 1020, 144.0f);
  Stack root;
  auto drawer = std::make_unique<scene::SettingsDrawer>();
  scene::SettingsDrawer* drawer_ptr = drawer.get();
  drawer_ptr->wire_callbacks();
  drawer_ptr->set_open(true);
  root.add_child(std::move(drawer));

  auto snack = std::make_unique<SnackbarW>();
  SnackbarW* snack_ptr = snack.get();
  snack_ptr->set_position("top-right");
  snack_ptr->attach(ns);
  root.add_child(std::move(snack));

  root.layout_tree({0, 0, 271.0f, 680.0f});
  UT_CHECK_EQ(snack_ptr->visible_count(), 0u);

  // Step A: UI 线程 push 常驻等待通知（如点击 Allow LAN 开关瞬间）
  const uint64_t wait_id = ns.push("info", tr8("layout.notices.adjusting"), 0);
  UT_CHECK_EQ(snack_ptr->visible_count(), 1u);
  UT_CHECK_EQ(snack_ptr->item_rects().size(), 1u);

  rt().begin_frame();
  rt().clear(ts.vars().theme_bg);
  root.paint_all(rt());
  rt().end_frame();

  const RectF rc_info = snack_ptr->item_rects()[0];
  // 逻辑坐标 (rc_info.x + 6, rc_info.y + 10) × 1.5 转物理像素采样通知左缘纯色背景
  const int px_x = static_cast<int>(std::round((rc_info.x + 6.0f) * 1.5f));
  const int px_y = static_cast<int>(std::round((rc_info.y + 10.0f) * 1.5f));
  const Px p_info = pixel(px_x, px_y);
  const Color4f exp_info = ts.palette().info_color;
  UT_CHECK(std::abs(static_cast<int>(p_info.r) - static_cast<int>(std::round(exp_info.r * 255.0f))) <= 3);
  UT_CHECK(std::abs(static_cast<int>(p_info.g) - static_cast<int>(std::round(exp_info.g * 255.0f))) <= 3);
  UT_CHECK(std::abs(static_cast<int>(p_info.b) - static_cast<int>(std::round(exp_info.b * 255.0f))) <= 3);

  // Step B: UI 线程 hide(wait_id) + push("success", ...)（模拟 UiPostbox 收尾回调）
  ns.hide(wait_id);
  UT_CHECK_EQ(snack_ptr->visible_count(), 0u);
  ns.push("success", "局域网连接 (Allow LAN) 已开启", 2500);
  UT_CHECK_EQ(snack_ptr->visible_count(), 1u);
  UT_CHECK_EQ(snack_ptr->items()[0].message, std::string("局域网连接 (Allow LAN) 已开启"));

  rt().begin_frame();
  rt().clear(ts.vars().theme_bg);
  root.paint_all(rt());
  rt().end_frame();

  const Px p_succ = pixel(px_x, px_y);
  const Color4f exp_succ = ts.palette().success_color;
  UT_CHECK(std::abs(static_cast<int>(p_succ.r) - static_cast<int>(std::round(exp_succ.r * 255.0f))) <= 3);
  UT_CHECK(std::abs(static_cast<int>(p_succ.g) - static_cast<int>(std::round(exp_succ.g * 255.0f))) <= 3);
  UT_CHECK(std::abs(static_cast<int>(p_succ.b) - static_cast<int>(std::round(exp_succ.b * 255.0f))) <= 3);

  // Step C: 临时 SnackbarW 析构绝不能冲掉主窗口 snack_ptr 的订阅句柄
  {
    SnackbarW temp_bar;
    temp_bar.attach(ns);
    UT_CHECK_EQ(temp_bar.visible_count(), 1u);
  }
  clear_notices(ns);
  UT_CHECK_EQ(snack_ptr->visible_count(), 0u);
  ns.push("success", "主窗口通知条仍然存活", 2500);
  UT_CHECK_EQ(snack_ptr->visible_count(), 1u);

  clear_notices(ns);
}

// 8. 代理方式三段同步与「添加网址」重复点击回归
UT_TEST(t38_routing_card_sync_and_addurl_reclick) {
  make_offscreen(271, 680, 96.0f);
  scene::SettingsDrawer drawer;
  int patch_calls = 0;
  int add_rule_calls = 0;
  drawer.on_patch_verge = [&](const std::string&, const nlohmann::json&) {
    ++patch_calls;
  };
  drawer.on_add_rule = [&] { ++add_rule_calls; };
  drawer.wire_callbacks();
  drawer.set_open(true);
  drawer.layout_tree({0, 0, 271.0f, 680.0f});

  // sync_routing 静默同步三种配置态，不触发 on_patch_verge
  drawer.sync_routing("proxy");
  UT_CHECK_EQ(drawer.routing_card()->seg_->selected(), 0);
  drawer.sync_routing("direct");
  UT_CHECK_EQ(drawer.routing_card()->seg_->selected(), 1);
  drawer.sync_routing("addurl");
  UT_CHECK_EQ(drawer.routing_card()->seg_->selected(), 2);
  UT_CHECK_EQ(patch_calls, 0);

  // 已处于第 2 段（添加网址）时再次点击第 2 段：必须照常打开 AddUrlDialog
  Segmented* seg = drawer.routing_card()->seg_;
  const PointF third_seg_pt{seg->bounds().w * 0.85f, seg->bounds().h * 0.5f};
  MouseEvent down{MouseEventType::Down, third_seg_pt, kMouseLeft};
  MouseEvent up{MouseEventType::Up, third_seg_pt, kMouseLeft};
  seg->on_mouse(down);
  seg->on_mouse(up);
  UT_CHECK_EQ(add_rule_calls, 1);
}
