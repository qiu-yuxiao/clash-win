// BUG-317..362 UI 回归单测套件：
// 覆盖 BUG-321 / 332 / 336 / 337 / 338 / 339 / 341 / 342 / 343 / 344 /
//      350 / 354 / 355 / 356 / 357 / 358 / 359 / 360 / 361。
#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "events/notice.h"
#include "i18n/i18n.h"
#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/main_window.h"
#include "ui/notice_handler.h"
#include "ui/scenes/node_data.h"
#include "ui/scenes/node_list.h"
#include "ui/scenes/quick_bar.h"
#include "ui/scenes/settings_drawer.h"
#include "ui/scenes/traffic_pane.h"
#include "ui/scenes/utility_modals.h"
#include "ui/slider.h"
#include "ui/textfield.h"
#include "ui/theme/theme.h"
#include "util/dirs.h"
#include "util/utf.h"

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
          ("clashwin_bug317_362_" + std::to_string(GetTickCount64()) + "_" +
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

struct LangRestoreGuard {
  std::string prev;
  LangRestoreGuard() : prev(i18n::I18n::instance().language()) {
    if (!i18n::I18n::instance().inited()) {
      i18n::I18n::instance().init(util::dirs::app_resources_dir() + L"\\locales",
                                  "zh");
      prev = i18n::I18n::instance().language();
    }
  }
  ~LangRestoreGuard() { i18n::I18n::instance().set_language(prev); }
};

void clear_all_notices() {
  auto& svc = events::NoticeService::instance();
  for (const auto& item : svc.snapshot()) {
    svc.hide(item.id);
  }
}

struct MockNodeFixture {
  std::vector<std::string> all = {"hk-01", "sg-02", "jp-03", "DIRECT"};
  std::string now = "hk-01";
  std::map<std::string, std::string> types = {
      {"hk-01", "VLESS"},
      {"sg-02", "HYSTERIA2"},
      {"jp-03", "TROJAN"},
      {"DIRECT", "Direct"},
  };
  int64_t clock_ms = 10000;
  bool select_ok = true;
  std::string test_select_ret;
  BatchTestSummary batch_sum;
  int test_select_calls = 0;
  std::vector<std::string> last_subset;
  std::function<void(NodeData&)> during_test;

  NodeData build() {
    DataHooks h;
    h.fetch_group = [this](std::vector<std::string>* out_all,
                           std::string* out_now) {
      *out_all = all;
      *out_now = now;
      return true;
    };
    h.fetch_proxies =
        [this](std::map<std::string, mihomo::ProxyItem>* out_items) {
          for (const auto& [name, type] : types) {
            mihomo::ProxyItem it;
            it.name = name;
            it.type = type;
            it.alive = true;
            (*out_items)[name] = it;
          }
          return true;
        };
    h.select_node = [this](const std::string& target) {
      if (select_ok) now = target;
      return select_ok;
    };
    h.last_select_error = [] { return std::string("mock select error"); };
    h.test_group = [](const std::string&) { return true; };
    h.now_ms = [this] { return clock_ms; };
    return NodeData(std::move(h));
  }
};

}  // namespace

// BUG-321: 任何测速入口（单点/子集/全量）异常或遗漏节点时，-2 Pending 必须回滚原值，绝不永久转圈
UT_TEST(bug321_pending_delay_rollback_on_failure_and_omission) {
  MockNodeFixture fx;
  NodeData data = fx.build();
  UT_CHECK(data.load());

  data.set_delay("hk-01", 88);
  // sg-02 保持未测 (-1)，jp-03 设为 210ms
  data.set_delay("jp-03", 210);

  auto snap = data.snapshot_delays({"hk-01", "sg-02", "jp-03"});
  data.set_delays_batch({"hk-01", "sg-02", "jp-03"}, -2);
  UT_CHECK_EQ(data.row_delay("hk-01"), -2);
  UT_CHECK_EQ(data.row_delay("sg-02"), -2);
  UT_CHECK_EQ(data.row_delay("jp-03"), -2);

  // 模拟后端仅返回 jp-03 新延迟，hk-01 与 sg-02 因繁忙/超时中断未返回
  data.set_delay("jp-03", 145);
  UT_CHECK(data.restore_delays_for_still_pending(snap));

  UT_CHECK_EQ(data.row_delay("hk-01"), 88);
  UT_CHECK_EQ(data.row_delay("sg-02"), -1);
  UT_CHECK_EQ(data.row_delay("jp-03"), 145);
  // 再次调用应无任何节点处于 -2，返回 false
  UT_CHECK(!data.restore_delays_for_still_pending(snap));
}

// BUG-332 & BUG-350: WM_SIZING 上缘拖拽最小高度防下移跨多轮 DPI/重建周期恒成立
UT_TEST(bug332_350_wm_sizing_top_clamp_across_recreate_cycles) {
  for (float dpi_scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
    const int min_h_px = static_cast<int>(kMinWindowH * dpi_scale + 0.5f);
    for (uint32_t side : {WMSZ_TOP, WMSZ_TOPLEFT, WMSZ_TOPRIGHT}) {
      RECT rc{100, 480, 420, 520};  // 高度 40 < min_h_px
      const LONG orig_bottom = rc.bottom;
      UT_CHECK(clamp_sizing_top(side, rc, min_h_px));
      UT_CHECK_EQ(rc.bottom, orig_bottom);
      UT_CHECK_EQ(rc.bottom - rc.top, min_h_px);
    }
    // 下缘缩放不篡改 top
    RECT bottom_rc{100, 200, 420, 220};
    UT_CHECK(!clamp_sizing_top(WMSZ_BOTTOM, bottom_rc, min_h_px));
    UT_CHECK_EQ(bottom_rc.top, 200);
  }
}

// BUG-337 & BUG-343: 流量面板局部脏区隔离 + TextField::set_text_silent / NodeList::set_filter 同词去重
UT_TEST(bug337_343_traffic_isolation_and_textfield_filter_dedup) {
  make_offscreen(400, 300, 96.0f);

  // 1) TextField::set_text_silent 绝不触发 on_text_changed，且相同文本 set_text 不重复触发
  TextField tf;
  int text_changed_count = 0;
  tf.on_text_changed = [&](const std::wstring&) { ++text_changed_count; };
  tf.set_text_silent(L"hk");
  UT_CHECK_EQ(text_changed_count, 0);
  UT_CHECK(tf.text() == L"hk");
  tf.set_text(L"hk");
  UT_CHECK_EQ(text_changed_count, 0);
  tf.set_text(L"sg");
  UT_CHECK_EQ(text_changed_count, 1);

  // 2) NodeList::set_filter 相同过滤词不重复重建
  TempHomeScope home;
  MockNodeFixture fx;
  NodeData data = fx.build();
  UT_CHECK(data.load());
  NodeList nl;
  nl.bind(&data);
  nl.layout_tree({0, 0, 271, 400});
  nl.set_filter("hk");
  const uint64_t rebuilds_after_first = nl.sync_rebuild_count();
  nl.set_filter("hk");
  UT_CHECK_EQ(nl.sync_rebuild_count(), rebuilds_after_first);

  // 3) TrafficPane 局部刷新只脏自身矩形，不污染上方节点列表区域
  FrameClock clock;
  RectF dirty_accum{};
  int flush_calls = 0;
  clock.bind([&](const RectF& d) {
    dirty_accum = d;
    ++flush_calls;
  });
  set_frame_clock(&clock);

  Stack root;
  nl.set_bounds({0, 0, 271, 200});
  TrafficPane tp;
  int64_t t_ms = 1000;
  tp.set_now_ms([&] { return t_ms; });
  tp.layout_tree({0, 200, 271, 100});
  clock.flush();
  dirty_accum = {};
  flush_calls = 0;

  t_ms = 2500;
  UT_CHECK(tp.on_traffic_payload(R"({"up":1024,"down":4096})"));
  clock.flush();
  if (flush_calls > 0) {
    UT_CHECK(dirty_accum.y >= 199.0f);
  }
  set_frame_clock(nullptr);
  clear_input_state();
}

// BUG-338: LogViewerModal 滚轮边界钳制、正序贴底自动跟随（含 1000 行淘汰补偿）与倒序锚点补偿
UT_TEST(bug338_log_viewer_scroll_clamping_stick_bottom_and_desc_anchor) {
  make_offscreen(500, 420, 96.0f);
  LogViewerModal lv;
  lv.open();
  lv.layout_tree({0, 0, 500, 420});

  // 灌入 40 条日志，使内容高度远超可视区
  lv.set_desc(false);  // 升序（最新在底）
  for (int i = 0; i < 40; ++i) {
    lv.push_log("info", "line-" + std::to_string(i), "12:00:00");
  }
  lv.flush_incoming();
  const float max_s = lv.max_scroll();
  UT_CHECK(max_s > 50.0f);
  // 初始 stick_to_bottom=true，自动贴底
  UT_CHECK(lv.stick_to_bottom());
  UT_CHECK(near_f(lv.scroll_y(), max_s, 1.0f));

  // 超界设置必须被钳制到 [0, max_scroll()]
  lv.set_scroll_y(max_s + 9999.0f);
  UT_CHECK(near_f(lv.scroll_y(), lv.max_scroll(), 0.5f));
  lv.set_scroll_y(-500.0f);
  UT_CHECK(near_f(lv.scroll_y(), 0.0f, 0.5f));
  UT_CHECK(!lv.stick_to_bottom());

  // 停留在顶部回看历史时追加新日志（未达 1000 上限），scroll_y 保持锚定不跳回底部
  lv.set_scroll_y(60.0f);
  UT_CHECK(!lv.stick_to_bottom());
  lv.push_log("warn", "while-reading-history", "12:00:01");
  lv.flush_incoming();
  UT_CHECK(near_f(lv.scroll_y(), 60.0f, 1.0f));

  // 倒序模式（最新在顶）：当用户已向下滚动 > 4px 查看旧日志时，顶部插入新行自动加上 added_h 保锚点
  lv.set_desc(true);
  UT_CHECK(near_f(lv.scroll_y(), 0.0f, 0.5f));
  lv.set_scroll_y(100.0f);
  const float before_desc_h = lv.max_scroll();
  lv.push_log("error", "new-top-entry", "12:00:02");
  lv.flush_incoming();
  const float added_h = lv.max_scroll() - before_desc_h;
  UT_CHECK(added_h > 10.0f);
  UT_CHECK(near_f(lv.scroll_y(), 100.0f + added_h, 1.5f));

  // 清空日志后 scroll_y 立即归零
  lv.clear_logs();
  UT_CHECK(near_f(lv.scroll_y(), 0.0f, 0.01f));
  clear_input_state();
}

// BUG-339: NodeData 同值延迟短接无通知 + NodeList 可见序列未变时原地更新行控件不重建
UT_TEST(bug339_node_data_noop_short_circuit_and_node_list_in_place_reuse) {
  TempHomeScope home;
  make_offscreen(271, 500, 96.0f);
  MockNodeFixture fx;
  NodeData data = fx.build();
  UT_CHECK(data.load());

  int notify_count = 0;
  uint64_t sub = data.subscribe([&] { ++notify_count; });

  data.set_delay("hk-01", 120);
  UT_CHECK_EQ(notify_count, 1);
  // 同值再次写入不应触发冗余 notify（仅延长内部保鲜时间戳）
  data.set_delay("hk-01", 120);
  UT_CHECK_EQ(notify_count, 1);

  nlohmann::json batch = {{"group", "PROXY"},
                          {"results", nlohmann::json::array({{"hk-01", 120}})}};
  UT_CHECK(data.apply_delay_results(batch, true));
  UT_CHECK_EQ(notify_count, 1);

  // 挂载 NodeList（切到原始序 sort_type=0 以保证延迟变化不改变排序序列）
  NodeList nl;
  nl.bind(&data);
  nl.layout_tree({0, 0, 271, 500});
  while (nl.sorted_by_delay()) nl.toggle_sort();  // 切出按延迟排序

  const uint64_t rebuilds_before = nl.sync_rebuild_count();
  data.set_delay("hk-01", 95);
  data.set_delay("sg-02", 180);
  // 可见节点名称列表与列数均未变 → 走 update_cards_in_place，重建次数恒定不涨
  UT_CHECK_EQ(nl.sync_rebuild_count(), rebuilds_before);

  data.unsubscribe(sub);
  clear_input_state();
}

// BUG-341: BasicCard 2x2 紧凑间距（gap_x=12, 项内间距 4px）与四个标签悬停 Tooltip
UT_TEST(bug341_basic_card_compact_grid_and_label_tooltips) {
  LangRestoreGuard lg;
  make_offscreen(271, 680, 96.0f);
  SettingsDrawer drawer;
  drawer.set_open(true);
  drawer.layout_tree({0, 0, 271, 680});

  BasicCard* bc = drawer.basic_card();
  UT_CHECK(bc != nullptr);
  // 左列开关右缘与右列标签左缘间距：col_w = (w - 20 - 12)/2，验证两列无重叠且标签可命中
  for (int i = 0; i < 4; ++i) {
    UT_CHECK(bc->labels_[i] != nullptr);
    const RectF gb = bc->labels_[i]->global_bounds();
    UT_CHECK(gb.w > 20.0f && gb.h > 10.0f);
    Widget* hit = drawer.hit_test({gb.x + 4.0f, gb.y + gb.h * 0.5f});
    UT_CHECK(hit == bc->labels_[i]);
  }
  for (const char* lang : {"zh", "en", "de", "ru"}) {
    i18n::I18n::instance().set_language(lang);
    for (int i = 0; i < 4; ++i) {
      UT_CHECK(!bc->labels_[i]->tooltip_text().empty());
    }
  }
  clear_input_state();
}

// BUG-342: 快速路由规则置顶提示走多语言 quickRuleAdded 模板（2500ms）
UT_TEST(bug342_quick_rule_added_localized_notice_and_error_toast) {
  LangRestoreGuard lg;
  clear_all_notices();

  i18n::I18n::instance().set_language("en");
  UT_CHECK(dispatch_notice("quick_rule_added", "DOMAIN-SUFFIX,openai.com,PROXY"));
  auto snap = events::NoticeService::instance().snapshot();
  UT_CHECK(!snap.empty());
  if (!snap.empty()) {
    const auto& last = snap.back();
    UT_CHECK_EQ(last.type, std::string("success"));
    UT_CHECK_EQ(last.duration_ms, 2500);
    UT_CHECK(last.message.find("DOMAIN-SUFFIX,openai.com,PROXY") !=
             std::string::npos);
  }

  // 兼容旧 status="profile_switched" 且剥离硬编码中文前缀
  clear_all_notices();
  UT_CHECK(dispatch_notice(
      "profile_switched",
      "手动路径控制规则已置顶生效: PROCESS-NAME,chrome.exe,DIRECT"));
  snap = events::NoticeService::instance().snapshot();
  UT_CHECK(!snap.empty());
  if (!snap.empty()) {
    UT_CHECK(snap.back().message.find("PROCESS-NAME,chrome.exe,DIRECT") !=
             std::string::npos);
    UT_CHECK(snap.back().message.find("手动路径控制规则已置顶生效:") ==
             std::string::npos);
  }
  clear_all_notices();
}

// BUG-344: fromProbe=false 缓存复用结果前端保鲜上限钳制在 30s，实时探测保持长 TTL
UT_TEST(bug344_reused_metrics_30s_ttl_cap_vs_probe_ttl) {
  MockNodeFixture fx;
  fx.clock_ms = 100000;
  NodeData data = fx.build();
  UT_CHECK(data.load());

  nlohmann::json reused = {
      {"group", "PROXY"},
      {"fromProbe", false},
      {"results", nlohmann::json::array({{"hk-01", 66}})},
  };
  nlohmann::json probed = {
      {"group", "PROXY"},
      {"fromProbe", true},
      {"results", nlohmann::json::array({{"sg-02", 77}})},
  };
  data.apply_delay_results(reused, false);
  data.apply_delay_results(probed, true);
  UT_CHECK_EQ(data.row_delay("hk-01"), 66);
  UT_CHECK_EQ(data.row_delay("sg-02"), 77);

  // 25s 后：两者均仍在保鲜期内
  fx.clock_ms = 100000 + 25000;
  UT_CHECK_EQ(data.row_delay("hk-01"), 66);
  UT_CHECK_EQ(data.row_delay("sg-02"), 77);

  // 31s 后：fromProbe=false 的缓存复用条目立即过期回 -1，而实时探测仍有效
  fx.clock_ms = 100000 + 31000;
  UT_CHECK_EQ(data.row_delay("hk-01"), -1);
  UT_CHECK_EQ(data.row_delay("sg-02"), 77);
}

// BUG-354: 窗口高 <= 830px 时设置抽屉帮助按钮常驻右上（关闭按钮左侧 28x28）且菜单向下展开不裁切
UT_TEST(bug354_settings_drawer_help_button_visible_below_830_and_downward_menu) {
  make_offscreen(640, 860, 96.0f);
  SettingsDrawer drawer;
  drawer.wire_callbacks();
  drawer.set_open(true);

  // 1) 窄高窗 271x680（<=830）：bottom_help_ 仍可见且停靠在 {271-68, 3, 28, 28}
  drawer.layout_tree({0, 0, 271, 680});
  UT_CHECK(drawer.bottom_help_ != nullptr);
  UT_CHECK(drawer.bottom_help_->visible());
  UT_CHECK(near_f(drawer.bottom_help_->bounds().x, 271.0f - 68.0f));
  UT_CHECK(near_f(drawer.bottom_help_->bounds().y, 3.0f));
  UT_CHECK(near_f(drawer.bottom_help_->bounds().w, 28.0f));
  UT_CHECK(near_f(drawer.bottom_help_->bounds().h, 28.0f));

  // 2) 宽矮窗 640x620（视口总高 620+30+135=785 <= 830，右列挂载 ConnectionsPanel）：
  //    点击帮助按钮向下弹出菜单且不被右列盖住
  drawer.layout_tree({0, 0, 640, 620});
  UT_CHECK(drawer.bottom_help_->visible());
  const RectF hb = drawer.bottom_help_->global_bounds();
  click(&drawer, hb.x + 14.0f, hb.y + 14.0f);
  UT_CHECK(drawer.help_menu() != nullptr);
  UT_CHECK(drawer.help_menu()->is_open());
  const RectF mb = drawer.help_menu()->menu_rect();
  UT_CHECK(mb.y >= hb.bottom() - 1.0f);
  UT_CHECK(mb.right() <= 640.0f);
  UT_CHECK(mb.bottom() <= 620.0f);
  Widget* top_hit = drawer.hit_test({mb.x + 20.0f, mb.y + 16.0f});
  UT_CHECK(top_hit != nullptr && top_hit != drawer.conn_panel());
  drawer.help_menu()->close();

  // 3) 大尺寸高窗 640x860（>830）：帮助按钮回到底部栏左侧 {0, 860-24, 80, 24}
  drawer.layout_tree({0, 0, 640, 860});
  UT_CHECK(drawer.bottom_help_->visible());
  UT_CHECK(near_f(drawer.bottom_help_->bounds().x, 0.0f));
  UT_CHECK(near_f(drawer.bottom_help_->bounds().w, 80.0f));
  clear_input_state();
}

// BUG-355: 全局快捷键 Ctrl+F / F4 / F1 分发与弹窗/输入态/IME/长按连发/Alt+F4 守卫
UT_TEST(bug355_global_shortcuts_ctrl_f_f4_f1_and_guards) {
  UT_CHECK(classify_global_shortcut('F', true, false, false, false, false,
                                    false, false) ==
           GlobalShortcutAction::FocusFilter);
  UT_CHECK(classify_global_shortcut(VK_F4, false, false, false, false, false,
                                    false, false) ==
           GlobalShortcutAction::TestVisibleSubset);
  UT_CHECK(classify_global_shortcut(VK_F1, false, false, false, false, false,
                                    false, false) ==
           GlobalShortcutAction::TestFullGroup);

  // Alt+F4 / Ctrl+F4 绝不误拦截为测速
  UT_CHECK(classify_global_shortcut(VK_F4, false, true, false, false, false,
                                    false, false) ==
           GlobalShortcutAction::None);
  UT_CHECK(classify_global_shortcut(VK_F4, true, false, false, false, false,
                                    false, false) ==
           GlobalShortcutAction::None);

  // 抽屉/弹窗打开、输入控件聚焦、IME 组词中、长按 repeat 均完全屏蔽业务热键
  UT_CHECK(classify_global_shortcut('F', true, false, false, false, false, true,
                                    false) == GlobalShortcutAction::None);
  UT_CHECK(classify_global_shortcut(VK_F4, false, false, false, false, false,
                                    false, true) == GlobalShortcutAction::None);
  UT_CHECK(classify_global_shortcut(VK_F1, false, false, false, true, false,
                                    false, false) ==
           GlobalShortcutAction::None);
  UT_CHECK(classify_global_shortcut(VK_F1, false, false, false, false, true,
                                    false, false) ==
           GlobalShortcutAction::None);

  // NodeList::focus_filter_input 聚焦并全选已有关键字
  TempHomeScope home;
  make_offscreen(271, 400, 96.0f);
  MockNodeFixture fx;
  NodeData data = fx.build();
  UT_CHECK(data.load());
  NodeList nl;
  nl.bind(&data);
  nl.layout_tree({0, 0, 271, 400});
  nl.set_filter("sg");
  nl.focus_filter_input();
  UT_CHECK(nl.search_box() != nullptr);
  UT_CHECK(nl.search_box()->focused());
  UT_CHECK(nl.search_box()->selection() == L"sg");
  clear_input_state();
}

// BUG-356 & BUG-336: 0ms 乐观节点切换 + 失败回滚原节点 + 5000ms 轮换跳过判死阈值统一
UT_TEST(bug356_336_optimistic_node_switch_and_5000ms_cycle_threshold) {
  static_assert(kNodeDelayMaxMs == 5000);
  MockNodeFixture fx;
  NodeData data = fx.build();
  UT_CHECK(data.load());
  UT_CHECK_EQ(data.now(), std::string("hk-01"));

  const std::string prev = data.set_now_optimistic("sg-02");
  UT_CHECK_EQ(prev, std::string("hk-01"));
  UT_CHECK_EQ(data.now(), std::string("sg-02"));

  // 模拟异步 IPC 失败回滚至 prev
  data.set_now_optimistic(prev);
  UT_CHECK_EQ(data.now(), std::string("hk-01"));
}

// BUG-357: 未测节点显示 "-" + 右侧 65px 独立单节点测速热区（预设节点不渲染/不触发）
UT_TEST(bug357_untested_dash_and_single_node_delay_hotzone) {
  LangRestoreGuard lg;
  make_offscreen(260, 40, 96.0f);
  UT_CHECK(format_delay(-1) == L"-");

  NodeRowWidget row;
  int select_clicks = 0;
  int single_tests = 0;
  row.on_click = [&] { ++select_clicks; };
  row.on_test_single = [&] { ++single_tests; };
  row.bind("VLESS", "hk-01", -1, false);
  row.layout_tree({0, 0, 250, kNodeRowH});

  // 悬停右侧 65px 延迟列 → 返回单节点测速提示文案；点击只触发 on_test_single 不切节点
  move_to(&row, 230.0f, 13.0f);
  UT_CHECK(!row.tooltip_text().empty());
  click(&row, 230.0f, 13.0f);
  UT_CHECK_EQ(single_tests, 1);
  UT_CHECK_EQ(select_clicks, 0);

  // 点击左侧/中间名称列 → 只触发 on_click 切节点，不触发单测
  click(&row, 100.0f, 13.0f);
  UT_CHECK_EQ(select_clicks, 1);
  UT_CHECK_EQ(single_tests, 1);

  // 预设节点 DIRECT：右侧无热区提示，点击右侧按普通行选中处理
  NodeRowWidget direct_row;
  int direct_clicks = 0;
  int direct_tests = 0;
  direct_row.on_click = [&] { ++direct_clicks; };
  direct_row.on_test_single = [&] { ++direct_tests; };
  direct_row.bind("Direct", "DIRECT", -1, false);
  direct_row.layout_tree({0, 0, 250, kNodeRowH});
  move_to(&direct_row, 230.0f, 13.0f);
  UT_CHECK(direct_row.tooltip_text().empty());
  click(&direct_row, 230.0f, 13.0f);
  UT_CHECK_EQ(direct_tests, 0);
  UT_CHECK_EQ(direct_clicks, 1);
  clear_input_state();
}

// BUG-358: 主题滑块拖动全程零同步写盘，松手 on_commit / 200ms 定时器 / 关抽屉精准落盘一次
UT_TEST(bug358_skin_slider_drag_zero_sync_io_and_commit_flush) {
  TempHomeScope home;
  make_offscreen(271, 680, 96.0f);
  ThemeService::instance().init(SkinId::Retro3d, ThemeMode::Light);

  SettingsDrawer drawer;
  uint32_t armed_ms = 0;
  int arm_calls = 0;
  drawer.on_arm_skin_persist_timer = [&](uint32_t ms) {
    armed_ms = ms;
    ++arm_calls;
  };
  drawer.wire_callbacks();
  drawer.set_open(true);
  drawer.layout_tree({0, 0, 271, 680});
  reset_skin_prefs_save_count();

  Slider* ds = drawer.theme_card()->depth_slider_;
  UT_CHECK(ds != nullptr);
  const RectF sb = ds->global_bounds();

  // 按下并连续拖动 12 步：ThemeService 实时响应，但同步写盘次数必须为 0！
  press(&drawer, sb.x + 4.0f, sb.y + sb.h * 0.5f);
  for (int i = 1; i <= 12; ++i) {
    const float ratio = static_cast<float>(i) / 15.0f;
    move_to(&drawer, sb.x + sb.w * ratio, sb.y + sb.h * 0.5f, kMouseLeft);
  }
  UT_CHECK_EQ(skin_prefs_save_count(), 0u);
  UT_CHECK(arm_calls > 0);
  UT_CHECK_EQ(armed_ms, 200u);
  UT_CHECK(drawer.has_pending_skin_prefs());

  // 松开鼠标触发 Slider::on_commit → 立即合并落盘恰好 1 次，且后续重复 flush 幂等去重
  release(&drawer, sb.x + sb.w * 0.8f, sb.y + sb.h * 0.5f);
  UT_CHECK_EQ(skin_prefs_save_count(), 1u);
  UT_CHECK(!drawer.has_pending_skin_prefs());
  drawer.flush_pending_skin_prefs();
  UT_CHECK_EQ(skin_prefs_save_count(), 1u);
  clear_input_state();
}

// BUG-359: network_health::* 拦截不弹底部 Toast，驱动 QuickBar 持久告警胶囊与点击自愈重测
UT_TEST(bug359_network_health_notice_interception_and_quickbar_pill) {
  LangRestoreGuard lg;
  make_offscreen(640, 40, 96.0f);
  clear_all_notices();
  set_latest_network_health(NetworkHealthIssue::None);

  QuickBar qb;
  int cycle_calls = 0;
  int retest_calls = 0;
  qb.on_cycle_node = [&] { ++cycle_calls; };
  qb.on_self_heal_retest = [&] { ++retest_calls; };
  qb.set_node_text(L"hk-01");
  qb.set_node_meta("VLESS", L"1.2.3.4:443");
  qb.set_badge(0, true);

  uint64_t sub = subscribe_network_health(
      [&](NetworkHealthIssue issue) { qb.set_network_health(issue); });

  // 1) 派发 network_health::offline：被拦截（0 条 Snackbar Toast），胶囊出现且点击触发自愈重测
  UT_CHECK(dispatch_notice("network_health::offline", ""));
  UT_CHECK(events::NoticeService::instance().snapshot().empty());
  UT_CHECK(qb.network_health() == NetworkHealthIssue::Offline);
  qb.layout_tree({0, 0, 640, 30});
  const RectF pill = qb.health_pill_rect();
  UT_CHECK(pill.w > 20.0f && near_f(pill.h, 20.0f));

  move_to(&qb, pill.x + pill.w * 0.5f, pill.y + pill.h * 0.5f);
  UT_CHECK(!qb.tooltip_text().empty());
  click(&qb, pill.x + pill.w * 0.5f, pill.y + pill.h * 0.5f);
  UT_CHECK_EQ(retest_calls, 1);
  UT_CHECK_EQ(cycle_calls, 0);

  // 2) 派发 network_health::all_dead 与恢复 network_health::ok
  UT_CHECK(dispatch_notice("network_health::all_dead", ""));
  UT_CHECK(events::NoticeService::instance().snapshot().empty());
  UT_CHECK(qb.network_health() == NetworkHealthIssue::AllDead);

  UT_CHECK(dispatch_notice("network_health::ok", ""));
  UT_CHECK(events::NoticeService::instance().snapshot().empty());
  UT_CHECK(qb.network_health() == NetworkHealthIssue::None);
  qb.layout_tree({0, 0, 640, 30});
  UT_CHECK(near_f(qb.health_pill_rect().w, 0.0f));

  unsubscribe_network_health(sub);
  clear_input_state();
}

// BUG-360: 过滤框 700ms 可取消定时器 + 11px 尾部旋转指示器 + Escape 优先取消待测速不清空关键字
UT_TEST(bug360_filter_auto_test_700ms_timer_spinner_and_escape_cancel) {
  TempHomeScope home;
  LangRestoreGuard lg;
  make_offscreen(271, 400, 96.0f);
  MockNodeFixture fx;
  NodeData data = fx.build();
  UT_CHECK(data.load());

  NodeList nl;
  uint32_t armed_ms = 0;
  int cancel_calls = 0;
  nl.on_arm_filter_auto_test_timer = [&](uint32_t ms) { armed_ms = ms; };
  nl.on_cancel_filter_auto_test_timer = [&] { ++cancel_calls; };
  nl.bind(&data);
  nl.layout_tree({0, 0, 271, 400});

  TextField* sb = nl.search_box();
  UT_CHECK(sb != nullptr);
  nl.focus_filter_input();
  // 模拟用户输入字符 'h'（触发 on_text_changed）
  chr(&nl, L'h');
  UT_CHECK_EQ(armed_ms, 700u);
  UT_CHECK(nl.pending_auto_test());
  UT_CHECK(sb->end_spinner_visible());
  UT_CHECK(sb->text() == L"h");

  // 第一次按 Escape：取消 700ms 待执行自动测速并隐藏旋转指示器，保留输入内容 "h"
  key(&nl, VK_ESCAPE);
  UT_CHECK(!nl.pending_auto_test());
  UT_CHECK(!sb->end_spinner_visible());
  UT_CHECK(cancel_calls > 0);
  UT_CHECK(sb->text() == L"h");
  clear_input_state();
}

// BUG-361: 托盘左键单击前台切换隐藏（含 Shell_TrayWnd 抢焦点 450ms 失焦窗口判定）
UT_TEST(bug361_tray_click_foreground_and_450ms_blur_hide_toggle) {
  // 1) 当前在前台激活且可见未最小化 → 收起隐藏
  UT_CHECK(compute_should_tray_click_hide(false, true, false, true, 0, 5000));
  // 2) 点击托盘瞬间被任务栏抢走焦点（失焦 <= 450ms）→ 视为刚在前台，收起隐藏
  UT_CHECK(compute_should_tray_click_hide(false, true, false, false, 4600, 5000));
  UT_CHECK(compute_should_tray_click_hide(false, true, false, false, 4550, 5000));
  // 3) 失焦已超过 450ms（用户在别的窗口工作后点托盘）→ 唤出置前而非隐藏
  UT_CHECK(!compute_should_tray_click_hide(false, true, false, false, 4549, 5000));
  // 4) 处于轻量模式 / 已隐藏 / 已最小化 → 一律唤出恢复，绝不隐藏
  UT_CHECK(!compute_should_tray_click_hide(true, true, false, true, 4900, 5000));
  UT_CHECK(!compute_should_tray_click_hide(false, false, false, true, 4900, 5000));
  UT_CHECK(!compute_should_tray_click_hide(false, true, true, true, 4900, 5000));
}
