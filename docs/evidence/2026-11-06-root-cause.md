# 2026-11-06 只读根因分析：管道口径 / 自更新缺环 / 阶段标记

> 方法：全仓只读检索（Glob / Grep / Read + 只读 ls / find），**未修改任何文件、未构建、未执行 git 写操作**；未访问外部参考目录 `ClashVerge`；未触碰 `sidecar/` 5 个上游 exe；分析期间未读取正被验收脚本改写的 `dist/`、`installer/stage/`、`build/bin/Release/`。
>
> 用途：为 `docs/06-验收状态台账.md` §2 D 与 §4 的定性提供证据。**本文件只给判断，不含修复方案；本轮未改任何源码。**

## 1. 命名管道口径矛盾 —— 文档写错前提，代码实质已达标

**【事实】**

| 位置 | 原句 / 值 |
| --- | --- |
| `docs/01-目的与方针.md:21` | 「本程序用 `\\.\pipe\clash-win-mihomo` 级别的自有名字，凡 Mini 侧出现 `mini-mihomo-win` 字样的硬编码都必须同步改（漏改会连到对方内核……）」 |
| `docs/README.md:24` | 「控制面实际走命名管道 `\\.\pipe\mini-mihomo-win`（`src/util/dirs.cpp:210`）」 |
| `src/util/dirs.cpp:207-211` | 全仓**唯一运行时定义** `ipc_path()` 返回 `\\.\pipe\mini-mihomo-win`，注释自陈「共存错开：Clash Mini 用 `\\.\pipe\mini-mihomo`，Clash Win 用独立管道名」 |
| `src/core/core_manager.cpp:165` | `cmd += L" -ext-ctl-pipe " + s.ipc_pipe;` —— 管道名是**命令行参数**，不是内核内建常量 |
| `src/config/clash.cpp:198-208,352` | `guard_external_controller_ipc()` 把同一 `ipc_path()` 写进配置 `external-controller-pipe` |
| `docs/03-踩坑与避坑清单.md:57`、`docs/phase1/02_backend_census.md:68` | Mini 侧用的是 `\\.\pipe\mini-mihomo`（不带 `-win`） |
| `src/mihomo/service_ipc.h:23` | 服务 IPC 固定名 `clash-verge-service`，与 Mini 共用（方针 `:25` 明确「服务名只有一个」，靠 `X-IPC-Magic` 鉴权与「谁开谁写」约定，不按程序分隔） |
| 消费点 | `src/ui/main_window.cpp:875`、`src/cmd/cmd_support.cpp:280`、`src/core/core_manager.cpp:447,608-611`、客户端层只做「裸名补前缀」通用处理 |

**【推断】**

- **不是「上游内核硬编码、改不了」**：管道名由 `-ext-ctl-pipe` 参数与配置注入，值来自本项目单一函数 `util::dirs::ipc_path()`。不可改动的是 `sidecar/` 里的内核**二进制**，不是可注入的参数。
- **也不是「本可改却漏改」**：`mini-mihomo-win` 正是本项目为共存自造、已与 Mini 的 `mini-mihomo` 错开的值。方针 `:21` 把本项目自己的值误当成「Mini 侧硬编码」，前提写错了；它想要的「自有名字、不连到对方内核」**已经实现**。差别只剩字面是否叫 `clash-win-mihomo`。
- 共存冲突风险：内核控制管道已按不同名隔离，现状不冲突；服务 IPC 是**刻意不隔离**。只有把 `ipc_path()` 改成与 Mini 同名才会触发方针描述的「配置生效但界面数据对不上」。
- 结论：**文档口径矛盾，非代码缺陷**。若要统一，应改方针 `:21` 的措辞与举例，而不是动 `dirs.cpp`；改字符串会连带 `tests/test_core_commandline.cpp:21,31,43` 的期望值与多处常量注释，属无功能收益的改动。

## 2. 客户端自更新「安装 / 重启」—— 能力齐备、编排缺环（确实没写完）

**【事实】完整调用链**

| 段 | 位置 | 状态 |
| --- | --- | --- |
| 检查更新 | `src/cmd/cmd_app.cpp:318-327` → `src/update/client_updater.cpp:323-334`（拉 manifest、版本比较） | 真实实现 |
| 下载 | `src/cmd/cmd_app.cpp:331-349` → `client_updater.cpp:336-399`（三代理下载、CNG SHA-256、Ed25519 验签，返回 `%TEMP%\clashwin_setup_update.exe`） | 真实实现 |
| 安装 | `src/update/client_updater.cpp:401-423` `install_app_update()` 已写好 `lpVerb=L"open"`、`/S` 参数 | **零调用点**：全仓仅定义 + `include/clashwin/update/client_updater.h:61` 声明，无任何命令注册 |
| 重启 | `src/cmd/cmd_app.cpp:263-272` → `src/app/lifecycle.cpp:322-392`（快照 → clean → `CreateProcessW` → `ExitProcess`） | 真实实现，但**与 update 链无接线** |

- 断链点：`src/ui/main_window.cpp:765-775` 的 `on_start_update` 成功分支**空置** —— 只处理 `!r.is_ok()`，不读取 `r.value()["path"]`，不触发安装或重启。
- 「done」是纯文案：`client_updater.cpp:396-397` 发 `下载完成，正在准备安装并重启...` 后直接 return；`src/ui/scenes/utility_modals.cpp:324-337` 收到 `done` 只置 `is_downloading_=false`、改文案。
- 自陈未接线：`cmd_app.cpp:329-330`「仅落地到临时目录；安装/重启需提权，由调用方在本轮末尾统一列清单后再接」；`main_window.cpp:758`「安装器启动（NSIS + UAC）不在本轮接线，见 K 提权清单」。
- 裁决与完成标准：`docs/phase2/07_user_rulings.md:5` 明确「自动更新逻辑**完全模仿 Clash Mini**（GitHub Releases + 检查/下载/安装同构）」；`docs/phase2/06_task_breakdown.md:185`（T24 完成标准③）列 `check/download/install`，且「若 §10-1 裁定为仅检查则相应降级验收」——实际裁决不是仅检查，**降级条件不成立**。
- NSIS 侧真实约束：`installer/clashwin.nsi:9-10` 定义 `/S [/KEEP] [/DIR=]`；`:117-119` 的 `RunMainBinary` 只挂 MUI finish page 复选框，`:175/:191/:425` 静默模式跳过 finish page → **`/S` 不会自动拉起新程序**；`:374-380` `KillAppProcesses` 会 `taskkill /F` 当前进程。仓库内无「先退出 → 静默装 → 自动重启」闭环。
- 可复用能力：`Lifecycle::restart_app()` 有；提权启动有 `src/core/service_mode.cpp:37-45`、`src/system/service_runner.cpp:60-68`（`lpVerb=L"runas"`）；**服务重装被设计禁用**（`src/cmd/cmd_service.cpp:39-43` `refuse_service_mutation()` 显式拒绝 uninstall/reinstall/repair，避免破坏共用方），不是可复用函数。**（2026-11-09 更新：该禁令已被 D2 裁决解除——reinstall/repair 已放开；uninstall 与 Mini 一致改为「后端保留能力、UI 不暴露」，`refuse_service_mutation()` 已删除。本行保留为当时的审计记录。）**
- 附带口径差异：`updater/app-update.json:8` 的 url 是 `local://updater/clash-win-v0.1.0-windows-x86_64.zip`、`signature` 空、无 sha256；而 `client_updater.cpp:327-328` 的 manifest URL 仍硬编码 GitHub raw 地址，且 `fetch_manifest`（`:102-117`）走 `http_get`，无法处理 `local://` scheme。

**【推断】** 不属于「设计上留给人工」：裁决与 T24 完成标准都要求 install，注释措辞是「本轮范围内挂起」，`install_app_update()` 连 `/S` 都写好却零调用。属于**确实没写完，且是开发节奏上的主动暂缓**。台账 §2 D 定性「属独立功能改动，本轮未动，待优先级确认」准确。补齐属功能改动，需 1:1 对照 Mini 源码 + 提权/重启编排设计 + 重编译全量回归，**本轮不改，待用户裁决优先级**。

## 3. 2903 / T21 / T25 —— 阶段标记残留，不是待实现清单

**【事实】**

- `2903` 全仓 6 处，其中 `resources/geosite.dat:4432,5071` 是域名数据里的巧合数字；代码相关仅 4 处。唯一定义 `src/cmd/cmd_support.h:42` `kNotImplemented = 2903; // 桩命令（T21/T25 落地）`。
- **没有任何命令被注册为纯桩命令**。唯一实际返回点 `src/cmd/cmd_app.cpp:147` 是 `copy_icon_file` **缺参**分支，该命令本身有真实实现（`:141-163` 调 `IconCache::copy_icon_file`）；`tests/test_cmd_registry.cpp:74-84` 把这一缺参返回固化为测试。`src/cmd/command_service.h:11` 注释所称「桩命令」无对应实体。
- `T21` 独立 token 26 行、`T25` 20 行，重叠 5 行，去重 41 行；其中真正含「桩/占位/未接线」语义的仅 6 行（`command_service.h:11`、`cmd_support.h:42`、`06_task_breakdown.md:93`、台账 `:68/:69`、`cmd_verge.cpp:157`）。其余为模块归属 / 任务编号 / 测试名标注，对应功能已落地。
- `cmd_verge.cpp:98,100,148,151,157,160` 六处 `pending Txx` 是阶段标记残留，台账 `:68` 已定性「非功能缺口」。

**【推断】** 与问题 2 性质不同：这是 T09 → T21 → T25 阶段化开发的**标记层未随功能落地清理**，属于注释/文案与「收口」表述不一致，不构成待实现清单。

## 4. 共同根因

**三处表面不同，根因同一条：这是 1:1 语言移植项目，「源码为唯一事实源、逐条落到 Mini 的 file:line」的纪律只覆盖代码层；文档层（方针/裁决/台账）与「跨层装配收尾层」是不同批次、不同时点写入，缺少同一道收口门禁，于是都表现为「文档/注释说一套、代码装配做一套」。**

| 表现 | 层的缺口 | 定性 |
| --- | --- | --- |
| 管道矛盾 | 方针 `:21` 先于/脱离代码写成「应有」，未核对 `dirs.cpp:210` 现状 | 文档口径矛盾，**代码实质已达标** |
| 自更新缺环 | 裁决要求 install，但「下载 path → install → restart」编排在「本轮」被注释挂起 | **唯一真正的功能缺环** |
| 2903 / T21 / T25 | 阶段标记随 T21/T25 完成后未清理，注释仍称有桩命令 | 标记残留，非功能缺口 |

代码层本身收敛得较好：管道名只在 `ipc_path()`、错误码只在 `cmd_support.h`、更新能力集中在 `ClientUpdater` / `Lifecycle`。**缺的是「文档口径一致性」与「跨层装配收尾」两道门禁**——不是上游二进制限制，也不主要是「设计上留给人工」。

## 5. 本轮边界

- 未改任何源码、未新增测试、未重编译。
- 三处结论仅更新台账定性；问题 2 的接线是否实施、以什么优先级实施，留待用户裁决。
- 全程只读，不涉及 `ClashVerge` 与 `sidecar/`。
