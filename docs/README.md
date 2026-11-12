# Clash Win 工程文档索引与现状总览

> **一句话定位**：把 **Clash Mini**（当前 1:1 同步至 **v3.2.5**）原封不动移植为纯 Windows 原生客户端（**C++20 + Win32 + Direct2D/DirectWrite**），零 WebView2、零浏览器运行时，除编程语言与显示名（`Clash Mini` → `Clash Win`）外实现像素级与语义级 1:1 对齐。

---

## 一、项目当前状态（v0.2.9）

| 维度 | 当前状态 |
| :--- | :--- |
| **当前发布版本** | `v0.2.9`（单一事实源：`include/clashwin/config/constants.h` 的 `kAppVersion`） |
| **上游同步基线** | **Clash Mini v3.2.5**（自 `v3.2.2` 起步，已完整同步跟进 `v3.2.3` BUG-314、`v3.2.4` BUG-315、`v3.2.5` BUG-316） |
| **代理内核与服务** | 上游 `MetaCubeX/mihomo` **v1.19.33**（`with_gvisor`，重命名为 `win-mihomo.exe`，缺失时支持自动下载校验恢复）；系统服务共用 `clash_verge_service` |
| **与 Clash Mini 关系** | **可同机共处、不可同时运行**（「借用与归还」方针）：双端配置文件隔离、互不删改磁盘与注册表；启动或安装一方时自动礼貌终止另一方运行进程并共用服务，卸载时若对方仍在则自动归还服务路径 |
| **网络与单实例参数** | 1:1 对齐：Mixed `10801` / SOCKS `10802` / HTTP `10803` / Guard 兜底 `7899` / Controller `9098` / 单实例 `33335` / IPC 管道 `\\.\pipe\mini-mihomo-win` |
| **视觉与多语言** | 6 套主题皮肤（`retro-3d` / `original` / `modern-flat` / `frosted-glass` / `cyberpunk` / `monochrome`）× 亮暗模式 + 逐皮肤双因子滑块实时驱动 + 13 套 UI 语言包热切换 + 悬浮独立分层 Tooltip |
| **后台防杀与自愈守护** | 真正零渲染轻量化托盘待机 + 双频（`TCP:53` 直连公网 IP / DNS 兜底）15~25 秒断流自愈 + 1×1 屏幕外零开销防清理哨兵窗口对 + `--guardian` 超轻量伴生看门狗（`~1 MB`、零断流 `--guardian-respawn` 无感重挂） |
| **发版与自动更新闭环** | `scripts/release.ps1` 确定性一键发版流水线 + 7 道预检门禁（`release_preflight.ps1`）+ Minisign Ed25519 应用内自动更新强验签闭环 |

---

## 二、常青核心文档（现行有效，优先查阅）

| 文档 | 说明 |
| :--- | :--- |
| [`../移植基本准则.md`](../移植基本准则.md) | **项目最高宪章**：1:1 移植三大铁律、事实源层级、共存互斥「借用与归还」裁决、版本线与允许偏差清单 |
| [`RELEASE_PROCESS.md`](RELEASE_PROCESS.md) | **标准发版手册**：`scripts/release.ps1` 两步发版法、7 道门禁（GATE 1~7）说明与异常恢复速查 |
| [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md) | **已知限制常青披露**：无商业 Authenticode 签名说明、`sidecar/` 二进制策略、Direct2D 毛玻璃近似等 |
| [`05-sidecar-内核资产.md`](05-sidecar-内核资产.md) | **sidecar 基线台账**：5 个上游 `.exe` 的字节大小、SHA256 基线、`win-mihomo.exe` 溯源与 `verify_sidecar.ps1` 校验规范 |

---

## 三、常用研发与发版命令速查

```powershell
# 0. 克隆仓库后安装强制 Git 钩子（只需执行一次）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\install_hooks.ps1

# 1. 校验 5 个 sidecar 上游二进制完整性
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify_sidecar.ps1

# 2. 标准编译（自动剥离冲突代理环境变量并编译 Release）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build.ps1

# 3. 运行全量隔离单元测试（或加 -Filter <子串> 过滤）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run_tests.ps1

# 4. 运行 UI 源码出处门禁与 7 道发版预检
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ui_src_gate.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release_preflight.ps1

# 5. 标准发版（本地全链路预演 → 正式推送发布）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release.ps1 -Version <X.Y.Z>
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release.ps1 -Version <X.Y.Z> -SkipBuild -Push -Yes
```

---

## 四、历史研发纪要与审计归档（时间线留档）

以下文档记录了项目从早期架构设计、逐模块 1:1 攻坚到历次全量审计的工程轨迹，按「保留历史现场、不涂抹时间线」原则归档备查；凡与现行宪章冲突的历史口径，一律以 [`../移植基本准则.md`](../移植基本准则.md) 与上文常青文档为准：

| 目录 / 文件 | 归档内容说明 |
| :--- | :--- |
| [`01-目的与方针.md`](01-目的与方针.md) | 项目立项方针、独立可用性（P0）要求与端口回归记录 |
| [`02-方法与流程.md`](02-方法与流程.md) | 从读 TS/Rust 源码到落地 C++ Widget 树的移植工序与出处门禁方法论 |
| [`03-踩坑与避坑清单.md`](03-踩坑与避坑清单.md) | 研发期真实踩坑台账（DPI 换算、浮层 `hit_self` 命中链、CSS 变量作用域等） |
| [`04-工程技巧.md`](04-工程技巧.md) | 离屏像素断言、隔离测试目录、DPI 探针等原生客户端调试技巧 |
| [`06-验收状态台账.md`](06-验收状态台账.md) | v0.1.0 阶段的验收证据矩阵与历史断链收口记录 |
| [`bug_list.md`](bug_list.md) | 历轮桌面实测问题单与逐项修复闭环追踪表（均已 ✅ 闭环） |
| [`evidence/`](evidence/) | 历次 1:1 全量审计、反向审计、皮肤因子语义核查与版本增量审计报告（含原根目录下 2026-11-09《Win 和 Mini 的差别》审计归档） |
| [`phase1/`](phase1/) · [`phase2/`](phase2/) | 立项初期的前后端源码普查、PRD、C++20 架构设计与早期任务拆解 |
