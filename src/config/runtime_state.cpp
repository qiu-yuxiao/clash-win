#include "config/runtime_state.h"
#include "enhance/enhance.h"

#include <algorithm>
#include <sstream>

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
  YAML::Emitter emitter;
  emitter << output.config;
  yaml_str_ = emitter.c_str();

  config_json_ = yaml_node_to_json(output.config);

  exists_keys_.assign(output.exists_keys.begin(), output.exists_keys.end());
  std::sort(exists_keys_.begin(), exists_keys_.end());

  chain_logs_ = nlohmann::json::object();
  for (const auto& [uid, logs] : output.script_logs) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& log : logs) {
      arr.push_back(nlohmann::json::array({log.level, log.message}));
    }
    chain_logs_[uid] = arr;
  }

  has_runtime_ = true;
}

void RuntimeState::clear() {
  std::lock_guard<std::mutex> lock(mu_);
  has_runtime_ = false;
  yaml_str_.clear();
  config_json_ = nullptr;
  exists_keys_.clear();
  chain_logs_ = nlohmann::json::object();
}

bool RuntimeState::has_runtime() const {
  std::lock_guard<std::mutex> lock(mu_);
  return has_runtime_;
}

std::optional<std::string> RuntimeState::get_runtime_yaml() const {
  std::lock_guard<std::mutex> lock(mu_);
  if (!has_runtime_) return std::nullopt;
  return yaml_str_;
}

nlohmann::json RuntimeState::get_runtime_config_json() const {
  std::lock_guard<std::mutex> lock(mu_);
  return config_json_;
}

std::vector<std::string> RuntimeState::get_exists_keys() const {
  std::lock_guard<std::mutex> lock(mu_);
  return exists_keys_;
}

nlohmann::json RuntimeState::get_chain_logs() const {
  std::lock_guard<std::mutex> lock(mu_);
  return chain_logs_;
}

}  // namespace clashwin::config
