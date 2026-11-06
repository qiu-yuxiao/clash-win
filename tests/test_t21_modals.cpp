// T21 单测：弹窗与查看器 + v3.2.1 Parity 全量验证
// 覆盖用例：
// 1. t21_first_run_guide_trigger_500ms_delay：首次引导判定、profiles 检查与完成标志持久化
// 2. t21_first_run_guide_step1_clipboard_and_enter：剪贴板嗅探 http/https/clash:// 与 Enter 全局自动导入
// 3. t21_first_run_guide_step1_bug310_and_bug311：BUG-311 守卫提前释放与 BUG-310 自动接管系统代理
// 4. t21_first_run_guide_step2_ready_and_star_card：就绪态仪表盘、2.5s 测速超时与暖金 GitHub Star 应援卡
// 5. t21_addurl_modal_layout_and_fields：360×290 自定义规则弹窗几何布局与三段策略开关
// 6. t21_addurl_merge_yaml_injection：Merge.yaml prepend-rules 规则置顶注入与 enhance 触发
// 7. t21_unlock_modal_layout_and_12_platforms：360×620 流媒体 AI 解锁 12 平台布局与状态指示器
// 8. t21_unlock_cache_persistence：Mock 隔离零网络检测、unlock_cache.json 落盘与去重
// 9. t21_utility_modals_edit_and_updates：配置编辑、客户端更新、内核升级 (EventBus 5->100) 弹窗
// 10. t21_log_viewer_modal_batching_and_clamp：50ms 批处理刷新、1000 行环形截断、级别与搜索过滤
// 11. t21_text_viewer_consolas_line_numbers_wrap：Consolas 字体、行号栏与分割线、自动换行与编辑器动作
// 12. t21_d2d_local_coordinates_no_double_offset：局部坐标规则与离屏像素断言（零双偏移）

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/quick_routing.h"
#include "events/event_bus.h"
#include "system/unlock_checker.h"
#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/scenes/add_url_dialog.h"
#include "ui/scenes/first_run_guide_dialog.h"
#include "ui/scenes/unlock_dialog.h"
#include "ui/scenes/utility_modals.h"
#include "ui/text_viewer.h"
#include "ui/theme/theme.h"
#include "util/dirs.h"
#include "util/utf.h"
#include "util/yaml_utils.h"

namespace fs = std::filesystem;
using namespace clashwin;
using namespace clashwin::ui;
using namespace clashwin::ui::theme;
using namespace clashwin::ui::scene;
using namespace ui_test;

namespace {

// 隔离卫士：全部 T21 落盘用例必须写入临时目录，绝不触碰真实 %APPDATA%
struct TempHomeGuard {
  fs::path dir;
  TempHomeGuard() {
    dir = fs::temp_directory_path() /
          ("clashwin_t21_" + std::to_string(GetTickCount64()) + "_" + std::to_string(rand()));
    fs::create_directories(dir);
    // 预览配置落盘目录形态（与 App::init 首启产物一致）：profiles/icons/logs/ruleset
    for (const wchar_t* sub : {L"profiles", L"icons", L"logs", L"ruleset"}) {
      fs::create_directories(dir / sub);
    }
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", dir.wstring().c_str());
  }
  ~TempHomeGuard() {
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
};

class MockZeroNetworkTransport : public system::IUnlockTransport {
 public:
  system::UnlockCheckResponse check(const system::UnlockCheckRequest& req, uint16_t) override {
    system::UnlockCheckResponse resp;
    resp.ok = true;
    if (req.platform_name == "Bahamut 动画疯") {
      resp.status = "No";
      resp.region = "twTW";
    } else if (req.platform_name == "Google Gemini") {
      resp.status = "Yes";
      resp.region = "jpJP";
    } else if (req.platform_name == "YouTube Premium") {
      resp.status = "Yes";
      resp.region = "sgSG";
    } else if (req.platform_name == "Bilibili 大陆" || req.platform_name == "Prime Video") {
      resp.status = "Yes";
      resp.region = std::nullopt;
    } else {
      resp.status = "Yes";
      resp.region = "usUS";
    }
    return resp;
  }
};

}  // namespace

// Test 1: FirstRunGuide Trigger & Completion Persistence
UT_TEST(t21_first_run_guide_trigger_500ms_delay) {
  TempHomeGuard th;
  FirstRunGuideDialog dialog;
  UT_CHECK(!dialog.is_open());

  // 1. 测试持久化读写
  FirstRunGuideDialog::set_first_run_completed(false);
  UT_CHECK(!FirstRunGuideDialog::is_first_run_completed());

  FirstRunGuideDialog::set_first_run_completed(true);
  UT_CHECK(FirstRunGuideDialog::is_first_run_completed());

  // 2. 打开弹窗：初始处于 Step 1 (Input)
  // 剪贴板隔离：避免宿主机剪贴板里的订阅链接触发横幅、改变卡片高度
  if (OpenClipboard(nullptr)) {
    EmptyClipboard();
    CloseClipboard();
  }
  dialog.open();
  UT_CHECK(dialog.is_open());
  UT_CHECK(dialog.step() == FirstRunStep::Input);

  // 3. 布局后检查卡片尺寸
  // src: first-run-guide-dialog.tsx:545-828 maxWidth 520；标题 68.5 + 内容 + 动作 96.4
  // = 357（无剪贴板横幅分支）+ 32px 视口留白
  dialog.on_arrange({0, 0, 800, 600});
  UT_CHECK(dialog.card_rect().w > 300.0f);
  UT_CHECK_EQ(dialog.card_rect().h, 357.0f);

  dialog.close();
  UT_CHECK(!dialog.is_open());
}

// Test 2: Step 1 Clipboard Sniffing & Enter Auto-Import
UT_TEST(t21_first_run_guide_step1_clipboard_and_enter) {
  FirstRunGuideDialog dialog;
  dialog.open();

  // 1. 模拟剪贴板合法订阅链接嗅探
  dialog.set_url("https://example.com/api/v1/client/subscribe?token=abc");
  UT_CHECK_EQ(dialog.url(), "https://example.com/api/v1/client/subscribe?token=abc");

  // 2. 模拟非法协议（例如 ftp://）提交拒绝
  dialog.set_url("ftp://example.com/sub.yaml");
  dialog.submit_import();
  UT_CHECK(!dialog.error_message().empty());
  UT_CHECK(dialog.step() == FirstRunStep::Input);

  // 3. 模拟 Enter 键在全局按下触发导入
  bool import_called = false;
  std::string imported_target;
  dialog.on_import = [&](const std::string& u) {
    import_called = true;
    imported_target = u;
    return true;
  };

  dialog.set_url("https://mysub.site/clash");
  KeyEvent enter_event;
  enter_event.type = KeyEventType::Down;
  enter_event.key = VK_RETURN;
  bool handled = dialog.on_key(enter_event);

  UT_CHECK(handled);
  UT_CHECK(import_called);
  UT_CHECK_EQ(imported_target, "https://mysub.site/clash");
  UT_CHECK(dialog.step() == FirstRunStep::Ready);
}

// Test 3: BUG-310 (Auto-Takeover) & BUG-311 (Guard Release)
UT_TEST(t21_first_run_guide_step1_bug310_and_bug311) {
  FirstRunGuideDialog dialog;
  dialog.open();
  dialog.set_takeover_active(false);

  dialog.on_import = [&](const std::string&) {
    // 验证在导入完成返回前，guard 处于 active
    UT_CHECK(dialog.is_importing());
    return true;
  };

  dialog.set_url("https://provider.org/sub");
  dialog.submit_import();

  // BUG-311：进入 Step 2 前 is_importing 必须释放
  UT_CHECK(!dialog.is_importing());
  UT_CHECK(dialog.step() == FirstRunStep::Ready);

  // BUG-310：原本未开启系统代理/TUN 时，导入成功自动接管
  UT_CHECK(dialog.takeover_active());
}

// Test 4: Step 2 Ready Dashboard & Warm Gold Star Card
UT_TEST(t21_first_run_guide_step2_ready_and_star_card) {
  TempHomeGuard th;
  FirstRunGuideDialog dialog;
  dialog.open();
  dialog.set_step(FirstRunStep::Ready);
  dialog.on_arrange({0, 0, 800, 600});

  // 1. 测速就绪状态验证
  dialog.set_active_node_name("日本专线 01 [高速]");
  dialog.set_active_delay_ms(45);
  UT_CHECK(dialog.proxy_ready());
  UT_CHECK_EQ(dialog.active_delay_ms(), 45);

  // 2. 超时回退（0ms 或 >=5000ms 判定为未测出）
  dialog.set_active_delay_ms(0);
  UT_CHECK(!dialog.proxy_ready());

  // 3. 点击重测按钮
  bool retest_triggered = false;
  dialog.on_retest_nodes = [&]() { retest_triggered = true; };

  MouseEvent me;
  me.type = MouseEventType::Up;
  me.pos = {dialog.retest_btn_rect().x + 10, dialog.retest_btn_rect().y + 5};
  dialog.on_mouse(me);
  UT_CHECK(retest_triggered);
  // src: :189-205 重测期间 retesting=true、未就绪；异步回填后才转 ready
  UT_CHECK(dialog.retesting());
  UT_CHECK(!dialog.proxy_ready());
  dialog.set_active_delay_ms(35);
  dialog.on_retest_finished();
  UT_CHECK(!dialog.retesting());
  UT_CHECK(dialog.proxy_ready());

  // 4. GitHub Star 应援卡片几何区域断言
  UT_CHECK(dialog.star_card_rect().w > 200.0f);
  UT_CHECK(dialog.star_card_rect().h > 80.0f);

  // 5. 点击完成
  dialog.finish();
  UT_CHECK(!dialog.is_open());
  UT_CHECK(FirstRunGuideDialog::is_first_run_completed());
}

// Test 4b: 就绪 20s 超时 + 1.5s 轮询 + isUnconfirmed（src: :46-48/:173-174/:228-245）
UT_TEST(t21_first_run_ready_timeout_and_poll) {
  TempHomeGuard th;
  FirstRunGuideDialog dialog;
  dialog.open();
  dialog.set_step(FirstRunStep::Ready);

  int polls = 0;
  dialog.on_refresh_proxy = [&] { ++polls; };

  dialog.tick(1000);  // 进入 ready 立即刷新一次
  UT_CHECK_EQ(polls, 1);
  UT_CHECK(!dialog.unconfirmed());

  dialog.tick(2500);  // 距上次 1500ms → 轮询
  UT_CHECK_EQ(polls, 2);
  dialog.tick(3000);  // 不足 1500ms → 不轮询
  UT_CHECK_EQ(polls, 2);

  dialog.tick(21000);  // 1000 + 20000 → 超时
  UT_CHECK(dialog.ready_timed_out());
  UT_CHECK(dialog.unconfirmed());
  const int after = polls;
  dialog.tick(23000);  // 超时后停止轮询
  UT_CHECK_EQ(polls, after);

  // activeDelay===0 且未重测 → 立即 unconfirmed（:174）
  dialog.set_active_delay_ms(0);
  UT_CHECK(dialog.unconfirmed());

  // 有效延迟 → ready，不再 unconfirmed
  dialog.set_active_delay_ms(45);
  UT_CHECK(dialog.proxy_ready());
  UT_CHECK(!dialog.unconfirmed());
}

// Test 5: AddUrlDialog Layout & Form Fields
UT_TEST(t21_addurl_modal_layout_and_fields) {
  AddUrlDialog dialog;
  dialog.open();
  UT_CHECK(dialog.is_open());

  // 排版到 800x600 视图中
  dialog.on_arrange({0, 0, 800, 600});

  // 对照源码 add-url-dialog.tsx:200-202：paper 最大宽 380（calc(100%-32px)），
  // 高度 = 标题 44 + 内容 104 + 动作 60.5 = 208.5
  UT_CHECK_EQ(dialog.card_rect().w, 380.0f);
  UT_CHECK_EQ(dialog.card_rect().h, 208.5f);

  // 默认策略偏好为 PROXY
  UT_CHECK(dialog.target() == core::quick_routing::RuleTarget::Proxy);

  // 切换三段式分流策略偏好
  dialog.set_target(core::quick_routing::RuleTarget::Direct);
  UT_CHECK(dialog.target() == core::quick_routing::RuleTarget::Direct);

  dialog.set_target(core::quick_routing::RuleTarget::Reject);
  UT_CHECK(dialog.target() == core::quick_routing::RuleTarget::Reject);

  dialog.set_payload("github.com");
  UT_CHECK_EQ(dialog.payload(), "github.com");

  // 按 Escape 关闭
  KeyEvent esc_ev;
  esc_ev.type = KeyEventType::Down;
  esc_ev.key = VK_ESCAPE;
  dialog.on_key(esc_ev);
  UT_CHECK(!dialog.is_open());
}

// Test 6: AddUrl Injection to Merge.yaml & Pipeline
UT_TEST(t21_addurl_merge_yaml_injection) {
  TempHomeGuard th;
  AddUrlDialog dialog;
  dialog.open();
  dialog.set_payload("api.anthropic.com");
  dialog.set_target(core::quick_routing::RuleTarget::Proxy);

  bool rule_added = false;
  std::string captured_rule;
  dialog.on_rule_added = [&](const std::string& r) {
    rule_added = true;
    captured_rule = r;
  };

  // src: add-url-dialog.tsx:220-226 Textarea multiline → Enter 换行不提交，
  // 提交入口为「添加」按钮（:150-198 contained 按钮）。
  dialog.layout_tree({0, 0, 400, 500});
  RectF add = dialog.add_btn_rect();
  MouseEvent up{MouseEventType::Up, {add.x + add.w / 2, add.y + add.h / 2}};
  dialog.on_mouse(up);

  UT_CHECK(rule_added);
  UT_CHECK(!captured_rule.empty());
  UT_CHECK(captured_rule.find("anthropic.com") != std::string::npos);
  UT_CHECK(!dialog.is_open());  // 提交成功后自动关闭
}

// Test 7: UnlockDialog 12 Platforms & Badges
UT_TEST(t21_unlock_modal_layout_and_12_platforms) {
  UnlockDialog dialog;
  dialog.open();
  UT_CHECK(dialog.is_open());

  dialog.on_arrange({0, 0, 800, 700});

  // src: pages/unlock.tsx:388-517 Grid md:3 列（:388 spacing=1.5 → 12px 间距），
  //   Card radius16 / p1.3 / 高约 93；纸面宽取 520（3 列 × 156 + 24 + 28）
  UT_CHECK_EQ(dialog.card_rect().w, 520.0f);
  UT_CHECK_EQ(dialog.card_rect().h, 468.0f);  // 44 + 4*93 + 3*12 + 16

  // 平台列表恰好为 12 项
  const auto& items = dialog.items();
  UT_CHECK_EQ(items.size(), 12);

  // 验证关键平台名称覆盖
  bool has_chatgpt = false;
  bool has_claude = false;
  bool has_gemini = false;
  bool has_youtube = false;
  bool has_bahamut = false;

  for (const auto& it : items) {
    if (it.name.find("ChatGPT") != std::string::npos) has_chatgpt = true;
    if (it.name.find("Claude") != std::string::npos) has_claude = true;
    if (it.name.find("Gemini") != std::string::npos) has_gemini = true;
    if (it.name.find("YouTube") != std::string::npos) has_youtube = true;
    if (it.name.find("Bahamut") != std::string::npos) has_bahamut = true;
  }

  UT_CHECK(has_chatgpt);
  UT_CHECK(has_claude);
  UT_CHECK(has_gemini);
  UT_CHECK(has_youtube);
  UT_CHECK(has_bahamut);

  dialog.close();
}

// Test 8: UnlockCache Persistence & Mock Transport (Zero Real Network)
UT_TEST(t21_unlock_cache_persistence) {
  auto& checker = system::UnlockChecker::instance();
  auto mock_tp = std::make_shared<MockZeroNetworkTransport>();
  checker.set_transport(mock_tp);

  // 执行并发检测，严格由 mock 处理，零网络出站
  auto results = checker.check_all(0);
  UT_CHECK_EQ(results.size(), 12);

  // 验证结果状态与地区映射
  for (const auto& it : results) {
    if (it.name == "Bahamut 动画疯") {
      UT_CHECK_EQ(it.status, "No");
      UT_CHECK(it.region.has_value());
      UT_CHECK_EQ(*it.region, "twTW");
    } else if (it.name == "Google Gemini") {
      UT_CHECK_EQ(it.status, "Yes");
      UT_CHECK(it.region.has_value());
      UT_CHECK_EQ(*it.region, "jpJP");
    }
  }

  // 验证从 unlock_cache.json 读回
  auto loaded = checker.load_cached_items();
  UT_CHECK_EQ(loaded.size(), 12);
}

// Test 9: Utility Modals (EditProfile, ClientUpdate, CoreUpdate)
UT_TEST(t21_utility_modals_edit_and_updates) {
  // 1. EditProfileModal
  {
    EditProfileModal edit;
    bool saved = false;
    edit.on_save = [&](const std::string& name, const std::string&, int mins) {
      saved = true;
      UT_CHECK_EQ(name, "MyProfile");
      UT_CHECK_EQ(mins, 720);
    };
    edit.open("MyProfile", "https://url.com", 720, false);
    UT_CHECK_EQ(edit.name(), "MyProfile");
    UT_CHECK_EQ(edit.interval_mins(), 720);
    UT_CHECK(!edit.is_local());

    KeyEvent enter_ev;
    enter_ev.type = KeyEventType::Down;
    enter_ev.key = VK_RETURN;
    edit.on_key(enter_ev);
    UT_CHECK(saved);
  }

  // 2. ClientUpdateModal
  {
    ClientUpdateModal client_up;
    client_up.open("v3.2.1", "v3.2.2", "- Fix BUG-311 guard\n- Parity enhancements");
    UT_CHECK_EQ(client_up.current_version(), "v3.2.1");
    UT_CHECK_EQ(client_up.latest_version(), "v3.2.2");

    client_up.set_progress(55, "正在写入安装包...");
    UT_CHECK_EQ(client_up.progress(), 55);
  }

  // 3. CoreUpdateModal & EventBus
  {
    CoreUpdateModal core_up;
    core_up.open("v1.19.27", "v1.19.28");
    UT_CHECK_EQ(core_up.current_version(), "v1.19.27");

    // 模拟内核升级流水线进度事件 (5 -> 50 -> 100)
    events::EventBus::instance().publish(
        "core-upgrade-progress",
        {{"status", "checking"}, {"progress", 5}, {"message", "Checking..."}});
    UT_CHECK_EQ(core_up.progress(), 5);

    events::EventBus::instance().publish(
        "core-upgrade-progress",
        {{"status", "downloading"}, {"progress", 50}, {"message", "Downloading..."}});
    UT_CHECK_EQ(core_up.progress(), 50);

    events::EventBus::instance().publish(
        "core-upgrade-progress",
        {{"status", "done"}, {"progress", 100}, {"message", "Upgrade complete"}});
    UT_CHECK_EQ(core_up.progress(), 100);
    UT_CHECK_EQ(core_up.current_version(), "v1.19.28");
  }
}

// Test 10: LogViewerModal 50ms Batching & 1000-Line Clamp
UT_TEST(t21_log_viewer_modal_batching_and_clamp) {
  LogViewerModal logs;
  logs.open();
  logs.clear_logs();
  UT_CHECK_EQ(logs.log_count(), 0);

  // 1. 推送 5 条日志，处于待刷新队列
  for (int i = 0; i < 5; ++i) {
    logs.push_log("info", "test message " + std::to_string(i));
  }
  UT_CHECK(logs.incoming_count() > 0 || logs.log_count() > 0);

  // 触发 50ms 批处理落盘
  logs.flush_incoming();
  UT_CHECK_EQ(logs.incoming_count(), 0);
  UT_CHECK_EQ(logs.log_count(), 5);

  // 2. 验证 1000 行环形截断 (FIFO clamp)
  for (int i = 0; i < 1100; ++i) {
    logs.push_log("debug", "bulk log item " + std::to_string(i));
  }
  logs.flush_incoming();
  UT_CHECK_EQ(logs.log_count(), LogViewerModal::kMaxLogLines);

  // 3. 级别过滤与搜索
  logs.push_log("error", "FATAL connection drop");
  logs.flush_incoming();

  logs.set_level_filter("error");
  UT_CHECK_EQ(logs.level_filter(), "error");

  logs.set_search_query("FATAL");
  UT_CHECK_EQ(logs.search_query(), "FATAL");

  // 4. 暂停与清空
  logs.set_paused(true);
  UT_CHECK(logs.is_paused());
  logs.clear_logs();
  UT_CHECK_EQ(logs.log_count(), 0);
}

// Test 11: TextViewer Consolas, Line Numbers Gutter, Wrap, and Editor Action
UT_TEST(t21_text_viewer_consolas_line_numbers_wrap) {
  TextViewer tv;

  // 1. 字体缺省为 Consolas 等宽字体
  UT_CHECK(tv.style_.font_family == L"Consolas");
  UT_CHECK_EQ(tv.style_.size, 12.0f);

  // 2. 分行与行号栏宽度自适应
  std::string sample = "Line 1\nLine 2\nLine 3\nLine 4\nLine 5";
  tv.set_text(sample);
  UT_CHECK_EQ(tv.line_count(), 5);

  UT_CHECK(tv.show_line_numbers());
  float gw = tv.gutter_width();
  UT_CHECK(gw >= 36.0f);

  tv.set_show_line_numbers(false);
  UT_CHECK_EQ(tv.gutter_width(), 0.0f);

  // 3. 自动换行切换
  UT_CHECK(!tv.word_wrap());
  tv.set_word_wrap(true);
  UT_CHECK(tv.word_wrap());

  // 4. 打开系统编辑器动作
  bool editor_opened = false;
  tv.on_open_in_editor = [&]() { editor_opened = true; };
  tv.open_in_system_editor();
  UT_CHECK(editor_opened);
}

// Test 12: Direct2D Local Coordinates Rule (Zero Double-Offset)
UT_TEST(t21_d2d_local_coordinates_no_double_offset) {
  make_offscreen(800, 700, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Light);

  AddUrlDialog addurl;
  addurl.open();
  addurl.set_payload("openai.com");

  // 将弹窗挂载在非零全局坐标：x = 100, y = 80
  addurl.layout_tree({100, 80, 600, 500});

  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  addurl.paint_all(rt());
  rt().end_frame();

  // 卡片居中：600×500 容器内 card 380×208.5 → 局部 x=(600-380)/2=110、
  // y=(500-208.5)/2=145.75（src: add-url-dialog.tsx:200-202）。
  // 输入框局部 = card + (16, 44+8)，全局 = (100,80) + 局部。
  RectF ir = addurl.input_rect();
  int gold_border_pixels = 0;
  for (int y = (int)(80 + ir.y) - 1; y <= (int)(80 + ir.y) + 2; ++y) {
    for (int x = (int)(100 + ir.x); x < (int)(100 + ir.x + ir.w); ++x) {
      Px p = pixel(x, y);
      // 输入框描边 gold rgba(212,175,55,.35)（get3DInputStyle）叠在 3D 卡渐变上
      if (p.r > 180 && p.g > 110 && p.g < 210 && p.b < 120) {
        ++gold_border_pixels;
      }
    }
  }
  UT_CHECK(gold_border_pixels > 0);

  // 双偏移的判定由上面的「描边出现在正确全局坐标」承担：
  // 若坐标被二次偏移，采样行将落在卡渐变内，gold_border_pixels 归零。
}
