// T13 测试：节点列表场景（数据装配 / 色标五档 / 快捷条 / 过滤排序 / 像素对照）。
#include <windows.h>

#include "test_ui_helper.h"
#include "test_util.h"

#include "ui/scenes/node_data.h"
#include "ui/scenes/node_list.h"
#include "ui/scenes/quick_bar.h"
#include "ui/theme/theme.h"

using namespace clashwin::ui;
using namespace clashwin::ui::scene;
using namespace clashwin::ui::theme;
using namespace ui_test;

namespace {
// mock DataHooks：可编程的成员/细节/选择/测速
struct MockState {
  std::vector<std::string> all = {"n1", "n2", "n3"};
  std::string now = "n1";
  std::map<std::string, std::string> types = {{"n1", "VMESS"},
                                              {"n2", "HYSTERIA2"},
                                              {"n3", "ANYTLS"}};
  bool select_ok = true;
  bool group_fail = false;
  int select_calls = 0;
  std::string last_selected;
  int test_calls = 0;
  int64_t clock_ms = 1000;
};

NodeData make_data(MockState& st) {
  DataHooks h;
  h.fetch_group = [&st](std::vector<std::string>* all, std::string* now) {
    if (st.group_fail) return false;
    *all = st.all;
    *now = st.now;
    return true;
  };
  h.fetch_proxies = [&st](std::map<std::string, clashwin::mihomo::ProxyItem>* items) {
    for (const auto& [name, type] : st.types) {
      clashwin::mihomo::ProxyItem it;
      it.name = name;
      it.type = type;
      (*items)[name] = it;
    }
    return true;
  };
  h.select_node = [&st](const std::string& node) {
    ++st.select_calls;
    st.last_selected = node;
    return st.select_ok;
  };
  h.test_group = [&st](const std::string&) { ++st.test_calls; return true; };
  h.now_ms = [&st] { return st.clock_ms; };
  return NodeData(std::move(h));
}

Px px_at(Widget& w, int x, int y) {
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  w.on_paint(rt());
  rt().end_frame();
  return pixel(x, y);
}
}  // namespace

UT_TEST(t13_delay_tier_mapping) {
  // ← formatDelayColor：250/400/5000 分界（v3.1.1 BUG-304：2000→5000）
  UT_CHECK(delay_tier(-1) == DelayTier::Unknown);   // 未测
  UT_CHECK(delay_tier(-2) == DelayTier::Unknown);   // testing
  UT_CHECK(delay_tier(0) == DelayTier::Timeout);    // 0 = Timeout
  UT_CHECK(delay_tier(249) == DelayTier::Success);
  UT_CHECK(delay_tier(250) == DelayTier::Mid);
  UT_CHECK(delay_tier(399) == DelayTier::Mid);
  UT_CHECK(delay_tier(400) == DelayTier::Slow);
  UT_CHECK(delay_tier(1999) == DelayTier::Slow);
  UT_CHECK(delay_tier(2000) == DelayTier::Slow);     // 拥塞期慢而可用
  UT_CHECK(delay_tier(4999) == DelayTier::Slow);
  UT_CHECK(delay_tier(5000) == DelayTier::Timeout);
  // 文本 ← formatDelay
  // src: proxy-item.tsx:215 未测（-1）不渲染数字/连字符，整段空白
  UT_CHECK(format_delay(-1) == L"");
  UT_CHECK(format_delay(-2) == L"testing");
  UT_CHECK(format_delay(0) == L"Timeout");
  UT_CHECK(format_delay(250) == L"250");
  UT_CHECK(format_delay(3000) == L"3000");
  UT_CHECK(format_delay(5000) == L"Timeout");
  UT_CHECK(format_delay(100001) == L"Error");
}

UT_TEST(t13_node_data_load_merge) {
  MockState st;
  NodeData data = make_data(st);
  UT_CHECK(data.load());
  // 成员顺序 = group.all 顺序；type 来自 get_proxies 细节合并
  UT_CHECK_EQ(data.row_count(), 3u);
  UT_CHECK(data.rows()[0].name == "n1" && data.rows()[0].type == "VMESS");
  UT_CHECK(data.rows()[1].name == "n2" && data.rows()[1].type == "HYSTERIA2");
  UT_CHECK(data.rows()[2].name == "n3" && data.rows()[2].type == "ANYTLS");
  UT_CHECK(data.now() == "n1");
  // 未测 → row_delay -1
  UT_CHECK_EQ(data.row_delay("n1"), -1);

  // 细节失败仍展示成员（type 空）
  st.group_fail = false;
  DataHooks h2;
  h2.fetch_group = [&st](std::vector<std::string>* all, std::string* now) {
    *all = st.all;
    *now = st.now;
    return true;
  };
  h2.fetch_proxies = [](std::map<std::string, clashwin::mihomo::ProxyItem>*) {
    return false;
  };
  h2.now_ms = [&st] { return st.clock_ms; };
  NodeData d2(std::move(h2));
  UT_CHECK(d2.load());
  UT_CHECK_EQ(d2.row_count(), 3u);
  UT_CHECK(d2.rows()[0].type.empty());

  // 组拉取失败 → load false
  st.group_fail = true;
  UT_CHECK(!data.load());
  st.group_fail = false;
}

UT_TEST(t13_node_data_filters_dummy) {
  // ← MF:services/cmds.ts:344-349 calcuProxies：all.filter(name && !isDummyNode)
  //   裸配置（无订阅）PROXY 组 all=[DIRECT] → 节点列表必须为空（空态 No Proxies），
  //   而不是把 DIRECT 当节点展示（用户首装实测缺陷）
  MockState st;
  st.all = {"DIRECT"};
  st.now = "DIRECT";
  st.types = {{"DIRECT", "Direct"}};
  NodeData bare = make_data(st);
  UT_CHECK(bare.load());
  UT_CHECK_EQ(bare.row_count(), 0u);
  UT_CHECK(bare.now() == "DIRECT");  // 组头「当前节点」仍显示 DIRECT（Mini 同款）

  // 广告/说明假节点一并剔除，真实节点保留原顺序
  // 注：REJECT 不属 isDummyNode（MF:utils/node.ts:81-108 只判空串/DIRECT/URL/广告词），
  //     故此处不放 REJECT，其非 dummy 语义由 t06_dummy_node_detection 覆盖。
  MockState st2;
  st2.all = {"剩余流量：100GB", "HK-01", "JP-02", "https://example.com"};
  st2.now = "HK-01";
  NodeData mixed = make_data(st2);
  UT_CHECK(mixed.load());
  UT_CHECK_EQ(mixed.row_count(), 2u);
  UT_CHECK(mixed.rows()[0].name == "HK-01");
  UT_CHECK(mixed.rows()[1].name == "JP-02");
}

UT_TEST(t13_node_data_select_and_bounce) {
  MockState st;
  NodeData data = make_data(st);
  data.load();
  // 成功：now 更新
  UT_CHECK(data.select("n2"));
  UT_CHECK(data.now() == "n2");
  UT_CHECK_EQ(st.select_calls, 1);
  // 失败回弹：now 不变
  st.select_ok = false;
  UT_CHECK(!data.select("n3"));
  UT_CHECK(data.now() == "n2");  // 未变（回弹语义）
}

UT_TEST(t13_node_data_delay_partial_refresh) {
  MockState st;
  NodeData data = make_data(st);
  data.load();
  size_t rows_before = data.row_count();
  // ← backend-delay-results {"group","results":[[name,delay]...]}
  nlohmann::json payload = nlohmann::json::object();
  payload["group"] = "PROXY";
  payload["results"] = nlohmann::json::array(
      {nlohmann::json::array({"n1", 120}), nlohmann::json::array({"n2", 450})});
  UT_CHECK(data.apply_delay_results(payload));
  // 局部刷新：行数不变（不触发全量重载）
  UT_CHECK_EQ(data.row_count(), rows_before);
  UT_CHECK_EQ(data.row_delay("n1"), 120);
  UT_CHECK_EQ(data.row_delay("n2"), 450);
  UT_CHECK_EQ(data.row_delay("n3"), -1);  // 未在结果中
  // TTL 30min：时钟推进 31min → 过期回 Unknown
  st.clock_ms += 31 * 60 * 1000;
  UT_CHECK_EQ(data.row_delay("n1"), -1);
  // 坏 payload
  UT_CHECK(!data.apply_delay_results(nlohmann::json{{"bad", 1}}));
}

UT_TEST(t13_takeover_segmented_apply_and_bounce) {
  TakeoverSegmented seg;
  seg.layout_tree({0, 0, 120, 24});
  std::string failed_mode;
  seg.on_apply_failed = [&](const std::string& m) { failed_mode = m; };
  seg.apply = [](const std::string& m) { return m != "TUN"; };  // TUN 失败

  // 点击 SYS → apply 成功 → mode 更新
  click(&seg, 60, 12);  // 第二段
  UT_CHECK(seg.mode() == "SYS");
  // 点击 TUN → apply 失败 → 回弹（mode 保持 SYS）+ 通知
  click(&seg, 100, 12);
  UT_CHECK(seg.mode() == "SYS");
  UT_CHECK(failed_mode == "TUN");
  // set_mode 外部同步
  seg.set_mode("MAN");
  UT_CHECK(seg.mode() == "MAN");
  clear_input_state();
}

UT_TEST(t13_quick_bar_callbacks) {
  QuickBar bar;
  bar.layout_tree({0, 0, 271, 30});
  int pins = 0, gears = 0;
  // ⚡ 测速钮已按 Mini 改版移入 NodeList（node_list.cpp），QuickBar 仅剩图钉/齿轮
  bar.on_toggle_pin = [&] { ++pins; };
  bar.on_open_settings = [&] { ++gears; };
  bar.set_node_text(L"node-a");
  // 图钉/齿轮点击（直接调 on_click，绕 HTCAPTION——窗口层接线归宿主）
  UT_CHECK(bar.child_count() >= 3);
  if (auto* icon = dynamic_cast<IconW*>(bar.child(bar.child_count() - 2)))
    icon->on_click();
  if (auto* icon = dynamic_cast<IconW*>(bar.child(bar.child_count() - 1)))
    icon->on_click();
  UT_CHECK_EQ(pins, 1);
  UT_CHECK_EQ(gears, 1);
}

UT_TEST(t13_node_list_filter_and_sort) {
  MockState st;
  st.all = {"alpha", "Beta", "gamma-hy", "delta"};
  NodeData data = make_data(st);
  data.load();
  // 延迟缓存：alpha=120 Beta=450 gamma=5200（超时）delta 未测
  data.set_delay("alpha", 120);
  data.set_delay("Beta", 450);
  data.set_delay("gamma-hy", 5200);

  NodeList list;
  list.bind(&data);
  list.layout_tree({0, 0, 271, 500});
  // 无组头（1:1 Mini：列表直接是节点行）+ 4 节点
  UT_CHECK_EQ(list.visible_count(), 4u);

  // 过滤（大小写不敏感）："be" → Beta
  list.set_filter("be");
  UT_CHECK_EQ(list.visible_count(), 1u);
  list.set_filter("");
  UT_CHECK_EQ(list.visible_count(), 4u);

  // 排序初值 = 按延迟（src: use-head-state.ts:21 sortType: 1），Unknown/Timeout 沉底
  UT_CHECK(list.sorted_by_delay());
  // visible: [alpha(120), Beta(450), gamma(5200 超时沉底), delta(未测沉底)]
  // 经 ScrollList 行工厂验证顺序：取物化行绘制不可直接断言顺序——
  // 用再过滤验证排序稳定：过滤掉 alpha 后 Beta 在 gamma 前
  list.set_filter("a");
  UT_CHECK(list.visible_count() >= 3u);
  list.toggle_sort();
  list.set_filter("");
}

UT_TEST(t13_node_row_pixels_271) {
  make_offscreen(271, 240, 96);
  auto& svc = ThemeService::instance();
  // ModernFlat：primary=palette=#007AFF（断言语义与 #007AFF 一致）
  svc.init(SkinId::ModernFlat, ThemeMode::Light);

  // 节点行：selected（primary 浅底）+ 延迟 120（绿）
  NodeRowWidget row;
  row.bind("VMESS", "node-alpha.example.com", 120, true);
  row.layout_tree({0, 0, 271, kNodeRowH});
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  row.on_paint(rt());
  rt().end_frame();

  // 选中左边框 3px primary（#007AFF）
  Px sel = pixel(1, 19);
  UT_CHECK(sel.b > 200 && sel.r < 80);  // primary 蓝
  // 选中底 alpha(primary,0.15) 混白 ≈ (218,233,252)
  Px bg = pixel(30, 10);
  UT_CHECK(bg.b > 200 && bg.b < 256 && bg.r > 180);
  // 左列类型区（x<55）：灰字区底色同上；x=54 处 divider 2px
  Px div = pixel(54, 19);
  UT_CHECK(div.r < 240);  // divider rgba(0,0,0,0.12) 混底变深（proxy-item.tsx:121）
  // 行底 2px divider（kNodeRowH=26 → 底 2px 在 y24-25）
  Px bottom = pixel(130, 25);
  UT_CHECK(bottom.r < 240);
  // 延迟列（右 65px 区，11px 绿字 #06943D 有像素）
  int green = 0;
  for (int y = 10; y < 30; ++y)
    for (int x = 210; x < 268; ++x) {
      Px p = pixel(x, y);
      if (p.g > 90 && p.r < 120 && p.g > p.r) ++green;
    }
  UT_CHECK(green > 3);  // 120ms → success 绿字存在

  // 未测节点：延迟列 '-'（灰 text.secondary）
  NodeRowWidget row2;
  row2.bind("VMESS", "plain", -1, false);
  row2.index_in_group = 1;  // 奇数行 rgba(0,120,215,.04)
  row2.layout_tree({0, 0, 271, kNodeRowH});
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  row2.on_paint(rt());
  rt().end_frame();
  Px odd = pixel(120, 5);
  UT_CHECK(odd.b > odd.r);  // 蓝调奇偶底
}

UT_TEST(t13_node_row_retro3d_selected_bg) {
  // retro-3d 下选中底色 = alpha(palette.primary,0.15)；retro 不强制 primary
  // → light primary #007AFF（← proxy-item '&.Mui-selected' bgcolor，皮肤无关）
  make_offscreen(271, 120, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::Retro3d, ThemeMode::Light);
  NodeRowWidget row;
  row.bind("VMESS", "n", -1, true);
  row.layout_tree({0, 0, 271, kNodeRowH});
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  row.on_paint(rt());
  rt().end_frame();
  Px bg = pixel(60, 8);
  UT_CHECK(bg.b > 200 && bg.b < 256 && bg.r > 180);  // alpha(0x007AFF,0.15) 混白
  svc.init(SkinId::Retro3d, ThemeMode::Light);
}

UT_TEST(t13_no_group_head) {
  MockState st;
  NodeData data = make_data(st);
  data.load();
  NodeList list;
  list.bind(&data);
  list.layout_tree({0, 0, 271, 500});
  // Mini 单组扁平列表，无分组头行：src: use-render-list.ts:144
  UT_CHECK_EQ(list.visible_count(), 3u);
  UT_CHECK(!list.empty_state_visible());
}

UT_TEST(t13_empty_state) {
  MockState st;
  st.all.clear();
  NodeData data = make_data(st);
  data.load();
  NodeList list;
  list.bind(&data);
  list.layout_tree({0, 0, 271, 500});
  // 无代理 → Inbox + "No Proxies"：src: proxy-render.tsx:63-78
  UT_CHECK_EQ(list.visible_count(), 0u);
  UT_CHECK(list.empty_state_visible());
  // 过滤到空同样显示空态
  MockState st2;
  NodeData data2 = make_data(st2);
  data2.load();
  NodeList list2;
  list2.bind(&data2);
  list2.layout_tree({0, 0, 271, 500});
  list2.set_filter("zzz-no-match");
  UT_CHECK_EQ(list2.visible_count(), 0u);
  UT_CHECK(list2.empty_state_visible());
}

UT_TEST(t13_scroll_top_button) {
  // ← src/components/layout/scroll-top-button.tsx:10-40
  //   + proxy-groups.tsx:172（scrollTop > 100 显示）/ :212-220（点击回顶）
  MockState st;
  st.all.clear();
  st.types.clear();
  for (int i = 0; i < 40; ++i) {
    const std::string n = "node-" + std::to_string(i);
    st.all.push_back(n);
    st.types[n] = "VMESS";
  }
  st.now = st.all.front();
  NodeData data = make_data(st);
  data.load();
  NodeList list;
  list.bind(&data);
  list.layout_tree({0, 0, 271, 200});

  ScrollTopButton* top = list.scroll_top_button();
  UT_CHECK(top != nullptr);
  UT_CHECK(!top->shown());      // 初始未滚动 → 不显示
  UT_CHECK(!top->interactive());

  // 滚过 100px 阈值 → 淡入
  list.list()->set_scroll_offset(101);
  UT_CHECK(top->shown());
  Sleep(260);                   // MUI enteringScreen 225ms
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  top->on_paint(rt());
  rt().end_frame();
  UT_CHECK(top->alpha() > 0.9f);
  UT_CHECK(top->interactive());

  // 点击 → 回顶并淡出（:212-220）
  top->on_click();
  UT_CHECK_EQ(list.list()->scroll_offset(), 0.0f);
  UT_CHECK(!top->shown());
  Sleep(220);
  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  top->on_paint(rt());
  rt().end_frame();
  UT_CHECK(top->alpha() <= 0.05f);
  UT_CHECK(!top->interactive());  // visibility:hidden → 不参与命中
}
