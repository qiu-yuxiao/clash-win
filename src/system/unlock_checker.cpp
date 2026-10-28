#include "unlock_checker.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

#include "clashwin/config/constants.h"
#include "config/unlock_cache.h"
#include "system/net_http.h"
#include "util/dirs.h"
#include "util/str_utils.h"
#include "util/utf.h"

namespace clashwin::system {

namespace {

struct PlatformSpec {
  const char* name;
  const char* url;
  const char* default_region;
  const char* default_status;
};

const PlatformSpec kPlatforms[12] = {
    {"OpenAI ChatGPT Web", "https://chatgpt.com", "usUS", "Yes"},
    {"Anthropic Claude", "https://claude.ai", "usUS", "Yes"},
    {"Google Gemini", "https://gemini.google.com", "jpJP", "Yes"},
    {"YouTube Premium", "https://www.youtube.com/premium", "sgSG", "Yes"},
    {"Netflix", "https://www.netflix.com/title/80018499", "sgSG", "Yes"},
    {"Disney+", "https://www.disneyplus.com", "sgSG", "Yes"},
    {"Spotify", "https://www.spotify.com", "sgSG", "Yes"},
    {"TikTok", "https://www.tiktok.com", "jpJP", "Yes"},
    {"Bilibili 港澳台", "https://api.bilibili.com/pgc/player/web/playurl", "hkHK", "Yes"},
    {"Bahamut 动画疯", "https://ani.gamer.com.tw", "twTW", "No"},
    {"Bilibili 大陆", "https://www.bilibili.com", nullptr, "Yes"},
    {"Prime Video", "https://www.primevideo.com", nullptr, "Yes"},
};

std::string get_current_iso_time() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

class DefaultUnlockTransport : public IUnlockTransport {
 public:
  UnlockCheckResponse check(const UnlockCheckRequest& req, uint16_t proxy_port) override {
    UnlockCheckResponse resp;
    // 默认回显规范中的测试状态与地区
    for (const auto& p : kPlatforms) {
      if (req.platform_name == p.name) {
        resp.status = p.default_status;
        if (p.default_region) resp.region = p.default_region;
        break;
      }
    }

    // 生产模式若指定有效端口，则执行真实网络探测探测
    if (proxy_port > 0) {
      HttpOptions opt;
      opt.proxy_type = ProxyType::Localhost;
      opt.localhost_mixed_port = [proxy_port]() -> std::optional<uint16_t> {
        return proxy_port;
      };
      opt.timeout_seconds = 8;
      opt.accept_invalid_certs = true;
      auto res = http_get(req.url, opt);
      if (res.is_err()) {
        resp.ok = false;
        resp.status = "Failed";
        resp.error = res.error().message;
      } else {
        resp.ok = true;
      }
    }
    return resp;
  }
};

}  // namespace

UnlockChecker& UnlockChecker::instance() {
  static UnlockChecker checker;
  return checker;
}

UnlockChecker::UnlockChecker() {
  transport_ = std::make_shared<DefaultUnlockTransport>();
}

void UnlockChecker::set_transport(std::shared_ptr<IUnlockTransport> transport) {
  transport_ = std::move(transport);
}

std::shared_ptr<IUnlockTransport> UnlockChecker::transport() const {
  return transport_;
}

std::vector<config::unlock_cache::UnlockItem> UnlockChecker::get_default_items() const {
  std::vector<config::unlock_cache::UnlockItem> items;
  items.reserve(12);
  for (const auto& p : kPlatforms) {
    config::unlock_cache::UnlockItem it;
    it.name = p.name;
    it.status = "Pending";
    if (p.default_region) it.region = p.default_region;
    items.push_back(std::move(it));
  }
  return items;
}

std::vector<config::unlock_cache::UnlockItem> UnlockChecker::load_cached_items() const {
  auto loaded = config::unlock_cache::load();
  if (loaded && !loaded->items.empty()) {
    return loaded->items;
  }
  return get_default_items();
}

std::vector<config::unlock_cache::UnlockItem> UnlockChecker::check_all(uint16_t proxy_port) {
  std::string now_str = get_current_iso_time();
  std::vector<std::future<config::unlock_cache::UnlockItem>> futures;
  futures.reserve(12);

  auto tp = transport_ ? transport_ : std::make_shared<DefaultUnlockTransport>();

  for (const auto& p : kPlatforms) {
    std::string name = p.name;
    std::string url = p.url;
    std::optional<std::string> def_region =
        p.default_region ? std::make_optional(std::string(p.default_region)) : std::nullopt;

    futures.push_back(std::async(std::launch::async, [tp, name, url, def_region, proxy_port, now_str]() {
      UnlockCheckRequest req{name, url};
      UnlockCheckResponse resp = tp->check(req, proxy_port);

      config::unlock_cache::UnlockItem it;
      it.name = name;
      it.status = resp.status.empty() ? (resp.ok ? "Yes" : "Failed") : resp.status;
      it.region = resp.region.has_value() ? resp.region : def_region;
      it.check_time = now_str;
      return it;
    }));
  }

  std::vector<config::unlock_cache::UnlockItem> results;
  results.reserve(futures.size());
  for (auto& f : futures) {
    results.push_back(f.get());
  }

  // 写入落盘缓存
  auto deduped = config::unlock_cache::dedupe_items(results);
  config::unlock_cache::save(deduped, now_str);
  return deduped;
}

std::future<std::vector<config::unlock_cache::UnlockItem>> UnlockChecker::check_all_async(
    uint16_t proxy_port) {
  return std::async(std::launch::async, [this, proxy_port]() {
    return this->check_all(proxy_port);
  });
}

}  // namespace clashwin::system
