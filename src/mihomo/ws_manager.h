#pragma once
// T05 WS 四通道订阅管理（← use-mihomo-ws-subscription.ts sharedSubscriptions 语义
// + mihomo.rs ws_traffic/ws_memory/ws_connections/ws_logs）。
//
// - 引用计数：同通道多个订阅者共享一条底层 WS（shared subscription）；最后一个
//   订阅者退订后关闭底层连接
// - 断线自动重连：对齐原版 RECONNECT_DELAY_MS=1000（原版为固定 1s 间隔轮询，
//   非指数退避；见 use-mihomo-ws-subscription.ts:16）
// - 消息分发：原始 JSON 字符串投递给通道全部订阅者（回调风格，T09 接 EventSink）
// - disconnect_all()：立即关闭全部底层 WS（订阅保留，管理线程 1s 后重连）——
//   与原版 clear_all_ws_connections + 前端 scheduleReconnect 组合行为一致；
//   内核侧 REST DELETE /connections 归 MihomoClient，此处不做
// - 线程模型：每通道一个管理线程（连接/重连守护）+ WS 读线程（T04 PipeWebSocket）
// - 回调在 WS 读线程上下文执行，订阅者须自行保证线程安全
#include "clashwin/mihomo/models.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace clashwin::mihomo {

using WsHandler = std::function<void(const std::string& payload)>;

class WsManager {
 public:
  WsManager();
  ~WsManager();
  WsManager(const WsManager&) = delete;
  WsManager& operator=(const WsManager&) = delete;

  // 目标切换：关闭现有全部 WS，管理线程以新参数重连（对齐 update_socket_path 语义）
  void configure(const std::wstring& pipe_name, const std::string& secret);

  // 订阅；返回订阅 id（全通道唯一）。同通道重复订阅共享同一条 WS。
  uint64_t subscribe_traffic(WsHandler handler);
  uint64_t subscribe_memory(WsHandler handler);
  uint64_t subscribe_connections(WsHandler handler);
  uint64_t subscribe_logs(LogLevel level, WsHandler handler);
  // 退订（任意通道通用）；通道订阅归零后底层 WS 随之关闭
  void unsubscribe(uint64_t sub_id);

  // ws_disconnect 语义：立即断开全部底层 WS；订阅保留、自动重连
  void disconnect_all();

  // 观测（测试用）：通道当前是否有活跃底层连接
  bool channel_connected(const std::string& channel_key);

  static constexpr const char* kTraffic = "traffic";
  static constexpr const char* kMemory = "memory";
  static constexpr const char* kConnections = "connections";
  static std::string logs_key(LogLevel level) {
    return std::string("logs:") + log_level_to_str(level);
  }

 private:
  struct Channel;

  Channel& channel(const std::string& key, const std::string& ws_path);
  uint64_t subscribe_generic(const std::string& key, const std::string& ws_path,
                             WsHandler handler);
  static void worker_loop(Channel* ch, WsManager* self, std::string ws_path);

  std::mutex mu_;
  std::map<std::string, std::unique_ptr<Channel>> channels_;
  std::wstring pipe_name_;
  std::string secret_;
  std::atomic<uint64_t> next_sub_id_{1};
  std::atomic<bool> stopping_{false};
};

}  // namespace clashwin::mihomo
