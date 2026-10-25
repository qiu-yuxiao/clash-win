// T05 单测：MihomoClient + WsManager 对 mock mihomo 内核全链路
// （mock 响应样例取自真实内核 v1.19.27 与 models.rs；WS 载荷按 04§2 协议）
#include "test_util.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "clashwin/mihomo/models.h"
#include "mihomo/mihomo_client.h"
#include "mihomo/ws_manager.h"
#include "mock_mihomo_server.h"

using namespace clashwin;
using namespace clashwin::mihomo;

namespace {

std::wstring mock_pipe() {
  static std::atomic<int> seq{0};
  wchar_t buf[128];
  swprintf_s(buf, L"\\\\.\\pipe\\clashwin-t05mock-%d", seq.fetch_add(1));
  return buf;
}

}  // namespace

// ---------- REST 全端点（含请求构造/鉴权/错误分类） ----------
UT_TEST(mihomo_mock_rest_all) {
  auto pipe = mock_pipe();
  mockmihomo::MockMihomo srv(pipe);
  srv.start([](HANDLE, const std::string&) {});

  MihomoClient c(pipe, mockmihomo::kSecret);

  auto ver = c.get_version();
  UT_CHECK(ver.is_ok());
  UT_CHECK_EQ(ver.value().version, std::string("v1.19.27-mock"));
  UT_CHECK_EQ(ver.value().meta, true);

  auto cfg = c.get_base_config();
  UT_CHECK(cfg.is_ok());
  UT_CHECK_EQ(cfg.value().mixed_port, 20801);
  UT_CHECK_EQ(cfg.value().mode, std::string("rule"));
  UT_CHECK_EQ(cfg.value().log_level, std::string("info"));

  // PATCH 校验：mock 校验 body 含 log-level；请求体原样送达
  UT_CHECK(c.patch_base_config(BaseConfig::patch_of({{"log-level", "warning"}})).is_ok());
  UT_CHECK(srv.state.last().find("PATCH /configs") != std::string::npos);
  UT_CHECK(srv.state.last().find("\"log-level\":\"warning\"") != std::string::npos);
  auto bad = c.patch_base_config(BaseConfig::patch_of({{"other", 1}}));
  UT_CHECK(!bad.is_ok());
  UT_CHECK_EQ(bad.error().code, static_cast<int>(MihomoErrc::HttpError));

  UT_CHECK(c.reload_config(true, "C:\\x\\config.yaml").is_ok());
  UT_CHECK(srv.state.last().rfind("PUT /configs?force=true", 0) == 0);
  UT_CHECK(srv.state.last().find("\"path\":\"C:\\\\x\\\\config.yaml\"") != std::string::npos);

  auto proxies = c.get_proxies();
  UT_CHECK(proxies.is_ok());
  UT_CHECK_EQ(proxies.value().proxies.size(), size_t(2));
  UT_CHECK(proxies.value().proxies.at("PROXY").is_group());

  auto direct = c.get_proxy_by_name("DIRECT");
  UT_CHECK(direct.is_ok());
  UT_CHECK_EQ(direct.value().type, std::string("Direct"));

  UT_CHECK(c.select_node_for_group("PROXY", "DIRECT").is_ok());
  UT_CHECK(srv.state.last().rfind("PUT /proxies/PROXY", 0) == 0);
  UT_CHECK(srv.state.last().find("\"name\":\"DIRECT\"") != std::string::npos);
  UT_CHECK(c.unfixed_proxy("PROXY").is_ok());
  UT_CHECK(srv.state.last().rfind("DELETE /proxies/PROXY", 0) == 0);

  auto delay = c.delay_proxy_by_name("DIRECT", "http://t", 2000);
  UT_CHECK(delay.is_ok());
  UT_CHECK_EQ(delay.value().delay, 123u);

  auto groups = c.get_groups();
  UT_CHECK(groups.is_ok() && groups.value().proxies.size() == 1);
  auto grp = c.get_group_by_name("PROXY");
  UT_CHECK(grp.is_ok() && grp.value().now.value_or("") == "DIRECT");
  auto gdelay = c.delay_group("PROXY", "http://t", 2000);
  UT_CHECK(gdelay.is_ok());
  UT_CHECK_EQ(gdelay.value().at("DIRECT"), 123u);

  auto provs = c.get_proxy_providers();
  UT_CHECK(provs.is_ok() && provs.value().providers.count("p1") == 1);
  auto prov = c.get_proxy_provider_by_name("p1");
  UT_CHECK(prov.is_ok() && prov.value().name == "p1");
  UT_CHECK(c.update_proxy_provider("p1").is_ok());
  UT_CHECK(c.healthcheck_proxy_provider("p1").is_ok());
  auto hc = c.healthcheck_node_in_provider("p1", "n1", "http://t", 2000);
  UT_CHECK(hc.is_ok() && hc.value().delay == 88);

  auto rules = c.get_rules();
  UT_CHECK(rules.is_ok() && rules.value().rules.size() == 1);
  auto rprov = c.get_rule_providers();
  UT_CHECK(rprov.is_ok() && rprov.value().providers.at("r1").rule_count == 10);
  UT_CHECK(c.update_rule_provider("r1").is_ok());

  auto conns = c.get_connections();
  UT_CHECK(conns.is_ok() && conns.value().connections.size() == 1);
  UT_CHECK_EQ(conns.value().memory, 100u);
  UT_CHECK(c.close_connection("c1").is_ok());
  UT_CHECK(c.close_all_connections().is_ok());

  auto dns = c.get_dns_query("www.baidu.com");
  UT_CHECK(dns.is_ok());
  UT_CHECK_EQ(dns.value()["Status"], 0);

  // 生命周期/geo 端点
  UT_CHECK(c.restart().is_ok());
  UT_CHECK(c.update_geo().is_ok());
  UT_CHECK(c.upgrade_geo().is_ok());
  UT_CHECK(c.upgrade_core("release", false).is_ok());
  UT_CHECK(c.upgrade_ui().is_ok());
  UT_CHECK(c.flush_fakeip().is_ok());
  UT_CHECK(c.flush_dns().is_ok());

  // b. Bearer 鉴权（mock 校验 secret；TCP 模式内核行为）
  MihomoClient wrong(pipe, "wrong-secret");
  auto w = wrong.get_version();
  UT_CHECK(!w.is_ok());
  UT_CHECK_EQ(w.error().code, static_cast<int>(MihomoErrc::AuthDenied));
  MihomoClient none(pipe, "");
  auto n = none.get_version();
  UT_CHECK(!n.is_ok());
  UT_CHECK_EQ(n.error().code, static_cast<int>(MihomoErrc::AuthDenied));
  // 正确 secret 成功（分类不误伤）
  UT_CHECK(c.get_version().is_ok());

  // 404 → HttpError 分类
  auto miss = c.get_rules();  // mock 404 路径：改用 update_controller 指向不存在管道不在此测
  (void)miss;

  srv.stop();
}

// ---------- WsManager 四通道（snapshot/delta/traffic/logs/memory 协议） ----------
namespace {
struct Sink {
  std::mutex mu;
  std::vector<std::string> msgs;
  std::condition_variable cv;
  void push(const std::string& s) {
    std::lock_guard lk(mu);
    msgs.push_back(s);
    cv.notify_all();
  }
  bool wait(size_t n, int ms) {
    std::unique_lock lk(mu);
    return cv.wait_for(lk, std::chrono::milliseconds(ms), [&] { return msgs.size() >= n; });
  }
};
}  // namespace

UT_TEST(mihomo_mock_ws_channels) {
  auto pipe = mock_pipe();
  mockmihomo::MockMihomo srv(pipe);
  // WS 推送脚本：按通道按 04§2 协议推帧
  srv.start([](HANDLE h, const std::string& path) {
    if (path.rfind("/traffic", 0) == 0) {
      for (int i = 0; i < 30; ++i) {
        std::string payload = i == 5 ? R"({"up":10,"down":20,"upTotal":100,"downTotal":200})"
                                     : R"({"up":1,"down":2})";
        std::string f = mockmihomo::ws_enc_frame(0x1, payload);
        if (!mo_write(h, f.data(), static_cast<DWORD>(f.size()), 10000)) return;
        Sleep(100);
      }
    } else if (path.rfind("/memory", 0) == 0) {
      std::string f = mockmihomo::ws_enc_frame(0x1, R"({"inuse":1048576,"oslimit":0})");
      mo_write(h, f.data(), static_cast<DWORD>(f.size()), 10000);
      // 保活读：drain 客户端帧
      mockmihomo::WsFrame fr;
      while (mockmihomo::ws_read_frame(h, &fr, 2000)) {
        if (fr.opcode == 0x8) return;
      }
    } else if (path.rfind("/connections", 0) == 0) {
      // 真实内核发的是裸 Connections 全量帧（snapshot/delta 信封由 WsManager
      // 转换器生成，← mihomo.rs ws_connections）：帧A 全量(c1)，帧B 差分(c1→c2)
      const char* frame_a =
          R"({"downloadTotal":20,"uploadTotal":10,"memory":100,)"
          R"("connections":[{"id":"c1","metadata":{"network":"tcp","type":"HTTP","host":"a.com"},)"
          R"("upload":1,"download":2,"start":"2026-05-25T00:00:00Z","chains":["PROXY"],)"
          R"("rule":"MATCH","rulePayload":""}]})";
      const char* frame_b =
          R"({"downloadTotal":40,"uploadTotal":30,"memory":100,)"
          R"("connections":[{"id":"c2","metadata":{"network":"udp","type":"Socks5","host":"b.com"},)"
          R"("upload":5,"download":6,"start":"2026-05-25T00:00:01Z","chains":[],"rule":"",)"
          R"("rulePayload":""}]})";
      std::string f1 = mockmihomo::ws_enc_frame(0x1, frame_a);
      std::string f2 = mockmihomo::ws_enc_frame(0x1, frame_b);
      mo_write(h, f1.data(), static_cast<DWORD>(f1.size()), 10000);
      Sleep(150);
      mo_write(h, f2.data(), static_cast<DWORD>(f2.size()), 10000);
      Sleep(300);
      // 继续推全量帧（重连验证时新连接也能收到）
      mo_write(h, f1.data(), static_cast<DWORD>(f1.size()), 10000);
      mockmihomo::WsFrame fr;
      while (mockmihomo::ws_read_frame(h, &fr, 2000)) {
        if (fr.opcode == 0x8) return;
      }
    } else if (path.rfind("/logs?level=", 0) == 0) {
      std::string f1 = mockmihomo::ws_enc_frame(0x1, R"({"type":"info","payload":"Start initial"})");
      mo_write(h, f1.data(), static_cast<DWORD>(f1.size()), 10000);
      mockmihomo::WsFrame fr;
      while (mockmihomo::ws_read_frame(h, &fr, 2000)) {
        if (fr.opcode == 0x8) return;
      }
    }
  });

  auto ws = std::make_unique<WsManager>();
  ws->configure(pipe, mockmihomo::kSecret);

  // traffic：≥2 条且第 6 条含 upTotal（← 04§2 {up,down,upTotal?,downTotal?}）
  Sink traffic;
  ws->subscribe_traffic([&](const std::string& p) { traffic.push(p); });
  bool got_traffic = traffic.wait(2, 10000);
  UT_CHECK(got_traffic);
  if (!got_traffic) return;
  {
    std::lock_guard lk(traffic.mu);
    auto j = nlohmann::json::parse(traffic.msgs[0], nullptr, false);
    UT_CHECK(!j.is_discarded() && j.is_object());
    TrafficMsg t = TrafficMsg::from_json(j);
    UT_CHECK_EQ(t.up, 1u);
    UT_CHECK_EQ(t.down, 2u);
  }
  // 等 upTotal 帧
  bool saw_total = false;
  for (int i = 0; i < 50 && !saw_total; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::lock_guard lk(traffic.mu);
    for (auto& m : traffic.msgs) {
      auto j = nlohmann::json::parse(m, nullptr, false);
      if (!j.is_discarded() && j.contains("upTotal")) saw_total = true;
    }
  }
  UT_CHECK(saw_total);

  // connections：裸流经 WsManager 转换器 → snapshot(seq0) → delta(seq1，
  // added=c2 / removed=c1；updated 仅差分 upload/download，本序列无变化)
  Sink conn;
  ws->subscribe_connections([&](const std::string& p) { conn.push(p); });
  bool got_conn = conn.wait(2, 10000);
  UT_CHECK(got_conn);
  if (!got_conn) return;
  {
    std::lock_guard lk(conn.mu);
    auto m1 = ConnectionMessage::from_json(nlohmann::json::parse(conn.msgs[0], nullptr, false));
    auto m2 = ConnectionMessage::from_json(nlohmann::json::parse(conn.msgs[1], nullptr, false));
    UT_CHECK_EQ(m1.type, std::string("snapshot"));
    UT_CHECK(m1.snapshot.has_value());
    UT_CHECK_EQ(m1.snapshot->sequence_id, 0u);
    UT_CHECK_EQ(m1.snapshot->connections.size(), size_t(1));
    UT_CHECK_EQ(m1.snapshot->connections[0].id, std::string("c1"));
    UT_CHECK_EQ(m1.snapshot->upload_total, 10u);
    UT_CHECK_EQ(m1.snapshot->download_total, 20u);
    UT_CHECK_EQ(m2.type, std::string("delta"));
    UT_CHECK(m2.delta.has_value());
    UT_CHECK_EQ(m2.delta->sequence_id, 1u);
    UT_CHECK_EQ(m2.delta->added.size(), size_t(1));
    UT_CHECK_EQ(m2.delta->added[0].id, std::string("c2"));
    UT_CHECK_EQ(m2.delta->removed, std::vector<std::string>{"c1"});
    UT_CHECK_EQ(m2.delta->upload_total, 30u);
    UT_CHECK_EQ(m2.delta->download_total, 40u);
    UT_CHECK(m2.delta->updated.empty());
  }

  // logs（level=info 请求路径由 WsManager 构造）
  Sink logs;
  ws->subscribe_logs(LogLevel::Info, [&](const std::string& p) { logs.push(p); });
  bool got_logs = logs.wait(1, 10000);
  UT_CHECK(got_logs);
  if (got_logs) {
    std::lock_guard lk(logs.mu);
    if (!logs.msgs.empty()) {
      LogItem l = LogItem::from_json(nlohmann::json::parse(logs.msgs[0], nullptr, false));
      UT_CHECK_EQ(l.type, std::string("info"));
    }
  }

  // memory：连通即可（前端无 consumer）
  Sink mem;
  ws->subscribe_memory([&](const std::string& p) { mem.push(p); });
  bool got_mem = mem.wait(1, 10000);
  UT_CHECK(got_mem);
  if (got_mem) {
    std::lock_guard lk(mem.mu);
    if (!mem.msgs.empty()) {
      MemoryMsg m = MemoryMsg::from_json(nlohmann::json::parse(mem.msgs[0], nullptr, false));
      UT_CHECK_EQ(m.inuse, 1048576u);
    }
  }

  // disconnect_all 后自动重连（1s 间隔）并恢复推送
  ws->disconnect_all();
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  int before = 0;
  {
    std::lock_guard lk(traffic.mu);
    before = static_cast<int>(traffic.msgs.size());
  }
  bool recovered = false;
  for (int i = 0; i < 40 && !recovered; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::lock_guard lk(traffic.mu);
    if (static_cast<int>(traffic.msgs.size()) > before + 1) recovered = true;
  }
  UT_CHECK(recovered);

  ws.reset();  // 先停 WsManager worker 再关 mock（避免析构竞态）
  srv.stop();
}

// ---------- 引用计数共享 / 退订关闭 / 重订 ----------
UT_TEST(mihomo_mock_ws_shared) {
  auto pipe = mock_pipe();
  mockmihomo::MockMihomo srv(pipe);
  srv.start([](HANDLE h, const std::string& path) {
    if (path.rfind("/traffic", 0) == 0) {
      for (int i = 0; i < 60; ++i) {
        std::string f = mockmihomo::ws_enc_frame(0x1, R"({"up":1,"down":2})");
        if (!mo_write(h, f.data(), static_cast<DWORD>(f.size()), 10000)) return;
        Sleep(100);
      }
    }
  });

  auto ws = std::make_unique<WsManager>();
  ws->configure(pipe, mockmihomo::kSecret);

  Sink a, b;
  uint64_t id_a = ws->subscribe_traffic([&](const std::string& p) { a.push(p); });
  uint64_t id_b = ws->subscribe_traffic([&](const std::string& p) { b.push(p); });
  UT_CHECK(id_a != 0 && id_b != 0 && id_a != id_b);
  // 同通道多订阅者共享一条 WS：消息同时投递
  UT_CHECK(a.wait(3, 10000));
  UT_CHECK(b.wait(3, 10000));

  // 退订其一：另一订阅者继续收
  ws->unsubscribe(id_a);
  int before_b;
  {
    std::lock_guard lk(b.mu);
    before_b = static_cast<int>(b.msgs.size());
  }
  bool b_grew = false;
  for (int i = 0; i < 30 && !b_grew; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::lock_guard lk(b.mu);
    if (static_cast<int>(b.msgs.size()) > before_b) b_grew = true;
  }
  UT_CHECK(b_grew);

  // 全部退订：底层关闭；再订阅自动重连
  ws->unsubscribe(id_b);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  UT_CHECK(!ws->channel_connected(WsManager::kTraffic));
  Sink c;
  ws->subscribe_traffic([&](const std::string& p) { c.push(p); });
  UT_CHECK(c.wait(2, 10000));

  ws.reset();  // 先停 WsManager worker 再关 mock（避免析构竞态）
  srv.stop();
}

// ---------- 目标切换 update_controller / update_secret ----------
UT_TEST(mihomo_mock_update_controller) {
  auto pipe1 = mock_pipe();
  mockmihomo::MockMihomo srv1(pipe1);
  srv1.start([](HANDLE, const std::string&) {});

  MihomoClient c(pipe1, mockmihomo::kSecret);
  UT_CHECK(c.get_version().is_ok());

  // 切换 secret 后请求头变化（mock 校验新 secret）
  c.update_secret("new-secret");
  auto r1 = c.get_version();  // mock 校验旧 secret → 401
  UT_CHECK(!r1.is_ok());
  UT_CHECK_EQ(r1.error().code, static_cast<int>(MihomoErrc::AuthDenied));
  c.update_secret(mockmihomo::kSecret);
  UT_CHECK(c.get_version().is_ok());

  // 切换管道：旧 mock 停止、新 mock 起来后 update_controller 生效
  auto pipe2 = mock_pipe();
  mockmihomo::MockMihomo srv2(pipe2);
  srv2.start([](HANDLE, const std::string&) {});
  UT_CHECK(c.update_controller(pipe2).is_ok());
  UT_CHECK(c.get_version().is_ok());
  UT_CHECK_EQ(c.pipe_name(), pipe2);

  srv1.stop();
  srv2.stop();
}
