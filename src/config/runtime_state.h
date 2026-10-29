#pragma once
// T25 运行时配置与日志状态缓存单例
// 缓存 EnhanceOutput 全量产物（YAML 字符串、JSON 对象、exists 键集合、Chain 脚本日志）
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

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
  std::optional<std::string> get_runtime_yaml() const;
  nlohmann::json get_runtime_config_json() const;
  std::vector<std::string> get_exists_keys() const;
  nlohmann::json get_chain_logs() const;

 private:
  RuntimeState() = default;
  ~RuntimeState() = default;
  RuntimeState(const RuntimeState&) = delete;
  RuntimeState& operator=(const RuntimeState&) = delete;

  mutable std::mutex mu_;
  bool has_runtime_ = false;
  std::string yaml_str_;
  nlohmann::json config_json_;
  std::vector<std::string> exists_keys_;
  nlohmann::json chain_logs_;
};

}  // namespace clashwin::config
