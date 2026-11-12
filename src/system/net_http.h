#pragma once
// WinHTTP 同步下载封装（← utils/network.rs NetworkManager 逐行为对齐）
//
// 能力：
//   - 同步 GET/POST，重定向手动跟随（reqwest redirect::Policy::limited(10) 口径：
//     301/302/303 POST→GET，307/308 保方法；跨 host 剥 Authorization）
//   - 代理三态 ProxyType::{None=直连, System=读当前 WinINET 系统代理,
//     Localhost=经 127.0.0.1:<mixed-port>（← network.rs verge_mixed_port →
//     clash mixed-port 回退链，端口由注入的 resolver 提供）
//   - 可中断：cancel_event 有信号时在阶段边界中止；各阶段有超时
//   - User-Agent 可指定（未指定时默认采用 kSubscriptionCompatUserAgent = "clash-verge/v2.5.10"，
//     对齐当前内置 Mihomo v1.19.x 协议代际并与 UI 版本号解耦；原因详见《有关User-Agent的思考.md》）
//   - 响应大小上限 50MB（kSubscriptionMaxBytes），默认总超时 20s
//     （connect 上限 min(t,10)，← network.rs connect_timeout）
//   - 响应体必须为合法 UTF-8（← String::from_utf8 严格语义）
//   - 订阅响应头解析纯函数：Subscription-Userinfo（兼容 x-amz-meta- 等前缀）、
//     Content-Disposition filename*（RFC 5987）/filename、
//     profile-update-interval（时→分）、profile-web-page-url
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <windows.h>

#include "util/result.h"

namespace clashwin::system {

// ← network.rs::ProxyType
enum class ProxyType { None, Localhost, System };

struct HttpHeader {
  std::string name;
  std::string value;
};

// ← network.rs::HttpResponse
struct HttpResponse {
  uint16_t status = 0;
  std::vector<HttpHeader> headers;  // 原始顺序与大小写
  std::string body;

  // ← HeaderMap::get：首个同名头（大小写不敏感）；无 → nullptr
  const std::string* header(std::string_view name) const;
  bool is_success() const { return status >= 200 && status <= 299; }
};

// ---------- 轻量 URL 解析（Url::parse 在 http(s) 域内的使用面） ----------
// host 归一小写；WHATWG 数字主机（"127.1"）归一为点分四段并置 is_ipv4；
// 默认端口剥离；空路径归一为 "/"。
struct UrlParts {
  std::string scheme;    // 小写 "http"/"https"
  std::string username;  // 原始未解码（userinfo）
  std::string password;
  std::string host;      // 归一 host（IPv6 去括号）
  bool is_ipv4 = false;  // host 已按 WHATWG 规则归一为点分四段
  bool is_ipv6 = false;
  std::string port_str;  // 空 = 未指定
  std::string path;      // 含前导 '/'，空归一为 "/"
  std::string query;     // 不含 '?'
  std::string fragment;  // 不含 '#'

  uint16_t effective_port() const;      // 未指定 → scheme 默认 80/443
  std::string request_target() const;   // path + ("?" + query)?
  std::string host_header() const;      // host[:port]（v6 带括号；非默认端口）
  std::string serialize() const;        // 归一后的完整 URL
};

// 解析失败（scheme 非 http/https、无 authority、非法端口等）→ Error
Result<UrlParts> parse_http_url(const std::string& url);

struct HttpOptions {
  ProxyType proxy_type = ProxyType::None;
  std::optional<uint64_t> timeout_seconds;  // nullopt → 20（kProfileTimeoutSeconds）
  std::optional<std::string> user_agent;    // nullopt → kSubscriptionCompatUserAgent（"clash-verge/v2.5.10"）
  bool accept_invalid_certs = false;        // ← danger_accept_invalid_certs
  size_t max_response_bytes = 50u << 20;    // 50MB
  int max_redirects = 10;                   // ← redirect::Policy::limited(10)
  HANDLE cancel_event = nullptr;            // 有信号 → 中止（阶段边界检查）
  // ProxyType::Localhost 的 mixed-port 来源（prfitem 注入 verge→clash 回退链）
  std::function<std::optional<uint16_t>()> localhost_mixed_port;
};

// ← NetworkManager::get_with_interrupt（同步化；WinHTTP 恒用平台证书库，
// Rust 的 static webpki roots 回退不适用）
Result<HttpResponse> http_get(const std::string& url, const HttpOptions& opts);
Result<HttpResponse> http_post(const std::string& url, std::string body,
                               const HttpOptions& opts);

// 流式下载到文件（← network.rs create_request + reqwest Response::chunk 循环）：
// 分块落盘并逐块回调 downloaded/total；total=0 表示服务端未给 Content-Length。
// 重定向/代理/超时/中断语义与 http_get 一致，唯一区别是不把响应体留在内存。
Result<void> http_download_file(
    const std::string& url, const std::wstring& dest_path, const HttpOptions& opts,
    std::function<void(uint64_t downloaded, uint64_t total)> on_progress);

// ---------- 订阅响应头解析纯函数（供单测；← prfitem.rs from_url） ----------

// ← help.rs::parse_str::<u64>：';' 分段、trim、'=' 切两半、键精确匹配
std::optional<uint64_t> parse_str_u64(std::string_view target,
                                      std::string_view key);

// ← prfitem.rs：头名（小写）等于 subscription-userinfo，或以
// "-subscription-userinfo" 结尾（x-amz-meta-/x-obs-meta-/x-cos-meta- 前缀族）
bool is_subscription_userinfo_header(std::string_view lower_name);

// ← Subscription-Userinfo 头 → PrfExtra（缺项 0）
struct SubscriptionUserinfo {
  uint64_t upload = 0, download = 0, total = 0, expire = 0;
};
std::optional<SubscriptionUserinfo> parse_subscription_userinfo(
    std::string_view value);

// ← Content-Disposition：filename*（percent-decode + 按 "''" 取尾部）优先，
// 否则 filename（剥包裹引号）；两者皆无 → nullopt
std::optional<std::string> parse_content_disposition_filename(
    std::string_view value);

// ← help.rs::get_last_part_and_decode：URL 末段 percent-decode
std::string get_last_part_and_decode(std::string_view url);

// ← profile-update-interval：时 → 分（解析失败 → nullopt）
std::optional<uint64_t> parse_profile_update_interval(std::string_view value);

// 读取当前 WinINET 用户系统代理（← sysproxy::get_system_proxy 使用面）：
// ProxyEnable=1 且 ProxyServer 非空 → "host:port"，否则 nullopt
std::optional<std::string> read_system_proxy();

}  // namespace clashwin::system
