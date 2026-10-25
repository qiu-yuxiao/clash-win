// T05 mihomo 数据模型 JSON 解析（← models.rs serde 属性逐字段对照）
#include "clashwin/mihomo/models.h"

#include <cstdio>

namespace clashwin::mihomo {

using nlohmann::json;

namespace {

const json* find_key(const json& j, std::initializer_list<const char*> names) {
  for (const char* n : names) {
    auto it = j.find(n);
    if (it != j.end() && !it->is_null()) return &(*it);
  }
  return nullptr;
}

std::string get_str(const json& j, const char* key, const char* alt = nullptr) {
  if (auto* p = find_key(j, alt ? std::initializer_list<const char*>{key, alt}
                                : std::initializer_list<const char*>{key})) {
    if (p->is_string()) return p->get<std::string>();
  }
  return {};
}

int64_t get_i64(const json& j, const char* key, const char* alt = nullptr) {
  if (auto* p = find_key(j, alt ? std::initializer_list<const char*>{key, alt}
                                : std::initializer_list<const char*>{key})) {
    if (p->is_number_integer() || p->is_number_unsigned()) return p->get<int64_t>();
    if (p->is_boolean()) return p->get<bool>() ? 1 : 0;
  }
  return 0;
}

uint64_t get_u64(const json& j, const char* key, const char* alt = nullptr) {
  return static_cast<uint64_t>(get_i64(j, key, alt));
}

bool get_bool(const json& j, const char* key) {
  auto it = j.find(key);
  return it != j.end() && it->is_boolean() && it->get<bool>();
}

std::optional<std::string> get_opt_str(const json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null() || !it->is_string()) return std::nullopt;
  return it->get<std::string>();
}

std::optional<std::vector<std::string>> get_opt_strs(const json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null() || !it->is_array()) return std::nullopt;
  std::vector<std::string> out;
  for (const auto& e : *it) {
    if (e.is_string()) out.push_back(e.get<std::string>());
  }
  return out;
}

}  // namespace

// ---------- Version ----------
MihomoVersion MihomoVersion::from_json(const json& j) {
  MihomoVersion v;
  v.meta = get_bool(j, "meta");
  v.version = get_str(j, "version");
  return v;
}

// ---------- LogLevel ----------
const char* log_level_to_str(LogLevel l) {
  switch (l) {
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info";
    case LogLevel::Warning: return "warning";
    case LogLevel::Error: return "error";
    case LogLevel::Silent: return "silent";
  }
  return "info";
}

LogLevel log_level_from_str(const std::string& s) {
  if (s == "debug") return LogLevel::Debug;
  if (s == "warning") return LogLevel::Warning;
  if (s == "error") return LogLevel::Error;
  if (s == "silent") return LogLevel::Silent;
  return LogLevel::Info;
}

// ---------- BaseConfig ----------
BaseConfig BaseConfig::from_json(const json& j) {
  BaseConfig c;
  c.port = static_cast<uint16_t>(get_u64(j, "port"));
  c.socks_port = static_cast<uint16_t>(get_u64(j, "socks-port"));
  c.redir_port = static_cast<uint16_t>(get_u64(j, "redir-port"));
  c.tproxy_port = static_cast<uint16_t>(get_u64(j, "tproxy-port"));
  c.mixed_port = static_cast<uint16_t>(get_u64(j, "mixed-port"));
  c.allow_lan = get_bool(j, "allow-lan");
  c.bind_address = get_str(j, "bind-address");
  c.mode = get_str(j, "mode");
  c.unified_delay = get_bool(j, "unified-delay");
  c.log_level = get_str(j, "log-level");
  c.ipv6 = get_bool(j, "ipv6");
  c.interface_name = get_str(j, "interface-name");
  c.tcp_concurrent = get_bool(j, "tcp-concurrent");
  c.find_process_mode = get_str(j, "find-process-mode");
  c.sniffing = get_bool(j, "sniffing");
  c.etag_support = get_bool(j, "etag-support");
  if (auto it = j.find("tun"); it != j.end() && it->is_object()) c.tun = *it;
  c.raw = j;
  return c;
}

nlohmann::json BaseConfig::to_patch_json(const std::vector<std::string>& keys) const {
  json out = json::object();
  for (const auto& k : keys) {
    if (auto it = raw.find(k); it != raw.end()) out[k] = *it;
  }
  return out;
}

nlohmann::json BaseConfig::patch_of(
    std::initializer_list<std::pair<const char*, nlohmann::json>> kv) {
  json out = json::object();
  for (const auto& [k, v] : kv) out[k] = v;
  return out;
}

// ---------- Proxies ----------
DelayHistory DelayHistory::from_json(const json& j) {
  DelayHistory h;
  h.time = get_str(j, "time");
  h.delay = static_cast<uint16_t>(get_u64(j, "delay"));
  return h;
}

ProxyExtra ProxyExtra::from_json(const json& j) {
  ProxyExtra e;
  e.alive = get_bool(j, "alive");
  if (auto it = j.find("history"); it != j.end() && it->is_array()) {
    for (const auto& h : *it) e.history.push_back(DelayHistory::from_json(h));
  }
  return e;
}

ProxyItem ProxyItem::from_json(const json& j) {
  ProxyItem p;
  p.all = get_opt_strs(j, "all");
  p.expected_status = get_opt_str(j, "expected-status");
  p.fixed = get_opt_str(j, "fixed");
  if (auto it = j.find("hidden"); it != j.end() && it->is_boolean()) p.hidden = it->get<bool>();
  p.icon = get_opt_str(j, "icon");
  p.now = get_opt_str(j, "now");
  p.test_url = get_opt_str(j, "test-url");
  p.id = get_opt_str(j, "id");
  p.alive = get_bool(j, "alive");
  if (auto it = j.find("history"); it != j.end() && it->is_array()) {
    for (const auto& h : *it) p.history.push_back(DelayHistory::from_json(h));
  }
  if (auto it = j.find("extra"); it != j.end() && it->is_object()) {
    for (auto it2 = it->begin(); it2 != it->end(); ++it2) {
      p.extra[it2.key()] = ProxyExtra::from_json(it2.value());
    }
  }
  p.name = get_str(j, "name");
  p.udp = get_bool(j, "udp");
  p.uot = get_bool(j, "uot");
  p.type = get_str(j, "type");
  p.xudp = get_bool(j, "xudp");
  p.tfo = get_bool(j, "tfo");
  p.mptcp = get_bool(j, "mptcp");
  p.smux = get_bool(j, "smux");
  p.interface = get_str(j, "interface");
  p.dialer_proxy = get_str(j, "dialer-proxy");
  p.routing_mark = static_cast<int32_t>(get_i64(j, "routing-mark"));
  p.provider_name = get_str(j, "provider-name");
  return p;
}

Proxies Proxies::from_json(const json& j) {
  Proxies out;
  if (auto it = j.find("proxies"); it != j.end() && it->is_object()) {
    for (auto it2 = it->begin(); it2 != it->end(); ++it2) {
      out.proxies[it2.key()] = ProxyItem::from_json(it2.value());
    }
  }
  return out;
}

Groups Groups::from_json(const json& j) {
  Groups g;
  if (auto it = j.find("proxies"); it != j.end() && it->is_array()) {
    for (const auto& e : *it) g.proxies.push_back(ProxyItem::from_json(e));
  }
  return g;
}

ProxyDelay ProxyDelay::from_json(const json& j) {
  ProxyDelay d;
  d.delay = static_cast<uint32_t>(get_u64(j, "delay"));
  return d;
}

// ---------- Providers ----------
SubscriptionInfo SubscriptionInfo::from_json(const json& j) {
  SubscriptionInfo s;
  s.upload = get_i64(j, "Upload");
  s.download = get_i64(j, "Download");
  s.total = get_i64(j, "Total");
  s.expire = get_i64(j, "Expire");
  return s;
}

ProxyProvider ProxyProvider::from_json(const std::string& name, const json& j) {
  ProxyProvider p;
  p.name = name;
  p.type = get_str(j, "type");
  p.vehicle_type = get_str(j, "vehicleType");
  if (auto it = j.find("proxies"); it != j.end() && it->is_array()) {
    for (const auto& e : *it) p.proxies.push_back(ProxyItem::from_json(e));
  }
  p.test_url = get_str(j, "testUrl");
  p.expected_status = get_str(j, "expectedStatus");
  p.updated_at = get_opt_str(j, "updatedAt");
  if (auto it = j.find("subscriptionInfo"); it != j.end() && it->is_object()) {
    p.subscription_info = SubscriptionInfo::from_json(*it);
  }
  return p;
}

ProxyProviders ProxyProviders::from_json(const json& j) {
  ProxyProviders out;
  if (auto it = j.find("providers"); it != j.end() && it->is_object()) {
    for (auto it2 = it->begin(); it2 != it->end(); ++it2) {
      out.providers[it2.key()] = ProxyProvider::from_json(it2.key(), it2.value());
    }
  }
  return out;
}

Rule Rule::from_json(const json& j) {
  Rule r;
  r.type = get_str(j, "type");
  r.index = static_cast<int32_t>(get_i64(j, "index"));
  r.payload = get_str(j, "payload");
  r.proxy = get_str(j, "proxy");
  r.size = static_cast<int32_t>(get_i64(j, "size"));
  return r;
}

Rules Rules::from_json(const json& j) {
  Rules out;
  if (auto it = j.find("rules"); it != j.end() && it->is_array()) {
    for (const auto& e : *it) out.rules.push_back(Rule::from_json(e));
  }
  return out;
}

RuleProvider RuleProvider::from_json(const std::string& name, const json& j) {
  RuleProvider p;
  p.behavior = get_str(j, "behavior");
  p.format = get_str(j, "format");
  p.name = name;
  p.rule_count = static_cast<uint32_t>(get_u64(j, "ruleCount"));
  p.type = get_str(j, "type");
  p.updated_at = get_str(j, "updatedAt");
  p.vehicle_type = get_str(j, "vehicleType");
  return p;
}

RuleProviders RuleProviders::from_json(const json& j) {
  RuleProviders out;
  if (auto it = j.find("providers"); it != j.end() && it->is_object()) {
    for (auto it2 = it->begin(); it2 != it->end(); ++it2) {
      out.providers[it2.key()] = RuleProvider::from_json(it2.key(), it2.value());
    }
  }
  return out;
}

// ---------- Connections ----------
ConnectionMetaData ConnectionMetaData::from_json(const json& j) {
  ConnectionMetaData m;
  m.network = get_str(j, "network");
  m.type = get_str(j, "type");
  m.source_ip = get_str(j, "sourceIP");
  m.destination_ip = get_str(j, "destinationIP");
  m.source_geo_ip = get_opt_strs(j, "sourceGeoIP");
  m.destination_geo_ip = get_opt_strs(j, "destinationGeoIP");
  m.source_ip_asn = get_str(j, "sourceIPASN");
  m.destination_ip_asn = get_str(j, "destinationIPASN");
  m.source_port = get_str(j, "sourcePort");
  m.destination_port = get_str(j, "destinationPort");
  m.inbound_ip = get_str(j, "inboundIP");
  m.inbound_port = get_str(j, "inboundPort");
  m.inbound_name = get_str(j, "inboundName");
  m.inbound_user = get_str(j, "inboundUser");
  m.host = get_str(j, "host");
  m.dns_mode = get_str(j, "dnsMode");
  m.uid = static_cast<uint32_t>(get_u64(j, "uid"));
  m.process = get_str(j, "process");
  m.process_path = get_str(j, "processPath");
  m.special_proxy = get_str(j, "specialProxy");
  m.special_rules = get_str(j, "specialRules");
  m.remote_destination = get_str(j, "remoteDestination");
  m.dscp = static_cast<uint8_t>(get_u64(j, "dscp"));
  m.sniff_host = get_str(j, "sniffHost");
  return m;
}

ConnectionItem ConnectionItem::from_json(const json& j) {
  ConnectionItem c;
  c.id = get_str(j, "id");
  if (auto it = j.find("metadata"); it != j.end() && it->is_object()) {
    c.metadata = ConnectionMetaData::from_json(*it);
  }
  c.upload = get_u64(j, "upload");
  c.download = get_u64(j, "download");
  c.start = get_str(j, "start");
  if (auto it = j.find("chains"); it != j.end() && it->is_array()) {
    for (const auto& e : *it) {
      if (e.is_string()) c.chains.push_back(e.get<std::string>());
    }
  }
  c.provider_chains = get_opt_strs(j, "providerChains");
  c.rule = get_str(j, "rule");
  c.rule_payload = get_str(j, "rulePayload");
  return c;
}

Connections Connections::from_json(const json& j) {
  Connections c;
  c.download_total = get_u64(j, "downloadTotal");
  c.upload_total = get_u64(j, "uploadTotal");
  if (auto it = j.find("connections"); it != j.end() && it->is_array()) {
    for (const auto& e : *it) c.connections.push_back(ConnectionItem::from_json(e));
  }
  c.memory = get_u64(j, "memory");
  return c;
}

// ---------- snapshot / delta（04§2 原样） ----------
ConnectionsSnapshot ConnectionsSnapshot::from_json(const json& j) {
  ConnectionsSnapshot s;
  s.epoch_id = get_str(j, "epochId");
  s.sequence_id = get_u64(j, "sequenceId");
  s.upload_total = get_u64(j, "uploadTotal");
  s.download_total = get_u64(j, "downloadTotal");
  if (auto it = j.find("connections"); it != j.end() && it->is_array()) {
    for (const auto& e : *it) s.connections.push_back(ConnectionItem::from_json(e));
  }
  s.memory = get_u64(j, "memory");
  return s;
}

ConnectionsDelta ConnectionsDelta::from_json(const json& j) {
  ConnectionsDelta d;
  d.epoch_id = get_str(j, "epochId");
  d.sequence_id = get_u64(j, "sequenceId");
  d.upload_total = get_u64(j, "uploadTotal");
  d.download_total = get_u64(j, "downloadTotal");
  d.memory = get_u64(j, "memory");
  if (auto it = j.find("added"); it != j.end() && it->is_array()) {
    for (const auto& e : *it) d.added.push_back(ConnectionItem::from_json(e));
  }
  if (auto it = j.find("removed"); it != j.end() && it->is_array()) {
    for (const auto& e : *it) {
      if (e.is_string()) d.removed.push_back(e.get<std::string>());
    }
  }
  // 扁平数组步长3：[id1, up1, down1, id2, up2, down2, ...]
  if (auto it = j.find("updated"); it != j.end() && it->is_array()) {
    const auto& arr = *it;
    for (size_t i = 0; i + 2 < arr.size(); i += 3) {
      if (!arr[i].is_string()) continue;
      d.updated.emplace_back(arr[i].get<std::string>(),
                             static_cast<int64_t>(arr[i + 1].is_number() ? arr[i + 1].get<int64_t>() : 0),
                             static_cast<int64_t>(arr[i + 2].is_number() ? arr[i + 2].get<int64_t>() : 0));
    }
  }
  return d;
}

ConnectionMessage ConnectionMessage::from_json(const json& j) {
  ConnectionMessage m;
  m.type = get_str(j, "type");
  auto data = j.find("data");
  if (data == j.end() || !data->is_object()) return m;
  if (m.type == "snapshot") {
    m.snapshot = ConnectionsSnapshot::from_json(*data);
  } else if (m.type == "delta") {
    m.delta = ConnectionsDelta::from_json(*data);
  }
  return m;
}

// ---------- traffic / memory / logs ----------
TrafficMsg TrafficMsg::from_json(const json& j) {
  TrafficMsg t;
  t.up = get_u64(j, "up");
  t.down = get_u64(j, "down");
  if (auto it = j.find("upTotal"); it != j.end() && it->is_number()) {
    t.up_total = it->get<uint64_t>();
  }
  if (auto it = j.find("downTotal"); it != j.end() && it->is_number()) {
    t.down_total = it->get<uint64_t>();
  }
  return t;
}

MemoryMsg MemoryMsg::from_json(const json& j) {
  MemoryMsg m;
  m.inuse = get_u64(j, "inuse");
  m.oslimit = get_u64(j, "oslimit");
  return m;
}

LogItem LogItem::from_json(const json& j) {
  LogItem l;
  l.type = get_str(j, "type");
  l.payload = get_str(j, "payload");
  return l;
}

}  // namespace clashwin::mihomo
