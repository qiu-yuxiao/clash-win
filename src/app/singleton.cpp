#include "app/singleton.h"
#include "app/lifecycle.h"
#include "config/config.h"
#include "clashwin/config/constants.h"
#include "util/log.h"
#include "util/str_utils.h"
#include "util/utf.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <sstream>
#include <vector>

namespace clashwin::app {

namespace {
// 本进程实际绑定的单实例端口（start 成功后写入）
std::atomic<uint16_t> g_singleton_port{config::consts::kSingletonPort};
}  // namespace

uint16_t current_singleton_port() { return g_singleton_port.load(); }

SingletonServer::SingletonServer() = default;

SingletonServer::~SingletonServer() {
  stop();
}

bool SingletonServer::start(uint16_t port,
                            std::function<void()> on_visible,
                            std::function<void(const std::string&)> on_scheme) {
  stop();

  port_ = port;
  on_visible_ = std::move(on_visible);
  on_scheme_ = std::move(on_scheme);

  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;

  // socket() 句柄默认带 OBJ_INHERIT：拉起 sidecar 时 CreateProcessW 的 bInheritHandles=TRUE
  // （stdout/stderr 管道必须重定向）会把它一并继承给 win-mihomo，导致本进程退出后
  // 33337 仍被 sidecar 持着 LISTEN。源头清除继承位，覆盖所有 CreateProcessW 调用点。
  ::SetHandleInformation(reinterpret_cast<HANDLE>(s), HANDLE_FLAG_INHERIT, 0);

  // 独占绑定：端口被占用（含其它程序监听）时 bind 直接失败，
  // 杜绝 Windows SO_REUSEADDR 的重复绑定/劫持语义
  int opt = 1;
  setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&opt), sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    closesocket(s);
    return false;
  }

  if (listen(s, 16) != 0) {
    closesocket(s);
    return false;
  }

  g_singleton_port.store(port);
  listen_sock_ = static_cast<uintptr_t>(s);
  running_ = true;
  worker_ = std::thread([this] { accept_loop(); });
  return true;
}

void SingletonServer::stop() {
  if (!running_.exchange(false)) return;

  if (listen_sock_) {
    SOCKET s = static_cast<SOCKET>(listen_sock_);
    listen_sock_ = 0;
    closesocket(s);
  }

  if (worker_.joinable()) {
    worker_.join();
  }
}

void SingletonServer::accept_loop() {
  SOCKET s = static_cast<SOCKET>(listen_sock_);
  while (running_.load()) {
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(s, &rset);
    timeval tv{0, 250 * 1000};  // 250ms
    int r = select(0, &rset, nullptr, nullptr, &tv);
    if (r <= 0) continue;

    SOCKET c = accept(s, nullptr, nullptr);
    if (c == INVALID_SOCKET) continue;

    ::SetHandleInformation(reinterpret_cast<HANDLE>(c), HANDLE_FLAG_INHERIT, 0);
    handle_connection(static_cast<uintptr_t>(c));
  }
}

void SingletonServer::handle_connection(uintptr_t client_sock) {
  SOCKET c = static_cast<SOCKET>(client_sock);
  char buf[4096];
  int received = recv(c, buf, sizeof(buf) - 1, 0);
  if (received > 0) {
    buf[received] = '\0';
    std::string resp = handle_http_request(std::string(buf, received));
    send(c, resp.data(), static_cast<int>(resp.size()), 0);
  }
  shutdown(c, SD_BOTH);
  closesocket(c);
}

std::string SingletonServer::handle_http_request(const std::string& raw_request) {
  std::istringstream iss(raw_request);
  std::string method, full_path, protocol;
  if (!(iss >> method >> full_path >> protocol)) {
    return "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
  }

  if (method != "GET") {
    return "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
  }

  std::string path = full_path;
  std::string query;
  size_t qpos = full_path.find('?');
  if (qpos != std::string::npos) {
    path = full_path.substr(0, qpos);
    query = full_path.substr(qpos + 1);
  }

  if (path == "/commands/visible") {
    if (on_visible_) on_visible_();
    std::string body = "ok";
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/plain\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Connection: close\r\n\r\n" + body;
  }

  if (path == "/commands/pac") {
    std::string pac = generate_pac_script();
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: application/x-ns-proxy-autoconfig\r\n"
           "Content-Length: " + std::to_string(pac.size()) + "\r\n"
           "Connection: close\r\n\r\n" + pac;
  }

  if (path == "/commands/scheme") {
    std::string param_val;
    size_t pos = query.find("param=");
    if (pos != std::string::npos) {
      size_t start = pos + 6;
      size_t end = query.find('&', start);
      if (end == std::string::npos) {
        param_val = query.substr(start);
      } else {
        param_val = query.substr(start, end - start);
      }
    }
    std::string decoded_param = util::percent_decode(param_val);
    if (on_scheme_) {
      on_scheme_(decoded_param);
    }
    std::string body = "ok";
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/plain\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Connection: close\r\n\r\n" + body;
  }

  std::string not_found = "Not Found";
  return "HTTP/1.1 404 Not Found\r\n"
         "Content-Type: text/plain\r\n"
         "Content-Length: " + std::to_string(not_found.size()) + "\r\n"
         "Connection: close\r\n\r\n" + not_found;
}

std::string SingletonServer::generate_pac_script(uint16_t port_override) {
  uint16_t port = port_override;
  if (port == 0) {
    auto verge = config::Config::instance().verge().latest_arc();
    if (verge && verge->verge_mixed_port.has_value() && *verge->verge_mixed_port > 0) {
      port = *verge->verge_mixed_port;
    } else {
      auto mapping = config::Config::instance().clash().latest_arc()->mapping();
      if (mapping["mixed-port"]) {
        try {
          port = static_cast<uint16_t>(mapping["mixed-port"].as<int>());
        } catch (...) {
          port = 0;
        }
      }
    }
  }
  if (port == 0) port = 7897;

  std::string pac;
  auto verge = config::Config::instance().verge().latest_arc();
  if (verge && verge->pac_file_content.has_value() && !verge->pac_file_content->empty()) {
    pac = *verge->pac_file_content;
  } else {
    pac = config::consts::kDefaultPac;
  }

  std::string needle = "%mixed-port%";
  std::string replacement = std::to_string(port);
  size_t pos = 0;
  while ((pos = pac.find(needle, pos)) != std::string::npos) {
    pac.replace(pos, needle.length(), replacement);
    pos += replacement.length();
  }
  return pac;
}

// ---------- SingletonClient 实现 ----------

bool SingletonClient::notify_or_exit(uint16_t port, int argc, wchar_t* argv[], bool do_exit) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    closesocket(s);
    return false;
  }
  closesocket(s);

  bool has_silent = false;
  std::string scheme_url;

  for (int i = 1; i < argc; ++i) {
    if (!argv[i]) continue;
    std::wstring arg_w = argv[i];
    std::string arg = utf::utf16_to_utf8(arg_w);
    if (arg == "--silent") {
      has_silent = true;
    } else if (arg.starts_with("clash:") || arg.starts_with("clash://") ||
               arg.starts_with("clash-verge:") || arg.starts_with("clash-verge://") ||
               arg.starts_with("clash-mini:") || arg.starts_with("clash-mini://")) {
      scheme_url = arg;
    }
  }

  if (has_silent) {
    CWLOG_INFO("SingletonClient: --silent detected on secondary instance, exiting immediately");
    if (do_exit) exit(0);
    return true;
  }

  if (!scheme_url.empty()) {
    std::string encoded_param;
    for (char c : scheme_url) {
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
        encoded_param.push_back(c);
      } else {
        char hex[4];
        snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
        encoded_param += hex;
      }
    }
    std::string path = "/commands/scheme?param=" + encoded_param;
    send_request(port, path);
  } else {
    send_request(port, "/commands/visible");
  }

  if (do_exit) exit(0);
  return true;
}

int SingletonClient::send_request(uint16_t port, const std::string& path_and_query, std::string* response_out) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return -1;

  DWORD timeout = 500;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    closesocket(s);
    return -1;
  }

  std::string req = "GET " + path_and_query + " HTTP/1.1\r\n"
                    "Host: 127.0.0.1\r\n"
                    "Connection: close\r\n\r\n";
  send(s, req.data(), static_cast<int>(req.size()), 0);

  char buf[4096];
  int n = recv(s, buf, sizeof(buf) - 1, 0);
  int status_code = -1;
  if (n > 0) {
    buf[n] = '\0';
    if (response_out) *response_out = std::string(buf, n);
    if (n >= 12 && strncmp(buf, "HTTP/1.", 7) == 0) {
      status_code = atoi(buf + 9);
    }
  }

  closesocket(s);
  return status_code;
}

}  // namespace clashwin::app
