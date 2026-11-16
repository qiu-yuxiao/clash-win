#include "config/runtime_state.h"
#include "enhance/enhance.h"

#include <yaml-cpp/yaml.h>

namespace clashwin::config {

namespace {

nlohmann::json yaml_node_to_json(const YAML::Node& node) {
  if (!node.IsDefined() || node.IsNull()) return nullptr;
  if (node.IsScalar()) {
    const std::string& s = node.Scalar();
    if (s == "true") return true;
    if (s == "false") return false;
    if (s == "null" || s == "~" || s.empty()) return nullptr;
    try {
      size_t pos = 0;
      long long i = std::stoll(s, &pos);
      if (pos == s.size()) return static_cast<int64_t>(i);
    } catch (...) {}
    try {
      size_t pos = 0;
      double d = std::stod(s, &pos);
      if (pos == s.size()) return d;
    } catch (...) {}
    return s;
  }
  if (node.IsMap()) {
    nlohmann::json obj = nlohmann::json::object();
    for (const auto& kv : node) {
      obj[kv.first.Scalar()] = yaml_node_to_json(kv.second);
    }
    return obj;
  }
  if (node.IsSequence()) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& item : node) {
      arr.push_back(yaml_node_to_json(item));
    }
    return arr;
  }
  return nullptr;
}

}  // namespace

RuntimeState& RuntimeState::instance() {
  static RuntimeState s_instance;
  return s_instance;
}

void RuntimeState::update(const enhance::EnhanceOutput& output) {
  std::lock_guard<std::mutex> lock(mu_);
  config_json_ = yaml_node_to_json(output.config);
  has_runtime_ = true;
}

void RuntimeState::clear() {
  std::lock_guard<std::mutex> lock(mu_);
  has_runtime_ = false;
  config_json_ = nullptr;
}

bool RuntimeState::has_runtime() const {
  std::lock_guard<std::mutex> lock(mu_);
  return has_runtime_;
}

nlohmann::json RuntimeState::get_runtime_config_json() const {
  std::lock_guard<std::mutex> lock(mu_);
  return config_json_;
}

}  // namespace clashwin::config
