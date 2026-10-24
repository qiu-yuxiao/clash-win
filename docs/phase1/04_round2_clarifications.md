# 二轮侦查收口报告（澄清与修正）

> 2026-10-24 · scout-frontend / scout-backend 二轮只读侦查 · 本文件对 01/02 报告中的"未确认项"做最终裁定与补充，**冲突处以本文件为准**。

## 1. 增强流量图裁定：活跃代码，P0（修正"未确认"）

- 唯一引用方：`src/pages/_layout/components/mini-traffic-panel.tsx:6,62`。MiniTrafficPanel 是主窗口底部常驻流量面板。
- 渲染分支（mini-traffic-panel.tsx:59-63）：`isMinimalWidth ? <TrafficGraph/> : <EnhancedCanvasTrafficGraph/>`——内容宽 >285px（MINI_WIDTH_THRESHOLD，src/constants.ts:32）挂增强图；≤285 退简易 Canvas 图。
- 数据源同一份：useTrafficData（mini-traffic-panel.tsx:27）。
- 结论：宽窗默认形态即增强图；use-traffic-monitor/traffic.worker 的 Worker 采样链路（10min 原始 + 60min 压缩）必须完整复刻。

## 2. 前端 WS 通道：Tauri Channel 回调（修正"事件名未确认"）

- `crates/tauri-plugin-mihomo/guest-js/index.ts:421-436 openWebSocketCommand`：创建 `Channel<RawTextChannelMessage>`，随 invoke 传入；:398-409 normalize 为 `{type:'Text', data:string}`（ArrayBuffer/Uint8Array 用 TextDecoder 解码为字符串）。
- 订阅侧：`src/hooks/use-mihomo-ws-subscription.ts:107-113`，owner 收到**原始 JSON 字符串**（含 "Websocket error" 前缀错误约定，consumer 手工判断）。
- 载荷结构：
  - traffic（use-traffic-data.ts:54-66）：`{up, down, upTotal?, downTotal?}`，50ms 同签名去重，1s 节流。
  - connections（use-connection-data.ts:58-207）：`snapshot = {type:'snapshot', data:{epochId, sequenceId, uploadTotal, downloadTotal, connections[]}}`；`delta = {type:'delta', data:{epochId, sequenceId, uploadTotal, downloadTotal, added[], removed[], updated:[id,up,down] 扁平数组步长3}}`；epochId≠当前或 sequenceId≠last+1 → 全量 resync；关闭连接保留上限 500。
  - logs（use-log-data.ts:93-129）：`ILogItem[]` 或单条 `{type, payload}`；连接成功后 getClashLogs 拉历史；50ms flush 合批，上限 1000 条。
  - memory 通道：Mini 前端无 consumer（预留/上游遗留，移植可降级 P2）。

## 3. 内核升级进度事件

- 事件名 `"core-upgrade-progress"`（core_updater.rs:280、:295）。payload（:28-33）：`{status: String, progress: u32, message: String}`。
- status 序列：`checking(5)` → `downloading(10~84)` → `verifying(85)` → `extracting(90)` → `done(100)`；失败 `error(0)`。

## 4. 深链 scheme（scheme.rs 全 156 行）

- 接受 `clash://` / `clash-verge://` / `clash-mini://`（:42）；仅一个动作：导入订阅。
- 参数：`name=`（可选）、`url=`（:54-60，手工 find 前缀容忍嵌套 URL）；url 防双解码（最多 2 次 percent-decode，能 parse 即停，:62-80）。
- 流程：`PrfItem::from_url` 下载 → `profiles_append_item_safe` → apply+save → notice → refresh_verge + notify_profile_changed；仅当"此前无 current 且新项成为 current"时 `update_config_forced()`（:121-135）。入口串以 `[...]` 包裹先剥外层（:14-20）。

## 5. sysproxy 真实实现（sysproxy-rs rev f0775f6，windows.rs）

- 读：winreg 直读 `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Internet Settings` 的 ProxyEnable/ProxyServer（兼容多协议与单一格式）/ProxyOverride；PAC 读 AutoConfigURL（:236-243）。
- 写：**不用注册表写**，用 `InternetSetOptionW(INTERNET_OPTION_PER_CONNECTION_OPTION, ...)`（:154-166）：全局代理 = FLAGS `PROXY_TYPE_PROXY|PROXY_TYPE_DIRECT` + PROXY_SERVER + PROXY_BYPASS；PAC = FLAGS `PROXY_TYPE_AUTO_DETECT|PROXY_TYPE_AUTO_PROXY_URL|PROXY_TYPE_DIRECT` + AUTOCONFIG_URL；关闭 = FLAGS `PROXY_TYPE_DIRECT`。对 LAN（pszConnection=null）与每个 RAS/VPN 拨号连接（RasEnumEntriesW，:287-359）各应用一次。
- 广播（:168-176）：`INTERNET_OPTION_PROXY_SETTINGS_CHANGED` + `INTERNET_OPTION_REFRESH`。
- C++ 移植指令：**原样复刻 PER_CONN_OPTION 方案（含 RAS 枚举）**，不得简化为直写 ProxyEnable/ProxyServer。

## 6. clash-verge-service-ipc 协议（rev 21e661f，v2.3.0 client）

- 管道：`\\.\pipe\clash-verge-service`（lib.rs:28）；鉴权头 `X-IPC-Magic`，值 = `A thing of beauty is a joy for ever. Its loveliness increases; it will never pass into nothingness.`（lib.rs:36）。
- 协议：HTTP/1.1 over 命名管道（kode_bridge::IpcHttpClient），body JSON。
- 端点：GET /version、GET /status、GET /clash/logs、POST /clash/start、DELETE /clash/stop、PUT /writer、GET /magic（探测）。
- payload：`ClashConfig{core_config:{core_path, core_ipc_path, config_path, config_dir}, log_config:{directory, max_log_size, max_log_files}}`；响应 `{code: u16, message, data?}`，**code>0 即失败**。
- C++ 移植指令：与 mihomo 管道共用同一套"HTTP over named pipe"客户端实现。

## 7. quit() 退出序列（feat/window.rs:57-76）

```
quit():
  1. prepare_exit()                     // :10-45
     ├─ Handle::set_is_exiting()
     ├─ monitor::abort_monitor() + lightweight::abort_lightweight_cleanup()
     ├─ resolve::abort_startup_tasks()
     ├─ MONITOR_WAKEUP_NOTIFY.notify_one() + PROFILE_SWITCH_NOTIFY.notify_waiters()
     ├─ Timer::global().shutdown()
     ├─ mihomo.clear_all_ws_connections()
     ├─ Logger::flush()
     ├─ sleep 200ms
     ├─ server::shutdown_embedded_server()   // 关 33335
     └─ Config::apply_all_and_save_file()    // clash/verge/profiles 三份落盘
  2. clean_async()                      // :78-171，两任务并行：
     proxy_task: enable_system_proxy 开启时 reset_sysproxy()（超时 1500ms）
     core_task : enable_tun_mode 开启时 PATCH /configs {"tun":{"enable":false}}（超时 1000ms）
                 → stop_core()（超时 2s）→ kill_all_mini_cores()
  3. Logger::global().shutdown()
  4. app_handle.exit(cleanup_result ? 0 : 1)   // 返回值=代理与内核清理双成功与否
```
- restart_app()（feat/clash.rs:33-52）同构，结尾 `app_handle.restart()`。

## 8. 用户已裁定的 8 项决策（2026-10-24）

1. JS 合并脚本引擎：**嵌 QuickJS 全量移植**。
2. Monaco 替代：**首版自绘只读文本框 + "用系统编辑器打开"按钮**。
3. 13 语言：**全量保留**（zh/en 优先精确）。
4. 轻量模式等价实现：**隐藏窗口 + 释放 D2D 资源 + 断开 WS 订阅**。
5. 客户端自更新：**移植**（保留内核热升级 + 客户端自更新）。
6. 增强流量图：**P0**（本文件 §1）。
7. P2 六项疑似死代码：**不移植**，留档备查。
8. 五盲区：**已收口**（本文件 §3-§7）。
