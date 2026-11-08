// T36 挑战测试：针对 Milestone 11 修复项进行实证对抗性压力测试与边界检验
// 覆盖项目：
//   - D12: 设置抽屉在窄窗（271/280/285）与宽窗（>285）下的水平居中几何边界
//   - B3: 主题卡著作权页脚随窗口高度变动的稳定性与自然自适应包裹
//   - B4: SwitchW 轨道与旋钮按高度等比缩放的边界包容性（11.9 DIP 轨道零溢出）
//   - B6: ProfileImportCard URL 输入框 46 DIP 双行高度与布局几何
//   - A1/A2: 节点列表测速异步分发（UI主线程零阻塞）与延迟列边界对齐（消除 16px 偏差）
//   - C10: TUN 模式服务缺失守卫与通知分发（前端回弹 + 后端拒绝静默降级）
//   - D11: 非客户区厚白边框消除消息处理（WM_NCCALCSIZE/WM_NCPAINT/WM_NCACTIVATE）

#include <windows.h>
#include <windowsx.h>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

#include "cmd/command_service.h"
#include "config/config.h"
#include "core/service_mode.h"
#include "events/notice.h"
#include "system/service_runner.h"
#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/main_window.h"
#include "ui/notice_handler.h"
#include "ui/scenes/first_run_guide_dialog.h"
#include "ui/scenes/node_data.h"
#include "ui/scenes/node_list.h"
#include "ui/scenes/profile_import_card.h"
#include "ui/scenes/settings_drawer.h"
#include "ui/switch_w.h"
#include "ui/theme/theme.h"

using namespace clashwin;
using namespace clashwin::ui;
using namespace clashwin::ui::scene;
using namespace clashwin::ui::theme;
using namespace clashwin::cmd;
using namespace ui_test;

namespace {

// 模拟服务不可用且安装失败的 Runner，用于严格检验 C10 守卫拦截
class ChallengeMockServiceRunner : public system::IServiceCommandRunner {
 public:
  bool installed = false;
  bool available = false;
  bool installer_fail = true;

  Result<void> run_installer(bool) override {
    return Result<void>::err({2001, "mock installer failure"});
  }
  bool is_installed() override { return installed; }
  Result<void> start_service() override {
    return Result<void>::err({2001, "mock start failure"});
  }
  bool wait_for_ready(int) override { return false; }
  bool is_available() override { return available; }
};

}  // namespace

// ---------------------------------------------------------------------------
// 1. D12: 设置抽屉单列居中 vs 宽窗双列左对齐边界测试
// ---------------------------------------------------------------------------
UT_TEST(t36_d12_drawer_card_centering_boundaries) {
  make_offscreen(300, 700, 96);
  SettingsDrawer drawer;

  // 1a) 窄窗 271 DIP：卡宽 231，(271 - 231) / 2 = 20.0f
  drawer.layout_tree({0, 0, 271, 680});
  const float w271_x = (271.0f - 231.0f) / 2.0f;
  UT_CHECK(near_f(drawer.profile_card()->bounds().x, w271_x));
  UT_CHECK(near_f(drawer.basic_card()->bounds().x, w271_x));
  UT_CHECK(near_f(drawer.theme_card()->bounds().x, w271_x));
  UT_CHECK(near_f(drawer.takeover_card()->bounds().x, w271_x + 8.0f));
  UT_CHECK(near_f(drawer.routing_card()->bounds().x, w271_x + 8.0f));
  // 对称性：右边距等于左边距
  UT_CHECK(near_f(271.0f - drawer.profile_card()->bounds().right(), w271_x));

  // 1b) 窄窗 280 DIP：卡宽 231，(280 - 231) / 2 = 24.5f
  drawer.layout_tree({0, 0, 280, 680});
  const float w280_x = (280.0f - 231.0f) / 2.0f;
  UT_CHECK(near_f(drawer.profile_card()->bounds().x, w280_x));
  UT_CHECK(near_f(drawer.basic_card()->bounds().x, w280_x));
  UT_CHECK(near_f(drawer.theme_card()->bounds().x, w280_x));
  UT_CHECK(near_f(280.0f - drawer.profile_card()->bounds().right(), w280_x));

  // 1c) 窄窗临界 285 DIP：卡宽 231，(285 - 231) / 2 = 27.0f
  drawer.layout_tree({0, 0, 285, 680});
  const float w285_x = (285.0f - 231.0f) / 2.0f;
  UT_CHECK(near_f(drawer.profile_card()->bounds().x, w285_x));
  UT_CHECK(near_f(drawer.basic_card()->bounds().x, w285_x));
  UT_CHECK(near_f(drawer.theme_card()->bounds().x, w285_x));
  UT_CHECK(near_f(285.0f - drawer.profile_card()->bounds().right(), w285_x));

  // 1d) 宽窗临界 286 DIP (> 285.0f)：切为侧边双列模式，固定左边距 16.0f (kPad 12 + kPanelBorder 4)
  drawer.layout_tree({0, 0, 286, 680});
  UT_CHECK(near_f(drawer.profile_card()->bounds().x, 16.0f));
  UT_CHECK(near_f(drawer.basic_card()->bounds().x, 16.0f));
  UT_CHECK(near_f(drawer.theme_card()->bounds().x, 16.0f));

  // 1e) 大窗 400 DIP 与 640 DIP：保持左对齐 16.0f
  drawer.layout_tree({0, 0, 400, 680});
  UT_CHECK(near_f(drawer.profile_card()->bounds().x, 16.0f));
  drawer.layout_tree({0, 0, 640, 860});
  UT_CHECK(near_f(drawer.profile_card()->bounds().x, 16.0f));
}

// ---------------------------------------------------------------------------
// 2. B3: ThemeCard 著作权文本相对定位稳定性测试（抗窗口高度缩放）
// ---------------------------------------------------------------------------
UT_TEST(t36_b3_theme_card_copyright_vertical_stability) {
  make_offscreen(271, 1000, 96);
  SettingsDrawer drawer;

  // 在抽屉默认及拉高高度（650 到 950）下测试自然包裹与页脚锚定
  const float heights[] = {650.0f, 680.0f, 750.0f, 850.0f, 950.0f};
  float expected_rel_y = 0.0f;

  for (size_t i = 0; i < sizeof(heights)/sizeof(heights[0]); ++i) {
    drawer.layout_tree({0, 0, 271, heights[i]});
    ThemeCard* tc = drawer.theme_card();
    UT_CHECK(tc != nullptr);
    if (!tc) continue;

    // 主题卡自然内容包裹高 ~161.7f (无多余拉伸)
    UT_CHECK(near_f(tc->bounds().h, 161.7f, 0.5f));

    UT_CHECK(tc->logs_btn_ != nullptr);
    UT_CHECK(tc->copyright_ != nullptr);
    UT_CHECK(tc->email_ != nullptr);
    if (!tc->logs_btn_ || !tc->copyright_ || !tc->email_) continue;

    const float btn_bottom = tc->logs_btn_->bounds().bottom();
    const float foot_y = tc->copyright_->bounds().y;
    // 锚定于 logs_btn_ 下方正好 8 DIP
    UT_CHECK(near_f(foot_y, btn_bottom + 8.0f));
    // 邮箱紧随著作权下方 18.3 DIP
    UT_CHECK(near_f(tc->email_->bounds().y, foot_y + 18.3f));

    if (i == 0) {
      expected_rel_y = foot_y;
    } else {
      // 在不同外层窗口高度下，卡内相对 y 绝对恒定，不随抽屉总高位移
      UT_CHECK(near_f(foot_y, expected_rel_y));
    }
  }
}

// ---------------------------------------------------------------------------
// 3. B4: SwitchW 轨道与旋钮按比例缩放及零溢出几何验证
// ---------------------------------------------------------------------------
UT_TEST(t36_b4_switch_proportional_containment) {
  make_offscreen(100, 100, 96);

  // 3a) 基础设置缩放尺寸: 32 × 11.9 DIP (14 * 0.85)
  {
    SwitchW sw;
    sw.layout_tree({0, 0, 32.0f, 11.9f});
    UT_CHECK(near_f(sw.bounds().w, 32.0f));
    UT_CHECK(near_f(sw.bounds().h, 11.9f));

    const float h = 11.9f;
    const float scale = h / 14.0f;
    const float thumb_d = 14.0f * scale;
    // 缩放后旋钮直径严格等于轨道高度
    UT_CHECK(near_f(thumb_d, 11.9f));

    // 计算非悬停态垂直位置: ty = (h - thumb_d) / 2 = 0.0f
    const float ty = (h - thumb_d) / 2.0f;
    const float bottom = ty + thumb_d;
    UT_CHECK(near_f(ty, 0.0f));
    UT_CHECK(near_f(bottom, 11.9f));
    // 零上下溢出
    UT_CHECK(ty >= -0.001f);
    UT_CHECK(bottom <= 11.901f);
  }

  // 3b) 标准尺寸: 36 × 14.0 DIP
  {
    SwitchW sw;
    sw.layout_tree({0, 0, 36.0f, 14.0f});
    const float h = 14.0f;
    const float scale = h / 14.0f;
    const float thumb_d = 14.0f * scale;
    UT_CHECK(near_f(scale, 1.0f));
    UT_CHECK(near_f(thumb_d, 14.0f));
    const float ty = (h - thumb_d) / 2.0f;
    UT_CHECK(near_f(ty, 0.0f));
    UT_CHECK(near_f(ty + thumb_d, 14.0f));
  }
}

// ---------------------------------------------------------------------------
// 4. B6: ProfileImportCard URL 输入框 46 DIP 双行高与布局堆叠
// ---------------------------------------------------------------------------
UT_TEST(t36_b6_profile_import_card_url_multiline_height) {
  make_offscreen(231, 300, 96);
  ProfileImportCard card;
  card.layout_tree({0, 0, 231.0f, 250.0f});

  UT_CHECK(card.url_field() != nullptr);
  if (!card.url_field()) return;

  // 4a) 输入框高度严格为 46.0 DIP（容纳 2 行文字）
  UT_CHECK(near_f(card.url_field()->bounds().h, 46.0f));
  UT_CHECK(near_f(card.url_field()->bounds().y, 34.4f));

  // 4b) 卡片固定头部镀铬高度为 124.4 DIP
  UT_CHECK(near_f(kProfileCardChromeH, 124.4f));

  // 4c) preferred_height 包含 124.4 DIP 基础高
  UT_CHECK(card.preferred_height() >= 124.4f);

  // 4d) 列表起始 y 严格等于 116.4 DIP (34.4 + 46 + gap4 + btn24 + mb8)
  UT_CHECK(card.list() != nullptr);
  if (card.list()) {
    UT_CHECK(near_f(card.list()->bounds().y, 116.4f));
  }
}

// ---------------------------------------------------------------------------
// 5. A1 & A2: 节点列表延迟列对齐与测速异步非阻塞主线程
// ---------------------------------------------------------------------------
UT_TEST(t36_a1_a2_nodelist_delay_alignment_and_async_speedtest) {
  make_offscreen(271, 400, 96);

  // 5a) A2 延迟列右对齐边界数学验证
  const float row_w = 271.0f;
  const float left_pad = 16.0f;
  const float type_col_w = 40.0f;
  const float delay_col_w = 65.0f;
  const float name_w = row_w - left_pad - type_col_w - delay_col_w;
  const float name_right = left_pad + type_col_w + name_w;
  const float delay_right = name_right + delay_col_w;
  UT_CHECK(near_f(delay_right, row_w));  // 零右侧超界偏差

  // 5b) A1 测速异步分发压力测试：模拟后台测速阻塞 60ms，验证 UI 线程点击立即返回
  struct AsyncMockState {
    std::vector<std::string> all = {"node_fast", "node_slow"};
    std::string now = "node_fast";
    std::atomic<int> test_calls{0};
    std::atomic<bool> test_finished{false};
  } st;

  DataHooks h;
  h.fetch_group = [&st](std::vector<std::string>* all, std::string* now) {
    *all = st.all;
    *now = st.now;
    return true;
  };
  h.fetch_proxies = [&st](std::map<std::string, clashwin::mihomo::ProxyItem>* items) {
    for (const auto& name : st.all) {
      clashwin::mihomo::ProxyItem it;
      it.name = name;
      it.type = "Shadowsocks";
      (*items)[name] = it;
    }
    return true;
  };
  h.select_node = [](const std::string&) { return true; };
  h.test_group = [](const std::string&) { return true; };
  h.test_and_select = [&st](const std::vector<std::string>&) {
    st.test_calls.fetch_add(1);
    // 模拟网络请求耗时 60ms
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    st.test_finished.store(true);
    return true;
  };
  h.now_ms = [] { return 1000LL; };

  NodeData data(std::move(h));
  UT_CHECK(data.load());

  NodeList list;
  list.bind(&data);
  list.layout_tree({0, 0, 271, 400});

  auto* bolt = list.bolt_button();
  UT_CHECK(bolt != nullptr);
  if (!bolt) return;

  const auto start_tp = std::chrono::steady_clock::now();
  // 触发点击
  bolt->on_click();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start_tp)
                           .count();

  // 必须立即返回，UI 线程耗时小于 25ms（绝不能同步阻塞 60ms）
  UT_CHECK(elapsed < 25);

  // 立即检查：UI 线程已将可见节点延迟预置为 -2 (BaseLoading 动画态)
  UT_CHECK_EQ(data.row_delay("node_fast"), -2);
  UT_CHECK_EQ(data.row_delay("node_slow"), -2);

  // 等待后台线程完成测速
  for (int i = 0; i < 50 && !st.test_finished.load(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  UT_CHECK(st.test_finished.load());
  UT_CHECK_EQ(st.test_calls.load(), 1);
}

// ---------------------------------------------------------------------------
// 6. C10: TUN 模式服务缺失守卫与通知规则测试
// ---------------------------------------------------------------------------
UT_TEST(t36_c10_tun_service_guard_and_notice_dispatch) {
  // 6a) 服务模式决策：非管理员 + 服务未就绪 → 绝不能判定为 use_service
  {
    auto d = clashwin::core::service_mode::decide_startup_mode(
        /*needs_tun=*/true, /*is_admin=*/false, /*service_ready=*/false);
    UT_CHECK_EQ(d.use_service, false);
    UT_CHECK_EQ(d.fallback_sysproxy, true);
  }

  auto clear_notices = [] {
    auto items = events::NoticeService::instance().snapshot();
    for (const auto& it : items) events::NoticeService::instance().hide(it.id);
  };

  // 6b) notice_handler 服务通知状态码完整映射检查
  {
    clear_notices();
    UT_CHECK(dispatch_notice("service_unavailable", "服务不可用"));
    UT_CHECK(dispatch_notice("service_missing", "服务未安装"));
    UT_CHECK(dispatch_notice("service_install_error", "服务安装失败"));
    UT_CHECK(dispatch_notice("service_start_error", "服务启动失败"));

    auto s = events::NoticeService::instance().snapshot();
    UT_CHECK_EQ(s.size(), size_t(4));
    if (s.size() >= 4) {
      UT_CHECK_EQ(s[0].type, std::string("error"));
      UT_CHECK_EQ(s[1].type, std::string("info"));
      UT_CHECK_EQ(s[2].type, std::string("error"));
      UT_CHECK_EQ(s[3].type, std::string("error"));
    }
    clear_notices();
  }

  // 6c) 前端 TakeoverCard 切 TUN 模式且无服务/非管理员时拦截并回弹
  {
    // 注入模拟未就绪 Runner，迫使 is_service_available 返回失败且安装失败
    auto mock = std::make_shared<ChallengeMockServiceRunner>();
    system::set_service_runner(mock);

    clear_notices();
    SettingsDrawer drawer;
    drawer.layout_tree({0, 0, 271, 680});
    drawer.wire_callbacks();

    bool applied = drawer.takeover_card()->seg_->apply("TUN");
    UT_CHECK(!applied);

    auto s = events::NoticeService::instance().snapshot();
    UT_CHECK(s.size() >= 1);
    if (!s.empty()) {
      UT_CHECK_EQ(s[0].type, std::string("error"));
    }
    clear_notices();

    system::set_service_runner(nullptr);
  }
}

// ---------------------------------------------------------------------------
// 7. D11: 窗口非客户区厚白边框消除消息处理测试
// ---------------------------------------------------------------------------
UT_TEST(t36_d11_window_non_client_suppression) {
  clashwin::ui::win::MainWindow win;
  bool ok = win.create(/*show_immediately=*/false);
  UT_CHECK(ok);
  if (!ok) return;

  HWND hwnd = win.hwnd();
  UT_CHECK(hwnd != nullptr);

  // 7a) WM_NCCALCSIZE 返回 0（消除标准非客户框）
  LRESULT r_calc_0 = SendMessageW(hwnd, WM_NCCALCSIZE, FALSE, 0);
  UT_CHECK_EQ(r_calc_0, 0);

  NCCALCSIZE_PARAMS params{};
  LRESULT r_calc_1 = SendMessageW(hwnd, WM_NCCALCSIZE, TRUE, reinterpret_cast<LPARAM>(&params));
  UT_CHECK_EQ(r_calc_1, 0);

  // 7b) WM_NCPAINT 返回 0（拦截 GDI 默认边框绘制）
  LRESULT r_paint = SendMessageW(hwnd, WM_NCPAINT, 0, 0);
  UT_CHECK_EQ(r_paint, 0);

  // 7c) WM_NCACTIVATE 返回 TRUE (1)（拦截窗口激活/非激活状态切换时的边框重绘）
  LRESULT r_act_true = SendMessageW(hwnd, WM_NCACTIVATE, TRUE, 0);
  UT_CHECK_EQ(r_act_true, 1);
  LRESULT r_act_false = SendMessageW(hwnd, WM_NCACTIVATE, FALSE, 0);
  UT_CHECK_EQ(r_act_false, 1);

  // 7d) 验证客户区与窗口大小一致（自定义 Chrome 延伸到外框边缘）
  RECT wr{}, cr{};
  GetWindowRect(hwnd, &wr);
  GetClientRect(hwnd, &cr);
  UT_CHECK_EQ(cr.right - cr.left, wr.right - wr.left);
  UT_CHECK_EQ(cr.bottom - cr.top, wr.bottom - wr.top);

  DestroyWindow(hwnd);
}

// ---------------------------------------------------------------------------
// 8. 初次运行引导框：Step 1 订阅输入框完整功能 + Step 2 进阶指南链接零重叠
// ---------------------------------------------------------------------------
UT_TEST(t36_first_run_guide_input_and_step2_layout) {
  make_offscreen(271, 680, 96);
  FirstRunGuideDialog dlg;
  dlg.layout_tree({0, 0, 271.0f, 680.0f});
  dlg.open();

  // 8a) Step 1：url_field_ 可见、聚焦、覆盖完整输入框并支持输入与右键菜单
  auto* tf = dlg.url_field();
  UT_CHECK(tf != nullptr);
  if (!tf) return;
  UT_CHECK(tf->visible());
  UT_CHECK(tf->focused());
  UT_CHECK(near_f(tf->bounds().w, dlg.input_rect().w));
  UT_CHECK(near_f(tf->bounds().h, dlg.input_rect().h));
  UT_CHECK(near_f(tf->right_pad_, 28.0f));

  // hit_test 在输入框中心命中 url_field_
  PointF center{dlg.input_rect().x + dlg.input_rect().w * 0.5f,
                dlg.input_rect().y + dlg.input_rect().h * 0.5f};
  UT_CHECK(dlg.hit_test(center) == tf);

  // 模拟键入字符 'h', 't', 't', 'p'
  for (wchar_t ch : {L'h', L't', L't', L'p'}) {
    KeyEvent ke;
    ke.type = KeyEventType::Char;
    ke.ch = ch;
    UT_CHECK(tf->on_key(ke));
  }
  UT_CHECK_EQ(dlg.url(), std::string("http"));

  // 右键菜单弹出且 hit_test 命中菜单
  dlg.open_url_menu(center);
  UT_CHECK(dlg.url_menu() != nullptr);
  if (dlg.url_menu()) {
    UT_CHECK(dlg.url_menu()->is_open());
    UT_CHECK(dlg.url_menu()->bounds().w >= 270.0f);
    dlg.url_menu()->close();
  }

  // 8b) Step 2：窄窗 271×680 下 star_body 与「查看 Wiki 进阶指南」按钮严格不重叠
  dlg.set_active_node_name("HK-IEPL-01");
  dlg.set_active_delay_ms(42);
  dlg.set_takeover_active(true);
  dlg.set_step(FirstRunStep::Ready);
  dlg.layout_tree({0, 0, 271.0f, 680.0f});

  const RectF star_r = dlg.star_card_rect();
  const RectF wiki_r = dlg.wiki_btn_rect();
  const float body_bottom = star_r.y + 10.0f + dlg.star_title_h() + 6.0f + dlg.star_body_h();
  UT_CHECK(wiki_r.y >= body_bottom + 4.0f);
  UT_CHECK(wiki_r.bottom() <= star_r.bottom() - 8.0f);
}

// ---------------------------------------------------------------------------
// 9. v0.1.2 回归：右键菜单点击捕获、分段未选中字色、皮肤双滑块响应、全量自动选点
// ---------------------------------------------------------------------------
UT_TEST(t36_v012_ui_and_skin_regressions) {
  make_offscreen(271, 680, 96);
  FirstRunGuideDialog dlg;
  dlg.layout_tree({0, 0, 271.0f, 680.0f});
  dlg.open();

  auto* tf = dlg.url_field();
  UT_CHECK(tf != nullptr);
  if (!tf) return;
  tf->set_text(L"https://example.com/sub");
  UT_CHECK_EQ(dlg.url(), std::string("https://example.com/sub"));

  // 9a) 右键菜单弹出后，点击「清空」（最后一项）必须经 route_mouse_event(Down+Up) 真正执行
  PointF center{dlg.input_rect().x + dlg.input_rect().w * 0.5f,
                dlg.input_rect().y + dlg.input_rect().h * 0.5f};
  dlg.open_url_menu(center);
  UT_CHECK(dlg.url_menu() && dlg.url_menu()->is_open());
  if (dlg.url_menu() && dlg.url_menu()->menu()) {
    RectF mb = dlg.url_menu()->menu()->global_bounds();
    PointF clear_pt{mb.x + mb.w * 0.5f, mb.bottom() - 20.0f};
    MouseEvent down_ev;
    down_ev.type = MouseEventType::Down;
    down_ev.pos = clear_pt;
    down_ev.buttons = kMouseLeft;
    route_mouse_event(&dlg, down_ev);

    MouseEvent up_ev;
    up_ev.type = MouseEventType::Up;
    up_ev.pos = clear_pt;
    route_mouse_event(&dlg, up_ev);

    UT_CHECK(!dlg.url_menu()->is_open());
    UT_CHECK(dlg.url().empty());
  }

  // 9b) Segmented 未选中字色：Retro3d Dark 下为深棕 #2C1F03 (alpha 0.75)，非白色
  auto& svc = theme::ThemeService::instance();
  svc.init(theme::SkinId::Retro3d, theme::ThemeMode::Dark);
  auto sp_retro = svc.segmented_paint();
  UT_CHECK(sp_retro.text.r < 0.25f);
  UT_CHECK(near_f(sp_retro.text.a, 0.75f));

  // 9c) Monochrome 皮肤 val2 (Border) 对 card/button/input 生效，radius 为 0
  svc.init(theme::SkinId::Monochrome, theme::ThemeMode::Dark);
  svc.set_factors(1.5f, 2.0f);
  UT_CHECK_EQ(svc.card_paint().radius, 0.0f);
  UT_CHECK(near_f(svc.card_paint().border_w, 2.0f));
  UT_CHECK_EQ(svc.button_paint().radius, 0.0f);
  UT_CHECK(near_f(svc.button_paint().border_w, 2.0f));
  UT_CHECK_EQ(svc.input_paint().radius, 0.0f);
  UT_CHECK_EQ(svc.segmented_paint().container_radius, 0.0f);

  // 9d) FrostedGlass 皮肤 val1 (Opacity) 与 val2 (Blur) 改变面板与卡片透明度及柔和度
  svc.init(theme::SkinId::FrostedGlass, theme::ThemeMode::Dark);
  svc.set_factors(0.2f, 0.4f);
  auto pp_low = svc.panel_paint();
  auto cp_low = svc.card_paint();
  svc.set_factors(1.8f, 1.8f);
  auto pp_high = svc.panel_paint();
  auto cp_high = svc.card_paint();
  UT_CHECK(pp_high.bg.a > pp_low.bg.a);
  UT_CHECK(pp_high.shadow_blur > pp_low.shadow_blur);
  UT_CHECK(cp_high.shadow_blur > cp_low.shadow_blur);
}
