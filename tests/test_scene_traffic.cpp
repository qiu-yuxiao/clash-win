// T14 测试：流量面板 + 连接数据管道（滚动窗/格式化/resync/delta/节流/像素）。
#include <windows.h>

#include "test_ui_helper.h"
#include "test_util.h"

#include "ui/scenes/connection_data.h"
#include "ui/scenes/connections_view.h"
#include "ui/scenes/traffic_pane.h"
#include "ui/theme/theme.h"
#include "util/parse_traffic.h"
#include "util/utf.h"

using clashwin::ui::theme::ThemeService;
namespace clutil = clashwin::utf;
using namespace clashwin::ui::theme;

using namespace clashwin::ui;
using namespace clashwin::ui::scene;
using namespace ui_test;

namespace {
nlohmann::json traffic_payload(double up, double down, double upTotal = 0,
                               double downTotal = 0) {
  nlohmann::json j = nlohmann::json::object();
  j["up"] = up;
  j["down"] = down;
  j["upTotal"] = upTotal;
  j["downTotal"] = downTotal;
  return j;
}

nlohmann::json snapshot_msg(int epoch, int seq,
                            const std::vector<std::string>& hosts) {
  nlohmann::json data = nlohmann::json::object();
  data["epochId"] = std::to_string(epoch);
  data["sequenceId"] = seq;
  data["uploadTotal"] = 1000;
  data["downloadTotal"] = 2000;
  nlohmann::json conns = nlohmann::json::array();
  for (const auto& h : hosts) {
    nlohmann::json c = nlohmann::json::object();
    c["id"] = h;
    nlohmann::json m = nlohmann::json::object();
    m["host"] = h;
    m["destinationPort"] = "443";
    c["metadata"] = m;
    c["chains"] = nlohmann::json::array({"PROXY"});
    c["upload"] = 100;
    c["download"] = 200;
    c["network"] = "tcp";
    conns.push_back(c);
  }
  data["connections"] = conns;
  nlohmann::json msg = nlohmann::json::object();
  msg["type"] = "snapshot";
  msg["data"] = data;
  return msg;
}
}  // namespace

UT_TEST(t14_traffic_series_window_and_peak) {
  TrafficSeries s;
  for (int i = 1; i <= 40; ++i) s.append(i * 100, i * 50);
  // 滚动窗 maxPoint+2=32
  UT_CHECK_EQ(s.size(), 32u);
  // 最新在尾
  UT_CHECK(s.up().back() == 4000.0f);
  UT_CHECK(s.down().back() == 2000.0f);
  UT_CHECK(s.peak() == 4000);
}

UT_TEST(t14_traffic_sample_1000ms_and_disable) {
  TrafficPane pane;
  pane.layout_tree({0, 0, 271, 135});
  int64_t clock = 1000;
  pane.set_now_ms([&] { return clock; });
  int fired = 0;
  pane.subscribe([&] { ++fired; });

  UT_CHECK(pane.on_traffic_json(traffic_payload(100, 200)));
  UT_CHECK_EQ(fired, 1);
  UT_CHECK_EQ(pane.series().size(), 1u);
  // src: traffic-graph.tsx:16 采样间隔 1000ms：1s 内 payload 只缓存、不入窗
  clock += 300;
  UT_CHECK(pane.on_traffic_json(traffic_payload(300, 400)));
  UT_CHECK_EQ(fired, 1);
  UT_CHECK_EQ(pane.series().size(), 1u);
  clock += 800;
  UT_CHECK(pane.on_traffic_json(traffic_payload(300, 400)));
  UT_CHECK_EQ(fired, 2);
  UT_CHECK_EQ(pane.series().size(), 2u);
  // 失焦只停“绘制”，采样/累计继续（P0-5）：帧仍入窗、但不触发重绘
  pane.set_enabled(false);
  clock += 1200;
  UT_CHECK(pane.on_traffic_json(traffic_payload(500, 600)));
  UT_CHECK_EQ(fired, 2);
  UT_CHECK_EQ(pane.series().down().back(), 600.0f);
  pane.set_enabled(true);
  UT_CHECK_EQ(fired, 3);
}

UT_TEST(t14_traffic_text_format) {
  TrafficPane pane;
  pane.layout_tree({0, 0, 271, 135});
  pane.on_traffic_json(traffic_payload(180.2 * 1024, 12.1 * 1024, 816.3 * 1024 * 1024, 135.4 * 1024 * 1024));
  // ← parse_traffic（T02）：1024 边界 KB/s / MB/s
  UT_CHECK_MSG(pane.down_text().find(L"KB/s") != std::wstring::npos,
               clashwin::utf::utf16_to_utf8(pane.down_text()).c_str());
  // 总计标签有内容（816.3 MB 量级）
  // 像素：指标区文本存在（↓ 深青 #006064 有像素）
  make_offscreen(271, 135, 96);
  pane.layout_tree({0, 0, 271, 135});
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  pane.on_paint(rt());
  pane.paint_all(rt());
  rt().end_frame();
  int dark = 0;
  for (int y = 91; y < 135; ++y)
    for (int x = 4; x < 260; ++x)
      if (pixel(x, y).r < 120) ++dark;
  std::printf("dbg traffic dark=%d\n", dark);
  {
    Widget* dlabel = pane.child(1);  // 文字段直挂 pane（child0=graph）
    std::printf("dbg label=(%f,%f,%f,%f) lvis=%d\n",
                dlabel->bounds().x, dlabel->bounds().y,
                dlabel->bounds().w, dlabel->bounds().h,
                (int)dlabel->visible());
    rt().begin_frame();
    rt().clear({1, 1, 1, 1});
    dlabel->on_paint(rt());
    rt().end_frame();
    int d2 = 0;
    for (int y = 0; y < 20; ++y)
      for (int x = 0; x < 129; ++x)
        if (pixel(x, y).r < 120) ++d2;
    std::printf("dbg solo dark=%d\n", d2);
  }
  UT_CHECK(dark > 3);
}

UT_TEST(t14_traffic_mini_layout) {
  make_offscreen(271, 126, 96);
  TrafficPane pane;
  pane.layout_tree({0, 0, 271, 100});  // src: _layout.tsx:1768 迷你窗面板高 100
  pane.on_traffic_json(traffic_payload(100, 200));
  pane.set_mini(true);
  pane.layout_tree({0, 0, 271, 100});
  // src: _layout.tsx:1764-1782 迷你窗仍渲染 MiniTrafficPanel 四卡（不隐藏）
  RectF g = pane.graph()->bounds();
  UT_CHECK(g.h > 0);
  UT_CHECK(g.h < 100);  // 图让出底部指标区（2 行卡 + mt）
  UT_CHECK(pane.child(1)->visible());
}

UT_TEST(t14_conn_snapshot_and_delta) {
  ConnectionData cd(300);
  int64_t clock = 0;
  cd.set_now_ms([&] { return clock; });
  int fired = 0;
  cd.subscribe([&] { ++fired; });

  // snapshot 全量重建
  UT_CHECK(cd.consume_json(snapshot_msg(0xA, 0, {"a.com", "b.com"})));
  UT_CHECK_EQ(cd.active_count(), 2u);
  UT_CHECK(cd.active()[0].host == "a.com:443");
  UT_CHECK(cd.active()[0].chains_cat == "Proxy");
  UT_CHECK(cd.active()[0].cur_upload == 0);
  int notified = fired;

  // delta 正常步进：seq=1
  nlohmann::json d1 = nlohmann::json::object();
  d1["type"] = "delta";
  nlohmann::json dd1 = nlohmann::json::object();
  dd1["epochId"] = "10";  // snapshot epoch=10（10 进制同串）
  dd1["sequenceId"] = 1;
  dd1["uploadTotal"] = 1100;
  dd1["downloadTotal"] = 2100;
  dd1["updated"] = nlohmann::json::array({"a.com", 250, 450});
  dd1["added"] = nlohmann::json::array();
  dd1["removed"] = nlohmann::json::array();
  d1["data"] = dd1;
  clock += 1000;  // 过节流窗
  UT_CHECK(cd.consume_json(d1));
  UT_CHECK(cd.active()[0].upload == 250 && cd.active()[0].cur_upload == 150);
  // 未 updated 的 b.com cur 清 0
  bool b_zero = false;
  for (const auto& c : cd.active())
    if (c.id == "b.com") b_zero = c.cur_upload == 0 && c.cur_download == 0;
  UT_CHECK(b_zero);

  // seq 断档（跳 2）→ resync + 数据保持旧态
  int resyncs = 0;
  cd.on_resync = [&] { ++resyncs; };
  nlohmann::json d3 = d1;
  dd1["sequenceId"] = 3;
  d3["data"] = dd1;
  clock += 1000;
  UT_CHECK(!cd.consume_json(d3));
  UT_CHECK_EQ(resyncs, 1);
  UT_CHECK(cd.active_count() == 2u);  // 旧态保持

  // epoch 变化（字符串比较）→ resync
  nlohmann::json de = d1;
  dd1["sequenceId"] = 2;   // 连续，但
  dd1["epochId"] = "11";   // epoch 变
  de["data"] = dd1;
  clock += 1000;
  UT_CHECK(!cd.consume_json(de));
  UT_CHECK_EQ(resyncs, 2);

  // epoch 相同但字符串不同（16hex 大小写敏感语义）→ resync
  nlohmann::json dh = d1;
  dd1["epochId"] = "10x";  // 不同字符串
  dd1["sequenceId"] = 2;
  dh["data"] = dd1;
  clock += 1000;
  UT_CHECK(!cd.consume_json(dh));
  UT_CHECK_EQ(resyncs, 3);
}

UT_TEST(t14_conn_removed_added_closed_cap) {
  ConnectionData cd(0);  // 无节流
  cd.set_now_ms([] { return 0; });
  // snapshot 2 连接
  UT_CHECK(cd.consume_json(snapshot_msg(1, 0, {"a", "b"})));
  // delta：移除 a、新增 c
  nlohmann::json d = nlohmann::json::object();
  d["type"] = "delta";
  nlohmann::json dd = nlohmann::json::object();
  dd["epochId"] = "1";
  dd["sequenceId"] = 1;
  dd["updated"] = nlohmann::json::array();
  dd["added"] = nlohmann::json::array({nlohmann::json::object({
      {"id", "c"},
      {"metadata", nlohmann::json::object({{"host", "c.dev"},
                                           {"destinationPort", "80"}})},
      {"chains", nlohmann::json::array({"DIRECT"})},
      {"upload", 5},
      {"download", 6},
  })});
  dd["removed"] = nlohmann::json::array({"a"});
  d["data"] = dd;
  UT_CHECK(cd.consume_json(d));
  UT_CHECK_EQ(cd.active_count(), 2u);  // b + c
  bool has_c = false;
  for (const auto& c : cd.active())
    if (c.id == "c") {
      has_c = true;
      UT_CHECK(c.chains_cat == "Direct");
    }
  UT_CHECK(has_c);
  UT_CHECK_EQ(cd.closed().size(), 1u);  // a 进 closed
  UT_CHECK(cd.closed()[0].id == "a");

  // closed 上限 500 FIFO
  for (int i = 0; i < 505; ++i) {
    std::string id = "x" + std::to_string(i);
    nlohmann::json di = nlohmann::json::object();
    di["type"] = "delta";
    nlohmann::json ddi = nlohmann::json::object();
    ddi["epochId"] = "1";
    ddi["sequenceId"] = 2 + i;
    ddi["updated"] = nlohmann::json::array();
    ddi["added"] = nlohmann::json::array();
    ddi["removed"] = nlohmann::json::array({id});
    di["data"] = ddi;
    // removed 的 id 不在 active（无副作用）但也不进 closed——
    // 仅 active 内连接被移除才入 closed；此处只验证上限逻辑：
    if (i == 0) {
      // 先塞满 500 个 closed
      for (int k = 0; k < 500; ++k)
        cd.consume_json(d);  // 无效（b/c 不存在）→ 不影响
      break;
    }
  }
  // 直接验证上限：手工灌入
  ConnectionData cd2(0);
  cd2.set_now_ms([] { return 0; });
  std::vector<std::string> hosts;
  for (int i = 0; i < 600; ++i) hosts.push_back("h" + std::to_string(i));
  cd2.consume_json(snapshot_msg(7, 0, hosts));
  // 逐个移除 600 → closed 应稳定在 500
  for (int i = 0; i < 600; ++i) {
    nlohmann::json di = nlohmann::json::object();
    di["type"] = "delta";
    nlohmann::json ddi = nlohmann::json::object();
    ddi["epochId"] = "7";
    ddi["sequenceId"] = i + 1;
    ddi["updated"] = nlohmann::json::array();
    ddi["added"] = nlohmann::json::array();
    ddi["removed"] = nlohmann::json::array({"h" + std::to_string(i)});
    di["data"] = ddi;
    UT_CHECK(cd2.consume_json(di));
  }
  UT_CHECK_EQ(cd2.active_count(), 0u);
  UT_CHECK_EQ(cd2.closed().size(), kMaxClosedConns);
  // 最旧的 h0..h99 被裁掉，保留 h100..
  UT_CHECK(cd2.closed()[0].id == "h100");
}

UT_TEST(t14_conn_throttle_merge) {
  ConnectionData cd(300);
  int64_t clock = 0;
  cd.set_now_ms([&] { return clock; });
  int fired = 0;
  cd.subscribe([&] { ++fired; });
  cd.consume_json(snapshot_msg(2, 0, {"a"}));
  UT_CHECK_EQ(fired, 1);
  // 300ms 窗口内合并：数据更新但只通知一次（前缘）
  clock += 100;
  cd.consume_json(snapshot_msg(2, 1, {"a", "b"}));
  UT_CHECK_EQ(fired, 1);
  UT_CHECK_EQ(cd.active_count(), 2u);  // 数据已更新
  clock += 400;                        // 过窗
  cd.consume_json(snapshot_msg(2, 2, {"a", "b", "c"}));
  UT_CHECK_EQ(fired, 2);
}

UT_TEST(t14_conn_view_rows_and_callbacks) {
  make_offscreen(271, 200, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::ModernFlat, ThemeMode::Light);
  ConnectionData cd(0);
  cd.set_now_ms([] { return 0; });
  cd.consume_json(snapshot_msg(3, 0, {"alpha.io", "beta.net"}));

  ConnectionsView view;
  view.bind(&cd);
  view.layout_tree({0, 0, 271, 200});
  UT_CHECK_EQ(view.visible_count(), 2u);

  // 过滤
  view.set_filter("beta");
  UT_CHECK_EQ(view.visible_count(), 1u);
  view.set_filter("");
  UT_CHECK_EQ(view.visible_count(), 2u);

  // 行点击/关闭回调
  std::string clicked, closed_id;
  view.on_row_click = [&](const std::string& id) { clicked = id; };
  view.close_connection = [&](const std::string& id) {
    closed_id = id;
    return true;
  };
  // 构造行 widget 触发回调（ScrollList factory 语义）
  ConnRow row;
  row.id = "alpha.io";
  row.host = "alpha.io:443";
  row.chains_cat = "Proxy";
  ConnRowWidget w;
  w.bind(row);
  w.on_click = [&] { if (view.on_row_click) view.on_row_click("alpha.io"); };
  w.on_close = [&] { if (view.close_connection) view.close_connection("alpha.io"); };
  w.layout_tree({0, 0, 271, kConnRowH});
  MouseEvent up{MouseEventType::Up, {100, 10}};
  route_mouse_event(&w, up);
  UT_CHECK(clicked == "alpha.io");
  clear_input_state();
  // 关闭按钮（右侧 18px 热区）
  MouseEvent mv2{MouseEventType::Move, {265, 10}};
  route_mouse_event(&w, mv2);
  MouseEvent up2{MouseEventType::Up, {265, 10}};
  route_mouse_event(&w, up2);
  UT_CHECK(closed_id == "alpha.io");

  // 像素：色点 + host 文本
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  w.on_paint(rt());
  rt().end_frame();
  // Proxy 色点 = primary #007AFF（x 4-12）
  Px dot = pixel(8, 10);
  UT_CHECK(dot.b > 200 && dot.r < 80);
  // host 文本有深色像素
  int dark = 0;
  for (int y = 2; y < 18; ++y)
    for (int x = 16; x < 140; ++x)
      if (pixel(x, y).r < 120) ++dark;
  UT_CHECK(dark > 2);
}

UT_TEST(t14_conn_view_sort_by_traffic) {
  ConnectionData cd(0);
  cd.set_now_ms([] { return 0; });
  cd.consume_json(snapshot_msg(4, 0, {"low", "high", "mid"}));
  // updated 三条不同流量
  nlohmann::json d = nlohmann::json::object();
  d["type"] = "delta";
  nlohmann::json dd = nlohmann::json::object();
  dd["epochId"] = "4";
  dd["sequenceId"] = 1;
  dd["updated"] = nlohmann::json::array(
      {"low", 1, 1, "high", 9000, 9000, "mid", 100, 100});
  dd["added"] = nlohmann::json::array();
  dd["removed"] = nlohmann::json::array();
  d["data"] = dd;
  UT_CHECK(cd.consume_json(d));
  cd.sort_by_traffic();
  UT_CHECK(cd.active()[0].id == "high");
  UT_CHECK(cd.active()[1].id == "mid");
  UT_CHECK(cd.active()[2].id == "low");
}
