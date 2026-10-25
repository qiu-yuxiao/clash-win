// T05 mihomo REST 客户端实现（← mihomo.rs 逐方法对照）
#include "mihomo/mihomo_client.h"

#include <nlohmann/json.hpp>

namespace clashwin::mihomo {

using nlohmann::json;

namespace {

// ← urlencoding::encode：仅 A-Za-z0-9 -_.~ 不转义
std::string percent_encode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0xF]);
    }
  }
  return out;
}

// 非 2xx 响应体解析 {"message": ...}；解析失败用状态码占位
std::string extract_error_message(const HttpResponse& resp) {
  json j = json::parse(resp.body, nullptr, false);
  if (!j.is_discarded() && j.is_object()) {
    auto it = j.find("message");
    if (it != j.end() && it->is_string()) return it->get<std::string>();
  }
  return "http status " + std::to_string(resp.status);
}

Error http_error(const HttpResponse& resp) {
  const std::string msg = extract_error_message(resp);
  if (resp.status == 401 || resp.status == 403) {
    return {static_cast<int>(MihomoErrc::AuthDenied), msg};
  }
  if (resp.status >= 500) {
    return {static_cast<int>(MihomoErrc::ServerError), msg};
  }
  return {static_cast<int>(MihomoErrc::HttpError), msg};
}

// delay 端点的 req timeout：timeout+2s（单节点）/ max(timeout+13s,15s)（组）
int delay_req_timeout(uint32_t timeout_ms, int override_ms) {
  if (override_ms > 0) return override_ms;
  return static_cast<int>(timeout_ms) + 2000;
}

}  // namespace

std::string url_encode(const std::string& s) { return percent_encode(s); }

MihomoClient::MihomoClient(const std::wstring& pipe_name, const std::string& secret,
                           PipeHttpConfig cfg)
    : pipe_name_(pipe_name), secret_(secret), http_(std::move(cfg)) {
  http_.connect(pipe_name_);
}

Result<void> MihomoClient::update_controller(const std::wstring& pipe_name) {
  pipe_name_ = pipe_name;
  http_.clear_pool();
  return http_.connect(pipe_name);
}

void MihomoClient::update_secret(const std::string& secret) { secret_ = secret; }

Result<HttpResponse> MihomoClient::request(const char* method, const std::string& path,
                                           const std::string& body, int timeout_ms) {
  std::map<std::string, std::string> headers;
  headers["Host"] = "localhost";
  headers["Content-Type"] = "application/json";
  if (!secret_.empty()) headers["Authorization"] = "Bearer " + secret_;
  return http_.send(method, path, headers, body, timeout_ms);
}

template <typename T>
Result<T> MihomoClient::request_json(const char* method, const std::string& path,
                                     const std::string& body, int timeout_ms) {
  auto resp = request(method, path, body, timeout_ms);
  if (!resp.is_ok()) return Result<T>::err(resp.error());
  if (!resp.value().is_success()) return Result<T>::err(http_error(resp.value()));
  json j = json::parse(resp.value().body, nullptr, false);
  if (j.is_discarded()) {
    return Result<T>::err({static_cast<int>(MihomoErrc::JsonParse), "response body is not JSON"});
  }
  return Result<T>::ok(T::from_json(j));
}

// ---- 基础 ----
Result<MihomoVersion> MihomoClient::get_version(int timeout_ms) {
  return request_json<MihomoVersion>("GET", "/version", "", timeout_ms);
}

Result<BaseConfig> MihomoClient::get_base_config(int timeout_ms) {
  return request_json<BaseConfig>("GET", "/configs", "", timeout_ms);
}

Result<void> MihomoClient::patch_base_config(const nlohmann::json& patch, int timeout_ms) {
  auto resp = request("PATCH", "/configs", patch.dump(), timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

Result<void> MihomoClient::reload_config(bool force, const std::string& config_path,
                                         int timeout_ms) {
  std::string path = std::string("/configs?force=") + (force ? "true" : "false");
  json body = json::object({{"path", config_path}});
  auto resp = request("PUT", path, body.dump(), timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

Result<void> MihomoClient::update_geo(int timeout_ms) {
  auto resp = request("POST", "/configs/geo", "", timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

Result<void> MihomoClient::upgrade_geo(int timeout_ms) {
  auto resp = request("POST", "/upgrade/geo", "", timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

Result<void> MihomoClient::upgrade_core(const std::string& channel, bool force, int timeout_ms) {
  std::string path = "/upgrade?channel=" + percent_encode(channel) +
                     "&force=" + (force ? "true" : "false");
  auto resp = request("POST", path, "", timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

Result<void> MihomoClient::upgrade_ui(int timeout_ms) {
  auto resp = request("POST", "/upgrade/ui", "", timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

Result<void> MihomoClient::restart(int timeout_ms) {
  auto resp = request("POST", "/restart", "", timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

Result<void> MihomoClient::flush_fakeip(int timeout_ms) {
  auto resp = request("POST", "/cache/fakeip/flush", "", timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

Result<void> MihomoClient::flush_dns(int timeout_ms) {
  auto resp = request("POST", "/cache/dns/flush", "", timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

// ---- Proxies ----
Result<Proxies> MihomoClient::get_proxies(int timeout_ms) {
  return request_json<Proxies>("GET", "/proxies", "", timeout_ms);
}

Result<ProxyItem> MihomoClient::get_proxy_by_name(const std::string& name, int timeout_ms) {
  return request_json<ProxyItem>("GET", "/proxies/" + percent_encode(name), "", timeout_ms);
}

Result<void> MihomoClient::select_node_for_group(const std::string& group, const std::string& node,
                                                 int timeout_ms) {
  json body = json::object({{"name", node}});
  auto resp = request("PUT", "/proxies/" + percent_encode(group), body.dump(), timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

Result<void> MihomoClient::unfixed_proxy(const std::string& group, int timeout_ms) {
  auto resp = request("DELETE", "/proxies/" + percent_encode(group), "", timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

Result<ProxyDelay> MihomoClient::delay_proxy_by_name(const std::string& name,
                                                     const std::string& test_url,
                                                     uint32_t timeout_ms,
                                                     int timeout_override_ms) {
  std::string path = "/proxies/" + percent_encode(name) + "/delay?timeout=" +
                     std::to_string(timeout_ms) + "&url=" + percent_encode(test_url);
  auto resp = request("GET", path, "", delay_req_timeout(timeout_ms, timeout_override_ms));
  if (!resp.is_ok()) return Result<ProxyDelay>::err(resp.error());
  // ← BUG-171/172：HTTP 失败即节点测速失败（Timeout），返回 {delay:0} 而非 Err
  if (!resp.value().is_success()) return Result<ProxyDelay>::ok(ProxyDelay{});
  json j = json::parse(resp.value().body, nullptr, false);
  if (j.is_discarded()) {
    return Result<ProxyDelay>::err({static_cast<int>(MihomoErrc::JsonParse),
                                    "delay response is not JSON"});
  }
  return Result<ProxyDelay>::ok(ProxyDelay::from_json(j));
}

Result<Groups> MihomoClient::get_groups(int timeout_ms) {
  return request_json<Groups>("GET", "/group", "", timeout_ms);
}

Result<ProxyItem> MihomoClient::get_group_by_name(const std::string& name, int timeout_ms) {
  return request_json<ProxyItem>("GET", "/group/" + percent_encode(name), "", timeout_ms);
}

Result<std::map<std::string, uint32_t>> MihomoClient::delay_group(const std::string& group,
                                                                  const std::string& test_url,
                                                                  uint32_t timeout_ms,
                                                                  int timeout_override_ms) {
  std::string path = "/group/" + percent_encode(group) + "/delay?url=" +
                     percent_encode(test_url) + "&timeout=" + std::to_string(timeout_ms);
  int req_timeout = timeout_override_ms > 0
                        ? timeout_override_ms
                        : (static_cast<int>(timeout_ms) + 13000 > 15000
                               ? static_cast<int>(timeout_ms) + 13000
                               : 15000);
  auto resp = request("GET", path, "", req_timeout);
  if (!resp.is_ok()) return Result<std::map<std::string, uint32_t>>::err(resp.error());
  if (!resp.value().is_success()) {
    return Result<std::map<std::string, uint32_t>>::err(http_error(resp.value()));
  }
  json j = json::parse(resp.value().body, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    return Result<std::map<std::string, uint32_t>>::err(
        {static_cast<int>(MihomoErrc::JsonParse), "group delay response is not an object"});
  }
  std::map<std::string, uint32_t> out;
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (it.value().is_number()) out[it.key()] = it.value().get<uint32_t>();
  }
  return Result<std::map<std::string, uint32_t>>::ok(std::move(out));
}

// ---- Providers ----
Result<ProxyProviders> MihomoClient::get_proxy_providers(int timeout_ms) {
  return request_json<ProxyProviders>("GET", "/providers/proxies", "", timeout_ms);
}

Result<ProxyProvider> MihomoClient::get_proxy_provider_by_name(const std::string& name,
                                                               int timeout_ms) {
  auto resp = request("GET", "/providers/proxies/" + percent_encode(name), "", timeout_ms);
  if (!resp.is_ok()) return Result<ProxyProvider>::err(resp.error());
  if (!resp.value().is_success()) return Result<ProxyProvider>::err(http_error(resp.value()));
  json j = json::parse(resp.value().body, nullptr, false);
  if (j.is_discarded()) {
    return Result<ProxyProvider>::err({static_cast<int>(MihomoErrc::JsonParse),
                                       "provider response is not JSON"});
  }
  return Result<ProxyProvider>::ok(ProxyProvider::from_json(name, j));
}

Result<void> MihomoClient::update_proxy_provider(const std::string& name, int timeout_ms) {
  auto resp = request("PUT", "/providers/proxies/" + percent_encode(name), "", timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

Result<void> MihomoClient::healthcheck_proxy_provider(const std::string& name, int timeout_ms) {
  auto resp = request("GET", "/providers/proxies/" + percent_encode(name) + "/healthcheck", "",
                      timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

Result<ProxyDelay> MihomoClient::healthcheck_node_in_provider(const std::string& provider,
                                                              const std::string& node,
                                                              const std::string& test_url,
                                                              uint32_t timeout_ms,
                                                              int timeout_override_ms) {
  std::string path = "/providers/proxies/" + percent_encode(provider) + "/" +
                     percent_encode(node) + "/healthcheck?url=" + percent_encode(test_url) +
                     "&timeout=" + std::to_string(timeout_ms);
  auto resp = request("GET", path, "", delay_req_timeout(timeout_ms, timeout_override_ms));
  if (!resp.is_ok()) return Result<ProxyDelay>::err(resp.error());
  // ← 原版：HTTP 失败解析 ErrorResponse 后返回 {delay:0}
  if (!resp.value().is_success()) return Result<ProxyDelay>::ok(ProxyDelay{});
  json j = json::parse(resp.value().body, nullptr, false);
  if (j.is_discarded()) {
    return Result<ProxyDelay>::err({static_cast<int>(MihomoErrc::JsonParse),
                                    "healthcheck response is not JSON"});
  }
  return Result<ProxyDelay>::ok(ProxyDelay::from_json(j));
}

Result<Rules> MihomoClient::get_rules(int timeout_ms) {
  return request_json<Rules>("GET", "/rules", "", timeout_ms);
}

Result<RuleProviders> MihomoClient::get_rule_providers(int timeout_ms) {
  return request_json<RuleProviders>("GET", "/providers/rules", "", timeout_ms);
}

Result<void> MihomoClient::update_rule_provider(const std::string& name, int timeout_ms) {
  auto resp = request("PUT", "/providers/rules/" + percent_encode(name), "", timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

// ---- Connections ----
Result<Connections> MihomoClient::get_connections(int timeout_ms) {
  return request_json<Connections>("GET", "/connections", "", timeout_ms);
}

Result<void> MihomoClient::close_connection(const std::string& id, int timeout_ms) {
  auto resp = request("DELETE", "/connections/" + percent_encode(id), "", timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

Result<void> MihomoClient::close_all_connections(int timeout_ms) {
  auto resp = request("DELETE", "/connections", "", timeout_ms);
  if (!resp.is_ok()) return Result<void>::err(resp.error());
  if (!resp.value().is_success()) return Result<void>::err(http_error(resp.value()));
  return Result<void>::ok();
}

// ---- DNS ----
Result<nlohmann::json> MihomoClient::get_dns_query(const std::string& name,
                                                   const std::string& type, int timeout_ms) {
  std::string path = "/dns/query?name=" + percent_encode(name) + "&type=" + percent_encode(type);
  auto resp = request("GET", path, "", timeout_ms);
  if (!resp.is_ok()) return Result<nlohmann::json>::err(resp.error());
  if (resp.value().status == 404) {
    return Result<nlohmann::json>::err(
        {static_cast<int>(MihomoErrc::HttpError), "dns/query endpoint not supported by core"});
  }
  if (!resp.value().is_success()) {
    return Result<nlohmann::json>::err(http_error(resp.value()));
  }
  json j = json::parse(resp.value().body, nullptr, false);
  if (j.is_discarded()) {
    return Result<nlohmann::json>::err({static_cast<int>(MihomoErrc::JsonParse),
                                        "dns query response is not JSON"});
  }
  return Result<nlohmann::json>::ok(std::move(j));
}

Result<bool> MihomoClient::dns_query_ready(const std::string& name, int timeout_ms) {
  std::string path = "/dns/query?name=" + percent_encode(name) + "&type=A";
  auto resp = request("GET", path, "", timeout_ms);
  // ← 原版：超时/连接类错误归入"未就绪"，允许调用方继续轮询
  if (!resp.is_ok()) {
    int code = resp.error().code;
    if (code == static_cast<int>(HttpErrc::Timeout) ||
        code == static_cast<int>(HttpErrc::PipeNotFound) ||
        code == static_cast<int>(HttpErrc::Busy) ||
        code == static_cast<int>(HttpErrc::PoolFull) ||
        code == static_cast<int>(HttpErrc::Other)) {
      return Result<bool>::ok(false);
    }
    return Result<bool>::err(resp.error());
  }
  if (resp.value().status == 404) {
    return Result<bool>::err(
        {static_cast<int>(MihomoErrc::HttpError), "dns/query endpoint not supported by core (404)"});
  }
  if (!resp.value().is_success()) return Result<bool>::ok(false);
  json j = json::parse(resp.value().body, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return Result<bool>::ok(false);
  // 不用子串匹配 "data"：Authority/Additional 记录同样含 data，会误判就绪
  auto status = j.find("Status");
  auto answer = j.find("Answer");
  bool status_ok = status != j.end() && status->is_number_integer() && status->get<int>() == 0;
  bool has_answer = answer != j.end() && answer->is_array() && !answer->empty();
  return Result<bool>::ok(status_ok && has_answer);
}

}  // namespace clashwin::mihomo
