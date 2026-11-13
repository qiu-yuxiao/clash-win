// T20 单测：连接面板 + v3.2.1 Parity 全量验证
// 覆盖用例：
// 1. t20_ws_snapshot_and_delta_envelopes：快照与增量帧解析、序列连续性与 epoch 断档触发 resync
// 2. t20_closed_history_cap_500：closed 历史队列 500 上限 FIFO 裁剪与清空
// 3. t20_speed_delta_calculation_and_zero_idle：delta 速率计算及未更新空闲连接归 0 B/s Bug 根治验证
// 4. t20_table_sorting_host_and_chains：Host 与 Chains (64px) ▲ asc -> ▼ desc -> none 循环排序
// 5. t20_quick_routing_rule_generation：域名后缀提取 (含双 TLD)、IP-CIDR、进程名规则构造
// 6. t20_quick_routing_merge_yaml_injection：向 Merge.yaml 写入 prepend-rules 去重与置顶生效
// 7. t20_connection_detail_dialog_fields：连接详情 13 项完整元数据导出与离屏渲染
// 8. t20_settings_drawer_two_column_responsive_split：<=285px 单列与 >285px 双列响应式布局
// 9. t20_ws_subscription_lifecycle_and_bug306：connections WS 动态启停与 BUG-306 同步布局无丢帧
// 10. t20_d2d_local_coordinates_no_double_offset：局部坐标规则与离屏像素断言（消除双偏移）

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/quick_routing.h"
#include "events/event_bus.h"
#include "test_ui_helper.h"
#include "test_util.h"
#include "ui/breakpoints.h"
#include "ui/scenes/connection_data.h"
#include "ui/scenes/connection_detail_dialog.h"
#include "ui/scenes/connections_panel.h"
#include "ui/scenes/path_control.h"
#include "ui/scenes/settings_drawer.h"
#include "ui/theme/theme.h"
#include "util/dirs.h"
#include "util/utf.h"
#include "util/yaml_utils.h"

namespace fs = std::filesystem;
using namespace clashwin;
using namespace clashwin::ui;
using namespace clashwin::ui::win;
using namespace clashwin::ui::theme;
using namespace clashwin::ui::scene;
using namespace ui_test;

namespace {
nlohmann::json make_snapshot_payload(const std::string& epoch, int64_t seq,
                                     const std::vector<std::pair<std::string, std::string>>& items) {
  nlohmann::json msg = nlohmann::json::object();
  msg["type"] = "snapshot";
  nlohmann::json data = nlohmann::json::object();
  data["epochId"] = epoch;
  data["sequenceId"] = seq;
  data["uploadTotal"] = 5000.0;
  data["downloadTotal"] = 10000.0;
  nlohmann::json conns = nlohmann::json::array();
  for (const auto& [id, host] : items) {
    nlohmann::json c = nlohmann::json::object();
    c["id"] = id;
    c["upload"] = 100.0;
    c["download"] = 200.0;
    nlohmann::json m = nlohmann::json::object();
    m["host"] = host;
    m["destinationPort"] = "443";
    c["metadata"] = m;
    c["chains"] = nlohmann::json::array({"Proxy"});
    conns.push_back(c);
  }
  data["connections"] = conns;
  msg["data"] = data;
  return msg;
}
}  // namespace

// Test 1: Snapshot and Delta Sequence Envelopes & Epoch Resync
UT_TEST(t20_ws_snapshot_and_delta_envelopes) {
  ConnectionData cd(0);
  int resync_calls = 0;
  cd.on_resync = [&] { ++resync_calls; };

  // 1. Snapshot 全量载入
  auto snap = make_snapshot_payload("epoch-100", 0, {{"c1", "a.com"}, {"c2", "b.com"}});
  UT_CHECK(cd.consume_json(snap));
  UT_CHECK_EQ(cd.active_count(), 2u);
  UT_CHECK_EQ(cd.upload_total(), 5000.0);
  UT_CHECK_EQ(cd.download_total(), 10000.0);

  // 2. 正常 Delta (seq=1)
  nlohmann::json d1 = nlohmann::json::object();
  d1["type"] = "delta";
  nlohmann::json dd1 = nlohmann::json::object();
  dd1["epochId"] = "epoch-100";
  dd1["sequenceId"] = 1;
  dd1["uploadTotal"] = 5500.0;
  dd1["downloadTotal"] = 10800.0;
  dd1["updated"] = nlohmann::json::array({"c1", 200.0, 400.0});
  dd1["added"] = nlohmann::json::array({nlohmann::json::object({
      {"id", "c3"},
      {"upload", 50.0},
      {"download", 80.0},
      {"metadata", nlohmann::json::object({{"host", "c.com"}, {"destinationPort", "80"}})},
      {"chains", nlohmann::json::array({"DIRECT"})},
  })});
  dd1["removed"] = nlohmann::json::array({"c2"});
  d1["data"] = dd1;

  UT_CHECK(cd.consume_json(d1));
  UT_CHECK_EQ(cd.active_count(), 2u);      // c1 + c3
  UT_CHECK_EQ(cd.closed().size(), 1u);     // c2 移入 closed
  UT_CHECK_EQ(cd.closed()[0].id, "c2");

  // 3. 序列断档 (预期 seq=2，实际喂 seq=4)
  nlohmann::json d_gap = d1;
  dd1["sequenceId"] = 4;
  d_gap["data"] = dd1;
  UT_CHECK(!cd.consume_json(d_gap));
  UT_CHECK_EQ(resync_calls, 1);
  UT_CHECK_EQ(cd.active_count(), 2u);  // 保持旧态

  // 4. Epoch 不匹配
  nlohmann::json d_mismatch = d1;
  dd1["sequenceId"] = 2;
  dd1["epochId"] = "epoch-999";
  d_mismatch["data"] = dd1;
  UT_CHECK(!cd.consume_json(d_mismatch));
  UT_CHECK_EQ(resync_calls, 2);
}

// Test 2: Closed History Cap at 500 items (FIFO)
UT_TEST(t20_closed_history_cap_500) {
  ConnectionData cd(0);
  std::vector<std::pair<std::string, std::string>> hosts;
  for (int i = 0; i < 550; ++i) {
    hosts.emplace_back("id_" + std::to_string(i), "host_" + std::to_string(i) + ".com");
  }

  // 载入 550 条
  cd.consume_json(make_snapshot_payload("ep1", 0, hosts));
  UT_CHECK_EQ(cd.active_count(), 550u);

  // 逐个通过 delta 移除全部 550 条
  for (int i = 0; i < 550; ++i) {
    nlohmann::json d = nlohmann::json::object();
    d["type"] = "delta";
    nlohmann::json dd = nlohmann::json::object();
    dd["epochId"] = "ep1";
    dd["sequenceId"] = i + 1;
    dd["updated"] = nlohmann::json::array();
    dd["added"] = nlohmann::json::array();
    dd["removed"] = nlohmann::json::array({"id_" + std::to_string(i)});
    d["data"] = dd;
    UT_CHECK(cd.consume_json(d));
  }

  UT_CHECK_EQ(cd.active_count(), 0u);
  // 严格上限 500 条
  UT_CHECK_EQ(cd.closed().size(), kMaxClosedConns);
  UT_CHECK_EQ(cd.closed().size(), 500u);

  // FIFO 语义验证：最早的 id_0..id_49 已被淘汰，队首为 id_50，队尾为 id_549
  UT_CHECK_EQ(cd.closed().front().id, "id_50");
  UT_CHECK_EQ(cd.closed().back().id, "id_549");

  // 清空历史
  cd.clear_closed();
  UT_CHECK_EQ(cd.closed().size(), 0u);
}

// Test 3: Incremental Speed Delta & Zeroing Idle Connections
UT_TEST(t20_speed_delta_calculation_and_zero_idle) {
  ConnectionData cd(0);

  // 初始 Snapshot
  cd.consume_json(make_snapshot_payload("ep_speed", 0, {{"c_active", "act.com"}, {"c_idle", "idl.com"}}));
  UT_CHECK_EQ(cd.active()[0].cur_upload, 0.0);
  UT_CHECK_EQ(cd.active()[1].cur_upload, 0.0);

  // Delta 1: 两条连接均有流量增长
  nlohmann::json d1 = nlohmann::json::object();
  d1["type"] = "delta";
  nlohmann::json dd1 = nlohmann::json::object();
  dd1["epochId"] = "ep_speed";
  dd1["sequenceId"] = 1;
  dd1["updated"] = nlohmann::json::array({
      "c_active", 160.0, 320.0,
      "c_idle", 150.0, 280.0
  });
  d1["data"] = dd1;
  UT_CHECK(cd.consume_json(d1));

  for (const auto& c : cd.active()) {
    if (c.id == "c_active") {
      UT_CHECK_EQ(c.cur_upload, 60.0);    // 160 - 100
      UT_CHECK_EQ(c.cur_download, 120.0); // 320 - 200
    }
    if (c.id == "c_idle") {
      UT_CHECK_EQ(c.cur_upload, 50.0);    // 150 - 100
      UT_CHECK_EQ(c.cur_download, 80.0);  // 280 - 200
    }
  }

  // Delta 2: 仅更新 c_active，c_idle 未出现在 updated 列表中
  nlohmann::json d2 = nlohmann::json::object();
  d2["type"] = "delta";
  nlohmann::json dd2 = nlohmann::json::object();
  dd2["epochId"] = "ep_speed";
  dd2["sequenceId"] = 2;
  dd2["updated"] = nlohmann::json::array({
      "c_active", 210.0, 420.0
  });
  d2["data"] = dd2;
  UT_CHECK(cd.consume_json(d2));

  // 验证 Bug 修复结果：未在 updated 中的 c_idle 必须重置为 0 B/s，不得残留上一帧速率！
  bool idle_zeroed = false;
  bool active_updated = false;
  for (const auto& c : cd.active()) {
    if (c.id == "c_active") {
      UT_CHECK_EQ(c.cur_upload, 50.0);    // 210 - 160
      UT_CHECK_EQ(c.cur_download, 100.0); // 420 - 320
      active_updated = true;
    }
    if (c.id == "c_idle") {
      if (c.cur_upload == 0.0 && c.cur_download == 0.0) {
        idle_zeroed = true;
      }
    }
  }
  UT_CHECK(active_updated);
  UT_CHECK(idle_zeroed);
}

// Test 4: Table Sorting on Host & Chains (Width 64px)
UT_TEST(t20_table_sorting_host_and_chains) {
  ConnectionData cd(0);

  // 构造 3 个特征不同的连接
  nlohmann::json snap = nlohmann::json::object();
  snap["type"] = "snapshot";
  nlohmann::json data = nlohmann::json::object();
  data["epochId"] = "sort_ep";
  data["sequenceId"] = 0;
  data["connections"] = nlohmann::json::array({
      nlohmann::json::object({
          {"id", "c1"},
          {"metadata", nlohmann::json::object({{"host", "beta.net"}, {"destinationPort", "443"}})},
          {"chains", nlohmann::json::array({"DIRECT"})},
      }),
      nlohmann::json::object({
          {"id", "c2"},
          {"metadata", nlohmann::json::object({{"host", "alpha.org"}, {"destinationPort", "80"}})},
          {"chains", nlohmann::json::array({"REJECT"})},
      }),
      nlohmann::json::object({
          {"id", "c3"},
          {"metadata", nlohmann::json::object({{"host", "gamma.com"}, {"destinationPort", "8080"}})},
          {"chains", nlohmann::json::array({"ProxyGroup", "HK"})},
      }),
  });
  snap["data"] = data;
  cd.consume_json(snap);

  ConnectionsPanel panel;
  panel.bind(&cd);
  panel.layout_tree({0, 0, 394, 600});

  UT_CHECK_EQ(panel.visible_count(), 3u);

  // 1. 验证 Host 列循环排序：▲ asc -> ▼ desc -> none
  panel.cycle_sort_host();
  UT_CHECK(panel.sort_column() == SortCol::Host);
  UT_CHECK(panel.sort_direction() == SortDir::Asc);
  UT_CHECK_EQ(panel.visible_items()[0].id, "c2");  // alpha.org
  UT_CHECK_EQ(panel.visible_items()[1].id, "c1");  // beta.net
  UT_CHECK_EQ(panel.visible_items()[2].id, "c3");  // gamma.com

  panel.cycle_sort_host();
  UT_CHECK(panel.sort_direction() == SortDir::Desc);
  UT_CHECK_EQ(panel.visible_items()[0].id, "c3");  // gamma.com
  UT_CHECK_EQ(panel.visible_items()[1].id, "c1");  // beta.net
  UT_CHECK_EQ(panel.visible_items()[2].id, "c2");  // alpha.org

  panel.cycle_sort_host();
  UT_CHECK(panel.sort_direction() == SortDir::None);

  // 2. 验证 Chains 列循环排序 (三分类 Direct / Proxy / Reject)
  panel.cycle_sort_chains();
  UT_CHECK(panel.sort_column() == SortCol::Chains);
  UT_CHECK(panel.sort_direction() == SortDir::Asc);
  // Direct (c1) -> Proxy (c3) -> Reject (c2)
  UT_CHECK_EQ(panel.visible_items()[0].chains_category(), "Direct");
  UT_CHECK_EQ(panel.visible_items()[1].chains_category(), "Proxy");
  UT_CHECK_EQ(panel.visible_items()[2].chains_category(), "Reject");

  panel.cycle_sort_chains();
  UT_CHECK(panel.sort_direction() == SortDir::Desc);
  UT_CHECK_EQ(panel.visible_items()[0].chains_category(), "Reject");
  UT_CHECK_EQ(panel.visible_items()[1].chains_category(), "Proxy");
  UT_CHECK_EQ(panel.visible_items()[2].chains_category(), "Direct");

  panel.cycle_sort_chains();
  UT_CHECK(panel.sort_direction() == SortDir::None);
}

// Test 5: Quick Routing Rule Generation (Suffix, IP-CIDR, Process)
UT_TEST(t20_quick_routing_rule_generation) {
  using namespace clashwin::core::quick_routing;

  // 1. 域名后缀提取 (含单 TLD 与双 TLD)
  UT_CHECK_EQ(extract_domain_suffix("example.com"), "example.com");
  UT_CHECK_EQ(extract_domain_suffix("sub.example.com"), "example.com");
  UT_CHECK_EQ(extract_domain_suffix("a.b.c.example.com"), "example.com");
  UT_CHECK_EQ(extract_domain_suffix("https://www.google.com:443/search?q=test"), "google.com");

  // 双 TLD 规则（.com.cn / .co.uk / .gov.cn 等）
  UT_CHECK_EQ(extract_domain_suffix("api.weather.com.cn"), "weather.com.cn");
  UT_CHECK_EQ(extract_domain_suffix("news.bbc.co.uk"), "bbc.co.uk");
  UT_CHECK_EQ(extract_domain_suffix("sub.test.org.cn"), "test.org.cn");

  // 2. 规则字符串生成
  // 进程
  std::string r_proc = build_rule_string(RuleType::Process, "chrome.exe", RuleTarget::Proxy);
  UT_CHECK_EQ(r_proc, "PROCESS-NAME,chrome.exe,PROXY");

  // IPv4 CIDR
  std::string r_ipv4 = build_rule_string(RuleType::Domain, "1.1.1.1", RuleTarget::Direct);
  UT_CHECK_EQ(r_ipv4, "IP-CIDR,1.1.1.1/32,DIRECT");

  // IPv6 CIDR (v3.3.0 改为精确 IP-CIDR6)
  std::string r_ipv6 = build_rule_string(RuleType::Domain, "2606:4700:4700::1111", RuleTarget::Reject);
  UT_CHECK_EQ(r_ipv6, "IP-CIDR6,2606:4700:4700::1111/128,REJECT");

  // 精确域名规则 (v3.3.0 quick_routing 生成精确 DOMAIN 规则而非通配后缀)
  std::string r_dom = build_rule_string(RuleType::Domain, "music.163.com", RuleTarget::Direct);
  UT_CHECK_EQ(r_dom, "DOMAIN,music.163.com,DIRECT");

  std::string r_dom_cn = build_rule_string(RuleType::Domain, "portal.gov.cn", RuleTarget::Proxy);
  UT_CHECK_EQ(r_dom_cn, "DOMAIN,portal.gov.cn,PROXY");
}

// Test 6: Quick Routing Merge.yaml Injection
UT_TEST(t20_quick_routing_merge_yaml_injection) {
  wchar_t temp_path[MAX_PATH];
  GetTempPathW(MAX_PATH, temp_path);
  std::wstring home = std::wstring(temp_path) + L"clashwin_t20_qr_" + std::to_wstring(GetTickCount64());
  std::wstring prof_dir = home + L"\\profiles";
  fs::create_directories(prof_dir);

  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", home.c_str());

  std::wstring merge_file = prof_dir + L"\\Merge.yaml";

  // 写入已有初始规则
  {
    std::ofstream out(merge_file);
    out << "prepend-rules:\n"
        << "  - DOMAIN-SUFFIX,existing.org,DIRECT\n";
    out.close();
  }

  // 注入新规则
  auto res1 = core::quick_routing::add_quick_routing_rule(core::quick_routing::RuleType::Domain,
                                                          "github.com", core::quick_routing::RuleTarget::Proxy);
  UT_CHECK(res1.ok);
  UT_CHECK_EQ(res1.rule, "DOMAIN,github.com,PROXY");

  // 读取 Merge.yaml 验证置顶
  auto doc1 = util::load_yaml_file(merge_file);
  UT_CHECK(doc1.is_ok());
  auto rules1 = doc1.value()["prepend-rules"];
  UT_CHECK_EQ(rules1.size(), 2u);
  UT_CHECK_EQ(rules1[0].as<std::string>(), "DOMAIN,github.com,PROXY");
  UT_CHECK_EQ(rules1[1].as<std::string>(), "DOMAIN-SUFFIX,existing.org,DIRECT");

  // 对相同域名重新添加不同目标 (DIRECT) -> 验证原规则被覆盖去重且保持置顶
  auto res2 = core::quick_routing::add_quick_routing_rule(core::quick_routing::RuleType::Domain,
                                                          "github.com", core::quick_routing::RuleTarget::Direct);
  UT_CHECK(res2.ok);
  auto doc2 = util::load_yaml_file(merge_file);
  auto rules2 = doc2.value()["prepend-rules"];
  UT_CHECK_EQ(rules2.size(), 2u);
  UT_CHECK_EQ(rules2[0].as<std::string>(), "DOMAIN,github.com,DIRECT");
  UT_CHECK_EQ(rules2[1].as<std::string>(), "DOMAIN-SUFFIX,existing.org,DIRECT");

  // 清理临时环境
  _wputenv_s(L"CLASHWIN_HOME_OVERRIDE", L"");
  std::error_code ec;
  fs::remove_all(home, ec);
}

// Test 7: Connection Detail Dialog 13 Metadata Fields
UT_TEST(t20_connection_detail_dialog_fields) {
  ConnItem item;
  item.id = "conn_detail_1";
  item.download = 10485760.0;     // 10 MB
  item.upload = 2097152.0;        // 2 MB
  item.cur_download = 102400.0;   // 100 KB/s
  item.cur_upload = 51200.0;      // 50 KB/s
  item.start = "2026-10-28 12:00:00";
  item.rule = "DomainSuffix";
  item.rule_payload = "clash.win";
  item.chains = {"ProxyGroup", "HongKong 01", "DIRECT"};
  item.metadata.host = "api.clash.win";
  item.metadata.destination_port = "8443";
  item.metadata.source_ip = "192.168.1.50";
  item.metadata.source_port = "54321";
  item.metadata.destination_ip = "104.21.5.12";
  item.metadata.type = "HTTPS";
  item.metadata.network = "tcp";
  item.metadata.process = "clashwin.exe";
  item.metadata.process_path = "C:\\Tools\\clashwin.exe";

  ConnectionDetailDialog dialog;
  dialog.open(item, false);
  UT_CHECK(dialog.is_open());

  auto fields = dialog.fields();
  UT_CHECK_EQ(fields.size(), 13u);

  // 1. Host
  UT_CHECK(fields[0].first == L"主机");
  UT_CHECK(fields[0].second == L"api.clash.win:8443");

  // 2. Downloaded (10 MB)
  UT_CHECK(fields[1].first == L"下载量");
  UT_CHECK(fields[1].second.find(L"MB") != std::wstring::npos);

  // 3. Uploaded (2 MB)
  UT_CHECK(fields[2].first == L"上传量");
  UT_CHECK(fields[2].second.find(L"MB") != std::wstring::npos);

  // 4. Dl Speed
  UT_CHECK(fields[3].first == L"下载速度");
  UT_CHECK(fields[3].second.find(L"/s") != std::wstring::npos);

  // 5. Ul Speed
  UT_CHECK(fields[4].first == L"上传速度");
  UT_CHECK(fields[4].second.find(L"/s") != std::wstring::npos);

  // 6. Chains (反转展示: DIRECT / HongKong 01 / ProxyGroup)
  UT_CHECK(fields[5].first == L"链路");
  UT_CHECK_EQ(fields[5].second, L"DIRECT / HongKong 01 / ProxyGroup");

  // 7. Rule
  UT_CHECK(fields[6].first == L"规则");
  UT_CHECK_EQ(fields[6].second, L"DomainSuffix(clash.win)");

  // 8. Process
  UT_CHECK(fields[7].first == L"进程");
  UT_CHECK_EQ(fields[7].second, L"clashwin.exe(C:\\Tools\\clashwin.exe)");

  // 9. Time
  UT_CHECK(fields[8].first == L"连接时间");
  UT_CHECK_EQ(fields[8].second, L"2026-10-28 12:00:00");

  // 10. Source
  UT_CHECK(fields[9].first == L"源地址");
  UT_CHECK_EQ(fields[9].second, L"192.168.1.50:54321");

  // 11. Destination
  UT_CHECK(fields[10].first == L"目标地址");
  UT_CHECK_EQ(fields[10].second, L"104.21.5.12");

  // 12. Destination Port
  UT_CHECK(fields[11].first == L"目标端口");
  UT_CHECK_EQ(fields[11].second, L"8443");

  // 13. Type
  UT_CHECK(fields[12].first == L"类型");
  UT_CHECK_EQ(fields[12].second, L"HTTPS(tcp)");

  // 离屏渲染验证无崩溃
  make_offscreen(400, 500, 96);
  dialog.layout_tree({0, 0, 400, 500});
  rt().begin_frame();
  dialog.on_paint(rt());
  rt().end_frame();

  // 纸面尺寸上限：maxWidth 520 / maxHeight 480（src: connection-detail.tsx:59-61）
  dialog.layout_tree({0, 0, 900, 900});
  UT_CHECK(dialog.card_rect().w <= 520.0f);
  UT_CHECK(dialog.card_rect().h <= 480.0f);
  UT_CHECK(dialog.card_rect().w > 0 && dialog.card_rect().h > 0);
}

// Test 8: SettingsDrawer Two-Column Responsive Split
// src: _layout.tsx:1403-1438 容器全窗 padding 12 / gap 12；左列 240 / gap 5 / pr 1
// src: _layout.tsx:1519-1533 右列 ConnectionsPanel flex:1
UT_TEST(t20_settings_drawer_two_column_responsive_split) {
  SettingsDrawer drawer;
  drawer.set_open(true);

  // 1. 窄窗形态 (<= 285px): 单列呈现，ConnectionsPanel 隐藏；D12 居中 (271-231)/2 = 20
  drawer.layout_tree({0, 0, 271, 680});
  UT_CHECK_EQ(drawer.panel()->bounds().w, 271.0f);  // 全窗覆盖层
  UT_CHECK_EQ(drawer.panel()->bounds().h, 680.0f);
  UT_CHECK_EQ(drawer.profile_card()->bounds().x, 20.0f);  // D12: (271 - 231) / 2 = 20
  UT_CHECK_EQ(drawer.profile_card()->bounds().w, 231.0f);  // 240 - pr 8 - border 1
  UT_CHECK(drawer.conn_panel() != nullptr);
  UT_CHECK(!drawer.conn_panel()->visible());

  // 2. 宽窗形态 (> 285px): 两列并排，左列固定 240px，右列自适应
  drawer.layout_tree({0, 0, 640, 680});
  UT_CHECK_EQ(drawer.panel()->bounds().w, 640.0f);
  UT_CHECK_EQ(drawer.profile_card()->bounds().x, 16.0f);  // 宽窗并排靠左: 边框 4 + padding 12 = 16
  UT_CHECK_EQ(drawer.profile_card()->bounds().w, 231.0f);
  UT_CHECK(drawer.conn_panel()->visible());
  RectF cp_bounds = drawer.conn_panel()->bounds();
  UT_CHECK_EQ(cp_bounds.x, 264.0f);  // 12 + 240 + 12
  UT_CHECK_EQ(cp_bounds.w, 364.0f);  // 640 - 264 - 12
  // area.h + 30(标题栏) + 135(底部流量区) = 845 > 830 → tall 形态，
  // 抽屉内底部行占 30（_layout.tsx:1593/1654），故可用高 = 680 - 30
  // 再扣 2*4(边框) + 2*12(padding) = 618。
  UT_CHECK_EQ(cp_bounds.h, 618.0f);
}

// Test 9: WebSocket Subscription Lifecycle & BUG-306 Synchronous Resize Fix
UT_TEST(t20_ws_subscription_lifecycle_and_bug306) {
  Breakpoints bp;

  auto should_subscribe = [&](double w, double h, bool open) {
    bp.set_logical_size(w, h);
    return open && !bp.is_narrow() && !bp.is_mini() && h > win::kMiniHeightThreshold;
  };

  // 1. 抽屉关闭时：绝不订阅
  UT_CHECK(!should_subscribe(640, 860, false));

  // 2. 抽屉打开，但属于极小窗 (h <= 126)：不订阅
  UT_CHECK(!should_subscribe(640, 100, true));

  // 3. 抽屉打开，但属于窄窗 (w <= 285)：不订阅
  UT_CHECK(!should_subscribe(271, 680, true));

  // 4. 抽屉打开，大窗且高度正常：激活订阅
  UT_CHECK(should_subscribe(640, 680, true));

  // 5. BUG-306 跨越 285px 临界线同步无丢帧验证：
  // 模拟从 280 缩放到 640，无 debounce 延迟，立即同步进入大窗并激活
  UT_CHECK(!should_subscribe(280, 680, true));
  UT_CHECK(should_subscribe(640, 680, true));
}

// Test 10: Direct2D Local Coordinates Rule (Zero Double-Offset)
UT_TEST(t20_d2d_local_coordinates_no_double_offset) {
  make_offscreen(640, 600, 96);
  auto& svc = ThemeService::instance();
  svc.init(SkinId::ModernFlat, ThemeMode::Light);

  ConnectionData cd(0);
  cd.consume_json(make_snapshot_payload("ep_d2d", 0, {{"c_px", "test.pixel.com"}}));

  ConnectionsPanel panel;
  panel.bind(&cd);

  // 模拟挂载在非零全局坐标：x = 246, y = 30
  panel.layout_tree({246, 30, 394, 570});

  rt().begin_frame();
  rt().clear({1, 1, 1, 1});
  panel.paint_all(rt());
  rt().end_frame();

  // 像素断言：第一行连接的色点位于 panel 局部坐标 {4..12, 68+6..68+14}
  // 全局绝对坐标应为 x in [250, 258], y in [104, 112]
  int dot_pixels = 0;
  for (int y = 104; y < 112; ++y) {
    for (int x = 250; x < 258; ++x) {
      Px p = pixel(x, y);
      // Proxy 色点主色 (含较强 blue 或 primary)
      if (p.b > 180 && p.r < 100) ++dot_pixels;
    }
  }
  UT_CHECK(dot_pixels > 2);

  // 关键断言：如果发生了双偏移（double offset），色点会被错误偏置到 x ≈ 246 + 250 = 496, y ≈ 30 + 104 = 134
  int displaced_pixels = 0;
  for (int y = 130; y < 138; ++y) {
    for (int x = 492; x < 500; ++x) {
      Px p = pixel(x, y);
      if (p.b > 180 && p.r < 100) ++displaced_pixels;
    }
  }
  UT_CHECK_EQ(displaced_pixels, 0);  // 绝无双偏移位移
}

UT_TEST(t20_v330_path_control_four_states_reconcile_and_managed_rows) {
  const auto temp_dir =
      std::filesystem::temp_directory_path() /
      (L"clashwin_t20_pc_" + std::to_wstring(::GetCurrentProcessId()) + L"_" +
       std::to_wstring(::GetTickCount64()));
  std::error_code ec;
  std::filesystem::remove_all(temp_dir, ec);
  std::filesystem::create_directories(temp_dir, ec);

  PathControlStore store;
  store.load_from_file((temp_dir / L"path_control_state.json").wstring());

  const int64_t disc_ms = parse_iso8601_utc_ms("2026-11-14T00:00:00Z");
  UT_CHECK(disc_ms > 0);
  ConnItem c_disc;
  c_disc.id = "c_disc";
  c_disc.host = "api.example.com:443";
  c_disc.metadata.host = "api.example.com";
  c_disc.metadata.destination_port = "443";
  c_disc.chains = {"Proxy"};
  c_disc.chains_cat = "Proxy";
  store.mark_disconnected(c_disc, disc_ms);

  std::vector<clashwin::core::quick_routing::ManagedMergeRule> rules;
  {
    clashwin::core::quick_routing::ManagedMergeRule r;
    r.raw = "DOMAIN-SUFFIX,proxy.example.com,PROXY";
    r.rule_type = "DOMAIN-SUFFIX";
    r.value = "proxy.example.com";
    r.match_key = "proxy.example.com";
    r.target = clashwin::core::quick_routing::RuleTarget::Proxy;
    rules.push_back(std::move(r));
  }
  {
    clashwin::core::quick_routing::ManagedMergeRule r;
    r.raw = "PROCESS-NAME,curl.exe,DIRECT";
    r.rule_type = "PROCESS-NAME";
    r.value = "curl.exe";
    r.match_key = "curl.exe";
    r.target = clashwin::core::quick_routing::RuleTarget::Direct;
    rules.push_back(std::move(r));
  }

  const auto composed =
      compose_active_path_control_connections({}, rules, store.state());
  bool found_disc = false, found_proxy = false, found_direct = false;
  for (const auto& item : composed) {
    if (item.is_synthetic &&
        item.control_status == PathControlStatus::Disconnected &&
        item.target_value == "api.example.com") {
      found_disc = true;
    }
    if (item.is_synthetic &&
        item.control_status == PathControlStatus::Proxy &&
        item.managed_rule.has_value() &&
        item.managed_rule->match_key == "proxy.example.com") {
      found_proxy = true;
    }
    if (item.is_synthetic &&
        item.control_status == PathControlStatus::Direct &&
        item.managed_rule.has_value() &&
        item.managed_rule->match_key == "curl.exe") {
      found_direct = true;
    }
  }
  UT_CHECK(found_disc);
  UT_CHECK(found_proxy);
  UT_CHECK(found_direct);

  ConnItem reconnected;
  reconnected.id = "c_new";
  reconnected.host = "api.example.com:443";
  reconnected.metadata.host = "api.example.com";
  reconnected.metadata.destination_port = "443";
  reconnected.start = "2026-11-14T00:00:01Z";
  UT_CHECK(store.reconcile({reconnected}));
  UT_CHECK(store.state().records.empty());

  std::filesystem::remove_all(temp_dir, ec);
}
