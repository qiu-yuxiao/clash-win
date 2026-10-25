// T05 模型解析测试：样例 JSON ← crates/tauri-plugin-mihomo/src/models.rs 序列化形态
// 与 mihomo.rs 测试样例（sample_connections_payload）、use-connection-data.ts 载荷结构
#include "test_util.h"

#include "clashwin/mihomo/models.h"

#include <nlohmann/json.hpp>

using namespace clashwin::mihomo;

UT_TEST(mihomo_parse_version) {
  auto r = parse_model<MihomoVersion>(R"({"meta":true,"version":"v1.19.8"})");
  UT_CHECK(r.is_ok());
  UT_CHECK_EQ(r.value().meta, true);
  UT_CHECK_EQ(r.value().version, std::string("v1.19.8"));
  // 缺 meta（serde default）
  auto r2 = parse_model<MihomoVersion>(R"({"version":"x"})");
  UT_CHECK(r2.is_ok());
  UT_CHECK_EQ(r2.value().meta, false);
  // 非法 JSON
  auto r3 = parse_model<MihomoVersion>("{bad");
  UT_CHECK(!r3.is_ok());
  UT_CHECK_EQ(r3.error().code, 4001);
}

UT_TEST(mihomo_parse_log_level) {
  UT_CHECK_EQ(std::string(log_level_to_str(LogLevel::Debug)), "debug");
  UT_CHECK_EQ(std::string(log_level_to_str(LogLevel::Silent)), "silent");
  UT_CHECK(log_level_from_str("warning") == LogLevel::Warning);
  UT_CHECK(log_level_from_str("silent") == LogLevel::Silent);
  UT_CHECK(log_level_from_str("unknown") == LogLevel::Info);  // 兜底
}

UT_TEST(mihomo_parse_base_config) {
  // GET /configs 响应为 kebab-case（← models.rs BaseConfig deserialize = kebab-case）
  const char* j = R"({
    "port": 0, "socks-port": 0, "redir-port": 0, "tproxy-port": 0,
    "mixed-port": 20801,
    "tun": {"enable": false, "device": "Mihomo", "stack": "Mixed", "dns-hijack": ["any:53"], "auto-route": true, "auto-detect-interface": true, "file-descriptor": 0},
    "allow-lan": false, "bind-address": "*", "mode": "rule",
    "unified-delay": false, "log-level": "info", "ipv6": false,
    "interface-name": "", "routing-mark": 0,
    "geox-url": {"geoip": "", "mmdb": "", "asn": "", "geosite": ""},
    "geo-auto-update": false, "geo-update-interval": 24,
    "geodata-mode": false, "geodata-loader": "standard", "geosite-matcher": "mph",
    "tcp-concurrent": true, "find-process-mode": "off", "sniffing": false,
    "global-ua": "clash.meta", "etag-support": true,
    "keep-alive-interval": 30, "keep-alive-idle": 120, "disable-keep-alive": false
  })";
  auto r = parse_model<BaseConfig>(j);
  UT_CHECK(r.is_ok());
  const BaseConfig& c = r.value();
  UT_CHECK_EQ(c.mixed_port, 20801);
  UT_CHECK_EQ(c.mode, std::string("rule"));
  UT_CHECK_EQ(c.log_level, std::string("info"));
  UT_CHECK_EQ(c.tcp_concurrent, true);
  UT_CHECK_EQ(c.find_process_mode, std::string("off"));
  UT_CHECK_EQ(c.etag_support, true);
  UT_CHECK(c.tun.is_object());
  UT_CHECK_EQ(c.tun["stack"], std::string("Mixed"));
  UT_CHECK_EQ(c.raw["global-ua"], std::string("clash.meta"));  // 未知/其余字段透传
  // patch 体：按键提取
  BaseConfig cc = c;
  auto patch = cc.to_patch_json({"log-level", "mode", "nonexistent-key"});
  UT_CHECK_EQ(patch["log-level"], std::string("info"));
  UT_CHECK_EQ(patch["mode"], std::string("rule"));
  UT_CHECK(patch.find("nonexistent-key") == patch.end());
  auto p2 = BaseConfig::patch_of({{"log-level", "warning"}, {"mixed-port", 20802}});
  UT_CHECK_EQ(p2["log-level"], std::string("warning"));
  UT_CHECK_EQ(p2["mixed-port"], 20802);
}

UT_TEST(mihomo_parse_proxies) {
  const char* j = R"({"proxies":{
    "DIRECT":{"alive":true,"history":[],"extra":{},"name":"DIRECT","udp":true,"uot":true,
      "type":"Direct","xudp":true,"tfo":false,"mptcp":false,"smux":false,"interface":"eth0",
      "dialer-proxy":"","routing-mark":0,"provider-name":""},
    "PROXY":{"alive":true,"all":["A","B"],"history":[{"time":"2026-05-25T00:00:00Z","delay":123}],
      "extra":{"http://u":{"alive":true,"history":[{"time":"2026-05-25T00:00:00Z","delay":456}]}},
      "name":"PROXY","now":"A","udp":true,"type":"Selector","xudp":false,"tfo":false,
      "mptcp":false,"smux":false,"interface":"","dialer-proxy":"","routing-mark":0,
      "provider-name":"","hidden":false,"icon":"https://x/y.png","expected-status":"200",
      "fixed":"","test-url":"http://u","id":"grp-1"}}})";
  auto r = parse_model<Proxies>(j);
  UT_CHECK(r.is_ok());
  UT_CHECK_EQ(r.value().proxies.size(), size_t(2));
  const ProxyItem& direct = r.value().proxies.at("DIRECT");
  UT_CHECK_EQ(direct.name, std::string("DIRECT"));
  UT_CHECK_EQ(direct.type, std::string("Direct"));
  UT_CHECK_EQ(direct.udp, true);
  UT_CHECK(!direct.is_group());
  const ProxyItem& grp = r.value().proxies.at("PROXY");
  UT_CHECK(grp.is_group());
  UT_CHECK(grp.all.has_value() && grp.all->size() == 2 && (*grp.all)[1] == "B");
  UT_CHECK(grp.now.has_value() && *grp.now == "A");
  UT_CHECK(grp.fixed.has_value() && grp.fixed->empty());  // 空串=Some("")
  UT_CHECK(grp.hidden.has_value() && !*grp.hidden);
  UT_CHECK_EQ(grp.icon.value_or(""), std::string("https://x/y.png"));
  UT_CHECK_EQ(grp.history.size(), size_t(1));
  UT_CHECK_EQ(grp.history[0].delay, 123);
  UT_CHECK_EQ(grp.extra.size(), size_t(1));
  UT_CHECK_EQ(grp.extra.at("http://u").history.at(0).delay, 456);
  UT_CHECK(grp.id.has_value() && *grp.id == "grp-1");
}

UT_TEST(mihomo_parse_groups) {
  const char* j =
      R"({"proxies":[{"name":"PROXY","type":"Selector","all":["A"],"now":"A","alive":true,
        "history":[],"extra":{},"udp":true,"uot":false,"xudp":false,"tfo":false,"mptcp":false,
        "smux":false,"interface":"","dialer-proxy":"","routing-mark":0,"provider-name":""}]})";
  auto r = parse_model<Groups>(j);
  UT_CHECK(r.is_ok());
  UT_CHECK_EQ(r.value().proxies.size(), size_t(1));
  UT_CHECK_EQ(r.value().proxies[0].name, std::string("PROXY"));
}

UT_TEST(mihomo_parse_providers) {
  const char* jp = R"({"providers":{"p1":{"name":"p1","type":"Proxy","vehicleType":"HTTP",
    "proxies":[{"name":"n1","type":"Shadowsocks","alive":true,"history":[],"extra":{},"udp":true,
      "uot":false,"xudp":false,"tfo":false,"mptcp":false,"smux":false,"interface":"",
      "dialer-proxy":"","routing-mark":0,"provider-name":"p1"}],
    "testUrl":"http://t","expectedStatus":"200","updatedAt":"2026-05-25T00:00:00Z",
    "subscriptionInfo":{"Upload":1,"Download":2,"Total":3,"Expire":1750000000}}}})";
  auto r = parse_model<ProxyProviders>(jp);
  UT_CHECK(r.is_ok());
  UT_CHECK_EQ(r.value().providers.size(), size_t(1));
  const ProxyProvider& p = r.value().providers.at("p1");
  UT_CHECK_EQ(p.name, std::string("p1"));
  UT_CHECK_EQ(p.type, std::string("Proxy"));
  UT_CHECK_EQ(p.vehicle_type, std::string("HTTP"));
  UT_CHECK_EQ(p.proxies.size(), size_t(1));
  UT_CHECK_EQ(p.proxies[0].name, std::string("n1"));
  UT_CHECK(p.subscription_info.has_value());
  UT_CHECK_EQ(p.subscription_info->upload, int64_t(1));  // PascalCase 键
  UT_CHECK_EQ(p.subscription_info->expire, int64_t(1750000000));

  const char* jr = R"({"providers":{"r1":{"behavior":"Domain","format":"YamlRule","name":"r1",
    "ruleCount":10,"type":"Rule","updatedAt":"2026-05-25T00:00:00Z","vehicleType":"HTTP"}}})";
  auto r2 = parse_model<RuleProviders>(jr);
  UT_CHECK(r2.is_ok());
  const RuleProvider& rp = r2.value().providers.at("r1");
  UT_CHECK_EQ(rp.behavior, std::string("Domain"));
  UT_CHECK_EQ(rp.format, std::string("YamlRule"));
  UT_CHECK_EQ(rp.rule_count, 10u);
}

UT_TEST(mihomo_parse_rules) {
  const char* j = R"({"rules":[
    {"type":"DomainSuffix","index":0,"payload":"example.com","proxy":"PROXY","size":1},
    {"type":"RuleSet","index":1,"payload":"gfwlist","proxy":"PROXY","size":0},
    {"type":"Match","index":2,"payload":"","proxy":"DIRECT","size":0}]})";
  auto r = parse_model<Rules>(j);
  UT_CHECK(r.is_ok());
  UT_CHECK_EQ(r.value().rules.size(), size_t(3));
  UT_CHECK_EQ(r.value().rules[0].type, std::string("DomainSuffix"));
  UT_CHECK_EQ(r.value().rules[1].payload, std::string("gfwlist"));
  UT_CHECK_EQ(r.value().rules[2].proxy, std::string("DIRECT"));
  // 空规则
  auto r2 = parse_model<Rules>(R"({"rules":[]})");
  UT_CHECK(r2.is_ok() && r2.value().rules.empty());
}

UT_TEST(mihomo_parse_connections) {
  // ← mihomo.rs tests::sample_connections_payload 样例
  const char* j = R"({"downloadTotal":1,"uploadTotal":2,"connections":[
    {"id":"bench-id","metadata":{"network":"tcp","type":"HTTP","sourceIP":"198.18.0.1",
     "destinationIP":"93.184.216.34","host":"example.com","dnsMode":"normal",
     "processPath":"/Applications/Example.app","sourcePort":"12345","destinationPort":"443",
     "sourceIPASN":"","destinationIPASN":"","inboundIP":"","inboundPort":"","inboundName":"",
     "inboundUser":"","uid":0,"process":"","specialProxy":"","specialRules":"",
     "remoteDestination":"","dscp":0,"sniffHost":""},
     "chains":["Proxy","DIRECT"],"rule":"MATCH","rulePayload":"",
     "upload":123456,"download":654321,"start":"2026-05-25T00:00:00Z"}],
    "memory":987654})";
  auto r = parse_model<Connections>(j);
  UT_CHECK(r.is_ok());
  UT_CHECK_EQ(r.value().upload_total, 2u);
  UT_CHECK_EQ(r.value().download_total, 1u);
  UT_CHECK_EQ(r.value().memory, 987654u);
  UT_CHECK_EQ(r.value().connections.size(), size_t(1));
  const ConnectionItem& c = r.value().connections[0];
  UT_CHECK_EQ(c.id, std::string("bench-id"));
  UT_CHECK_EQ(c.upload, 123456u);
  UT_CHECK_EQ(c.download, 654321u);
  UT_CHECK_EQ(c.metadata.network, std::string("tcp"));
  UT_CHECK_EQ(c.metadata.type, std::string("HTTP"));
  UT_CHECK_EQ(c.metadata.source_ip, std::string("198.18.0.1"));       // ← sourceIP
  UT_CHECK_EQ(c.metadata.destination_ip, std::string("93.184.216.34"));
  UT_CHECK_EQ(c.metadata.host, std::string("example.com"));
  UT_CHECK_EQ(c.metadata.dns_mode, std::string("normal"));
  UT_CHECK_EQ(c.metadata.process_path, std::string("/Applications/Example.app"));
  UT_CHECK_EQ(c.metadata.source_port, std::string("12345"));
  UT_CHECK_EQ(c.chains.size(), size_t(2));
  UT_CHECK_EQ(c.chains[0], std::string("Proxy"));
  // connections 可为 null（原版 Option）
  auto r2 = parse_model<Connections>(R"({"downloadTotal":0,"uploadTotal":0,"connections":null})");
  UT_CHECK(r2.is_ok() && r2.value().connections.empty());
}

UT_TEST(mihomo_parse_snapshot) {
  // ← 04§2：snapshot = {type:"snapshot", data:{epochId, sequenceId, uploadTotal,
  //   downloadTotal, connections[], memory}}
  const char* j = R"({"type":"snapshot","data":{
    "epochId":"000000000000abcd","sequenceId":0,
    "uploadTotal":10,"downloadTotal":20,"memory":100,
    "connections":[{"id":"c1","metadata":{"network":"tcp","type":"HTTP","host":"a.com"},
      "upload":1,"download":2,"start":"2026-05-25T00:00:00Z",
      "chains":["PROXY"],"rule":"MATCH","rulePayload":""}]}})";
  auto r = parse_model<ConnectionMessage>(j);
  UT_CHECK(r.is_ok());
  UT_CHECK_EQ(r.value().type, std::string("snapshot"));
  UT_CHECK(r.value().snapshot.has_value());
  const ConnectionsSnapshot& s = *r.value().snapshot;
  UT_CHECK_EQ(s.epoch_id, std::string("000000000000abcd"));
  UT_CHECK_EQ(s.sequence_id, 0u);
  UT_CHECK_EQ(s.upload_total, 10u);
  UT_CHECK_EQ(s.download_total, 20u);
  UT_CHECK_EQ(s.connections.size(), size_t(1));
}

UT_TEST(mihomo_parse_delta) {
  // ← 04§2：delta.updated 扁平数组步长3 [id,up,down,...]
  const char* j = R"({"type":"delta","data":{
    "epochId":"000000000000abcd","sequenceId":1,
    "uploadTotal":30,"downloadTotal":40,"memory":100,
    "added":[{"id":"c2","metadata":{"network":"udp","type":"Socks5","host":"b.com"},
      "upload":5,"download":6,"start":"2026-05-25T00:00:01Z","chains":[],"rule":"","rulePayload":""}],
    "removed":["c9"],
    "updated":["c1",100,200,"c3",7,8]}})";
  auto r = parse_model<ConnectionMessage>(j);
  UT_CHECK(r.is_ok());
  UT_CHECK_EQ(r.value().type, std::string("delta"));
  UT_CHECK(r.value().delta.has_value());
  const ConnectionsDelta& d = *r.value().delta;
  UT_CHECK_EQ(d.epoch_id, std::string("000000000000abcd"));
  UT_CHECK_EQ(d.sequence_id, 1u);
  UT_CHECK_EQ(d.added.size(), size_t(1));
  UT_CHECK_EQ(d.added[0].metadata.type, std::string("Socks5"));
  UT_CHECK_EQ(d.removed.size(), size_t(1));
  UT_CHECK_EQ(d.removed[0], std::string("c9"));
  UT_CHECK_EQ(d.updated.size(), size_t(2));  // 步长3 → 2 条
  UT_CHECK(std::get<0>(d.updated[0]) == "c1" && std::get<1>(d.updated[0]) == 100 &&
           std::get<2>(d.updated[0]) == 200);
  UT_CHECK(std::get<0>(d.updated[1]) == "c3" && std::get<1>(d.updated[1]) == 7 &&
           std::get<2>(d.updated[1]) == 8);
  // 步长不齐尾部截断（updated 长度 4 → 1 条）
  const char* j2 = R"({"type":"delta","data":{"epochId":"e","sequenceId":2,
    "uploadTotal":0,"downloadTotal":0,"memory":0,"added":[],"removed":[],"updated":["x",1,2,"y"]}})";
  auto r2 = parse_model<ConnectionMessage>(j2);
  UT_CHECK(r2.is_ok());
  UT_CHECK_EQ(r2.value().delta->updated.size(), size_t(1));
  // 未知 type → 空
  auto r3 = parse_model<ConnectionMessage>(R"({"type":"other","data":{}})");
  UT_CHECK(r3.is_ok() && r3.value().type == "other" && !r3.value().snapshot.has_value() &&
           !r3.value().delta.has_value());
}

UT_TEST(mihomo_parse_traffic_memory_logs) {
  auto t = parse_model<TrafficMsg>(R"({"up":1,"down":2})");
  UT_CHECK(t.is_ok() && t.value().up == 1 && t.value().down == 2);
  UT_CHECK(!t.value().up_total.has_value());  // ← 04§2：upTotal? 可缺省
  auto t2 = parse_model<TrafficMsg>(R"({"up":1,"down":2,"upTotal":10,"downTotal":20})");
  UT_CHECK(t2.is_ok() && t2.value().up_total.value() == 10 &&
           t2.value().down_total.value() == 20);
  auto m = parse_model<MemoryMsg>(R"({"inuse":1048576,"oslimit":0})");
  UT_CHECK(m.is_ok() && m.value().inuse == 1048576 && m.value().oslimit == 0);
  auto l = parse_model<LogItem>(R"({"type":"info","payload":"Start initial configuration"})");
  UT_CHECK(l.is_ok() && l.value().type == "info" &&
           l.value().payload == "Start initial configuration");
  // logs 通道批量数组形态（← 04§2：ILogItem[] 或单条）
  auto arr = nlohmann::json::parse(R"([{"type":"info","payload":"a"},{"type":"error","payload":"b"}])");
  UT_CHECK(arr.is_array() && arr.size() == 2);
  auto l0 = LogItem::from_json(arr[0]);
  UT_CHECK_EQ(l0.payload, std::string("a"));
}

UT_TEST(mihomo_url_encode) {
  UT_CHECK_EQ(url_encode("PROXY"), "PROXY");
  UT_CHECK_EQ(url_encode("a b"), "a%20b");
  UT_CHECK_EQ(url_encode("中文"), "%E4%B8%AD%E6%96%87");
  UT_CHECK_EQ(url_encode("a+b&c=d"), "a%2Bb%26c%3Dd");
  UT_CHECK_EQ(url_encode("a~b-c_d.e"), "a~b-c_d.e");  // unreserved 不转义
}
