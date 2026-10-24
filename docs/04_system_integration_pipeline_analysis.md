# 📑 第四步成果物：Clash Mini 流量接管与系统集成全链路精读逆向报告

> **所属阶段**：阶段一：源系统全景解构与功能拓扑清单（Source Deconstruction & Functional Inventory）  
> **对应清单项**：`1.1.4 流量接管与系统集成链路（System Integration）`  
> **逆向参考目标（只读）**：[`c:\Users\sun_y\Documents\AntiGravity_Projects\ClashVerge`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge)  
> **文档定位**：全面解构 Windows Internet Settings 注册表代理设置、系统广播生效机制、TUN 虚拟网卡配置与 IPv6 防泄漏接管铁律、以及基于 Win32 SCM 的服务模式通信与优雅降级方案。

---

## 目录
1. [一、Windows 系统代理（Internet Settings）与原子控制](#一一windows-系统代理internet-settings与原子控制)
2. [二、TUN 虚拟网卡模式与 DNS/IPv6 防泄漏接管铁律](#二二tun-虚拟网卡模式与-dnsipv6-防泄漏接管铁律)
3. [三、系统服务模式（SCM）与 TUN-Service 隔离原则](#三三系统服务模式scm与-tun-service-隔离原则)
4. [四、异常退出时代理自愈与复原防线](#四四异常退出时代理自愈与复原防线)
5. [五、C++20 原生移植映射方案](#五五c20-原生移植映射方案)

---

## 一、Windows 系统代理（Internet Settings）与原子控制

### 1.1 源码位置
- [`src-tauri/src/core/sysopt.rs`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src-tauri/src/core/sysopt.rs) L1–L265

### 1.2 注册表路径与核心键值
所有系统代理均操作当前用户的 Internet 设置：
- **注册表项**：`HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Internet Settings`

| 键名 | 类型 | 开启状态取值 | 关闭状态取值 | 作用说明 |
| :--- | :---: | :--- | :--- | :--- |
| `ProxyEnable` | `REG_DWORD` | `1` | `0` | 系统全局代理总开关 |
| `ProxyServer` | `REG_SZ` | `127.0.0.1:10801` | - | 代理服务器地址（使用 Mixed 端口） |
| `ProxyOverride` | `REG_SZ` | 详见下方默认绕过名单 | - | 绕过代理的域名与本地 IP 网段 |
| `AutoConfigURL` | `REG_SZ` | `http://127.0.0.1:xxx/commands/pac` | 删除键或置空 | PAC 自动配置脚本地址 |

### 1.3 默认绕过名单（`DEFAULT_BYPASS`）
```
localhost;127.*;192.168.*;10.*;172.16.*;172.17.*;172.18.*;172.19.*;172.20.*;172.21.*;172.22.*;172.23.*;172.24.*;172.25.*;172.26.*;172.27.*;172.28.*;172.29.*;172.30.*;172.31.*;<local>
```
用户可在设置中自定义追加绕过地址，按逗号或分号与默认列表拼接。

### 1.4 设置顺序与 WinINET 系统广播（铁律）
> [!IMPORTANT]
> **在 Windows 上，禁用 PAC 时会清除 WinINET 的部分代理标志。因此切换至纯系统代理时，必须先清除 PAC，再开启全局代理！**

1. **步骤顺序（`proxy_apply_steps`）**：
   - 启用全局代理时：先清空/关闭 `Autoproxy (PAC)`，再写入并启用 `Sysproxy`。
   - 启用 PAC 时：先清空/关闭 `Sysproxy`，再写入并启用 `Autoproxy`。
   - 关闭全部代理时：清空 `Sysproxy` 与 `Autoproxy`。
2. **Win32 刷新广播**：
   写入注册表后，必须调用 Windows 内置 `wininet.dll` 刷新缓存：
   ```cpp
   InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
   InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
   ```
3. **排他写锁（Locking）**：
   所有代理写入操作必须在全局互斥锁保护下执行，杜绝并发写入导致乱序和代理残留断网。

---

## 二、TUN 虚拟网卡模式与 DNS/IPv6 防泄漏接管铁律

### 2.1 源码位置
- [`src-tauri/src/enhance/tun.rs`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src-tauri/src/enhance/tun.rs) L21–L139
- [`src-tauri/src/constants.rs`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src-tauri/src/constants.rs)

### 2.2 TUN 运行时配置规范
开启 TUN 模式时，写入 `config.yaml` 的关键配置段如下：
```yaml
tun:
  enable: true
  stack: gvisor                    # 统一使用 gvisor 协议栈，禁止 system 栈防崩溃
  auto-route: true
  auto-detect-interface: true
  inet6-address:
    - "fdfe:dcba:9876::1/126"      # 内核要求数组前缀，单字符串会导致解析失败
dns:
  enable: true
  enhanced-mode: fake-ip
  fake-ip-range: "198.18.0.1/16"
  fake-ip-range6: "2001:2::0/64"   # 必须落在 fc00::/7 之外，防止与局域网直连规则冲突
  ipv6: <user_ipv6>                # 由用户「Allow IPv6」开关决定
ipv6: true                         # 顶层必须恒为 true (防泄漏核心)
```

### 2.3 IPv6 防泄漏最高安全铁律
> [!CAUTION]
> **若顶层 `ipv6: false`，Mihomo 内核在 Windows 上不具备 IPv6 收包能力，系统默认路由 `::/0` 将继续指向物理网卡，导致所有 IPv6 流量 100% 直连泄漏！**

- **开关语义严格二分**：
  1. **顶层 `ipv6`**：管「TUN 是否接管 IPv6 默认路由（防泄漏）」。**TUN 开启时恒定强制为 `true`**。
  2. **`dns.ipv6`**：管「应用是否真正使用 IPv6」。由用户的「Allow IPv6」开关决定。关闭时内核 DNS 不返回 AAAA 记录，应用程序拿不到 IPv6 地址自动回退 IPv4。
- **本地直连规则全覆盖**：
  必须注入 IPv4 私网段、IPv6 本地段（`::1/128`, `fc00::/7`, `fe80::/10`）及 IPv4-mapped IPv6 段（`::ffff:127.0.0.0/104` 等）直连规则。
- **境外 IPv6 兜底防断流**：
  在私网规则后、MATCH 之前注入 `GEOIP,CN,DIRECT` 与 `IP-CIDR6,::/0,PROXY`（禁止带 `no-resolve`），防止 GFWList 之外的境外 IPv6 域名在直连时发生 `i/o timeout` 断流。
- **DoH 上游防投毒代理规则**：
  从 `dns.fallback` 推导，对 Google（`8.8.8.8` / `8.8.4.4`）与 Cloudflare（`1.1.1.1` / `1.0.0.1`）强制注入 `IP-CIDR,<ip>/32,PROXY,no-resolve`，保障 DNS 防投毒探针可用。

---

## 三、系统服务模式（SCM）与 TUN-Service 隔离原则

### 3.1 源码位置
- [`src-tauri/src/core/service.rs`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src-tauri/src/core/service.rs) L44–L300
- [`clash_mini_agreements.md`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/clash_mini_agreements.md) Section 1.3

### 3.2 TUN-Service 隔离门控（零服务竞争）
1. **旁路模式强制优先**：当用户仅使用「系统代理」或「手动代理」时，无论后台是否安装了服务，Clash Mini **100% 仅以普通子进程（Sidecar）运行内核**，完全不调用 Windows SCM API。
2. **免 UAC 快速通道**：
   - 若 ClashWin 本身已通过管理员权限（Administrator）运行，开启 TUN 时直接由本进程创建 Wintun 网卡，**跳过一切服务轮询**。
   - 仅当以普通用户权限运行且开启 TUN 模式时，才连接 `clash-verge-service`。
3. **服务安装与管理命令**：
   - 服务名称：`clash_verge_service`。
   - 安装器：`clash-verge-service-install.exe`。
   - 卸载器：`clash-verge-service-uninstall.exe`。
   - 状态查询：通过 Win32 SCM API（`OpenSCManagerW`, `OpenServiceW`, `QueryServiceStatusProcess`）或 `sc.exe query clash_verge_service`。

### 3.3 服务故障优雅降级（BUG-298 防御）
当开启 TUN 模式但检测到服务未就绪（如用户在 UAC 弹窗点击取消，或安装超时）：
1. **绝不静默失败**：禁止在界面上显示“TUN 已开启”而后台实际未生效。
2. **自动回退为系统代理**：
   ```rust
   // 关闭 TUN 模式配置，开启系统代理配置，并立即写入 OS 注册表
   verge.enable_tun_mode = false;
   verge.enable_system_proxy = true;
   Sysopt::global().update_sysproxy();
   ```
3. **弹出明确通知**：“系统服务未就绪，TUN 模式无法生效，已自动回退为系统代理模式”。

---

## 四、异常退出时代理自愈与复原防线

### 4.1 退出与注销复原
在主程序窗口销毁（`WM_DESTROY`）、系统关机（`WM_QUERYENDSESSION`）或崩溃处理钩子中：
1. 立即执行 `ResetSysproxy()`：
   - 将注册表 `ProxyEnable` 置为 `0`。
   - 广播 `INTERNET_OPTION_SETTINGS_CHANGED` 与 `INTERNET_OPTION_REFRESH`。
2. 彻底释放代理端口，防止用户关闭程序后浏览器出现“无法连接到代理服务器”错误。

---

## 五、C++20 原生移植映射方案

| 功能模块 | 源系统实现 (ClashVerge) | ClashWin C++20 原生映射实现 |
| :--- | :--- | :--- |
| **系统代理注册表操作** | `sysproxy` crate + `sysopt.rs` | 原生 Win32 `RegOpenKeyExW` / `RegSetValueExW` 操作 `Internet Settings` |
| **系统代理广播** | `InternetSetOptionW` | Win32 `wininet.h` API `InternetSetOptionW`（原子两步广播） |
| **TUN 配置生成** | `enhance/tun.rs::use_tun` | 原生 C++20 `ConfigCompiler::InjectTunConfig()`（注入 gvisor、IPv6 防泄漏、fake-ip） |
| **SCM 服务管理器** | `service.rs` + `sc.exe` | 原生 Win32 API：`OpenSCManagerW`、`OpenServiceW`、`ControlService` |
| **服务 IPC 通道** | `clash_verge_service_ipc` | 原生 Windows 命名管道 `\\.\pipe\clash-verge-service` 异步客户端 |
| **退出安全复原** | `feat/window.rs::reset_sysproxy` | RAII 守卫类 `SystemProxyGuard`，析构时 100% 保证注册表代理复原 |
