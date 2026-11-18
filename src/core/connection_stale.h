#pragma once
// BUG-396（Mini v3.5.3 commit a38b908f）：前台切点后精准清理旧节点存量连接 +
// 连接详情"代理节点"行判定。纯逻辑 header-only，便于单测且无副作用。
//
// src（Mini 权威）：
//   src-tauri/src/module/monitor.rs:219-239  should_close_stale_proxy_connection
//   src-tauri/src/module/monitor.rs:241-269  close_stale_proxy_connections
//   src/utils/path-control.ts:694-711         shouldCloseStaleProxyConnection
//   src/utils/path-control.ts:722-768         resolveConnectionNodeDisplay
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "mihomo/mihomo_client.h"

namespace clashwin::core {

namespace detail {
inline std::string trim_copy(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}
inline std::string ascii_upper(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return out;
}
}  // namespace detail

// 判断经过 group_name 代理组、但仍挂在非 target_node 出口上的连接是否应断开。
// 与 Mini Rust 后端逐语义对齐：target/group 先 trim；内置目标（DIRECT/REJECT/GLOBAL）
// 受保护绝不清理；chains 用精确（大小写敏感）相等匹配组名与目标节点。
inline bool should_close_stale_proxy_connection(
    const std::vector<std::string>& chains, const std::string& group_name,
    const std::string& target_node) {
  const std::string target = detail::trim_copy(target_node);
  const std::string group = detail::trim_copy(group_name);
  if (target.empty() || group.empty()) return false;
  const std::string upper_target = detail::ascii_upper(target);
  if (upper_target == "DIRECT" || upper_target == "REJECT" ||
      upper_target == "GLOBAL") {
    return false;
  }
  bool has_group = false;
  bool has_target = false;
  for (const auto& c : chains) {
    if (c == group) has_group = true;
    if (c == target) has_target = true;
  }
  return has_group && !has_target;
}

// 连接详情弹窗"代理节点"行的解析结果（← path-control.ts ConnectionNodeDisplay）。
struct ConnectionNodeDisplay {
  enum class Kind { None, Direct, Reject, Proxy };
  Kind kind = Kind::None;
  std::string node_name = "--";
  bool is_stale_proxy_node = false;
};

inline ConnectionNodeDisplay resolve_connection_node_display(
    const std::vector<std::string>& chains, bool is_synthetic, bool is_closed,
    const std::string& current_proxy_now) {
  ConnectionNodeDisplay out;
  if (chains.empty()) return out;
  const std::string leaf = detail::trim_copy(chains.front());
  const std::string upper_leaf = detail::ascii_upper(leaf);
  if (leaf.empty() || upper_leaf == "DISCONNECTED") return out;
  if (upper_leaf == "DIRECT") {
    out.kind = ConnectionNodeDisplay::Kind::Direct;
    out.node_name = "DIRECT";
    return out;
  }
  if (upper_leaf == "REJECT" || upper_leaf == "REJECT-DROP") {
    out.kind = ConnectionNodeDisplay::Kind::Reject;
    out.node_name = leaf;
    return out;
  }
  if (is_synthetic || upper_leaf == "PROXY" || upper_leaf == "GLOBAL") {
    return out;
  }
  const std::string active = detail::trim_copy(current_proxy_now);
  const std::string upper_active = detail::ascii_upper(active);
  const bool stale =
      !is_closed && !active.empty() && upper_active != "DIRECT" &&
      upper_active != "REJECT" && upper_active != "GLOBAL" &&
      upper_active != "PROXY" && leaf != active;
  out.kind = ConnectionNodeDisplay::Kind::Proxy;
  out.node_name = leaf;
  out.is_stale_proxy_node = stale;
  return out;
}

// 前台显式切点成功后，精准关闭仍挂在非目标节点的 PROXY 存量长连接。
// 后台自愈/冷启动严禁调用本函数（保持零断流热切换）。
// 返回实际发起关闭的连接条数（← mihomo-api.ts closeStaleProxyConnections）。
inline int close_stale_proxy_connections(mihomo::MihomoClient& client,
                                         const std::string& target_node,
                                         const std::string& group_name = "PROXY") {
  auto snapshot = client.get_connections();
  if (!snapshot.is_ok()) return 0;
  std::vector<std::string> stale_ids;
  for (const auto& conn : snapshot.value().connections) {
    if (should_close_stale_proxy_connection(conn.chains, group_name,
                                            target_node)) {
      stale_ids.push_back(conn.id);
    }
  }
  int closed = 0;
  for (const auto& id : stale_ids) {
    if (client.close_connection(id).is_ok()) ++closed;
  }
  return closed;
}

}  // namespace clashwin::core
