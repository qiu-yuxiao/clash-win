// T09 cmd_network（← cmd/network.rs）+ cmd_proxy（← cmd/proxy.rs）
#include "cmd/cmd_install.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <iptypes.h>

#include "cmd/cmd_support.h"
#include "cmd/command_service.h"
#include "config/config.h"
#include "config/proxy_head_state.h"
#include "enhance/enhance.h"
#include "system/network_info.h"
#include "system/sysproxy.h"
#include "util/dirs.h"
#include "util/utf.h"

#include <fstream>
#include <sstream>

#pragma comment(lib, "iphlpapi.lib")

namespace clashwin::cmd {

void install_cmd_network(CommandService& svc) {
  // ---- get_sys_proxy（← network.rs:11-38；T18 统一走 sysproxy access
  //      同一注册表口径，server "host:port" 读回拆分一致）----
  svc.register_command(
      "get_sys_proxy",
      {[](const Json&) -> CmdResult {
         auto s = system::wininet_access()->read_sysproxy();
         if (s.is_err()) return CmdResult::err(s.error());
         return CmdResult::ok(Json{{"enable", s.value().enable},
                                   {"server", utf::utf16_to_utf8(
                                                  s.value().host + L":" +
                                                  std::to_wstring(s.value().port))},
                                   {"bypass", utf::utf16_to_utf8(s.value().bypass)}});
       },
       consts::kTimeoutProbe});

  // ---- get_auto_proxy（← network.rs:41-59：AutoConfigURL）----
  svc.register_command(
      "get_auto_proxy",
      {[](const Json&) -> CmdResult {
         auto a = system::wininet_access()->read_autoproxy();
         if (a.is_err()) return CmdResult::err(a.error());
         return CmdResult::ok(Json{{"enable", a.value().enable},
                                   {"url", utf::utf16_to_utf8(a.value().url)}});
       },
       consts::kTimeoutProbe});

  // ---- get_system_hostname（← network.rs:62-74）----
  auto hostname_handler = [](const Json&) -> CmdResult {
    auto host = system::get_network_info_provider()->get_hostname();
    return CmdResult::ok(Json(host));
  };
  svc.register_command("get_system_hostname", {hostname_handler, consts::kTimeoutProbe});
  svc.register_command("hostname", {hostname_handler, consts::kTimeoutProbe});

  // ---- get_network_interfaces（← network.rs:77-80：GetAdaptersAddresses 友好名）----
  svc.register_command(
      "get_network_interfaces",
      {[](const Json&) -> CmdResult {
         auto list = system::get_network_info_provider()->list_interfaces();
         Json arr = Json::array();
         for (const auto& item : list) {
           arr.push_back(item);
         }
         return CmdResult::ok(arr);
       },
       consts::kTimeoutProbe});

  // ---- get_network_interfaces_info（← network.rs:83-97：名字+地址集）----
  svc.register_command(
      "get_network_interfaces_info",
      {[](const Json&) -> CmdResult {
         auto list = system::get_network_info_provider()->list_interfaces_info();
         Json arr = Json::array();
         for (const auto& info : list) {
           Json j{{"name", info.name},
                  {"mac", info.mac},
                  {"addresses", info.addresses}};
           arr.push_back(j);
         }
         return CmdResult::ok(arr);
       },
       consts::kTimeoutProbe});

  // ---- is_port_in_use（← network.rs:99-103：bind 探测）----
  svc.register_command(
      "is_port_in_use",
      {[](const Json& args) -> CmdResult {
         const Json* port = find_arg(args, {"port"});
         if (!port || !port->is_number()) return CmdResult::err({errs::kBadArgs, "port required"});
         uint16_t p = static_cast<uint16_t>(port->get<int>());
         static bool wsa = [] { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w) == 0; }();
         (void)wsa;
         SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
         if (s == INVALID_SOCKET) return CmdResult::ok(Json(true));
         sockaddr_in addr{};
         addr.sin_family = AF_INET;
         addr.sin_port = htons(p);
         addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
         bool in_use = ::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR;
         ::closesocket(s);
         return CmdResult::ok(Json(in_use));
       },
       consts::kTimeoutProbe});
}

void install_cmd_proxy(CommandService& svc) {
  // ---- get_proxy_addr（← proxy.rs:28-96：clash-win.yaml / provider 查 server:port）----
  svc.register_command(
      "get_proxy_addr",
      {[](const Json& args) -> CmdResult {
         const Json* name = find_arg(args, {"name"});
         if (!name || !name->is_string()) return CmdResult::err({errs::kBadArgs, "name required"});
         std::string n = name->get<std::string>();
         std::optional<std::string> provider;
         if (const Json* p = find_arg(args, {"provider"}); p && p->is_string()) {
           provider = p->get<std::string>();
         }
         std::wstring home = util::dirs::app_home_dir();
         auto find_in = [&](const std::string& content) -> std::optional<std::pair<std::string, uint16_t>> {
           YAML::Node cfg = YAML::Load(content);
           if (!cfg.IsMap()) return std::nullopt;
           const char* key = provider ? "proxy-providers" : "proxies";
           YAML::Node section = cfg[key];
           if (!section.IsMap() && !section.IsSequence()) {
             if (provider) return std::nullopt;
             return std::nullopt;
           }
           if (provider) {
             // provider mapping：取 path → 读文件 → proxies seq
             YAML::Node info = section[provider.value()];
             if (!info || !info["path"]) return std::nullopt;
             std::wstring ppath = utf::utf8_to_utf16(info["path"].Scalar());
             if (std::filesystem::path(ppath).is_relative()) ppath = home + L"\\" + ppath;
             std::ifstream pf(ppath, std::ios::binary);
             if (!pf.is_open()) return std::nullopt;
             std::ostringstream oss;
             oss << pf.rdbuf();
             YAML::Node pm = YAML::Load(oss.str());
             if (!pm["proxies"] || !pm["proxies"].IsSequence()) return std::nullopt;
             for (const auto& px : pm["proxies"]) {
               if (px["name"] && px["name"].Scalar() == n) {
                 uint16_t port = px["port"] ? static_cast<uint16_t>(px["port"].as<int>()) : 0;
                 return std::make_pair(px["server"] ? px["server"].Scalar() : "", port);
               }
             }
             return std::nullopt;
           }
           for (const auto& px : section) {
             if (px["name"] && px["name"].Scalar() == n) {
               uint16_t port = px["port"] ? static_cast<uint16_t>(px["port"].as<int>()) : 0;
               return std::make_pair(px["server"] ? px["server"].Scalar() : "", port);
             }
           }
           return std::nullopt;
         };

         std::wstring cfg_path = home + L"\\clash-win.yaml";
         std::ifstream f(cfg_path, std::ios::binary);
         if (f.is_open()) {
           std::ostringstream oss;
           oss << f.rdbuf();
           if (auto hit = find_in(oss.str()); hit) {
             return CmdResult::ok(Json{{"server", hit->first}, {"port", hit->second}});
           }
         }
         return CmdResult::ok(nullptr);  // ← Ok(None)
       },
       consts::kTimeoutProbe});

  // ---- get_proxy_head_state（← proxy.rs:107-116）----
  svc.register_command(
      "get_proxy_head_state",
      {[](const Json&) -> CmdResult {
         auto state = config::proxy_head_state::load();
         return CmdResult::ok(state.is_object() ? state : Json::object());
       },
       consts::kTimeoutProbe});

  // ---- save_proxy_head_state（← proxy.rs:99-104）----
  svc.register_command(
      "save_proxy_head_state",
      {[](const Json& args) -> CmdResult {
         auto r = config::proxy_head_state::save(args);
         if (r.is_err()) return CmdResult::err(r.error());
         return CmdResult::ok(Json::object());
       },
       consts::kTimeoutProbe});
}

}  // namespace clashwin::cmd
