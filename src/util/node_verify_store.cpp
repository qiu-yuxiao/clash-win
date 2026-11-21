// 真 HTTPS 验真状态机、24h 软隔离表与运行态指纹注册持久化
// （拆分自 src/util/node.cpp；← ClashVerge src-tauri/src/utils/node/verify_store.rs）
#include "util/node.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <sstream>

#include <nlohmann/json.hpp>

#include "util/dirs.h"
#include "util/log.h"

namespace clashwin::util {

namespace {

constexpr const wchar_t* kVerifyStoreFile = L"node_https_verify.json";

std::shared_mutex& verify_store_mu() {
  static std::shared_mutex mu;
  return mu;
}

std::optional<NodeVerifyStoreData>& verify_store_slot() {
  static std::optional<NodeVerifyStoreData> slot;
  return slot;
}

std::shared_mutex& topology_mu() {
  static std::shared_mutex mu;
  return mu;
}

std::optional<std::unordered_map<std::string, std::string>>& runtime_fps_slot() {
  static std::optional<std::unordered_map<std::string, std::string>> slot;
  return slot;
}

std::optional<std::unordered_set<std::string>>& runtime_tainted_slot() {
  static std::optional<std::unordered_set<std::string>> slot;
  return slot;
}

std::mutex& test_cfg_mu() {
  static std::mutex mu;
  return mu;
}

bool g_disable_disk_persistence_for_test = false;
std::optional<std::wstring> g_test_verify_store_path;

int64_t current_unix_ts() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::optional<std::wstring> resolve_verify_store_path() {
  {
    std::lock_guard<std::mutex> lock(test_cfg_mu());
    if (g_test_verify_store_path.has_value()) {
      return g_test_verify_store_path;
    }
    if (g_disable_disk_persistence_for_test) {
      return std::nullopt;
    }
  }
  const std::wstring home = dirs::app_home_dir();
  if (home.empty()) return std::nullopt;
  return home + L"\\" + kVerifyStoreFile;
}

void ensure_verify_store_loaded() {
  {
    std::shared_lock<std::shared_mutex> rlock(verify_store_mu());
    if (verify_store_slot().has_value()) return;
  }
  NodeVerifyStoreData loaded;
  if (auto p = resolve_verify_store_path(); p.has_value()) {
    loaded = load_verify_store_from_path(*p);
  }
  std::unique_lock<std::shared_mutex> wlock(verify_store_mu());
  if (!verify_store_slot().has_value()) {
    verify_store_slot() = std::move(loaded);
  }
}

void set_runtime_topology_if_none(
    std::unordered_map<std::string, std::string> map,
    std::unordered_set<std::string> tainted) {
  std::unique_lock<std::shared_mutex> wlock(topology_mu());
  if (!runtime_fps_slot().has_value()) {
    runtime_fps_slot() = std::move(map);
  }
  if (!runtime_tainted_slot().has_value()) {
    runtime_tainted_slot() = std::move(tainted);
  }
}

bool is_tainted_banner_fp(const std::string& fp) {
  ensure_runtime_node_fingerprints_loaded();
  std::shared_lock<std::shared_mutex> rlock(topology_mu());
  const auto& slot = runtime_tainted_slot();
  return slot.has_value() && slot->count(fp) > 0;
}

std::optional<std::string> fingerprint_for_node(const std::string& name) {
  ensure_runtime_node_fingerprints_loaded();
  std::shared_lock<std::shared_mutex> rlock(topology_mu());
  const auto& slot = runtime_fps_slot();
  if (!slot.has_value()) return std::nullopt;
  auto it = slot->find(name);
  if (it == slot->end()) return std::nullopt;
  return it->second;
}

void persist_verify_store_snapshot(const NodeVerifyStoreData& snapshot) {
  auto path_opt = resolve_verify_store_path();
  if (!path_opt.has_value()) return;
  if (!save_verify_store_to_path(*path_opt, snapshot)) {
    CWLOG_WARN("保存节点 HTTPS 验真状态失败");
  }
}

size_t clear_unverified_failures_in_records(
    std::map<std::string, NodeVerifyRecord>& records) {
  size_t cleared = 0;
  for (auto it = records.begin(); it != records.end();) {
    if (!it->second.verified_true && it->second.fail_count > 0) {
      ++cleared;
      it = records.erase(it);
    } else {
      ++it;
    }
  }
  return cleared;
}

}  // namespace

bool NodeVerifyRecord::is_confirmed_fake_at(int64_t now_ts) const {
  if (verified_true || fail_count <= kHttpsVerifyMaxFails) return false;
  if (last_verified_at <= 0) return true;
  const int64_t diff =
      (now_ts >= last_verified_at) ? (now_ts - last_verified_at) : 0;
  return diff < kHttpsVerifyBanTtlSecs;
}

bool NodeVerifyRecord::is_confirmed_fake() const {
  return is_confirmed_fake_at(current_unix_ts());
}

void set_runtime_node_fingerprints(
    std::unordered_map<std::string, std::string> map) {
  std::unique_lock<std::shared_mutex> wlock(topology_mu());
  runtime_fps_slot() = std::move(map);
}

void set_runtime_tainted_banner_fps(std::unordered_set<std::string> fps) {
  std::unique_lock<std::shared_mutex> wlock(topology_mu());
  runtime_tainted_slot() = std::move(fps);
}

void ensure_runtime_node_fingerprints_loaded() {
  {
    std::shared_lock<std::shared_mutex> rlock(topology_mu());
    if (runtime_fps_slot().has_value() && runtime_tainted_slot().has_value()) {
      return;
    }
  }
  bool skip_disk = false;
  {
    std::lock_guard<std::mutex> lock(test_cfg_mu());
    skip_disk = g_disable_disk_persistence_for_test &&
                !g_test_verify_store_path.has_value();
  }
  std::unordered_map<std::string, std::string> map;
  std::unordered_set<std::string> tainted;
  if (!skip_disk) {
    const std::wstring cfg_path = dirs::clash_mini_path();
    std::ifstream f(cfg_path, std::ios::binary);
    if (f.is_open()) {
      std::ostringstream oss;
      oss << f.rdbuf();
      try {
        YAML::Node val = YAML::Load(oss.str());
        if (val && val.IsMap() && val["proxies"] && val["proxies"].IsSequence()) {
          std::vector<RawProxyEntry> raw_entries;
          for (const auto& item : val["proxies"]) {
            if (!item.IsMap()) continue;
            const YAML::Node name_n = static_cast<const YAML::Node&>(item)["name"];
            if (!name_n || !name_n.IsScalar() || name_n.IsNull()) continue;
            const std::string& name_s = name_n.Scalar();
            const YAML::Node srv_n = static_cast<const YAML::Node&>(item)["server"];
            if (srv_n && srv_n.IsScalar() && !srv_n.IsNull() &&
                !is_valid_node_server(srv_n.Scalar())) {
              continue;
            }
            auto fp = proxy_endpoint_fingerprint(item);
            if (fp.has_value()) map[name_s] = *fp;
            RawProxyEntry e;
            e.name = name_s;
            e.fingerprint = std::move(fp);
            e.is_dummy = is_dummy_node(name_s);
            raw_entries.push_back(std::move(e));
          }
          tainted = resolve_proxy_clone_topology(raw_entries).tainted_fps;
        }
      } catch (...) {
      }
    }
  }
  set_runtime_topology_if_none(std::move(map), std::move(tainted));
}

bool is_https_quarantined_fp(const std::string& fp) {
  ensure_verify_store_loaded();
  const int64_t now_ts = current_unix_ts();
  std::shared_lock<std::shared_mutex> rlock(verify_store_mu());
  const auto& slot = verify_store_slot();
  if (!slot.has_value()) return false;
  auto it = slot->records.find(fp);
  return it != slot->records.end() && it->second.is_confirmed_fake_at(now_ts);
}

bool is_confirmed_fake_fp(const std::string& fp) {
  return is_https_quarantined_fp(fp) || is_tainted_banner_fp(fp);
}

bool is_verified_true_fp(const std::string& fp) {
  ensure_verify_store_loaded();
  std::shared_lock<std::shared_mutex> rlock(verify_store_mu());
  const auto& slot = verify_store_slot();
  if (!slot.has_value()) return false;
  auto it = slot->records.find(fp);
  return it != slot->records.end() && it->second.verified_true;
}

bool is_confirmed_fake_node(const std::string& name) {
  auto fp = fingerprint_for_node(name);
  return fp.has_value() && is_confirmed_fake_fp(*fp);
}

bool is_verified_true_node(const std::string& name) {
  auto fp = fingerprint_for_node(name);
  return fp.has_value() && is_verified_true_fp(*fp);
}

bool record_has_verify_failure(bool verified_true, uint8_t fail_count,
                               int64_t last_verified_at, int64_t now_ts) {
  if (verified_true || fail_count < kHttpsVerifyMigrateMinFails) return false;
  if (last_verified_at <= 0) return true;
  const int64_t diff =
      (now_ts >= last_verified_at) ? (now_ts - last_verified_at) : 0;
  return diff < kHttpsVerifyBanTtlSecs;
}

bool has_verify_failure_node(const std::string& name) {
  auto fp = fingerprint_for_node(name);
  if (!fp.has_value()) return false;
  ensure_verify_store_loaded();
  const int64_t now_ts = current_unix_ts();
  std::shared_lock<std::shared_mutex> rlock(verify_store_mu());
  const auto& slot = verify_store_slot();
  if (!slot.has_value()) return false;
  auto it = slot->records.find(*fp);
  if (it == slot->records.end()) return false;
  return record_has_verify_failure(it->second.verified_true,
                                   it->second.fail_count,
                                   it->second.last_verified_at, now_ts);
}

std::vector<std::string> confirmed_fake_node_names() {
  ensure_runtime_node_fingerprints_loaded();
  std::vector<std::pair<std::string, std::string>> pairs;
  std::unordered_set<std::string> tainted_fps;
  {
    std::shared_lock<std::shared_mutex> rlock(topology_mu());
    if (runtime_fps_slot().has_value()) {
      for (const auto& [k, v] : *runtime_fps_slot()) {
        pairs.emplace_back(k, v);
      }
    }
    if (runtime_tainted_slot().has_value()) {
      tainted_fps = *runtime_tainted_slot();
    }
  }
  if (pairs.empty()) return {};

  ensure_verify_store_loaded();
  const int64_t now_ts = current_unix_ts();
  std::unordered_set<std::string> https_fake_fps;
  {
    std::shared_lock<std::shared_mutex> rlock(verify_store_mu());
    if (verify_store_slot().has_value()) {
      const auto& recs = verify_store_slot()->records;
      for (const auto& [name, fp] : pairs) {
        (void)name;
        if (tainted_fps.count(fp) > 0) continue;
        auto it = recs.find(fp);
        if (it != recs.end() && it->second.is_confirmed_fake_at(now_ts)) {
          https_fake_fps.insert(fp);
        }
      }
    }
  }

  std::vector<std::string> out;
  for (const auto& [name, fp] : pairs) {
    if (tainted_fps.count(fp) > 0 || https_fake_fps.count(fp) > 0) {
      out.push_back(name);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool has_pending_unverified_nodes(const std::vector<std::string>& candidates) {
  if (candidates.empty()) return false;
  ensure_runtime_node_fingerprints_loaded();
  std::vector<std::string> fps;
  std::unordered_set<std::string> tainted_fps;
  {
    std::shared_lock<std::shared_mutex> rlock(topology_mu());
    if (runtime_fps_slot().has_value()) {
      const auto& map = *runtime_fps_slot();
      for (const auto& name : candidates) {
        auto it = map.find(name);
        if (it != map.end()) fps.push_back(it->second);
      }
    }
    if (runtime_tainted_slot().has_value()) {
      tainted_fps = *runtime_tainted_slot();
    }
  }
  if (fps.empty()) return false;

  ensure_verify_store_loaded();
  const int64_t now_ts = current_unix_ts();
  std::shared_lock<std::shared_mutex> rlock(verify_store_mu());
  if (!verify_store_slot().has_value()) return true;
  const auto& recs = verify_store_slot()->records;
  for (const auto& fp : fps) {
    if (tainted_fps.count(fp) > 0) continue;
    auto it = recs.find(fp);
    if (it == recs.end() ||
        (!it->second.verified_true && !it->second.is_confirmed_fake_at(now_ts))) {
      return true;
    }
  }
  return false;
}

VerifyBatchOutcome apply_verify_batch_to_records(
    std::map<std::string, NodeVerifyRecord>& records,
    const std::vector<std::string>& passed_fps,
    const std::vector<std::string>& failed_fps, int64_t now_ts) {
  VerifyBatchOutcome outcome;
  std::unordered_set<std::string> passed_set;
  passed_set.reserve(passed_fps.size());

  for (const auto& fp : passed_fps) {
    if (!passed_set.insert(fp).second) continue;
    auto& rec = records[fp];
    if (!rec.verified_true) {
      rec.verified_true = true;
      ++outcome.newly_verified;
      outcome.dirty = true;
    }
    if (rec.fail_count != 0) {
      rec.fail_count = 0;
      outcome.dirty = true;
    }
    if (rec.last_verified_at != now_ts) {
      rec.last_verified_at = now_ts;
      outcome.dirty = true;
    }
  }

  if (passed_set.empty()) return outcome;

  std::unordered_set<std::string> seen_failed;
  seen_failed.reserve(failed_fps.size());
  for (const auto& fp : failed_fps) {
    if (passed_set.count(fp) > 0 || !seen_failed.insert(fp).second) continue;
    auto& rec = records[fp];
    if (rec.verified_true || rec.is_confirmed_fake_at(now_ts)) continue;
    if (rec.fail_count > kHttpsVerifyMaxFails) rec.fail_count = 0;
    if (rec.fail_count < 255) rec.fail_count = static_cast<uint8_t>(rec.fail_count + 1);
    rec.last_verified_at = now_ts;
    outcome.dirty = true;
    if (rec.is_confirmed_fake_at(now_ts)) ++outcome.newly_confirmed_fake;
  }
  return outcome;
}

VerifyBatchOutcome record_https_verify_results(
    const std::vector<std::string>& passed_names,
    const std::vector<std::string>& failed_names) {
  ensure_runtime_node_fingerprints_loaded();
  std::vector<std::string> passed_fps;
  std::vector<std::string> failed_fps;
  {
    std::shared_lock<std::shared_mutex> rlock(topology_mu());
    if (runtime_fps_slot().has_value()) {
      const auto& map = *runtime_fps_slot();
      for (const auto& n : passed_names) {
        if (auto it = map.find(n); it != map.end()) passed_fps.push_back(it->second);
      }
      for (const auto& n : failed_names) {
        if (auto it = map.find(n); it != map.end()) failed_fps.push_back(it->second);
      }
    }
  }
  if (passed_fps.empty() && failed_fps.empty()) return {};

  ensure_verify_store_loaded();
  const int64_t now_ts = current_unix_ts();
  VerifyBatchOutcome outcome;
  std::optional<NodeVerifyStoreData> snapshot;
  {
    std::unique_lock<std::shared_mutex> wlock(verify_store_mu());
    if (!verify_store_slot().has_value()) {
      verify_store_slot() = NodeVerifyStoreData{};
    }
    outcome = apply_verify_batch_to_records(verify_store_slot()->records,
                                            passed_fps, failed_fps, now_ts);
    if (outcome.dirty) snapshot = *verify_store_slot();
  }
  if (snapshot.has_value()) {
    persist_verify_store_snapshot(*snapshot);
  }
  return outcome;
}

size_t clear_unverified_https_failures() {
  ensure_verify_store_loaded();
  size_t cleared = 0;
  std::optional<NodeVerifyStoreData> snapshot;
  {
    std::unique_lock<std::shared_mutex> wlock(verify_store_mu());
    if (!verify_store_slot().has_value()) {
      verify_store_slot() = NodeVerifyStoreData{};
    }
    cleared = clear_unverified_failures_in_records(verify_store_slot()->records);
    if (cleared > 0) snapshot = *verify_store_slot();
  }
  if (snapshot.has_value()) {
    persist_verify_store_snapshot(*snapshot);
  }
  return cleared;
}

NodeVerifyStoreData load_verify_store_from_path(const std::wstring& path) {
  NodeVerifyStoreData data;
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return data;
  std::ostringstream oss;
  oss << f.rdbuf();
  nlohmann::json j = nlohmann::json::parse(oss.str(), nullptr, false);
  if (j.is_discarded() || !j.is_object() || !j.contains("records") ||
      !j["records"].is_object()) {
    return data;
  }
  for (auto it = j["records"].begin(); it != j["records"].end(); ++it) {
    if (!it.value().is_object()) continue;
    NodeVerifyRecord rec;
    rec.verified_true = it.value().value("verified_true", false);
    const int fc = it.value().value("fail_count", 0);
    rec.fail_count = static_cast<uint8_t>(std::clamp(fc, 0, 255));
    rec.last_verified_at = it.value().value("last_verified_at", int64_t{0});
    data.records.emplace(it.key(), rec);
  }
  return data;
}

bool save_verify_store_to_path(const std::wstring& path,
                               const NodeVerifyStoreData& data) {
  std::error_code ec;
  std::filesystem::path p(path);
  if (p.has_parent_path()) {
    std::filesystem::create_directories(p.parent_path(), ec);
  }
  nlohmann::json recs = nlohmann::json::object();
  for (const auto& [fp, rec] : data.records) {
    recs[fp] = nlohmann::json{
        {"verified_true", rec.verified_true},
        {"fail_count", rec.fail_count},
        {"last_verified_at", rec.last_verified_at},
    };
  }
  nlohmann::json root = nlohmann::json{{"records", std::move(recs)}};
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return false;
  out << root.dump(2);
  return out.good();
}

void reset_verify_state_for_test(bool disable_disk_persistence) {
  {
    std::lock_guard<std::mutex> lock(test_cfg_mu());
    g_disable_disk_persistence_for_test = disable_disk_persistence;
    g_test_verify_store_path.reset();
  }
  {
    std::unique_lock<std::shared_mutex> wlock(verify_store_mu());
    verify_store_slot().reset();
  }
  {
    std::unique_lock<std::shared_mutex> wlock(topology_mu());
    runtime_fps_slot().reset();
    runtime_tainted_slot().reset();
  }
}

void set_verify_store_path_for_test(const std::optional<std::wstring>& path) {
  {
    std::lock_guard<std::mutex> lock(test_cfg_mu());
    g_test_verify_store_path = path;
  }
  std::unique_lock<std::shared_mutex> wlock(verify_store_mu());
  verify_store_slot().reset();
}

void set_verify_record_for_test(const std::string& fp,
                                const std::optional<NodeVerifyRecord>& rec) {
  ensure_verify_store_loaded();
  std::unique_lock<std::shared_mutex> wlock(verify_store_mu());
  if (!verify_store_slot().has_value()) {
    verify_store_slot() = NodeVerifyStoreData{};
  }
  if (rec.has_value()) {
    verify_store_slot()->records[fp] = *rec;
  } else {
    verify_store_slot()->records.erase(fp);
  }
}

}  // namespace clashwin::util
