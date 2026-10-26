// 连接数据管道实现（T14）：状态机逐行对照 use-connection-data.ts:57-215。
#include "connection_data.h"

#include <windows.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "util/log.h"

namespace clashwin::ui::scene {

namespace {
int64_t default_now_ms() { return static_cast<int64_t>(GetTickCount64()); }

// ← connection-table.tsx chains cell 三分类
std::string chains_category(const nlohmann::json& conn) {
  auto it = conn.find("chains");
  if (it == conn.end() || !it->is_array() || it->empty())
    return "Direct";  // chainsDirect
  for (const auto& c : *it) {
    std::string upper = c.get<std::string>();
    for (auto& ch : upper) ch = static_cast<char>(std::toupper((unsigned char)ch));
    if (upper == "REJECT") return "Reject";
  }
  for (const auto& c : *it) {
    std::string upper = c.get<std::string>();
    for (auto& ch : upper) ch = static_cast<char>(std::toupper((unsigned char)ch));
    if (upper == "DIRECT") return "Direct";
  }
  return "Proxy";
}

std::string host_of(const nlohmann::json& conn) {
  const auto& m = conn.contains("metadata") && conn["metadata"].is_object()
                      ? conn["metadata"]
                      : nlohmann::json::object();
  std::string host = m.value("host", "");
  std::string port = m.value("destinationPort", "");
  if (host.empty()) host = m.value("remoteDestination", "");
  return port.empty() ? host : host + ":" + port;
}

ConnRow parse_conn(const nlohmann::json& conn) {
  ConnRow row;
  row.id = conn.value("id", "");
  row.host = host_of(conn);
  row.chains_cat = chains_category(conn);
  row.network = conn.value("network", "");
  row.upload = static_cast<double>(conn.value("upload", 0.0));
  row.download = static_cast<double>(conn.value("download", 0.0));
  row.cur_upload = 0;
  row.cur_download = 0;
  return row;
}
}  // namespace

bool ConnectionData::consume(const std::string& payload_text) {
  try {
    return consume_json(nlohmann::json::parse(payload_text));
  } catch (const std::exception& e) {
    CWLOG_WARN(std::string("ConnectionData: payload 解析失败: ") + e.what());
    return false;
  }
}

bool ConnectionData::consume_json(const nlohmann::json& msg) {
  if (!msg.is_object() || !msg.contains("type")) return false;
  const std::string type = msg.value("type", "");

  if (type == "snapshot") {
    // ← :63-77：全量重建
    const auto& data = msg.contains("data") && msg["data"].is_object()
                           ? msg["data"]
                           : nlohmann::json::object();
    epoch_id_ = data.value("epochId", "");
    last_seq_ = data.value("sequenceId", static_cast<int64_t>(-1));
    upload_total_ = static_cast<double>(data.value("uploadTotal", 0.0));
    download_total_ = static_cast<double>(data.value("downloadTotal", 0.0));
    active_.clear();
    if (data.contains("connections") && data["connections"].is_array()) {
      active_.reserve(data["connections"].size());
      for (const auto& c : data["connections"]) active_.push_back(parse_conn(c));
    }
    notify_throttled();
    return true;
  }

  if (type == "delta") {
    const auto& d = msg.contains("data") && msg["data"].is_object()
                        ? msg["data"]
                        : nlohmann::json::object();
    const std::string epoch = d.value("epochId", "");
    int64_t seq = d.value("sequenceId", static_cast<int64_t>(-1));

    // ← :87-98：epoch 不等（字符串比较）或 seq != last+1 → resync
    if (epoch != epoch_id_ || seq != last_seq_ + 1) {
      CWLOG_WARN("ConnectionData: epoch/seq 断档 → resync（等下一 snapshot）");
      epoch_id_.clear();
      last_seq_ = -1;
      if (on_resync) on_resync();
      return false;  // return old
    }
    last_seq_ = seq;

    upload_total_ = static_cast<double>(d.value("uploadTotal", 0.0));
    download_total_ = static_cast<double>(d.value("downloadTotal", 0.0));

    // Map 重建（← :101-107）
    std::unordered_map<std::string, ConnRow> active_map;
    active_map.reserve(active_.size() * 2);
    for (const auto& c : active_) active_map[c.id] = c;

    // 1. Removals → closed 历史（← :110-120）
    std::vector<ConnRow> dropped;
    if (d.contains("removed") && d["removed"].is_array()) {
      for (const auto& id : d["removed"]) {
        std::string key = id.get<std::string>();
        auto it = active_map.find(key);
        if (it != active_map.end()) {
          dropped.push_back(it->second);
          active_map.erase(it);
        }
      }
    }

    // 2. Updates（扁平 [id,up,down]×N；cur = total - prev）（← :122-145）
    std::unordered_set<std::string> updated_set;
    if (d.contains("updated") && d["updated"].is_array()) {
      const auto& updated = d["updated"];
      for (size_t i = 0; i + 2 < updated.size(); i += 3) {
        std::string id = updated[i].get<std::string>();
        double up = updated[i + 1].get<double>();
        double down = updated[i + 2].get<double>();
        auto it = active_map.find(id);
        if (it != active_map.end()) {
          it->second.cur_upload = up - it->second.upload;
          it->second.cur_download = down - it->second.download;
          it->second.upload = up;
          it->second.download = down;
          updated_set.insert(id);
        }
      }
    }
    // 未 updated 的连接 cur 清 0（← :146-157）
    for (auto& c : active_) {
      if (!updated_set.count(c.id)) {
        c.cur_upload = 0;
        c.cur_download = 0;
      }
    }

    // 3. Additions（← :159-170）
    if (d.contains("added") && d["added"].is_array()) {
      for (const auto& c : d["added"]) {
        ConnRow row = parse_conn(c);
        active_map[row.id] = row;
      }
    }

    // 4. 顺序稳定重建：旧序优先 + 新增尾随（← :172-194）
    std::vector<ConnRow> next;
    next.reserve(active_map.size());
    for (const auto& prev : active_) {
      auto it = active_map.find(prev.id);
      if (it != active_map.end()) {
        next.push_back(it->second);
        active_map.erase(it);
      }
    }
    for (auto& [id, c] : active_map) next.push_back(c);
    active_ = std::move(next);

    // 5. closed 历史合并（上限 500 FIFO ← :196-209；裁掉最旧保持 ≤500）
    for (auto& c : dropped) closed_.push_back(c);
    while (closed_.size() > kMaxClosedConns) closed_.pop_front();

    notify_throttled();
    return true;
  }
  return false;
}

void ConnectionData::sort_by_traffic() {
  std::stable_sort(active_.begin(), active_.end(),
                   [](const ConnRow& a, const ConnRow& b) {
                     return a.upload + a.download > b.upload + b.download;
                   });
}

void ConnectionData::notify_throttled() {
  int64_t now = now_ms_ ? now_ms_() : default_now_ms();
  if (last_notify_ms_ < 0 || now - last_notify_ms_ >= throttle_ms_) {
    last_notify_ms_ = now;
    notify_pending_ = false;
    notify_now();
  } else {
    notify_pending_ = true;  // 窗口内合并（后缘只保留最新数据态）
  }
}

void ConnectionData::flush_notify_for_test() {
  last_notify_ms_ = -1;
  notify_pending_ = false;
  notify_now();
}

uint64_t ConnectionData::subscribe(std::function<void()> cb) {
  uint64_t id = next_id_++;
  subs_.emplace_back(id, std::move(cb));
  return id;
}

void ConnectionData::unsubscribe(uint64_t id) {
  subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
                             [id](const auto& p) { return p.first == id; }),
              subs_.end());
}

}  // namespace clashwin::ui::scene
