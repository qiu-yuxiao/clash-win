#pragma once
// 运行时配置状态缓存单例
// 缓存 EnhanceOutput 运行时配置 JSON 对象
#include <mutex>
#include <nlohmann/json.hpp>

namespace clashwin::enhance {
struct EnhanceOutput;
}

namespace clashwin::config {

class RuntimeState {
 public:
  static RuntimeState& instance();

  void update(const enhance::EnhanceOutput& output);
  void clear();

  bool has_runtime() const;
  nlohmann::json get_runtime_config_json() const;

 private:
  RuntimeState() = default;
  ~RuntimeState() = default;
  RuntimeState(const RuntimeState&) = delete;
  RuntimeState& operator=(const RuntimeState&) = delete;

  mutable std::mutex mu_;
  bool has_runtime_ = false;
  nlohmann::json config_json_;
};

}  // namespace clashwin::config
