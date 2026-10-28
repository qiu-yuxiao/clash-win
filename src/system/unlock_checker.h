#pragma once
// 流媒体与 AI 解锁检测器（T21）：并发检测 12 大主流流媒体与 AI 平台连通性与解锁状态。
// - 12 大平台：OpenAI ChatGPT Web, Anthropic Claude, Google Gemini, YouTube Premium,
//             Netflix, Disney+, Spotify, TikTok, Bilibili 港澳台, Bahamut 动画疯,
//             Bilibili 大陆, Prime Video
// - 生产流量走当前节点代理（127.0.0.1:<mixed-port>）；单测注入 IUnlockTransport 达成 100% 隔离无网络外联
// - 结果去重并落盘持久化至 <home>\unlock_cache.json
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "config/unlock_cache.h"

namespace clashwin::system {

struct UnlockCheckRequest {
  std::string platform_name;
  std::string url;
};

struct UnlockCheckResponse {
  bool ok = true;
  std::string status = "Yes";  // "Yes", "No", "Failed", "Pending"
  std::optional<std::string> region;
  std::string error;
};

class IUnlockTransport {
 public:
  virtual ~IUnlockTransport() = default;
  virtual UnlockCheckResponse check(const UnlockCheckRequest& req, uint16_t proxy_port) = 0;
};

class UnlockChecker {
 public:
  static UnlockChecker& instance();

  void set_transport(std::shared_ptr<IUnlockTransport> transport);
  std::shared_ptr<IUnlockTransport> transport() const;

  // 12 默认平台条目（Pending）
  std::vector<config::unlock_cache::UnlockItem> get_default_items() const;

  // 从本地 unlock_cache.json 加载结果，缺失则回落默认列表
  std::vector<config::unlock_cache::UnlockItem> load_cached_items() const;

  // 12 平台并发检测并持久化
  std::vector<config::unlock_cache::UnlockItem> check_all(uint16_t proxy_port = 0);
  std::future<std::vector<config::unlock_cache::UnlockItem>> check_all_async(
      uint16_t proxy_port = 0);

 private:
  UnlockChecker();
  std::shared_ptr<IUnlockTransport> transport_;
};

}  // namespace clashwin::system
