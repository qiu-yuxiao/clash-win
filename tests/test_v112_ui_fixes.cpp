// Clash Win v1.1.2 UI 修复回归单测套件：
// 1. 帮助与工具 Button 空文本图标委托与 HelpOutlineRounded 矢量绘制
// 2. 最大化与拖宽切换 Breakpoints / MainScene / CanvasGraph 随宽自适应与 3 列残行竖框消弭
// 3. 节点列表选中态「- 当前节点」仅在剩余宽度足够时并列显示、3列窄列自动挤出不遮挡节点名
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "cmd/cmd_install.h"
#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "config/config.h"
#include "config/prfitem.h"
#include "core/core_manager.h"
#include "enhance/enhance.h"
#include "events/event_bus.h"
#include "i18n/i18n.h"
#include "mihomo/pipe_http.h"
#include "mihomo/pipe_ws.h"
#include "mihomo/ws_manager.h"
#include "mock_pipe_server.h"
#include "system/tray.h"
#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/button.h"
#include "ui/icon_w.h"
#include "ui/main_scene.h"
#include "ui/main_window.h"
#include "ui/scenes/node_list.h"
#include "ui/scenes/settings_drawer.h"
#include "ui/scenes/traffic_pane.h"
#include "ui/theme/theme.h"
#include "util/dirs.h"
#include "yaml-cpp/yaml.h"

using namespace clashwin;
using namespace clashwin::ui;
using namespace clashwin::ui::scene;
using namespace clashwin::ui::theme;
using namespace clashwin::ui::win;
using namespace ui_test;

namespace {

struct LangGuard {
  std::string prev;
  LangGuard() {
    auto& inst = i18n::I18n::instance();
    if (!inst.inited()) {
      const std::wstring exe_locales = util::dirs::exe_dir() + L"\\locales";
      if (!inst.init(exe_locales, "zh")) {
        inst.init(util::dirs::app_resources_dir() + L"\\locales", "zh");
      }
    }
    prev = inst.language();
    inst.set_language("zh");
  }
  ~LangGuard() {
    if (!prev.empty()) i18n::I18n::instance().set_language(prev);
  }
};

struct DrawnTextCall {
  std::wstring text;
  RectF rect;
};

class RecorderRenderTarget : public IRenderTarget {
 public:
  std::vector<DrawnTextCall> texts;
  std::vector<RectF> filled_rects;
  std::vector<std::pair<PointF, PointF>> lines;
  int gradient_fills = 0;
  SizeF canvas{640.0f, 860.0f};
  PointF origin_{};

  void clear(const Color4f&) override {}
  void fill_rect(const RectF& r, const Brush&) override {
    filled_rects.push_back(r);
  }
  void round_rect(const RectF&, float, const Pen&, const Brush*) override {}
  void draw_line(const PointF& a, const PointF& b, const Pen&) override {
    lines.push_back({a, b});
  }
  void fill_polygon_vertical_gradient(const PointF*, int, float, float,
                                      const Color4f&,
                                      const Color4f&) override {
    ++gradient_fills;
  }
  void draw_polyline(const PointF*, int, const Pen&) override {}
  void draw_text(const std::wstring& text, const RectF& r,
                 const TextStyle&) override {
    texts.push_back({text, r});
  }
  void draw_bitmap(uint64_t, const RectF&, float) override {}
  void push_clip(const RectF&) override {}
  void pop_clip() override {}
  void set_origin(const PointF& global_origin) override { origin_ = global_origin; }
  PointF origin() const override { return origin_; }
  float scale() const override { return 1.0f; }
  SizeF size() const override { return canvas; }
  bool is_lost() const override { return false; }
};

int diff_pixels_against_blank_button(Button* target_btn, int w, int h) {
  Button blank_btn(L"");
  blank_btn.themed_ = target_btn->themed_;
  blank_btn.theme_color_type_ = target_btn->theme_color_type_;
  blank_btn.theme_outlined_ = target_btn->theme_outlined_;
  blank_btn.text_style = target_btn->text_style;
  blank_btn.layout_tree({0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)});

  rt().begin_frame();
  rt().clear({0.1f, 0.1f, 0.1f, 1.0f});
  blank_btn.on_paint(rt());
  rt().end_frame();
  std::vector<uint8_t> blank_buf;
  int bw = 0, bh = 0;
  if (!rt().read_pixels(blank_buf, bw, bh)) return 0;

  rt().begin_frame();
  rt().clear({0.1f, 0.1f, 0.1f, 1.0f});
  target_btn->set_bounds({0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)});
  target_btn->on_paint(rt());
  rt().end_frame();
  std::vector<uint8_t> help_buf;
  int hw = 0, hh = 0;
  if (!rt().read_pixels(help_buf, hw, hh)) return 0;
  if (bw != hw || bh != hh) return 0;

  int diff = 0;
  const int limit_y = (std::min)(h, bh);
  const int limit_x = (std::min)(w, bw);
  for (int y = 0; y < limit_y; ++y) {
    for (int x = 0; x < limit_x; ++x) {
      const size_t idx = (static_cast<size_t>(y) * bw + x) * 4;
      const int db = std::abs(static_cast<int>(help_buf[idx + 0]) -
                              static_cast<int>(blank_buf[idx + 0]));
      const int dg = std::abs(static_cast<int>(help_buf[idx + 1]) -
                              static_cast<int>(blank_buf[idx + 1]));
      const int dr = std::abs(static_cast<int>(help_buf[idx + 2]) -
                              static_cast<int>(blank_buf[idx + 2]));
      if (db + dg + dr > 24) ++diff;
    }
  }
  return diff;
}

}  // namespace

// 1) 帮助与工具按钮在空文本状态下仍绘制 HelpOutlineRounded 矢量问号（<=830px 28x28 & >830px 80x24）
UT_TEST(v112_help_icon_button_renders_question_mark) {
  LangGuard lg;
  ThemeService::instance().init(SkinId::Retro3d, ThemeMode::Dark);
  make_offscreen(120, 48, 96.0f);

  std::vector<MuiLoopSet> loops;
  UT_CHECK(IconW::mui_paths_for("helpoutline", loops));
  UT_CHECK(!loops.empty());
  std::vector<MuiLoopSet> loops_rounded;
  UT_CHECK(IconW::mui_paths_for("HelpOutlineRounded", loops_rounded));
  UT_CHECK(!loops_rounded.empty());

  SettingsDrawer drawer;
  drawer.wire_callbacks();
  drawer.set_open(true);
  UT_CHECK(drawer.bottom_help_ != nullptr);

  // 高 <= 830px 模式（28x28 右上角按钮）
  drawer.layout_tree({0.0f, 0.0f, 271.0f, 680.0f});
  const RectF compact_b = drawer.bottom_help_->bounds();
  UT_CHECK(near_f(compact_b.w, 28.0f));
  UT_CHECK(near_f(compact_b.h, 28.0f));
  UT_CHECK(diff_pixels_against_blank_button(drawer.bottom_help_, 28, 28) > 15);

  // 高 > 830px 模式（80x24 底栏左侧按钮）
  drawer.layout_tree({0.0f, 0.0f, 640.0f, 860.0f});
  const RectF wide_b = drawer.bottom_help_->bounds();
  UT_CHECK(near_f(wide_b.w, 80.0f));
  UT_CHECK(near_f(wide_b.h, 24.0f));
  UT_CHECK(diff_pixels_against_blank_button(drawer.bottom_help_, 80, 24) > 15);
  clear_input_state();
}

// 2) BUG-410：最大化与手拖共用单一随宽自适应流量曲线、保留历史波形且 3 列残行不画多余竖线
UT_TEST(v112_maximize_breakpoints_switches_traffic_graph_and_aligns_heights) {
  LangGuard lg;
  make_offscreen(640, 860, 96.0f);

  Breakpoints bp;
  ContentStack content;
  auto scene_ptr = std::make_unique<MainScene>();
  MainScene* scene = scene_ptr.get();
  content.add_child(std::move(scene_ptr));

  auto tp_ptr = std::make_unique<TrafficPane>();
  TrafficPane* tp = tp_ptr.get();
  int64_t fake_clock = 1000;
  tp->set_now_ms([&] { return fake_clock; });
  scene->traffic_slot()->add_child(std::move(tp_ptr));

  auto drawer_ptr = std::make_unique<SettingsDrawer>();
  SettingsDrawer* drawer = drawer_ptr.get();
  content.overlay = drawer;
  content.add_child(std::move(drawer_ptr));
  drawer->set_open(true);

  int sync_calls = 0;
  auto sync_layout = [&](float lw, float lh_content) {
    scene->apply_state(bp.state(), bp.state());
    scene->set_narrow(bp.is_narrow());
    tp->set_mini(bp.is_mini());
    tp->set_narrow(bp.is_narrow());
    content.bottom_inset = static_cast<float>(
        bp.is_narrow() ? kTrafficPaneMinimal : kTrafficPaneWide);
    content.layout_tree({0.0f, 0.0f, lw, lh_content});
  };
  bp.subscribe([&](WinState, WinState) { ++sync_calls; });

  // 注入真实流量样本，验证跨 285px 断点零销毁保留缓冲（BUG-410）
  UT_CHECK(tp->on_traffic_payload(R"({"up":2048,"down":16384,"upTotal":2048,"downTotal":16384})"));
  fake_clock += 1000;
  UT_CHECK(tp->on_traffic_payload(R"({"up":4096,"down":32768,"upTotal":6144,"downTotal":49152})"));
  UT_CHECK_EQ(tp->series().size(), size_t(2));
  CanvasGraph* const persistent_graph = tp->graph();
  UT_CHECK(persistent_graph != nullptr);
  UT_CHECK_EQ(persistent_graph->buffer_size(), size_t(63));

  // 初始窄窗 271x680（内容区高 = 680 - 30 = 650）：30s 极简双参考线
  bp.set_physical_size(271, 680, 1.0);
  bp.set_large(false);
  sync_layout(271.0f, 650.0f);
  UT_CHECK(bp.is_narrow());
  UT_CHECK(tp->graph() == persistent_graph);
  UT_CHECK(tp->graph()->visible());
  UT_CHECK(tp->graph()->is_minimal_width());
  UT_CHECK_EQ(tp->graph()->visible_points(), 30);
  UT_CHECK(near_f(scene->traffic_slot()->bounds().h, 100.0f));
  UT_CHECK(near_f(drawer->bounds().h, 550.0f));

  {
    RecorderRenderTarget rec_narrow;
    tp->graph()->on_paint(rec_narrow);
    UT_CHECK_EQ(rec_narrow.gradient_fills, 0);
    UT_CHECK(rec_narrow.texts.empty());
    UT_CHECK_EQ(rec_narrow.lines.size(), size_t(2));
  }

  // 手动拖宽至 440x680：无缝切为宽窗对数网格，时间跨度 30s < N < 60s
  bp.set_physical_size(440, 680, 1.0);
  UT_CHECK(!bp.is_narrow());
  sync_layout(440.0f, 650.0f);
  UT_CHECK(tp->graph() == persistent_graph);
  UT_CHECK(tp->graph()->visible());
  UT_CHECK(!tp->graph()->is_minimal_width());
  UT_CHECK(tp->graph()->visible_points() > 30);
  UT_CHECK(tp->graph()->visible_points() < 60);
  UT_CHECK(near_f(scene->traffic_slot()->bounds().h, 135.0f));

  // 恢复窄窗后再通过最大化切换 640x860（先 set_large 后 set_physical_size 旧序也必须触发切换）
  bp.set_physical_size(271, 680, 1.0);
  sync_layout(271.0f, 650.0f);
  const int before_bug_order = sync_calls;
  bp.set_large(true);
  bp.set_physical_size(640, 860, 1.0);
  UT_CHECK(sync_calls >= before_bug_order + 2);
  UT_CHECK(bp.is_large());
  UT_CHECK(!bp.is_narrow());
  sync_layout(640.0f, 830.0f);
  UT_CHECK(tp->graph() == persistent_graph);
  UT_CHECK(tp->graph()->visible());
  UT_CHECK(!tp->graph()->is_minimal_width());
  UT_CHECK_EQ(tp->graph()->visible_points(), 60);
  UT_CHECK_EQ(tp->series().size(), size_t(2));
  UT_CHECK(near_f(scene->traffic_slot()->bounds().h, 135.0f));
  // 抽屉与上栏严丝合缝（695 == 830 - 135），绝无 35px 下溢出残边
  UT_CHECK(near_f(drawer->bounds().h, 695.0f));
  UT_CHECK(near_f(scene->list_slot()->parent()->bounds().h, 695.0f));

  // 宽窗最大化渲染 3 条对数刻度标签("1M"/"10K"/"100B") + "60s" 角标 + 2 层面积渐变 + 10s 纵网格
  {
    RecorderRenderTarget rec_wide;
    tp->graph()->on_paint(rec_wide);
    UT_CHECK_EQ(rec_wide.gradient_fills, 2);
    UT_CHECK_EQ(rec_wide.lines.size(), size_t(8));  // 3 水平刻度 + 5 条纵向 10s 网格(50s..10s)
    bool has_1m = false, has_10k = false, has_100b = false, has_60s = false;
    for (const auto& t : rec_wide.texts) {
      if (t.text == L"1M") has_1m = true;
      if (t.text == L"10K") has_10k = true;
      if (t.text == L"100B") has_100b = true;
      if (t.text == L"60s") has_60s = true;
    }
    UT_CHECK(has_1m && has_10k && has_100b && has_60s);
  }

  // 点击 CanvasGraph 切换 Smooth(bezier) / Linear
  UT_CHECK(tp->graph()->is_bezier());
  MouseEvent md{};
  md.type = MouseEventType::Down;
  md.pos = {50.0f, 40.0f};
  md.buttons = kMouseLeft;
  UT_CHECK(tp->graph()->on_mouse(md));
  MouseEvent mu = md;
  mu.type = MouseEventType::Up;
  UT_CHECK(tp->graph()->on_mouse(mu));
  UT_CHECK(!tp->graph()->is_bezier());

  // 矮宽窗（高 110px）下底栏按 flex 0 1 135px + maxHeight 100% 自动收缩退让至 110px，绝不溢挤上栏为负
  bp.set_large(false);
  bp.set_physical_size(500, 140, 1.0);
  sync_layout(500.0f, 110.0f);
  UT_CHECK(near_f(scene->traffic_slot()->bounds().h, 110.0f));
  UT_CHECK(near_f(scene->list_slot()->parent()->bounds().h, 0.0f));

  // 还原 271x680：无缝切回窄窗 30s 极简视图且缓冲不断线
  bp.set_large(false);
  bp.set_physical_size(271, 680, 1.0);
  UT_CHECK(bp.is_narrow());
  sync_layout(271.0f, 650.0f);
  UT_CHECK(tp->graph()->visible());
  UT_CHECK(tp->graph()->is_minimal_width());
  UT_CHECK_EQ(tp->graph()->visible_points(), 30);
  UT_CHECK(near_f(scene->traffic_slot()->bounds().h, 100.0f));

  // NodeGridRowWidget 在 3 列模式下末行仅 1 张卡片时，只绘第 1 条竖框，不绘第 2 条悬空残边
  NodeGridRowWidget grid_row;
  grid_row.set_cards({{"VLESS", "only-one-card", 40, false, 0, 1, 1}}, 3);
  grid_row.layout_tree({0.0f, 0.0f, 616.0f, kNodeRowH});
  RecorderRenderTarget rec;
  grid_row.on_paint(rec);
  const float card_w = 616.0f / 3.0f;
  int first_div_rects = 0;
  int second_div_rects = 0;
  for (const auto& fr : rec.filled_rects) {
    if (std::fabs(fr.x - card_w) < 6.0f) ++first_div_rects;
    if (std::fabs(fr.x - 2.0f * card_w) < 6.0f) ++second_div_rects;
  }
  UT_CHECK_EQ(first_div_rects, 2);
  UT_CHECK_EQ(second_div_rects, 0);
  clear_input_state();
}

// 3) 选中节点行：3 列窄列下保留完整节点名、自动挤掉「- 当前节点」；宽列有空位时并列显示
UT_TEST(v112_selected_node_row_3col_preserves_name_and_wide_shows_suffix) {
  LangGuard lg;
  ThemeService::instance().init(SkinId::Retro3d, ThemeMode::Light);
  make_offscreen(400, 100, 96.0f);

  const std::wstring cur_word = tr("home.components.currentProxy.title");
  UT_CHECK(!cur_word.empty());
  UT_CHECK(cur_word == L"当前节点");

  // 1) 640px 宽窗 3 列卡片宽度 ≈ 205px（名称区仅 ~53px）：必须画节点名，不得用「当前节点」顶替
  NodeRowWidget row_narrow;
  row_narrow.bind("Trojan", "香港高速专线01", 35, true);
  row_narrow.layout_tree({0.0f, 0.0f, 205.0f, kNodeRowH});
  RecorderRenderTarget rec_narrow;
  row_narrow.on_paint(rec_narrow);

  bool drew_name_narrow = false;
  bool drew_suffix_narrow = false;
  for (const auto& call : rec_narrow.texts) {
    if (call.text == L"香港高速专线01") {
      drew_name_narrow = true;
      UT_CHECK(call.rect.w > 45.0f);
    }
    if (call.text.find(cur_word) != std::wstring::npos) {
      drew_suffix_narrow = true;
    }
  }
  UT_CHECK(drew_name_narrow);
  UT_CHECK(!drew_suffix_narrow);

  // 2) 宽单列 320px（短节点名）：节点名与「- 当前节点」必须从左到右并列绘制且不重叠
  NodeRowWidget row_wide;
  row_wide.bind("Trojan", "香港1", 35, true);
  row_wide.layout_tree({0.0f, 0.0f, 320.0f, kNodeRowH});
  RecorderRenderTarget rec_wide;
  row_wide.on_paint(rec_wide);

  RectF name_rect{};
  RectF suffix_rect{};
  bool drew_name_wide = false;
  bool drew_suffix_wide = false;
  for (const auto& call : rec_wide.texts) {
    if (call.text == L"香港1") {
      drew_name_wide = true;
      name_rect = call.rect;
    }
    if (call.text.find(cur_word) != std::wstring::npos) {
      drew_suffix_wide = true;
      suffix_rect = call.rect;
    }
  }
  UT_CHECK(drew_name_wide);
  UT_CHECK(drew_suffix_wide);
  TextStyle ns;
  ns.size = 12;
  ns.weight = static_cast<int>(Weight::Bold);
  const float actual_name_w =
      text_engine()->measure_text(L"香港1", ns, 1e6f).w;
  UT_CHECK(suffix_rect.x >= name_rect.x + actual_name_w + 3.5f);
  clear_input_state();
}

namespace {

std::wstring v112_unique_pipe(const char* tag) {
  static std::atomic<int> seq{1};
  wchar_t buf[160];
  swprintf_s(buf, L"\\\\.\\pipe\\clashwin-v112-%hs-%lu-%d", tag,
             ::GetCurrentProcessId(), seq.fetch_add(1));
  return buf;
}

struct V112TempHomeGuard {
  std::filesystem::path dir;
  V112TempHomeGuard() {
    static std::atomic<int> seq{1};
    dir = std::filesystem::temp_directory_path() /
          ("clashwin_v112_" + std::to_string(::GetTickCount64()) + "_" +
           std::to_string(seq.fetch_add(1)));
    std::filesystem::create_directories(dir);
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", dir.wstring().c_str());
  }
  ~V112TempHomeGuard() {
    _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
};

struct V112ProfilesRestoreGuard {
  clashwin::config::IProfiles saved;
  V112ProfilesRestoreGuard()
      : saved(*clashwin::config::Config::instance().profiles().latest_arc()) {}
  ~V112ProfilesRestoreGuard() {
    auto& p = clashwin::config::Config::instance().profiles();
    p.edit_draft([&](clashwin::config::IProfiles& d) { d = saved; });
    p.apply();
  }
};

}  // namespace

// 4) BUG-406: PipeHttpClient::clear_idle() 保活清空 + PipeWebSocket::close(0u) 0ms 即时释放
UT_TEST(v112_bug406_pipe_http_clear_idle_and_ws_zero_timeout_close) {
  // (A) PipeHttpClient::clear_idle() 仅清空空闲管道且不破坏 connected_ 状态
  {
    const std::wstring pipe = v112_unique_pipe("http-idle");
    mockpipe::Server srv(pipe, 2);
    srv.start([](const mockpipe::Request&) {
      mockpipe::Response r;
      r.status = 200;
      r.body = R"({"version":"1.19.33"})";
      return r;
    });

    mihomo::PipeHttpConfig cfg;
    cfg.min_connections = 1;
    cfg.health_check_interval_s = 60;
    mihomo::PipeHttpClient client(cfg);
    UT_CHECK(client.connect(pipe).is_ok());

    auto r1 = client.send("GET", "/version", {}, "", 2000);
    UT_CHECK(r1.is_ok());
    UT_CHECK_EQ(client.idle_count(), size_t(1));
    UT_CHECK_EQ(client.connections_created(), uint64_t(1));

    client.clear_idle();
    UT_CHECK(client.idle_empty());

    // 清空空闲池后无需重新 connect()，下一次请求直接新建管道成功
    auto r2 = client.send("GET", "/version", {}, "", 2000);
    UT_CHECK(r2.is_ok());
    UT_CHECK_EQ(client.connections_created(), uint64_t(2));

    client.clear_pool();
    srv.stop();
  }

  // (B) PipeWebSocket::close(0u) 在阻塞写缓冲下跳过 close 帧，于 <250ms 内第 0ms 立即释放
  {
    const std::wstring pipe = v112_unique_pipe("ws-zero");
    HANDLE hSrv = ::CreateNamedPipeW(
        pipe.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 64, 64, 0, nullptr);
    UT_CHECK(hSrv != INVALID_HANDLE_VALUE);

    std::atomic<bool> release_srv{false};
    std::thread srv_thread([&] {
      OVERLAPPED ov{};
      ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
      BOOL ok = ::ConnectNamedPipe(hSrv, &ov);
      if (!ok && ::GetLastError() == ERROR_IO_PENDING) {
        ::WaitForSingleObject(ov.hEvent, 3000);
      }
      ::CloseHandle(ov.hEvent);

      mockpipe::Request req;
      if (mockpipe::mo_read_request(hSrv, &req)) {
        auto it = req.headers.find("sec-websocket-key");
        const std::string accept =
            mihomo::ws_compute_accept(it != req.headers.end() ? it->second : "");
        const std::string hs =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " +
            accept + "\r\n\r\n";
        mockpipe::mo_write(hSrv, hs.data(), static_cast<DWORD>(hs.size()), 2000);
      }
      while (!release_srv.load(std::memory_order_acquire)) {
        ::Sleep(15);
      }
      ::DisconnectNamedPipe(hSrv);
      ::CloseHandle(hSrv);
    });

    mihomo::PipeWebSocket ws;
    mihomo::PipeWsCallbacks cbs;
    UT_CHECK(ws.connect(pipe, "/traffic", "", cbs, 2000).is_ok());
    const auto t0 = std::chrono::steady_clock::now();
    ws.close(0u);
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0)
            .count();
    UT_CHECK(elapsed_ms < 250);
    UT_CHECK(!ws.is_open());

    release_srv.store(true, std::memory_order_release);
    if (srv_thread.joinable()) srv_thread.join();
  }
}

// 5) BUG-407: selection_ensure_epoch 纪元自动取消与 WsManager 45ms 多通道错峰重连
UT_TEST(v112_bug407_selection_epoch_and_ws_stagger) {
  // (A) 新纪元淘汰旧选点恢复任务
  {
    core::CoreManager m;
    struct EpochBumpApi : core::ICoreApiClient {
      core::CoreManager& mgr;
      int base_calls = 0;
      int select_calls = 0;
      explicit EpochBumpApi(core::CoreManager& c) : mgr(c) {}
      Result<mihomo::BaseConfig> get_base_config() override {
        ++base_calls;
        if (base_calls == 1) {
          mgr.bump_selection_ensure_epoch();
          return Result<mihomo::BaseConfig>::err(Error{1, "not ready"});
        }
        return Result<mihomo::BaseConfig>::ok(mihomo::BaseConfig{});
      }
      Result<mihomo::ProxyItem> get_group_by_name(const std::string&) override {
        mihomo::ProxyItem g;
        g.name = "PROXY";
        g.all = std::vector<std::string>{"HK-01"};
        return Result<mihomo::ProxyItem>::ok(g);
      }
      Result<void> select_node_for_group(const std::string&, const std::string&) override {
        ++select_calls;
        return Result<void>::ok();
      }
      Result<void> reload_config(bool, const std::string&) override {
        return Result<void>::ok();
      }
    } api(m);

    const uint64_t initial_epoch = m.bump_selection_ensure_epoch();
    core::RestoreTimings timings;
    timings.api_ready_budget_ms = 200;
    timings.api_ready_poll_ms = 5;
    timings.group_fill_budget_ms = 100;
    timings.group_fill_poll_ms = 5;
    timings.select_budget_ms = 100;
    timings.select_retry_ms = 5;
    bool changed = false;
    const bool ok = m.ensure_proxy_group_selection(
        api, std::string("HK-01"), timings,
        std::make_pair(std::optional<std::string>{"HK-01"}, std::string()),
        initial_epoch, &changed);
    UT_CHECK(!ok);
    UT_CHECK(!changed);
    UT_CHECK_EQ(api.select_calls, 0);
  }

  // (B) WsManager flush_reconnect_backoff 对双活跃通道分配 0ms / 45ms 错峰
  {
    mihomo::WsManager mgr;
    mgr.configure(v112_unique_pipe("ws-stagger"), "");
    const uint64_t s1 = mgr.subscribe_traffic([](const std::string&) {});
    const uint64_t s2 = mgr.subscribe_memory([](const std::string&) {});
    mgr.flush_reconnect_backoff();
    UT_CHECK_EQ(mgr.channel_last_stagger_ms_for_test(mihomo::WsManager::kMemory),
                uint32_t(0));
    UT_CHECK_EQ(mgr.channel_last_stagger_ms_for_test(mihomo::WsManager::kTraffic),
                uint32_t(45));
    mgr.unsubscribe(s1);
    mgr.unsubscribe(s2);
  }
}

// 6) BUG-408: Sidecar 模式清理服务自启内核、死命令 restart_core 移除及 NSIS 优雅停服前置
UT_TEST(v112_bug408_sidecar_stops_autostarted_service_and_dead_restart_core_removed) {
  int stopped = 0;
  auto stop_fn = [&] { ++stopped; };
  cmd::stop_autostarted_service_core_if_sidecar_with(
      core::RunningMode::Sidecar, stop_fn);
  UT_CHECK_EQ(stopped, 1);
  cmd::stop_autostarted_service_core_if_sidecar_with(
      core::RunningMode::NotRunning, stop_fn);
  UT_CHECK_EQ(stopped, 2);
  cmd::stop_autostarted_service_core_if_sidecar_with(
      core::RunningMode::Service, stop_fn);
  UT_CHECK_EQ(stopped, 2);

  cmd::CommandService::instance().install_all();
  UT_CHECK(!cmd::CommandService::instance().exists("restart_core"));

  std::filesystem::path nsi_path = "installer/clashwin.nsi";
  if (!std::filesystem::exists(nsi_path)) {
    nsi_path = std::filesystem::path(__FILE__).parent_path().parent_path() /
               "installer" / "clashwin.nsi";
  }
  UT_CHECK(std::filesystem::exists(nsi_path));
  std::ifstream ifs(nsi_path, std::ios::binary);
  UT_CHECK(ifs.is_open());
  std::string raw((std::istreambuf_iterator<char>(ifs)),
                  std::istreambuf_iterator<char>());
  UT_CHECK(raw.size() >= 3);
  UT_CHECK_EQ(static_cast<unsigned char>(raw[0]), 0xEF);
  UT_CHECK_EQ(static_cast<unsigned char>(raw[1]), 0xBB);
  UT_CHECK_EQ(static_cast<unsigned char>(raw[2]), 0xBF);

  const size_t sec_install = raw.find("Section \"Install\"");
  const size_t sec_uninstall = raw.find("Section \"Uninstall\"");
  UT_CHECK(sec_install != std::string::npos);
  UT_CHECK(sec_uninstall != std::string::npos);
  const size_t sc_stop_pos = raw.find("sc.exe stop ${SERVICE_NAME}", sec_install);
  const size_t kill_svc_pos =
      raw.find("Get-Process clash-verge-service", sec_install);
  UT_CHECK(sc_stop_pos != std::string::npos);
  UT_CHECK(kill_svc_pos != std::string::npos);
  UT_CHECK(sc_stop_pos < kill_svc_pos);

  const size_t handback_pos =
      raw.find("Call un.HandBackOrRemoveService", sec_uninstall);
  const size_t kill_core_pos =
      raw.find("Get-Process win-mihomo", sec_uninstall);
  UT_CHECK(handback_pos != std::string::npos);
  UT_CHECK(kill_core_pos != std::string::npos);
  UT_CHECK(handback_pos < kill_core_pos);
}

// 7) BUG-409: 全局 Merge.yaml 去重不翻倍叠加 & profile_affects_runtime 修复
UT_TEST(v112_bug409_enhance_deduplicates_global_merge_and_profile_affects_runtime) {
  // (A) profile_affects_runtime 契约验证
  {
    config::IProfiles p;
    config::PrfItem item;
    item.uid = "p1";
    item.itype = "local";
    item.file = "p1.yaml";
    p.items = std::vector<config::PrfItem>{item};

    p.current = std::nullopt;
    UT_CHECK(!cmd::profile_affects_runtime(p, "Merge"));
    UT_CHECK(!cmd::profile_affects_runtime(p, "p1"));

    p.current = std::string("p1");
    UT_CHECK(cmd::profile_affects_runtime(p, "p1"));
    UT_CHECK(cmd::profile_affects_runtime(p, "Merge"));
    UT_CHECK(cmd::profile_affects_runtime(p, "Rules"));
    UT_CHECK(cmd::profile_affects_runtime(p, "Proxies"));
    UT_CHECK(cmd::profile_affects_runtime(p, "Groups"));
    UT_CHECK(cmd::profile_affects_runtime(p, "Script"));
    UT_CHECK(!cmd::profile_affects_runtime(p, "unrelated"));

    config::PrfOption opt;
    opt.merge = std::string("m_custom");
    p.items->at(0).option = opt;
    UT_CHECK(cmd::profile_affects_runtime(p, "Merge"));
    UT_CHECK(cmd::profile_affects_runtime(p, "m_custom"));
  }

  // (B) enhance::run_from_config 不在 option.merge 为空或 "Merge" 时重复叠加 Merge.yaml
  {
    V112TempHomeGuard th;
    V112ProfilesRestoreGuard restore_profiles;

    const std::filesystem::path prof_dir =
        std::filesystem::path(config::prfitem::profiles_dir());
    std::filesystem::create_directories(prof_dir);

    auto write_file = [](const std::filesystem::path& p, const std::string& s) {
      std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
      ofs.write(s.data(), static_cast<std::streamsize>(s.size()));
    };
    write_file(prof_dir / "sub_test.yaml",
               "mixed-port: 7890\n"
               "proxies:\n"
               "  - name: \"Node-01\"\n"
               "    type: direct\n"
               "rules:\n"
               "  - MATCH,PROXY\n");
    write_file(prof_dir / "Merge.yaml",
               "prepend-rules:\n"
               "  - DOMAIN-SUFFIX,bug409-once.test,DIRECT\n");
    write_file(prof_dir / "custom_merge.yaml",
               "append-rules:\n"
               "  - DOMAIN-SUFFIX,bug409-custom.test,DIRECT\n");

    auto count_rule = [](const YAML::Node& cfg, const std::string& target) {
      int cnt = 0;
      const YAML::Node rules = static_cast<const YAML::Node&>(cfg)["rules"];
      if (rules && rules.IsSequence()) {
        for (const auto& item : rules) {
          if (item && item.IsScalar() && item.Scalar() == target) ++cnt;
        }
      }
      return cnt;
    };

    auto set_profiles_with_merge = [&](std::optional<std::string> opt_merge) {
      auto& profiles = config::Config::instance().profiles();
      profiles.edit_draft([&](config::IProfiles& d) {
        config::PrfItem gm;
        gm.uid = "Merge";
        gm.itype = "merge";
        gm.file = "Merge.yaml";

        config::PrfItem cm;
        cm.uid = "m_custom";
        cm.itype = "merge";
        cm.file = "custom_merge.yaml";

        config::PrfItem sub;
        sub.uid = "sub_test";
        sub.itype = "local";
        sub.file = "sub_test.yaml";
        sub.name = "SubTest";
        if (opt_merge.has_value()) {
          config::PrfOption opt;
          opt.merge = *opt_merge;
          sub.option = opt;
        }

        d.current = std::string("sub_test");
        d.items = std::vector<config::PrfItem>{sub, gm, cm};
      });
      profiles.apply();
    };

    // 1) option 缺省 → Merge.yaml 仅生效一次
    set_profiles_with_merge(std::nullopt);
    auto r1 = enhance::run_from_config();
    UT_CHECK(r1.is_ok());
    UT_CHECK_EQ(count_rule(r1.value(), "DOMAIN-SUFFIX,bug409-once.test,DIRECT"), 1);

    // 2) option.merge == "Merge" → 仍然仅生效一次（绝不翻倍）
    set_profiles_with_merge(std::string("Merge"));
    auto r2 = enhance::run_from_config();
    UT_CHECK(r2.is_ok());
    UT_CHECK_EQ(count_rule(r2.value(), "DOMAIN-SUFFIX,bug409-once.test,DIRECT"), 1);

    // 3) option.merge == "m_custom" → 全局 Merge 与专属 Merge 各自生效一次
    set_profiles_with_merge(std::string("m_custom"));
    auto r3 = enhance::run_from_config();
    UT_CHECK(r3.is_ok());
    UT_CHECK_EQ(count_rule(r3.value(), "DOMAIN-SUFFIX,bug409-once.test,DIRECT"), 1);
    UT_CHECK_EQ(count_rule(r3.value(), "DOMAIN-SUFFIX,bug409-custom.test,DIRECT"), 1);
  }
}

// 8) 三态托盘图标色值 1:1 对齐（默认灰蓝/系统代理鲜绿/TUN 彩底）及轻量过滤态下 0ms 即时同步
UT_TEST(v112_tray_icons_match_3_distinct_modes_and_sync_under_lightweight) {
  auto find_icon = [](const char* file_name) -> std::filesystem::path {
    std::filesystem::path p = std::filesystem::path("resources") / "icons" / file_name;
    if (!std::filesystem::exists(p)) {
      p = std::filesystem::path(__FILE__).parent_path().parent_path() /
          "resources" / "icons" / file_name;
    }
    return p;
  };

  struct Bgra {
    uint8_t b, g, r, a;
  };
  auto sample_ico_pixel = [](const std::filesystem::path& path, int px, int py) -> Bgra {
    HICON hi = static_cast<HICON>(::LoadImageW(
        nullptr, path.wstring().c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
    UT_CHECK(hi != nullptr);
    ICONINFO ii{};
    UT_CHECK(::GetIconInfo(hi, &ii) != FALSE);
    UT_CHECK(ii.hbmColor != nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 32;
    bi.bmiHeader.biHeight = -32;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    std::vector<uint8_t> pixels(32u * 32u * 4u, 0);
    HDC hdc = ::GetDC(nullptr);
    const int lines = ::GetDIBits(hdc, ii.hbmColor, 0, 32, pixels.data(), &bi,
                                  DIB_RGB_COLORS);
    ::ReleaseDC(nullptr, hdc);
    if (ii.hbmColor) ::DeleteObject(ii.hbmColor);
    if (ii.hbmMask) ::DeleteObject(ii.hbmMask);
    ::DestroyIcon(hi);
    UT_CHECK(lines == 32);
    const size_t idx = (static_cast<size_t>(py) * 32u + static_cast<size_t>(px)) * 4u;
    return {pixels[idx + 0], pixels[idx + 1], pixels[idx + 2], pixels[idx + 3]};
  };

  const auto p_def = find_icon("tray-icon.ico");
  const auto p_sys = find_icon("tray-icon-sys.ico");
  const auto p_tun = find_icon("tray-icon-tun.ico");
  UT_CHECK(std::filesystem::exists(p_def));
  UT_CHECK(std::filesystem::exists(p_sys));
  UT_CHECK(std::filesystem::exists(p_tun));

  const Bgra def_corner = sample_ico_pixel(p_def, 1, 1);
  const Bgra def_ring = sample_ico_pixel(p_def, 24, 24);
  const Bgra sys_ring = sample_ico_pixel(p_sys, 24, 24);
  const Bgra tun_bg = sample_ico_pixel(p_tun, 24, 24);

  UT_CHECK_EQ(def_corner.a, 0);
  UT_CHECK(def_ring.a > 200);
  UT_CHECK(std::abs(static_cast<int>(def_ring.g) - static_cast<int>(def_ring.r)) < 30);

  // 系统代理图标必须是鲜绿环（不再是灰环）
  UT_CHECK(sys_ring.a > 200);
  UT_CHECK(static_cast<int>(sys_ring.g) > static_cast<int>(sys_ring.r) + 60);
  UT_CHECK(static_cast<int>(sys_ring.g) > static_cast<int>(sys_ring.b) + 60);

  // TUN 图标必须呈现粉紫-蓝渐变底色
  UT_CHECK(tun_bg.a > 200);
  UT_CHECK(tun_bg.r > 180);
  UT_CHECK(tun_bg.b > 150);

  // 运行期：即使 EventBus 开启轻量静默过滤，Tray::sync_from_verge 仍必须实时同步三态图标
  class MockTraySyncBackend : public system::ITrayBackend {
   public:
    std::vector<HICON> modified_icons;
    bool add_icon(HWND, UINT, UINT, HICON, const std::wstring&) override { return true; }
    bool modify_icon(HWND, UINT, HICON icon, const std::wstring&) override {
      modified_icons.push_back(icon);
      return true;
    }
    bool delete_icon(HWND, UINT) override { return true; }
    void show_menu(HWND, POINT, bool, std::function<void(int)>) override {}
  };

  auto backend = std::make_unique<MockTraySyncBackend>();
  auto* raw_backend = backend.get();
  system::Tray tray(std::move(backend));
  HWND dummy_hwnd = reinterpret_cast<HWND>(0x1122);
  UT_CHECK(tray.init(dummy_hwnd, system::TrayIconState::Default));

  const bool prev_lw = events::EventBus::instance().lightweight_filter();
  struct SyncGuard {
    bool prev;
    explicit SyncGuard(bool p) : prev(p) {}
    ~SyncGuard() {
      system::Tray::set_global_sync_target(nullptr);
      events::EventBus::instance().set_lightweight_filter(prev);
    }
  } guard(prev_lw);

  events::EventBus::instance().set_lightweight_filter(true);
  system::Tray::set_global_sync_target(&tray, nullptr);

  system::Tray::sync_from_verge(false, true);
  UT_CHECK_EQ(tray.current_state(), system::TrayIconState::SysProxy);
  system::Tray::sync_from_verge(true, false);
  UT_CHECK_EQ(tray.current_state(), system::TrayIconState::Tun);
  system::Tray::sync_from_verge(false, false);
  UT_CHECK_EQ(tray.current_state(), system::TrayIconState::Default);

  UT_CHECK_EQ(raw_backend->modified_icons.size(), size_t(3));
  UT_CHECK(raw_backend->modified_icons[0] ==
           tray.icon_for_state_for_test(system::TrayIconState::SysProxy));
  UT_CHECK(raw_backend->modified_icons[1] ==
           tray.icon_for_state_for_test(system::TrayIconState::Tun));
  UT_CHECK(raw_backend->modified_icons[2] ==
           tray.icon_for_state_for_test(system::TrayIconState::Default));
}
