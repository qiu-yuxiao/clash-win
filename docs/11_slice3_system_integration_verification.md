# 切片三（Slice 3）：系统代理注册表、TUN 网卡与 SCM 服务模式验证报告

> **执行日期**：2026-10-24  
> **验证目标**：Windows Internet Settings 注册表代理与 PAC 模式原子控制、WinINET 刷新广播、Windows SCM 服务状态查询与免 UAC 判定、BUG-298 自动降级安全自愈机制、TUN 虚拟网卡 gvisor 协议栈配置与 IPv6 防泄漏最高安全铁律。

---

## 1. 核心架构与实现要点

### 1.1 系统代理注册表与 WinINET 铁律时序（`SystemProxyManager`）
- **注册表路径**：`HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Internet Settings`
- **原子切换顺序**：
  - **启用系统代理时**：先清空 `AutoConfigURL (PAC)`，再写入并开启 `ProxyEnable = 1`、`ProxyServer` 与 `ProxyOverride`。杜绝因残留 PAC 导致 Windows 代理标志异常。
  - **启用 PAC 脚本时**：先清空 `ProxyServer` 并将 `ProxyEnable` 置 `0`，再写入 `AutoConfigURL`。
  - **关闭所有代理时**：原子重置 `ProxyEnable = 0`、`ProxyServer = ""` 并删除 `AutoConfigURL`。
- **WinINET 双步广播生效**：
  ```cpp
  InternetSetOptionW(NULL, INTERNET_OPTION_SETTINGS_CHANGED, NULL, 0);
  InternetSetOptionW(NULL, INTERNET_OPTION_REFRESH, NULL, 0);
  ```
- **崩溃与退出 100% 复原防线**：
  - 注册 `SetConsoleCtrlHandler` 捕获控制台注销/关机/Ctrl+C 事件。
  - 注册 `AddVectoredExceptionHandler` 捕获未处理异常与崩溃。
  - 提供 RAII 守卫类 `SystemProxyGuard`，析构时无条件还原系统注册表。

### 1.2 Windows SCM 服务模式与 BUG-298 优雅降级（`ServiceManager`）
- **管理员免 UAC 直通**：
  - `IsProcessElevated()` 基于 Win32 `AllocateAndInitializeSid` 与 `CheckTokenMembership` 检测管理员权限。
  - 若为管理员运行，直接由本进程创建 Wintun 网卡，完全跳过 Windows SCM 轮询。
- **TUN-Service 隔离门控（零服务竞争）**：
  - 仅在用户明确开启 TUN 模式时才查询/调用 SCM 服务管线；常规系统代理/混合端口模式下 100% 旁路普通子进程运行。
- **BUG-298 优雅降级自愈**：
  - 普通用户权限下若检测到 `clash_verge_service` 未安装或启动失败，**绝不静默失败**。
  - 自动将模式回退为系统代理（`enable_tun_mode = false; enable_system_proxy = true;`），并立即写入 OS 注册表并弹出通知。

### 1.3 TUN 模式配置编译器与 IPv6 防泄漏铁律（`TunCompiler`）
- **顶层 `ipv6: true` 强制注入**：
  - Mihomo 内核在 Windows 上只有在顶层 `ipv6: true` 时才会将 IPv6 默认路由（`::/0`）导入 TUN 网卡。若为 `false`，IPv6 会绕过代理从物理网卡直接泄漏。
- **gvisor 协议栈与 fake-ip-range6 规范**：
  - 协议栈统一使用 `stack: gvisor`，避免系统栈在某些 Windows 版本崩溃。
  - `inet6-address` 严格使用数组形式 `["fdfe:dcba:9876::1/126"]`。
  - `fake-ip-range6` 取值 `2001:2::0/64`，严格落在 `fc00::/7` 之外，杜绝假 IP 撞上本地直连规则。
- **全协议直连与 IPv6 兜底规则树**：
  - 注入 IPv4 私网段（127/8, 10/8, 172.16/12, 192.168/16, 100.64/10, 169.254/16）。
  - 注入 IPv6 本地段（`::1/128`, `fc00::/7`, `fe80::/10`, `ff00::/8`）。
  - 注入 IPv4-mapped IPv6 映射段（`::ffff:127.0.0.0/104` 等）。
  - DNS DoH fallback 强制走代理（`8.8.8.8/32`, `1.1.1.1/32`）。
  - IPv6 兜底分流（`GEOIP,CN,DIRECT` 与 `IP-CIDR6,::/0,PROXY`，刻意不加 `no-resolve` 保证域名解析命中并由 mmdb 分流）。

---

## 2. 自动化单元与集成测试结果

测试文件：`tests/test_slice3_system_integration.cpp`  
二进制产物：`build/bin/test_slice3_system_integration.exe`

```text
====================================================
[ClashWin 切片三] 系统代理注册表、TUN 与 SCM 验证测试
====================================================
[Test 1] 验证 Windows 系统代理注册表与 WinINET 状态机...
  [PASS] 系统代理注册表操作与铁律顺序全部验证通过！
[Test 2] 验证 Windows SCM 服务状态与 BUG-298 自动降级自愈...
  [SCM Info] 当前进程提权状态 (IsProcessElevated): NO
  [SCM Info] clash_verge_service 状态: 服务 clash_verge_service 未安装
  [PASS] SCM 状态查询与降级自愈防御验证通过！
[Test 3] 验证 TUN 模式配置编译器、IPv6 防泄漏与协议树注入...
  [PASS] TUN 虚拟网卡配置与 Agreement 全协议规则注入全部验证通过！

>>> 切片三: 系统代理注册表、TUN 与 SCM 服务模式全项 100% 验收通过！<<<
```

---

## 3. 验收结论

- [x] 系统代理注册表开启、关闭、PAC 模式、ResetAll 及 RAII 守卫复原 100% 通过。
- [x] WinINET 双步刷新广播机制运转正常。
- [x] 管理员特权检测与 SCM 服务状态机实现完整，BUG-298 降级自愈通过检验。
- [x] TUN 模式配置注入、gvisor 协议栈、顶层 IPv6 防泄漏最高铁律、fake-ip-range6 及 Agreement 规则集全部精准对齐。
- **切片三（4.3）验收状态：100% PASS**。
