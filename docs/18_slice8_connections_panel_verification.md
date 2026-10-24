# Phase 4 Slice 8 验收报告：右键分流 / 路径控制与全量连接面板 (Connections Panel)

## 1. 架构目标达成概述
对应源仓库 `ClashVerge` 中的活跃连接面板、右键快捷分流与路径控制能力，我们在 `ClashWin` 中完成了纯 C++20 原生的高性能连接管理器 `ConnectionManager` 与 Win32 交互界面对接。

彻底取代了此前的假数据模拟，实现了从内核真实 REST API (`GET /connections`, `DELETE /connections/{id}`, `DELETE /connections`) 读取全量 TCP/UDP 会话、实时解析上传/下载即时速率及链路径、支持在宽窗状态下对任意活跃连接右键调起快捷分流菜单，一键注入最高优先级规则至 `Merge.yaml` 并即时断开旧会话。

---

## 2. 核心组件与实现矩阵

### 2.1 全量活跃连接监控 (`ConnectionManager::Refresh` & `ParseConnectionsJson`)
- **内核 REST API 轮询**：每 1500ms（对齐 `CONN_POLL_INTERVAL_MS`）自动异步轮询 `GET /connections`。
- **全要素字段提取**：
  - `id`: 连接全局唯一 ID。
  - `metadata`: `network` (tcp/udp), `type` (HTTP/HTTPS/Socks5), `sourceIP`, `destinationIP`, `sourcePort`, `destinationPort`, `host`, `process`, `processPath`。
  - `upload`, `download`: 累计上传/下载字节。
  - `curUpload`, `curDownload`: 即时上传/下载速率 (B/s)。
  - `chains`: 完整出站节点链 (例如 `["PROXY", "HK-Node-01"]`)。
  - `rule`, `rulePayload`: 命中规则名称与匹配载荷。
- **全局速率聚合与底部流量栏联动**：实时计算全局下载与上传即时速率，同步更新 `MainWindow` 底部科学流量计。

### 2.2 右键分流一键注入最高优先级规则 (`ConnectionManager::RouteConnection`)
- **交互流程**：
  1. 用户在宽窗模式右侧连接列表中，鼠标右键点击任意连接行。
  2. 命中测试检测点击坐标落在 `m_connectionItemRects` 对应条目。
  3. 弹出原生 Win32 上下文菜单：
     - 🌐 直连此域名 (DIRECT)
     - ⚡ 代理此域名 (PROXY)
     - 🚫 拒绝此域名 (REJECT)
     - ---------------------
     - 🎯 直连此 IP (DIRECT)
     - ⚡ 代理此 IP (PROXY)
     - ---------------------
     - ❌ 断开此连接 (Close)
     - 🛑 断开全部连接 (Close All)
  4. 选定后，`ConnectionManager::RouteConnection` 自动生成对应的 `DOMAIN-SUFFIX` 或 `IP-CIDR` 规则，调用 `EnhanceChain::InjectRule` 插入到 `Merge.yaml` 的 `prepend-rules` 首位并持久化。
  5. 立即对该连接发送 `DELETE /connections/{id}` 关闭旧套接字，确保随后的网络请求立即走新配置的规则分流。

### 2.3 一键断开全部连接 (`CloseAllConnections`)
- 支持在右侧面板顶部点击 `✕ 断开所有连接` 3D 按钮，或在托盘/右键菜单中选择断开全部。
- 原生调用 `DELETE /connections`，瞬间释放所有后台残留连接，并重置即时流量计。

---

## 3. 测试验证与结果

创建并执行独立单元与集成测试 `test_slice8_connections_panel.exe`：
- `TestConnectionsJsonParsing`：验证真实结构 JSON 解析、元数据提取、链列表与速率聚合，**100% 通过**。
- `TestRightClickRouting`：验证域名与 IP 右键规则注入、`Merge.yaml` 顶层写入与连接自闭，**100% 通过**。
- `TestCloseAll`：验证一键断开全部连接及状态清零，**100% 通过**。

```
==================================================
  ClashWin Phase 4 Slice 8 - Connections Panel Test
==================================================
[Test 1] Testing /connections JSON parser...
  -> /connections JSON parsing passed!
[Test 2] Testing Right-click Rule Injection & Routing...
  -> Right-click Routing & Rule Injection passed!
[Test 3] Testing CloseAllConnections...
  -> CloseAllConnections passed!

>>> [ALL SLICE 8 TESTS PASSED SUCCESSFULLY!] <<<
```

`ClashWin.exe` 主工程无警告完成全编译与链接！
Slice 8 任务全量交付完毕！
