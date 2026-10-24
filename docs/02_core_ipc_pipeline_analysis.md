# 📑 第二步成果物：Clash Mini 内核管理与 IPC 通信全链路精读逆向报告

> **所属阶段**：阶段一：源系统全景解构与功能拓扑清单（Source Deconstruction & Functional Inventory）  
> **对应清单项**：`1.1.2 内核管理与通信链路（Core & IPC）`  
> **逆向参考目标（只读）**：[`c:\Users\sun_y\Documents\AntiGravity_Projects\ClashVerge`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge)  
> **文档定位**：全面解构 Mihomo 内核进程定位、参数规范、Go 运行时压制、Job Object 防孤儿机制、Windows 命名管道与 REST/WebSocket IPC 通讯协议，为后续 C++20 原生复刻提供绝对准确的技术依据。

---

## 目录
1. [一、内核文件定位与版本规范](#一一内核文件定位与版本规范)
2. [二、内核启动参数与 Go Runtime 深度压制](#二二内核启动参数与-go-runtime-深度压制)
3. [三、孤儿清理与 Win32 Job Object 绝对防线](#三三孤儿清理与-win32-job-object-绝对防线)
4. [四、Windows 命名管道（Named Pipe）与退避重试规范](#四四windows-命名管道named-pipe与退避重试规范)
5. [五、REST 控制器 API 与 Secret 安全握手](#五五rest-控制器-api-与-secret-安全握手)
6. [六、WebSocket 长连接流式推送协议](#六六websocket-长连接流式推送协议)
7. [七、内核重启与 PROXY 节点快照/恢复机制（Snapshot & Restore）](#七七内核重启与-proxy-节点快照恢复机制snapshot--restore)
8. [八、C++20 原生移植映射方案](#八八c20-原生移植映射方案)

---

## 一、内核文件定位与版本规范

### 1.1 源码位置
- [`src-tauri/src/core/manager/state.rs`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src-tauri/src/core/manager/state.rs) L33–L65
- [`src-tauri/src/core/core_updater.rs`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src-tauri/src/core/core_updater.rs)

### 1.2 优先级定位策略
系统启动内核时按以下优先级严格检索二进制文件：
1. **热升级目录**：`<app_home>/cores/mini-mihomo.exe`（用户在 UI 点击内核升级后下载解压的最新版内核）。
2. **随包内置目录**：随程序分发的 `mini-mihomo.exe`。
3. **独立命名空间铁律**：内核二进制文件必须使用 `mini-mihomo.exe`，严格与系统上其它 Clash Verge 的 `verge-mihomo.exe` 或原版 `mihomo.exe` 区分，杜绝进程名碰撞。

---

## 二、内核启动参数与 Go Runtime 深度压制

### 2.1 命令行参数（CLI Arguments）
```bash
mini-mihomo.exe -d "<app_home>" -f "<app_home>/config.yaml" -ext-ctl-pipe "\\.\pipe\mini-mihomo"
```
- `-d <app_home>`：指定配置根目录，确保 `Country.mmdb`、`geosite.dat` 等地理数据资源按需加载。
- `-f <config_file>`：指定当前生效的运行时配置文件 `config.yaml`。
- `-ext-ctl-pipe \\.\pipe\mini-mihomo`：开启 Windows 原生命名管道控制，避免纯 TCP 端口竞争。

### 2.2 Go 运行时环境变量压制（核心优化）
Mihomo 基于 Go 语言编写，若不加限制，Go 运行时默认会按物理机全部 CPU 核心拉起大量协程，且 GC 触发惰性导致常驻内存虚高。Clash Mini 通过启动环境变量实施三层压制：
* **`GOMEMLIMIT = 96MiB`（或 `128MiB`）**：设定 GC 软内存上限，迫使 Go 运行时更积极地回收内存。
* **`GOGC = 50`**：调紧 GC 周期（默认 100），使内存回收更加频繁，控制驻留峰值。
* **`GOMAXPROCS = 2`**：将并发线程数硬性压制为 2，桌面代理客户端无需多核狂跑，根绝空闲 CPU 抖动。

---

## 三、孤儿清理与 Win32 Job Object 绝对防线

### 3.1 启动前孤儿进程清理（`kill_all_mini_cores`）
- 在拉起新内核之前，遍历系统进程树，精准检索进程名包含 `mini-mihomo` 的所有残留进程并将其强制终止（Terminate）。
- 彻底释放前次非正常退出（如系统断电、任务管理器强杀）残留的 TCP 端口和命名管道句柄。

### 3.2 Win32 Job Object 终极守护（防孤儿核心）
在原生 C++ 实现中，绝不能依赖主进程退出时的析构函数（崩溃时无法执行）：
1. 主进程调用 `CreateJobObjectW(nullptr, nullptr)` 创建匿名 Windows 作业对象。
2. 配置作业限制参数：
   ```cpp
   JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
   jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
   SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
   ```
3. 通过 `CreateProcessW`（带 `CREATE_SUSPENDED` 标志）启动 `mini-mihomo.exe`。
4. 调用 `AssignProcessToJobObject(hJob, pi.hProcess)` 将子进程压入作业对象。
5. 调用 `ResumeThread(pi.hThread)` 唤醒运行。
> **防护效果**：一旦 ClashWin 主进程以任何原因退出（无论是正常退出、被任务管理器杀死还是发生 Crash 崩溃），Windows 操作系统内核会在句柄关闭的瞬间**100% 自动摧毁 Job 中的子进程**，绝不残留任何孤儿内核！

---

## 四、Windows 命名管道（Named Pipe）与退避重试规范

### 4.1 源码位置
- [`crates/tauri-plugin-mihomo/src/ipc.rs`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/crates/tauri-plugin-mihomo/src/ipc.rs) L112–L147

### 4.2 管道路径与并发退避重试（BUG-171/BUG-172 防御）
* **管道名称**：`\\.\pipe\mini-mihomo`
* **协议层封装**：在命名管道双向字节流之上运行 HTTP/1.1 请求（与常规 TCP HTTP 报文格式完全一致）。
* **高并发管线退避铁律**：
  - 在突发批量并发（如 36 个节点同时测速）时，Windows 命名管道频繁返回 `ERROR_PIPE_BUSY` (231)。
  - **最大重试次数固定为 24 次**（`busy_retry_count = 24`），单次重试退避间隔为 `125ms`，总重试窗口达 **3.0 秒**。
  - 配合 Win32 `WaitNamedPipeW(pipeName, 125)`，100% 根治管道繁忙导致的测速批量失败或假死。

---

## 五、REST 控制器 API 与 Secret 安全握手

### 5.1 基础配置与端口避让
* **默认监听地址**：`127.0.0.1:9098`（避开原生 Clash 7890/9090，避开普通 Verge 9097）。
* **Secret 握手**：
  - 启动配置 `config.yaml` 注入随机强 Secret（或固定哈希值）。
  - 所有发往 `127.0.0.1:9098` 的 HTTP 请求必须携带 Header：
    ```http
    Authorization: Bearer <secret>
    Content-Type: application/json
    ```

### 5.2 核心 REST 接口清单
| 接口方法与路径 | 功能说明 | 核心输入/输出 |
| :--- | :--- | :--- |
| `GET /version` | 获取内核版本与高级特性 | `{"version": "v1.18.x"}` |
| `GET /configs` | 获取内核当前运行时配置 | 混合端口、模式、LogLevel 等 |
| `PUT /configs?force=true` | 热重载内核配置文件 | 载荷：`{"path": "config.yaml"}` |
| `GET /proxies` | 全量节点与策略组信息 | 包含 `PROXY` 组、节点列表、历史延迟 |
| `GET /proxies/{name}` | 查询指定单一节点状态 | 节点类型、UDP 支持、延迟历史 |
| `PUT /proxies/{group}` | 切换策略组当前活跃节点 | 载荷：`{"name": "目标节点名"}` |
| `GET /proxies/{name}/delay` | 单节点延迟测试 | Query: `url`, `timeout`；返回 `{"delay": 120}` |
| `GET /group/{name}/delay` | 策略组批量并发延迟测试 | Query: `url`, `timeout`；批量返回延迟字典 |
| `GET /connections` | 获取当前所有活跃连接 | 上下行速率、连接列表、目标 IP、进程名 |
| `DELETE /connections/{id}` | 断开指定单一网络连接 | - |
| `DELETE /connections` | 一键断开全部网络连接 | 切换节点或排障时使用 |
| `GET /rules` | 获取当前生效的分流规则清单 | 规则类型、负载策略、目标 Proxy |
| `GET /providers/proxies` | 获取外部 Proxy Provider 状态 | 健康度、节点数、更新时间 |

---

## 六、WebSocket 长连接流式推送协议

### 6.1 WebSocket 建连与鉴权
- URL 格式：`ws://127.0.0.1:9098/<endpoint>?token=<secret>`
- 在 Win32 原生环境中，通过 WinHTTP 原生 WebSocket API（`WinHttpWebSocketCompleteUpgrade`、`WinHttpWebSocketReceive`）建立长连接。

### 6.2 常用推送频道
1. **实时流量监控（`/traffic`）**：
   - 每 1 秒推送一次 JSON：
     ```json
     { "up": 10240, "down": 204800 }
     ```
   - 驱动主界面与托盘的实时上传/下载速率展示。
2. **连接变动流（`/connections`）**：
   - 实时推送新建立与关闭的连接详情，驱动宽窗抽屉展开时的实时网络连接表。
3. **内核运行日志（`/logs`）**：
   - 参数：`?level=info` 或 `?level=debug`。
   - 实时推送内核产生的每一行标准输出日志，用于故障诊断。
4. **内存占用监控（`/memory`）**：
   - 推送 Go 堆内存与 OS 分配内存，辅助自愈与资源监控。

---

## 七、内核重启与 PROXY 节点快照/恢复机制（Snapshot & Restore）

### 7.1 源码位置
- [`src-tauri/src/core/manager/lifecycle.rs`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src-tauri/src/core/manager/lifecycle.rs) L277–L417

### 7.2 核心业务防御铁律
> [!IMPORTANT]
> **Mihomo 在执行 `restart` 或 `PUT /configs` 重新载入配置后，会将所有的 Selector 策略组重置为列表中的第一个节点（往往是排在首位的广告节点或无效节点）。**

为了保障用户选中的节点在内核重启后不被篡改丢失，Clash Mini 确立了严格的**快照与还原恢复时序**：

```
[准备重启内核 / 切换配置]
          │
          ▼
1. 快照当前 PROXY.now 选中的真实节点名 (排除 DIRECT/REJECT 等内置出站)
          │
          ▼
2. 执行内核重启 / 重新加载配置
          │
          ▼
3. 等待内核 API 就绪 (循环 GET /configs，超时预算 30 秒)
          │
          ▼
4. 等待 PROXY 组节点列表填充 (循环 GET /proxies/PROXY，超时预算 20 秒)
          │
          ▼
5. 校验目标节点是否在当前组列表中:
   ├── 在组内 ──> 发起 PUT /proxies/PROXY 恢复选择 (总超时预算 90 秒，遇错误每 3 秒重试)
   └── 不在组内 / 重试预算耗尽 ──> 读取用户保存的 filterText，在子集中自动降级选取第一个可用节点!
```

---

## 八、C++20 原生移植映射方案

在 C++20 / Win32 / Direct2D 原生体系下，内核管理与 IPC 的实现架构设计如下：

```
                    ┌────────────────────────┐
                    │  ClashWin C++20 主程序 │
                    └───────────┬────────────┘
                                │
               ┌────────────────┴────────────────┐
               ▼                                 ▼
   ┌───────────────────────┐         ┌───────────────────────┐
   │ Process Lifecycle 管控 │         │ WinHTTP 原生 IPC 引擎 │
   ├───────────────────────┤         ├───────────────────────┤
   │ 1. 扫描杀灭残留 mini-* │         │ 1. WinHTTP 异步 REST  │
   │ 2. CreateProcessW 挂起│         │ 2. WinHTTP WebSocket  │
   │ 3. Job Object 孤儿绑定 │         │ 3. 24 次退避重试机制  │
   │ 4. GOMEMLIMIT 压制参数 │         │ 4. PROXY 组快照与恢复 │
   └───────────┬───────────┘         └───────────┬───────────┘
               │                                 │
               └────────────────┬────────────────┘
                                │ 启动并管理 / REST & WS 通信
                                ▼
                    ┌────────────────────────┐
                    │    mini-mihomo.exe     │
                    │   (Windows 子进程)      │
                    └────────────────────────┘
```

1. **`ProcessManager` 模块**：
   - 封装 Win32 `CreateProcessW` 与 `CreateJobObjectW`，启动时强制注入环境变量 `GOMEMLIMIT=96MiB`、`GOGC=50`、`GOMAXPROCS=2`。
   - 实现 `KillAllMiniCores()` 遍历 Toolhelp32Snapshot 清理残留。
2. **`CoreClient` 模块**：
   - 基于原生 `winhttp.h` 封装高并发异步 HTTP 请求（GET、PUT、DELETE），支持 Header 注入与 24 次 `WaitNamedPipeW` / 重试退避。
   - 基于 `WinHttpWebSocket*` 实现低开销流式推送解析。
3. **`ProxySelector` 状态机**：
   - 封装 `SnapshotProxyGroup()` 与 `RestoreProxyGroup()`，严格实现 30s API 轮询 + 20s 组填充轮询 + 90s 重试预算 + 子集降级选点逻辑。
