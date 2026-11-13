#pragma once
// 连接数据管道（T14）：消费 T05 WsManager connections 信封。
// 状态机逐行对照 src/hooks/use-connection-data.ts:57-215（只读参考）：
// - snapshot → 全量重建（curUpload/curDownload 置 0）
// - delta：epochId 不等（**字符串比较**，16hex 恒定）或 sequenceId != last+1
//   → 重置计数 + resync 回调（原版= scheduleReconnect 重连 WS 等下一个
//   snapshot 帧；**无显式"请求全量"机制**——本层同语义，仅通知宿主）
// - delta.updated 扁平 [id,up,down]×N：cur = total - 上次 total（增量速率）
// - 未在 updated 中的连接 cur 清 0；added 追加；removed → closed 历史
// - closed 历史上限 500（FIFO 裁剪 ← MAX_CLOSED_CONNS_NUM）
// - uploadTotal/downloadTotal 取自 delta/snapshot
// 节流：消费即更新数据，UI 通知按 throttle_ms 合帧（可配置；原版 connections
// 订阅 throttleMs=1000，任务卡口径 300ms，默认取 300 可调）
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/quick_routing.h"

namespace clashwin::ui::scene {

inline constexpr size_t kMaxClosedConns = 500;  // ← MAX_CLOSED_CONNS_NUM

struct ConnMetadata {
  std::string network;          // tcp/udp
  std::string type;             // HTTPS, etc.
  std::string host;
  std::string source_ip;
  std::string source_port;
  std::string destination_port;
  std::string destination_ip;
  std::string remote_destination;
  std::string process;
  std::string process_path;
};

enum class PathControlStatus {
  Direct,
  Proxy,
  Reject,
  Disconnected,
};

struct ConnItem {
  std::string id;
  std::string host;        // host:port（host 空 → remoteDestination）
  std::string chains_cat;  // "Proxy"/"Direct"/"Reject"（← connection-table chains 三分类）
  std::string network;     // tcp/udp（协议色点判别用）
  ConnMetadata metadata;
  double upload = 0, download = 0;
  double cur_upload = 0, cur_download = 0;  // 本帧增量速率
  std::string start;
  std::vector<std::string> chains;
  std::string rule;
  std::string rule_payload;

  // v3.3.0 路径控制扩展字段
  PathControlStatus control_status = PathControlStatus::Proxy;
  bool is_synthetic = false;
  std::optional<core::quick_routing::ManagedMergeRule> managed_rule;
  std::optional<core::quick_routing::RuleType> target_type;
  std::string target_value;
  std::string target_key;
  std::string disconnected_key;

  std::string host_formatted() const;
  std::string chains_category() const;
  std::string chains_formatted() const;
  std::string rule_formatted() const;
  std::string process_formatted() const;
};

using ConnRow = ConnItem;

class ConnectionData {
 public:
  // resync 通知（epoch 变化/seq 断档）：宿主应触发底层 WS 重连（原版
  // scheduleReconnect 语义）；数据保持旧态等待下一个 snapshot
  std::function<void()> on_resync;

  explicit ConnectionData(int throttle_ms = 300) : throttle_ms_(throttle_ms) {}

  // ---- 消费入口（WsHandler payload 文本原样喂入；线程安全：调用方保证
  //      单线程或经 UiPostbox 转投 UI 线程） ----
  // 返回 true = 数据有变化（节流窗口内的帧也更新内部态，仅抑制通知）
  bool consume(const std::string& payload_text);
  bool consume_json(const nlohmann::json& msg);

  // ---- 读取 ----
  const std::vector<ConnRow>& active() const { return active_; }
  const std::deque<ConnRow>& closed() const { return closed_; }
  double upload_total() const { return upload_total_; }
  double download_total() const { return download_total_; }
  size_t active_count() const { return active_.size(); }

  // 视口排序：按 upload+download 降序（重排 active_ 的展示序；稳定）
  void sort_by_traffic();
  // 清空 closed 历史（← clearClosedConnections）
  void clear_closed() { closed_.clear(); }

  // UI 通知（节流合并：窗口内多次变化只触发一次；now_ms 注入时钟）
  uint64_t subscribe(std::function<void()> cb);
  void unsubscribe(uint64_t id);
  void set_now_ms(std::function<int64_t()> f) { now_ms_ = std::move(f); }
  void flush_notify_for_test();  // 测试：跳过节流窗立即通知

  int throttle_ms() const { return throttle_ms_; }

 private:
  void notify_throttled();
  void notify_now() {
    for (auto& [id, cb] : subs_) cb();
  }

  int throttle_ms_;
  std::string epoch_id_;      // 恒定 16hex；字符串比较判 resync
  int64_t last_seq_ = -1;     // 从 0 每帧 +1
  std::vector<ConnRow> active_;
  std::deque<ConnRow> closed_;
  double upload_total_ = 0, download_total_ = 0;

  std::vector<std::pair<uint64_t, std::function<void()>>> subs_;
  std::function<int64_t()> now_ms_;  // 缺省 GetTickCount64（见 .cpp）
  int64_t last_notify_ms_ = -1;
  bool notify_pending_ = false;
  uint64_t next_id_ = 1;
};

}  // namespace clashwin::ui::scene
