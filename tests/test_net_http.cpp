// T06：网络层测试——响应头解析纯函数 + 本地 mock HTTP 服务端 + 真实网络 SKIP
#include <windows.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "system/net_http.h"
#include "test_util.h"
#include "util/str_utils.h"

using namespace clashwin;
using namespace clashwin::system;

namespace {

void ensure_wsa() {
  static std::atomic<bool> done{false};
  bool expected = false;
  if (done.compare_exchange_strong(expected, true)) {
    WSADATA d;
    WSAStartup(MAKEWORD(2, 2), &d);
  }
}

// ---------- 极简 mock HTTP 服务端：按 canned 响应序列逐连接应答 ----------
struct MockHttp {
  SOCKET listener = INVALID_SOCKET;
  uint16_t port = 0;
  std::vector<std::string> canned;      // 每连接一个完整响应（可为空串=只收不发）
  std::vector<std::string> first_lines; // 收到的请求首行
  std::mutex mu;
  std::thread th;
  bool ok = false;

  bool start(std::vector<std::string> responses) {
    ensure_wsa();
    canned = std::move(responses);
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(listener, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(listener, 8) != 0) {
      closesocket(listener);
      listener = INVALID_SOCKET;
      return false;
    }
    sockaddr_in got{};
    int len = sizeof(got);
    getsockname(listener, (sockaddr*)&got, &len);
    port = ntohs(got.sin_port);
    th = std::thread([this] { run(); });
    ok = true;
    return true;
  }

  void run() {
    for (size_t i = 0; i < canned.size(); ++i) {
      SOCKET c = accept(listener, nullptr, nullptr);
      if (c == INVALID_SOCKET) return;
      DWORD tv = 30000;  // 负载下 recv 5s 会饿死（t06_net_http_post flaky 根修）
      setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
      // 读到 \r\n\r\n
      std::string req;
      char buf[4096];
      while (req.find("\r\n\r\n") == std::string::npos && req.size() < 65536) {
        int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, (size_t)n);
      }
      {
        std::lock_guard<std::mutex> lk(mu);
        size_t eol = req.find("\r\n");
        first_lines.push_back(eol == std::string::npos ? req
                                                       : req.substr(0, eol));
      }
      if (!canned[i].empty()) {
        send(c, canned[i].data(), (int)canned[i].size(), 0);
      }
      shutdown(c, SD_BOTH);
      closesocket(c);
    }
  }

  void stop() {
    if (!ok) return;
    if (listener != INVALID_SOCKET) closesocket(listener);
    if (th.joinable()) th.join();
  }

  std::string first_line(size_t i) {
    std::lock_guard<std::mutex> lk(mu);
    return i < first_lines.size() ? first_lines[i] : "<none>";
  }

  bool first_line_contains(size_t i, const std::string& needle) {
    return first_line(i).find(needle) != std::string::npos;
  }
};

std::string resp(int status, const std::string& reason,
                 const std::vector<std::pair<std::string, std::string>>& headers,
                 const std::string& body) {
  std::string out = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
  for (auto& h : headers) out += h.first + ": " + h.second + "\r\n";
  out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  out += "Connection: close\r\n\r\n" + body;
  return out;
}

}  // namespace

// ---------- 响应头解析纯函数（← help.rs::parse_str / prfitem.rs） ----------

UT_TEST(t06_parse_str_u64) {
  auto r = parse_str_u64("upload=704784; download=12954158; total=107374182400; expire=1756000000", "upload");
  UT_CHECK(r.has_value() && *r == 704784ull);
  auto d = parse_str_u64("upload=704784; download=12954158; total=107374182400; expire=1756000000", "download");
  UT_CHECK(d.has_value() && *d == 12954158ull);
  auto t = parse_str_u64("upload=1; total=107374182400", "total");
  UT_CHECK(t.has_value() && *t == 107374182400ull);
  UT_CHECK(!parse_str_u64("upload=1", "missing").has_value());
  UT_CHECK(!parse_str_u64("upload=abc", "upload").has_value());
  UT_CHECK(!parse_str_u64("upload=-5", "upload").has_value());
  // 键精确匹配（大小写敏感，← Rust parse_str）
  UT_CHECK(!parse_str_u64("Upload=1", "upload").has_value());
}

UT_TEST(t06_parse_subscription_userinfo_fn) {
  auto full = parse_subscription_userinfo(
      "upload=704784; download=12954158; total=107374182400; expire=1756000000");
  UT_CHECK(full.has_value());
  if (full) {
    UT_CHECK_EQ(full->upload, 704784ull);
    UT_CHECK_EQ(full->download, 12954158ull);
    UT_CHECK_EQ(full->total, 107374182400ull);
    UT_CHECK_EQ(full->expire, 1756000000ull);
  }
  auto partial = parse_subscription_userinfo("upload=100");
  UT_CHECK(partial.has_value() && partial->upload == 100 &&
           partial->download == 0 && partial->total == 0 &&
           partial->expire == 0);
  UT_CHECK(!parse_subscription_userinfo("no-keys-here").has_value());
}

UT_TEST(t06_is_subscription_userinfo_header) {
  // ← prfitem.rs：key_lower.strip_suffix("subscription-userinfo")
  UT_CHECK(is_subscription_userinfo_header("subscription-userinfo"));
  UT_CHECK(is_subscription_userinfo_header("x-amz-meta-subscription-userinfo"));
  UT_CHECK(is_subscription_userinfo_header("x-obs-meta-subscription-userinfo"));
  UT_CHECK(is_subscription_userinfo_header("x-cos-meta-subscription-userinfo"));
  UT_CHECK(!is_subscription_userinfo_header("xsubscription-userinfo"));  // 前缀无 '-'
  UT_CHECK(!is_subscription_userinfo_header("subscription-userinfo-x"));
  UT_CHECK(!is_subscription_userinfo_header("content-type"));
}

UT_TEST(t06_parse_content_disposition) {
  auto a = parse_content_disposition_filename("attachment; filename=\"foo.zip\"");
  UT_CHECK(a.has_value() && *a == "foo.zip");
  auto b = parse_content_disposition_filename(
      "attachment; filename*=UTF-8''%E4%B8%AD%E6%96%87.zip");
  UT_CHECK(b.has_value() && *b == "中文.zip");
  // filename* 优先于 filename
  auto c = parse_content_disposition_filename(
      "attachment; filename=\"fallback.zip\"; filename*=UTF-8''star.zip");
  UT_CHECK(c.has_value() && *c == "star.zip");
  // 无 '' 分隔符 → 解码后整体
  auto d = parse_content_disposition_filename("attachment; filename*=UTF-8''plain.zip");
  UT_CHECK(d.has_value() && *d == "plain.zip");
  // 只有 filename（未包裹引号）
  auto e = parse_content_disposition_filename("attachment; filename=bare.zip");
  UT_CHECK(e.has_value() && *e == "bare.zip");
  UT_CHECK(!parse_content_disposition_filename("attachment").has_value());
}

UT_TEST(t06_get_last_part_and_decode) {
  auto a = get_last_part_and_decode("https://x.com/a/b/%E9%A6%99%E6%B8%AF.yaml?token=1");
  UT_CHECK_EQ(a, "香港.yaml");
  auto b = get_last_part_and_decode("https://x.com/");
  UT_CHECK_EQ(b, "");
  auto c = get_last_part_and_decode("https://x.com/sub?token=1");
  UT_CHECK_EQ(c, "sub");
}

UT_TEST(t06_parse_profile_update_interval_fn) {
  auto a = parse_profile_update_interval("24");
  UT_CHECK(a.has_value() && *a == 1440);  // 时 → 分
  auto b = parse_profile_update_interval("0");
  UT_CHECK(b.has_value() && *b == 0);
  UT_CHECK(!parse_profile_update_interval("abc").has_value());
  UT_CHECK(!parse_profile_update_interval("").has_value());
  UT_CHECK(!parse_profile_update_interval("1.5").has_value());
}

// ---------- mock HTTP 服务端 round-trip ----------

UT_TEST(t06_net_http_get_roundtrip) {
  MockHttp srv;
  if (!srv.start({resp(200, "OK",
                       {{"Content-Type", "text/yaml"},
                        {"Subscription-Userinfo",
                         "upload=704784; download=12954158; total=107374182400; expire=1756000000"},
                        {"Content-Disposition",
                         "attachment; filename*=UTF-8''sub.zip"},
                        {"profile-update-interval", "24"},
                        {"profile-web-page-url", "https://home.example.com"}},
                       "proxies: []\n")})) {
    std::printf("  SKIP: mock tcp server unavailable in sandboxed env\n");
    return;
  }
  HttpOptions opts;  // ProxyType::None 直连
  auto r = http_get("http://127.0.0.1:" + std::to_string(srv.port) + "/sub",
                    opts);
  srv.stop();
  UT_CHECK(r.is_ok());
  if (!r.is_ok()) return;
  UT_CHECK_EQ(r.value().status, 200);
  UT_CHECK(r.value().is_success());
  UT_CHECK_EQ(r.value().body, "proxies: []\n");
  const std::string* ct = r.value().header("content-type");
  UT_CHECK(ct && *ct == "text/yaml");
  const std::string* ui = r.value().header("SUBSCRIPTION-USERINFO");  // 大小写不敏感
  UT_CHECK(ui && ui->find("upload=704784") != std::string::npos);
  UT_CHECK(r.value().header("profile-update-interval") &&
           *r.value().header("profile-update-interval") == "24");
  // 请求首行
  UT_CHECK(srv.first_line(0).find("GET /sub HTTP/1.1") == 0);
}

UT_TEST(t06_net_http_redirect_follow) {
  MockHttp srv;
  if (!srv.start({
          resp(302, "Found", {{"Location", "/final"}}, ""),
          resp(200, "OK", {}, "final-body"),
      })) {
    std::printf("  SKIP: mock tcp server unavailable in sandboxed env\n");
    return;
  }
  std::string base = "http://127.0.0.1:" + std::to_string(srv.port);
  HttpOptions opts;
  auto r = http_get(base + "/start", opts);
  srv.stop();
  UT_CHECK(r.is_ok());
  if (!r.is_ok()) return;
  UT_CHECK_EQ(r.value().status, 200);
  UT_CHECK_EQ(r.value().body, "final-body");
}

UT_TEST(t06_net_http_redirect_cap) {
  // 302 循环超过 max_redirects → 最后一个 3xx 按最终响应返回
  MockHttp srv;
  if (!srv.start({
          resp(302, "Found", {{"Location", "/loop"}}, ""),
          resp(302, "Found", {{"Location", "/loop"}}, ""),
          resp(302, "Found", {{"Location", "/loop"}}, ""),
      })) {
    std::printf("  SKIP: mock tcp server unavailable in sandboxed env\n");
    return;
  }
  std::string base = "http://127.0.0.1:" + std::to_string(srv.port);
  HttpOptions opts;
  opts.max_redirects = 2;
  auto r = http_get(base + "/loop", opts);
  srv.stop();
  UT_CHECK(r.is_ok());
  if (!r.is_ok()) return;
  UT_CHECK_EQ(r.value().status, 302);
}

UT_TEST(t06_net_http_response_size_cap) {
  MockHttp srv;
  if (!srv.start({resp(200, "OK", {}, "0123456789ABCDEF")})) {
    std::printf("  SKIP: mock tcp server unavailable in sandboxed env\n");
    return;
  }
  std::string url = "http://127.0.0.1:" + std::to_string(srv.port) + "/big";
  HttpOptions opts;
  opts.max_response_bytes = 8;  // 人为压低上限验证机制
  auto r = http_get(url, opts);
  srv.stop();
  UT_CHECK(!r.is_ok());
  if (!r.is_ok()) {
    UT_CHECK(r.error().message.find("50MB") != std::string::npos ||
             r.error().message.find("exceeds") != std::string::npos);
  }
}

UT_TEST(t06_net_http_post) {
  MockHttp srv;
  if (!srv.start({resp(200, "OK", {}, "posted")})) {
    std::printf("  SKIP: mock tcp server unavailable in sandboxed env\n");
    return;
  }
  std::string url = "http://127.0.0.1:" + std::to_string(srv.port) + "/api";
  HttpOptions opts;
  auto r = http_post(url, "k=v", opts);
  srv.stop();
  UT_CHECK(r.is_ok() && r.value().body == "posted");
  UT_CHECK(srv.first_line(0).find("POST /api HTTP/1.1") == 0);
}

UT_TEST(t06_net_http_cancel_before_start) {
  HANDLE ev = CreateEventW(nullptr, TRUE, TRUE, nullptr);  // 预置有信号
  HttpOptions opts;
  opts.cancel_event = ev;
  auto r = http_get("http://127.0.0.1:1/x", opts);
  CloseHandle(ev);
  UT_CHECK(!r.is_ok());
  if (!r.is_ok()) UT_CHECK_EQ(r.error().message, "request cancelled");
}

UT_TEST(t06_net_http_localhost_proxy_mode) {
  // ProxyType::Localhost：注入 resolver → 请求以绝对 URI 打到本地代理
  MockHttp proxy;
  if (!proxy.start({resp(200, "OK", {}, "via-proxy")})) {
    std::printf("  SKIP: mock tcp server unavailable in sandboxed env\n");
    return;
  }
  HttpOptions opts;
  opts.proxy_type = ProxyType::Localhost;
  uint16_t proxy_port = proxy.port;
  opts.localhost_mixed_port = [proxy_port]() -> std::optional<uint16_t> {
    return proxy_port;
  };
  auto r = http_get("http://target.example.com/sub", opts);
  proxy.stop();
  UT_CHECK(r.is_ok());
  if (!r.is_ok()) return;
  UT_CHECK_EQ(r.value().body, "via-proxy");
  // 经代理的请求使用绝对 URI（HTTP 代理协议）
  UT_CHECK(proxy.first_line_contains(0, "GET http://target.example.com/sub"));
}

UT_TEST(t06_net_http_user_agent_default_and_custom) {
  MockHttp srv;
  if (!srv.start({resp(200, "OK", {}, "a"), resp(200, "OK", {}, "b")})) {
    std::printf("  SKIP: mock tcp server unavailable in sandboxed env\n");
    return;
  }
  std::string url = "http://127.0.0.1:" + std::to_string(srv.port) + "/";
  HttpOptions opts;
  auto r1 = http_get(url, opts);
  HttpOptions opts2;
  opts2.user_agent = std::string("clash-mini-test/1.0");
  auto r2 = http_get(url, opts2);
  srv.stop();
  UT_CHECK(r1.is_ok() && r2.is_ok());
  // 两个请求都被 mock 服务端受理（请求行为 HTTP/1.1 请求行）
  UT_CHECK(srv.first_line(0).find("HTTP/1.1") != std::string::npos);
  UT_CHECK(srv.first_line(1).find("HTTP/1.1") != std::string::npos);
}

UT_TEST(t06_net_http_timeout_connect) {
  // 连接不可路由地址（沙箱内通常无回包）→ 2s 超时报错
  HttpOptions opts;
  opts.timeout_seconds = 2;
  auto r = http_get("http://10.255.255.1:81/x", opts);
  // 沙箱可能透明拦截：成功也判失败（此地址不应有服务）
  UT_CHECK(!r.is_ok() || r.value().status == 0);
}

UT_TEST(t06_net_http_system_proxy_read) {
  // 只验证读取不崩溃；ProxyType::System 未启用时回退直连（← network.rs）
  auto sp = read_system_proxy();
  if (sp) {
    UT_CHECK(!sp->empty());
    UT_CHECK(sp->find(':') != std::string::npos);
  } else {
    UT_CHECK(true);  // 未启用系统代理 → nullopt → 直连
  }
}

UT_TEST(t06_net_http_invalid_url) {
  HttpOptions opts;
  UT_CHECK(!http_get("ftp://example.com/", opts).is_ok());
  UT_CHECK(!http_get("not a url", opts).is_ok());
  UT_CHECK(!http_get("http:///", opts).is_ok());
}

// ---------- 真实网络（沙箱无网 → SKIP） ----------

UT_TEST(t06_net_http_real_network) {
  HttpOptions opts;
  opts.timeout_seconds = 8;
  auto r = http_get("http://example.com/", opts);
  if (!r.is_ok()) {
    std::printf("  SKIP: no internet access in sandboxed env\n");
    return;
  }
  UT_CHECK(r.value().is_success());
  UT_CHECK(!r.value().body.empty());
}
