# Clash Mini 源码精读与逆向剖析：数据契约与系统调用映射（Codebase Mapping）

> **归档位置**：`docs/06_codebase_data_contract_mapping.md`  
> **关联清单项**：Phase 1.2 源项目源码逆向精读与数据契约提取（Codebase Mapping）  
> **对齐源工程**：`ClashVerge` (`src-tauri/src/config/`, `src-tauri/src/utils/resolve/universal_parser.rs`, `src/pages/_layout.tsx`, `src/components/`, `src-tauri/src/core/sysopt.rs`, `src-tauri/src/core/service.rs`)

---

## 1. 核心数据模型与配置 Schema 权威规范

### 1.1 `verge.yaml` 用户持久化配置 Schema
位于 `%APPDATA%\clash_win\verge.yaml`（应用层配置，完全独立于内核运行时）：

| 字段名 | 类型 | 默认值 | 详细说明 |
| :--- | :--- | :--- | :--- |
| `config_version` | `u32` | `1` | 配置格式版本号（独立于软件版本），迁移时递增 |
| `theme_mode` | `string` | `"system"` | 主题亮暗模式：`"light"` \| `"dark"` \| `"system"` |
| `rule_fallback` | `string` | `"direct"` | 代理模式策略：`"proxy"`（全局） \| `"direct"`（GFWList 出厂默认） \| `"addurl"`（添加网址/手动名单） |
| `enable_system_proxy` | `bool` | `false` | 系统代理开关（绑定 IE 注册表 `ProxyEnable`） |
| `enable_tun_mode` | `bool` | `false` | TUN 虚拟网卡模式开关 |
| `enable_auto_launch` | `bool` | `false` | 开机自启动开关（写入注册表 `Run` 项） |
| `enable_silent_start` | `bool` | `false` | 静默启动（启动时不弹出主窗口，仅托盘常驻） |
| `enable_always_on_top` | `bool` | `false` | 窗口总在最前 |
| `auto_close_connection` | `bool` | `true` | 切换代理节点时自动中断已有的长连接 |
| `proxy_host` | `string` | `"127.0.0.1"` | 代理监听的主机 IP 地址 |
| `verge_mixed_port` | `u16` | `10801` | 混合代理端口（HTTP + SOCKS5） |
| `system_proxy_bypass` | `string` | (默认名单) | 系统代理直连例外网段与域名名单 |
| `use_default_bypass` | `bool` | `true` | 是否始终强制使用系统默认直连例外 |
| `home_cards` | `object` | `{}` | 首页功能卡片折叠/展示状态 |

### 1.2 `profiles.yaml` 订阅管理 Schema
位于 `%APPDATA%\clash_win\profiles.yaml`：
```yaml
config_version: 1
current: "d1a2b3c4"  # 当前激活的 Profile UID
items:
  - uid: "d1a2b3c4"
    type: "remote"     # remote | local | script | merge
    name: "My Subscription"
    url: "https://example.com/api/v1/client/subscribe?token=xxx"
    file: "d1a2b3c4.yaml"
    updated: 1729760000
    extra:
      upload: 1073741824
      download: 10737418240
      total: 107374182400
      expire: 1761292800
    selected:
      - name: "PROXY"
        now: "HK-Node-01"
    option:
      update_interval: 86400
      user_agent: "ClashVerge/v3.0.5"
      with_proxy: false
```

### 1.3 节点原始协议解析规范（Universal Parser）
`universal_parser.rs` 负责将各种协议分享链接转换为 Mihomo 兼容的 Map 结构：
- **VMess (`vmess://<base64>`)**：Base64 解码 JSON 结构体，字段包含 `add`, `port`, `id`, `aid`, `net` (ws/tcp/grpc), `type`, `host`, `path`, `tls`, `sni`, `alpn`。
- **Shadowsocks (`ss://<payload>`)**：
  - 格式 A：`ss://base64(method:password@host:port)[#name]`
  - 格式 B (SIP002)：`ss://base64(method:password)@host:port[?plugin=...][#name]`
- **Trojan (`trojan://<password>@<host>:<port>[?query][#name]`)**：
  - 解析 `sni`、`allowInsecure`、`alpn`。
  - **重要分词规则**：先按 `?` 切割 query，再在前半部分按最后一个 `@` 区分密码与主机，严禁被 query 中的 `@` 干扰。
- **Hysteria2 (`hysteria2://<password>@<host>:<port>[?query][#name]`)**：
  - 支持 `obfs` (salamander) 与 `obfs-password`，`sni`，`insecure`。
- **WireGuard (`wireguard://<private-key>@<host>:<port>[?query][#name]`)**：
  - 参数：`public-key`，`ip`，`reserved`，`mtu`。

---

## 2. 前端 React 组件与交互行为映射

### 2.1 主视口状态机（`src/pages/_layout.tsx`）
- **单主窗多形态**：
  - **窄窗极简形态（$\le 285px$）**：卸载连接面板与监控大表，仅保留顶部导航栏、核心节点卡片、小流量面板与折叠抽屉按钮。
  - **大窗全功能形态（$> 285px$）**：展开双栏/三栏布局，右侧挂载动态连接监控虚拟列表（`connection-table.tsx`）。
- **折叠抽屉（Cards Drawer）**：
  1. `ActiveNodeStatusCard`：显示当前活跃节点名、延迟徽章（带颜色分级），点击触发顺位快速轮换（FEAT-006）。
  2. `RoutingPreferenceCard`：分段切换器（全局代理 `proxy`、GFWList `direct`、添加网址 `addurl`）。
  3. `TakeoverModeCard`：系统代理开关与 TUN 模式开关。
  4. `ProfileImportCard`：订阅链接输入框、导入按钮、订阅管理与更新时间。
  5. `ThemeSettingsCard`：6 套主题切换与双参数调节滑块。
  6. `BasicSettingsCard`：开机自启、局域网共享、自动关闭连接等开关。

### 2.2 6 套 3D 拟物与特色皮肤及双滑块体系
LocalStorage 独立键存储，严禁数值污染：

| 主题标识 (`skin`) | 风格描述 | 滑块 1 标签与范围 | 滑块 2 标签与范围 | 默认字体族 |
| :--- | :--- | :--- | :--- | :--- |
| **`retro-3d`** | 默认黄金条与高浮雕按键 | `Depth` [0.0 ~ 1.0] | `Vibrancy` [0.0 ~ 5.0] | `Trebuchet MS, SimHei` |
| **`original`** | 经典工业风，灰白实体 | `Radius` [0.0 ~ 3.0] | `Accent` [0.0 ~ 3.0] | 系统默认 |
| **`modern-flat`** | 现代深色立体倒角 | `Roundness` [0.0 ~ 3.0] | `Shadow` [0.0 ~ 5.0] | 系统默认 |
| **`frosted-glass`**| 暗黑实体磨砂玻璃 | `Opacity` [0.0 ~ 5.0] | `Blur` [0.0 ~ 5.0] | 系统默认 |
| **`cyberpunk`** | 暗夜霓虹，高饱和电光蓝 | `Glow` [0.0 ~ 5.0] | `Speed` [0.0 ~ 5.0] | `Consolas, NSimSun` |
| **`monochrome`** | 黑白极简工业硬朗风 | `Radius` [0.0 ~ 3.0] | `Border` [0.0 ~ 5.0] | 系统默认 |

### 2.3 活跃连接监控（`connection-table.tsx`）
- 极速双列虚拟列表（行高固定 20px，双缓冲交替斑马纹背景）。
- 列 1：`host`（目标域名/IP:端口）。
- 列 2：`chains`（50px 宽，`DIRECT` 绿字直连、`PROXY` 蓝字代理、`REJECT` 红字阻断）。
- 右键上下文菜单：断开连接（`DELETE /connections/{id}`）、复制地址（`host:port`）、查看详情。

---

## 3. 后端 Rust / Win32 系统调用映射

### 3.1 注册表代理操作（`sysopt.rs`）
- **操作项**：
  - 路径：`HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Internet Settings`
  - 键值：`ProxyEnable` (DWORD), `ProxyServer` (SZ), `ProxyOverride` (SZ), `AutoConfigURL` (SZ).
- **原子变更与广播**：
  - 开启代理：先清空 `AutoConfigURL`，写入 `ProxyEnable = 1`，`ProxyServer = 127.0.0.1:10801`，`ProxyOverride = <bypass>`。
  - 关闭代理：写入 `ProxyEnable = 0`，清空 `ProxyServer`。
  - 广播刷新：调用 `InternetSetOptionW(NULL, INTERNET_OPTION_SETTINGS_CHANGED, ...)` 与 `INTERNET_OPTION_REFRESH`。

### 3.2 服务管理器交互（`service.rs`）
- **服务识别**：服务名 `clash_verge_service`，显示名 `Clash Verge Service`。
- **状态流转**：
  - `Ready`：服务正在运行且版本匹配。
  - `StartRequired`：服务已安装但处于停止状态（调用 `StartServiceW` 启动）。
  - `InstallRequired`：服务未安装（触发 UAC 提权执行 `clash-verge-service-install.exe`）。
- **进程提权处理**：
  - 通过 `deelevate::Token::with_current_process()` 检测当前权限。
  - 若为非特权用户且需操作服务，使用 `runas`（Win32 `ShellExecuteExW` 带 `runas` 动词）静默唤起安装器。

---

## 4. C++20 纯原生对齐实现架构图

```
┌────────────────────────────────────────────────────────────────────────┐
│                   ClashWin (C++20 Native Client)                       │
├────────────────────────────────────────────────────────────────────────┤
│ [UI View Layer]                                                        │
│  - Win32 Frameless Window (WM_NCCALCSIZE, WM_NCHITTEST, WM_SIZING)     │
│  - Direct2D / DirectWrite 硬件加速渲染 (6 套 3D 主题 + 双滑块自绘引擎)  │
│  - 响应式单/双栏布局 (断点 285px，最小尺寸 271x126px，空闲隐匿标题栏)  │
├────────────────────────────────────────────────────────────────────────┤
│ [State & Controller Layer]                                             │
│  - AppStateManager (权威持久化状态 verge.yaml / profiles.yaml)          │
│  - SingleInstanceServer (WinSock2 绑定 33335 端口与唤醒协议)            │
│  - LatencyProbeService (15s 恒定周期，3s 重试加速，2 击自愈)           │
├────────────────────────────────────────────────────────────────────────┤
│ [Kernel & IPC Layer]                                                   │
│  - CoreProcessManager (Windows Job Object 进程生命周期强绑定)           │
│  - NamedPipeIPC (\\.\pipe\mini-mihomo 24次退避重试管道通信)            │
│  - MihomoRestClient (WinHTTP 异步客户端，Bearer 鉴权)                  │
├────────────────────────────────────────────────────────────────────────┤
│ [System Integration Layer]                                             │
│  - Windows Registry Manager (HKCU Internet Settings 代理接管)          │
│  - SCM Service Client (clash_verge_service Win32 API 交互)             │
│  - TrayIconManager (NOTIFYICONDATAW 静态 3 态图标与 Win32 菜单)         │
└────────────────────────────────────────────────────────────────────────┘
```
