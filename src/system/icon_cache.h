#pragma once
// T25 节点与订阅图标缓存管理器（← feat/icon.rs）
// 提供图标下载去重、SSRF 安全校验、文件类型校验（SVG/PNG/ICO/JPEG/GIF/WebP 允许，HTML 拦截）、
// 图标版本轮转复制（copy_icon_file）、磁盘缓存超限淘汰清理（cleanup_icon_cache）
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "util/result.h"

namespace clashwin::system {

struct IconInfo {
  std::string name;
  std::string current_t;
  std::string previous_t;
};

class IIconCacheFetcher {
 public:
  virtual ~IIconCacheFetcher() = default;
  virtual Result<std::vector<uint8_t>> fetch(const std::string& url) = 0;
};

class DefaultIconCacheFetcher : public IIconCacheFetcher {
 public:
  Result<std::vector<uint8_t>> fetch(const std::string& url) override;
};

class IconCache {
 public:
  static IconCache& instance();

  void set_fetcher(std::shared_ptr<IIconCacheFetcher> fetcher);
  std::shared_ptr<IIconCacheFetcher> fetcher() const;

  // 校验逻辑
  static bool is_valid_icon_name(const std::string& name);
  static bool is_ssrf_safe_url(const std::string& url);
  static bool is_supported_icon_content(const std::vector<uint8_t>& bytes);

  // 核心功能
  Result<std::string> download_icon_cache(const std::string& url, const std::string& name);
  Result<std::string> copy_icon_file(const std::string& src_path, const IconInfo& info);
  Result<void> cleanup_icon_cache(size_t max_files);

 private:
  IconCache();
  ~IconCache() = default;
  IconCache(const IconCache&) = delete;
  IconCache& operator=(const IconCache&) = delete;

  std::shared_ptr<IIconCacheFetcher> fetcher_;
};

}  // namespace clashwin::system
