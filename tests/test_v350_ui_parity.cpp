// Clash Mini v3.5.0 前端、样式、连接合并与交互守卫全面对标单测：
// 覆盖：
// - util::is_supported_subscription_or_node_input
// - Monochrome SwitchW 28x14 方角与轨道 hover 描边、popup_card_paint / popup_tokens
// - NodeData confirmed-fake 过滤、BUG-387 假超时历史淘汰、-2 前时间保留
// - path_control 5 分桶排序 & compose(raw_closed)、ConnectionsPanel 路由导入导出 & UIA=2
// - AddUrlDialog 271px 左锚定 & 快捷键、DeleteProfileConfirmModal、FirstRunGuideDialog 校验与重开守卫
// - SettingsDrawer 删除确认与 MixedPort Esc / Close 失焦防保存、classify_global_shortcut F1/F4
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "i18n/i18n.h"
#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/main_window.h"
#include "ui/scenes/add_url_dialog.h"
#include "ui/scenes/connection_data.h"
#include "ui/scenes/connections_panel.h"
#include "ui/scenes/first_run_guide_dialog.h"
#include "ui/scenes/node_data.h"
#include "ui/scenes/path_control.h"
#include "ui/scenes/settings_drawer.h"
#include "ui/scenes/utility_modals.h"
#include "ui/switch_w.h"
#include "ui/theme/theme.h"
#include "util/dirs.h"
#include "util/node.h"

namespace fs = std::filesystem;
using namespace clashwin;
using namespace clashwin::ui;
using namespace clashwin::ui::scene;
using namespace clashwin::ui::theme;
using namespace clashwin::ui::win;
using namespace ui_test;

namespace {

struct TempHomeScope {
  fs::path dir;
  TempHomeScope() {
    dir = fs::temp_directory_path() /
          ("clashwin_v350_ui_" + std::to_string(GetTickCount64()) + "_" +
           std::to_string(std::rand()));
    fs::create_directories(dir);
    for (const wchar_t* sub : {L"profiles", L"icons", L"logs", L"ruleset"}) {
      fs::create_directories(dir / sub);
    }
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", dir.wstring().c_str());
  }
  ~TempHomeScope() {
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
};

struct LangGuard {
  std::string prev;
  LangGuard() {
    if (!i18n::I18n::instance().inited()) {
      i18n::I18n::instance().init(util::dirs::app_resources_dir() + L"\\locales",
                                  "zh");
    }
    prev = i18n::I18n::instance().language();
    i18n::I18n::instance().set_language("zh");
  }
  ~LangGuard() {
    if (!prev.empty()) i18n::I18n::instance().set_language(prev);
  }
};

struct ThemeRestoreGuard {
  SkinId skin;
  ThemeMode mode;
  ThemeRestoreGuard()
      : skin(ThemeService::instance().skin()),
        mode(ThemeService::instance().mode()) {}
  ~ThemeRestoreGuard() { ThemeService::instance().init(skin, mode); }
};

int64_t make_utc_epoch_ms(int year, int mon, int day, int hour, int min, int sec) {
  std::tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = mon - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = min;
  tm.tm_sec = sec;
  return static_cast<int64_t>(_mkgmtime(&tm)) * 1000LL;
}

bool near_eq(float a, float b) { return std::abs(a - b) < 1e-3f; }

}  // namespace

UT_TEST(v350_ui_supported_subscription_or_node_input) {
  using util::is_supported_subscription_or_node_input;
  UT_CHECK(is_supported_subscription_or_node_input("https://example.com/sub?a=1"));
  UT_CHECK(is_supported_subscription_or_node_input("  http://sub.example.org/yaml  "));
  UT_CHECK(is_supported_subscription_or_node_input("vmess://eyJhZGQiOiIxLjIuMy40In0="));
  UT_CHECK(is_supported_subscription_or_node_input(
      "ss://YWVzLTEyOC1nY206dGVzdA@1.1.1.1:443#HK\n"
      "vless://11111111-1111-1111-1111-111111111111@2.2.2.2:443#JP"));
  UT_CHECK(!is_supported_subscription_or_node_input(""));
  UT_CHECK(!is_supported_subscription_or_node_input("   \r\n  "));
  UT_CHECK(!is_supported_subscription_or_node_input("ftp://example.com/sub"));
  UT_CHECK(!is_supported_subscription_or_node_input("just some plain text"));
}

UT_TEST(v350_ui_monochrome_switch_and_popup_theme_tokens) {
  ThemeRestoreGuard tg;
  auto& svc = ThemeService::instance();

  // BUG-384: Monochrome 与 Cyberpunk 同用 buildSquareSwitchStyle；基础设置 4 个开关
  // 全部 size="small" → 轨道 18×10、thumb 8×8、两者 borderRadius 0（方角）。
  // src: src/components/base/base-switch.tsx:26-51 / :456-496
  for (ThemeMode m : {ThemeMode::Light, ThemeMode::Dark}) {
    svc.init(SkinId::Monochrome, m);
    const auto sp = svc.switch_paint();
    UT_CHECK(near_eq(sp.track_w, 18.0f));
    UT_CHECK(near_eq(sp.track_h, 10.0f));
    UT_CHECK(near_eq(sp.track_radius, 0.0f));
    UT_CHECK(near_eq(sp.thumb_d, 8.0f));
    UT_CHECK(near_eq(sp.thumb_radius, 0.0f));
    UT_CHECK(sp.track_hover);
    UT_CHECK(sp.track_hover_border.a > 0.0f);

    // 与 Cyberpunk 的小开关必须完全一致（同一构造器）
    svc.init(SkinId::Cyberpunk, m);
    const auto cp_sw = svc.switch_paint();
    UT_CHECK(near_eq(cp_sw.track_w, sp.track_w));
    UT_CHECK(near_eq(cp_sw.track_h, sp.track_h));
    UT_CHECK(near_eq(cp_sw.thumb_d, sp.thumb_d));
    UT_CHECK(near_eq(cp_sw.track_radius, sp.track_radius));
    UT_CHECK(near_eq(cp_sw.thumb_radius, sp.thumb_radius));
  }

  // FrostedGlass 弹窗高不透明实底防穿透
  for (ThemeMode m : {ThemeMode::Light, ThemeMode::Dark}) {
    svc.init(SkinId::FrostedGlass, m);
    const auto cp = svc.popup_card_paint();
    UT_CHECK(!cp.grad_bg);
    UT_CHECK(cp.bg.a >= 0.96f);
  }

  // Retro3d popup_tokens 高对比语义字色
  svc.init(SkinId::Retro3d, ThemeMode::Dark);
  const auto rt = svc.popup_tokens();
  UT_CHECK(rt.is_retro3d);
  UT_CHECK(rt.primary_text.r < 0.25f);
  UT_CHECK(rt.error_color.r > 0.5f);
  UT_CHECK(rt.ready_color.g > 0.3f);
  UT_CHECK(rt.accent_primary.b > 0.5f);
}

UT_TEST(v350_ui_node_data_fake_filter_bug387_and_pretest_ttl) {
  int64_t clock_ms = make_utc_epoch_ms(2026, 11, 16, 12, 0, 0);
  bool hk_alive = true;
  int hk_hist_delay = 120;
  std::string hk_hist_time = "2026-11-16T11:59:00Z";

  DataHooks h;
  h.now_ms = [&clock_ms] { return clock_ms; };
  h.fetch_group = [](std::vector<std::string>* all, std::string* now) {
    *all = {"hk-good", "fake-node"};
    *now = "hk-good";
    return true;
  };
  h.fetch_proxies = [&](std::map<std::string, mihomo::ProxyItem>* items) {
    mihomo::ProxyItem good;
    good.name = "hk-good";
    good.type = "VLESS";
    good.alive = hk_alive;
    good.history.push_back(
        mihomo::DelayHistory{hk_hist_time, static_cast<uint16_t>(hk_hist_delay)});
    (*items)["hk-good"] = good;

    mihomo::ProxyItem fake;
    fake.name = "fake-node";
    fake.type = "Shadowsocks";
    fake.alive = false;
    (*items)["fake-node"] = fake;
    return true;
  };
  h.fetch_confirmed_fake = [] {
    return std::unordered_set<std::string>{"fake-node"};
  };

  NodeData nd(h);
  UT_CHECK(nd.load());
  UT_CHECK_EQ(nd.row_count(), size_t(1));
  UT_CHECK_EQ(nd.rows()[0].name, "hk-good");

  // 1) BUG-387：缓存 Timeout(0) 写入于 12:00:00；若 history 在 12:01:00 有 alive 真延迟 -> 淘汰假超时
  nd.set_delay("hk-good", 0);
  UT_CHECK_EQ(nd.row_delay("hk-good"), 0);  // history 11:59:00 不晚于 12:00:00，不淘汰

  clock_ms = make_utc_epoch_ms(2026, 11, 16, 12, 1, 0);
  hk_hist_delay = 88;
  hk_hist_time = "2026-11-16T12:01:00Z";
  UT_CHECK(nd.load());
  UT_CHECK_EQ(nd.row_delay("hk-good"), 88);

  // BUG-399: 45 分钟窗口内(11m) 仍有效，超 45 分钟陈旧历史不回落 -> 判死 0
  clock_ms = make_utc_epoch_ms(2026, 11, 16, 12, 12, 0);
  UT_CHECK_EQ(nd.row_delay("hk-good"), 88);
  clock_ms = make_utc_epoch_ms(2026, 11, 16, 12, 47, 0);
  UT_CHECK_EQ(nd.row_delay("hk-good"), 0);

  // 2) -2 前置时间戳保留与复用指标 TTL 钳制（from_probe=false）
  clock_ms = make_utc_epoch_ms(2026, 11, 16, 13, 0, 0);
  nd.set_delay("hk-good", 90);
  // 距离 30min TTL 仅剩 8s（1792s 后）
  clock_ms += (kDelayCacheTtlMs - 8000);
  nd.set_delays_batch({"hk-good"}, -2);
  UT_CHECK_EQ(nd.row_delay("hk-good"), -2);
  clock_ms += 2000;  // 剩余 6s
  UT_CHECK(nd.apply_delay_results(
      std::vector<std::pair<std::string, int>>{{"hk-good", 95}}, false));
  UT_CHECK_EQ(nd.row_delay("hk-good"), 95);
  clock_ms += 5000;  // 已过 5s（<6s），仍有效
  UT_CHECK_EQ(nd.row_delay("hk-good"), 95);
  clock_ms += 2000;  // 超过原 6s 剩余寿命，过期
  UT_CHECK(nd.row_delay("hk-good") != 95);
}

UT_TEST(v350_ui_connections_panel_unified_5bucket_routes_and_uia) {
  TempHomeScope th;
  LangGuard lg;

  // 1) path_control 5 分桶优先级：syn_disc -> syn_reject -> live -> syn_idle -> closed
  std::vector<ConnItem> items;
  ConnItem closed_it;
  closed_it.id = "c-closed";
  closed_it.is_closed = true;
  closed_it.metadata.host = "closed.example.com";
  closed_it.start = "2026-11-16T12:05:00Z";

  ConnItem idle_it;
  idle_it.id = "c-idle";
  idle_it.is_synthetic = true;
  idle_it.control_status = PathControlStatus::Direct;

  ConnItem live_it;
  live_it.id = "c-live";
  live_it.is_synthetic = false;
  live_it.is_closed = false;
  live_it.metadata.host = "live.example.com";
  live_it.control_status = PathControlStatus::Proxy;

  ConnItem rej_it;
  rej_it.id = "c-rej";
  rej_it.is_synthetic = true;
  rej_it.control_status = PathControlStatus::Reject;

  ConnItem disc_it;
  disc_it.id = "c-disc";
  disc_it.is_synthetic = true;
  disc_it.control_status = PathControlStatus::Disconnected;

  items = {closed_it, idle_it, live_it, rej_it, disc_it};
  prioritize_path_control_connections(items);
  UT_CHECK_EQ(items.size(), size_t(5));
  UT_CHECK_EQ(items[0].id, "c-disc");
  UT_CHECK_EQ(items[1].id, "c-rej");
  UT_CHECK_EQ(items[2].id, "c-live");
  UT_CHECK_EQ(items[3].id, "c-idle");
  UT_CHECK_EQ(items[4].id, "c-closed");

  // 2) compose_active_path_control_connections 合并 raw_closed
  PathControlState st;
  auto composed = compose_active_path_control_connections({live_it}, {}, st, {closed_it});
  bool found_live = false;
  bool found_closed = false;
  for (const auto& c : composed) {
    if (c.id == "c-live" && !c.is_closed) found_live = true;
    if (c.id == "c-closed" && c.is_closed) found_closed = true;
  }
  UT_CHECK(found_live);
  UT_CHECK(found_closed);

  // 3) ConnectionsPanel UIA 虚拟子项数 = 2 & 路由导入/导出往返
  ConnectionsPanel panel;
  UT_CHECK_EQ(panel.a11y_virtual_child_count(), size_t(2));
  const std::string routes_txt = "- DOMAIN,openai.com,PROXY\n- DOMAIN,tracker.example,REJECT\n";
  const int imported = panel.import_routes_from_string(routes_txt, false);
  UT_CHECK(imported >= 2);
  const std::string exported = panel.export_routes_to_string();
  UT_CHECK(exported.find("openai.com") != std::string::npos);
  UT_CHECK(exported.find("tracker.example") != std::string::npos);

  // 4) ConnectionData::clear_all()
  ConnectionData cd;
  cd.consume(
      R"({"type":"snapshot","data":{"epochId":"e1","sequenceId":1,"uploadTotal":100,"downloadTotal":200,"connections":[{"id":"1","upload":1,"download":2,"start":"2026-11-16T12:00:00Z","chains":["PROXY"],"rule":"","rulePayload":"","metadata":{"host":"a.com"}}]}})");
  UT_CHECK_EQ(cd.active_count(), size_t(1));
  cd.clear_all();
  UT_CHECK_EQ(cd.active_count(), size_t(0));
  UT_CHECK(cd.closed().empty());
  UT_CHECK_EQ(cd.upload_total(), 0.0);
}

UT_TEST(v350_ui_popup_geometry_shortcuts_and_guards) {
  TempHomeScope th;
  LangGuard lg;

  // 1) AddUrlDialog: 271px 左锚定 + Ctrl+A / Ctrl+X / Ctrl+V
  {
    AddUrlDialog dlg;
    dlg.open();
    dlg.layout_tree({0, 0, 640, 600});
    UT_CHECK_EQ(dlg.card_rect().x, 0.0f);
    UT_CHECK_EQ(dlg.card_rect().w, 271.0f);

    dlg.set_payload("foo.example.com");
    dlg.on_key({KeyEventType::Down, 'A', 0, true, false, false});
    dlg.on_key({KeyEventType::Down, 'X', 0, true, false, false});
    UT_CHECK(dlg.payload().empty());

    AddUrlDialog::set_clipboard_reader_for_test([] { return L"bar.example.com\nbaz.example.com"; });
    dlg.on_key({KeyEventType::Down, 'V', 0, true, false, false});
    UT_CHECK(dlg.payload().find("bar.example.com") != std::string::npos);
    UT_CHECK(dlg.payload().find("baz.example.com") != std::string::npos);
    AddUrlDialog::set_clipboard_reader_for_test(nullptr);
  }

  // 2) DeleteProfileConfirmModal: 271px 左锚定 + 点击确认
  {
    DeleteProfileConfirmModal modal;
    std::string confirmed_uid;
    modal.on_confirm = [&](const std::string& uid) { confirmed_uid = uid; };
    modal.open("u-del-1", "MyProfile");
    modal.layout_tree({0, 0, 640, 600});
    UT_CHECK_EQ(modal.card_rect().x, 0.0f);
    UT_CHECK_EQ(modal.card_rect().w, 271.0f);
    const RectF db = modal.delete_btn_rect();
    const PointF center{db.x + db.w / 2.0f, db.y + db.h / 2.0f};
    modal.on_mouse({MouseEventType::Down, center, kMouseLeft});
    modal.on_mouse({MouseEventType::Up, center, kMouseLeft});
    UT_CHECK_EQ(confirmed_uid, "u-del-1");
    UT_CHECK(!modal.is_open());
  }

  // 3) FirstRunGuideDialog: errEmpty / errScheme & 重开不自动开启系统代理
  {
    FirstRunGuideDialog fr;
    FirstRunGuideDialog::set_first_run_completed(false);
    fr.open();
    UT_CHECK(!fr.is_reopened_after_completion());
    if (OpenClipboard(nullptr)) {
      EmptyClipboard();
      CloseClipboard();
    }
    fr.submit_import();
    UT_CHECK(!fr.error_message().empty());

    fr.set_url("ftp://unsupported.example.com/sub");
    fr.submit_import();
    UT_CHECK(fr.error_message() == i18n::I18n::instance().t("layout.components.firstRun.errScheme"));
    fr.close();

    // 用户重开引导（completed == true）：导入成功时不自动调 on_patch_system_proxy
    FirstRunGuideDialog::set_first_run_completed(true);
    bool auto_patched_proxy = false;
    fr.on_import = [](const std::string&) { return true; };
    fr.on_patch_system_proxy = [&](bool) { auto_patched_proxy = true; };
    fr.on_retest_nodes = [] {};
    fr.open();
    UT_CHECK(fr.is_reopened_after_completion());
    fr.set_url("https://example.com/sub");
    fr.submit_import();
    UT_CHECK(!auto_patched_proxy);
    fr.close();
  }

  // 4) SettingsDrawer: request_delete_profile + MixedPort Esc & CloseButton 失焦防保存
  {
    SettingsDrawer drawer;
    std::string deleted_uid;
    int save_calls = 0;
    drawer.on_delete_profile = [&](const std::string& uid) { deleted_uid = uid; };
    drawer.on_save_port = [&](int) {
      ++save_calls;
      return true;
    };
    drawer.wire_callbacks();
    drawer.layout_tree({0, 0, 640, 700});
    drawer.set_open(true);
    drawer.sync_basic(false, false, false, false, 7890);

    drawer.request_delete_profile("prof-xyz");
    UT_CHECK(drawer.delete_confirm_modal() != nullptr);
    UT_CHECK(drawer.delete_confirm_modal()->is_open());
    const RectF db = drawer.delete_confirm_modal()->delete_btn_rect();
    const PointF dc{db.x + db.w / 2.0f, db.y + db.h / 2.0f};
    drawer.delete_confirm_modal()->on_mouse({MouseEventType::Down, dc, kMouseLeft});
    drawer.delete_confirm_modal()->on_mouse({MouseEventType::Up, dc, kMouseLeft});
    UT_CHECK_EQ(deleted_uid, "prof-xyz");

    // MixedPort Escape 取消保存并回滚端口文本
    UT_CHECK(should_skip_mixed_port_blur_save(false, false, false));
    UT_CHECK(should_skip_mixed_port_blur_save(true, true, false));
    UT_CHECK(should_skip_mixed_port_blur_save(true, false, true));
    UT_CHECK(!should_skip_mixed_port_blur_save(true, false, false));

    TextField* pf = drawer.basic_card()->port_field_;
    pf->request_focus();
    pf->set_text(L"9999");
    pf->on_key({KeyEventType::Down, VK_ESCAPE, 0, false, false, false});
    UT_CHECK_EQ(save_calls, 0);
    UT_CHECK(pf->text() == L"7890");

    // 点击右上角 CloseButton ✕（8,3 右偏 28x28）时不得失焦保存修改中的端口
    pf->request_focus();
    pf->set_text(L"9998");
    const PointF close_pt{640.0f - 8.0f - 14.0f, 3.0f + 14.0f};
    click(&drawer, close_pt.x, close_pt.y);
    UT_CHECK(!drawer.is_open());
    UT_CHECK_EQ(save_calls, 0);
    clear_input_state();
  }

  // 5) classify_global_shortcut: editable_focused=true 时 F1/F4 生效而 Ctrl+F 屏蔽
  UT_CHECK(classify_global_shortcut(VK_F1, false, false, false, false, false, false, true) ==
           GlobalShortcutAction::TestFullGroup);
  UT_CHECK(classify_global_shortcut(VK_F4, false, false, false, false, false, false, true) ==
           GlobalShortcutAction::TestVisibleSubset);
  UT_CHECK(classify_global_shortcut('F', true, false, false, false, false, false, true) ==
           GlobalShortcutAction::None);
  UT_CHECK(classify_global_shortcut('F', true, false, false, false, false, false, false) ==
           GlobalShortcutAction::FocusFilter);
}
