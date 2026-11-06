# clash_mini_agreements.md ↔ Clash Mini v3.2.2 源码 冲突核查（2026-11-06）

> 触发：用户 2026-11-06 15:02 口径——`clash_mini_agreements.md` **不是正规设计文档，只是未决事项的裁定记录**；原则上优先级最高、须严格遵守；**但与当前 v3.2.2 源码实证冲突时必须直接上报、由用户商量裁决，不得自行拍板**。
> 本文是「上报 / 裁决」的唯一台账。两仓全程只读；核查员未改动、未构建、未提交任何文件。

## 0. 审计对象与文件性质

| 项 | 实测 |
| --- | --- |
| 文件 | `ClashVerge/clash_mini_agreements.md`，744 行 / 109,972 B，mtime 2026-11-05 19:53:52 +0800 |
| 自称 | 「v2.9.1 及后续版本」「权威系统设计规范与工程开发宪章」 |
| 实际 | **持续追加到 v3.2.2**：附录 A 最高 `BUG-312`（标注 v3.2.2），正文含 v3.0.1/3.0.2/3.0.3/3.1.6/3.1.7/3.1.8/3.2.1/3.2.2 现场实证 |
| 定性 | 按 BUG 逐条追加的**裁定记录**（印证用户口径），非一次性设计书 → **不会全篇过期，冲突只可能是个别条目** |
| 基准 | `ClashVerge/package.json:3` `version:"3.2.2"`、`name:"clash-mini"`；HEAD `779dfadd` |
| **重要** | 该文件在 ClashVerge 工作区有 **41 行未提交补充**（`git diff` 实测）：新增 §1.8「移植完整性：服务自举与独立可用性」+ §2.9 托盘菜单改为两项带勾选态。本文核查的是**工作区当前内容**，不是 HEAD 版本 |

## 1. ★ 已由用户裁决并闭合

### 1.1 「8.8.8.8 DNS 注入」——不保留，对齐源码现状

| 方 | 证据 |
| --- | --- |
| 早期设计口径 | Clash Mini 核心特性含「**8.8.8.8 DNS 注入**」 |
| v3.2.2 源码 | 默认 nameserver = `https://doh.pub/dns-query` / `https://dns.alidns.com/dns-query` / `114.114.114.114`（`M:enhance/mod.rs:455-461`），**无 8.8.8.8** |
| 8.8.8.8 真实角色 | **被改写 / 被加规则的对象**：`sanitize_foreign_plain_dns_upstreams` 把订阅自带明文 8.8.8.8 改写成 DoH（BUG-308）；`dns_fallback_proxy_rules` 生成 `IP-CIDR,8.8.8.8/32,PROXY,no-resolve`；`M:enhance/mod.rs:541-547,670-696` |
| agreements 立场 | **与源码一致**：`agreements.md:362-364` 明写「严禁写死具体 IP」，`:717` BUG-302、`:723` BUG-308 同向 |
| 结论 | 冲突来源**不是 agreements vs 源码**，而是**更早的设计口径 vs 源码**；agreements 站在源码一边，方向被跨版本反转（注入 → 改写掉） |

**用户裁决（2026-11-06 15:20，原话口径）**：

> 「当然是和 clash mini 的现状对齐。因为现状是在经过实际操作的时候遇到了困难，为回避困难采取的手段。」

- 裁定：Clash Win **不保留「8.8.8.8 注入」**。v3.2.2 现状是**实践定型**（实操碰壁后的规避手段），早期设计意图不得反过来推翻它。
- **本轮实测 Win 侧**：`W:enhance/dns_defaults.cpp:106-107` 默认 nameserver 已是 `doh.pub` / `dns.alidns.com` / `114.114.114.114`；8.8.8.8 仅出现在改写表 `W:enhance/dns_defaults.cpp:150-153` 与规则对偶补齐 `W:enhance/mini_agreements.cpp:194-212`。**本就无注入逻辑 → 无需改码**，仅闭合状态。
- 助手此前在 `2026-11-06-1to1-diff.md` 写「按口径不采信」属**自行裁决**，已撤回。

**衍生口径（待用户确认是否作为通则）**：当「早期设计意图」与「源码现状」冲突、且现状是实操碰壁后的规避手段时，默认以源码现状为准；但仍须先上报，不自行套用。

## 2. 审计 B 结果（§2 / §4 / §7 / §8.9 / 附录 B，48 条）

计数：**冲突 6 / 一致 36 / 无法判定 6**。以下 6 项为实证冲突，仅供裁决，**未自行处理**。

| 编号 | agreements | 裁定摘要 | v3.2.2 源码 | 对 Clash Win 的影响 |
| --- | --- | --- | --- | --- |
| **C-1** | `:252` §2.9 | 「**严禁动态更新**托盘图标 / ToolTip / 动态生成菜单；图标常驻固定资产」 | `tray/mod.rs:152-158` `update_icon()` 按 TUN/系统代理/手动切三套图标，`:180` `set_icon`、`:138` `set_tooltip`、`:284-294` `item.set_text` 改轻量模式勾选态；调用点 `feat/config.rs:240`、`lightweight.rs` 7 处（非死代码） | **Win 已按源码实现动态**：`W:system/tray.cpp:143-158` `update_icon()` / `update_icon_state()` / `modify_icon()`，另有 `update_lite_mode_menu`。若严守 §2.9 需改回静态 → 与 1:1 成功标准相悖 |
| **C-2** | `:287` §2.12 | 「初次启动默认窗口本身为 **330px** 窄竖条」，226→306px 换算 | `M:utils/resolve/window.rs:18` `DEFAULT_WIDTH=271.0`；`MF:constants.ts:26` 271；全源码 grep `330` 无命中；`first-run-guide-dialog.tsx:550-553` 有 `m:0,width:'100%'` 但 `maxWidth:'520px'` | **Win 用 271**（与源码一致）。`W:ui/scenes/settings_drawer.cpp:1204-1208` 的 330 是「帮助菜单高度」，与此无关。文档该处与 §2.3:191、BUG-307:722 自相矛盾 |
| **C-3** | `:647` §8.9 | 「**一切**用户可见文案必须走 `t()`」，仅 2 类例外 | 25 文件用 `useTranslation/t`、56 处 `t(key,{defaultValue})`；但 `theme-settings-card.tsx:59-134` 双滑块标签硬编码英文 `Depth/Vibrancy/Radius/Accent/Glow/Opacity/...`；`enhanced-canvas-traffic-graph.tsx:441` `'未知时间'` → `:1309` 直出中文 Tooltip | **Win 滑块标签也是硬编码英文**：`W:ui/scenes/settings_drawer.cpp:580,608` `L"Depth"` / `L"Vibrancy"`，**与源码一致**。若按 §8.9 全量接线反而偏离 Mini |
| ~~C-4~~ | `:113` §1.5 | 「`Mixed Port`/`Allow LAN`/`TUN Mode`/`System Proxy`/`Direct`/`Rule`/`Global` 在**所有非英文语言包不得硬性翻译**」 | `Mixed Port` 13/13 保留 ✓；`allowLan`(settings.json:96) 除 en 外 12 语言全译；`tunMode`/`systemProxy`(settings.json:14-15) ko/zhtw/fa/es 等译本地词；`direct/rule/global`(ru/proxies.json:4-6) 译「Прямой/Правила/Глобальный」 | **归并至 §4 的 C10**（同一冲突，以审计 A 编号为单一口径，不重复裁决）。Win 130 个 locale 与 Mini 逐字节一致 |
| ~~C-5~~ | `:115` §1.5 | 西文语言一律 3 字母缩写 `MAN/SYS/TUN`、`DIR/RUL/PRX`、`SYS/LIGHT/DARK` | `MAN/SYS/TUN` ✓ 10 语言 settings.json:541-543；`DIR/RUL/PRX` 与 `SYS/LIGHT/DARK` 全 src 无；240px 侧栏 ✓ | **归并至 §4 的 C11**（同上，不重复裁决）。Win `quick_bar.cpp:33` 已实现 `MAN/SYS/TUN` |
| **C-6** | `:222` §2.6 | 「判定必须在物理像素域，**禁止读页面视口**」 | 权威判定 `window-provider.tsx:197-199` 确为物理像素；但 `:115-125` 首帧 useState 仍读 `window.innerWidth/innerHeight`，源码自注「仅供首帧布局粗估，挂载后立即校正」 | 轻度边界差异；Win 为 C++ 无 WebView 视口概念，语义等价即可 |

**关键一致项（复核）**：§2.3 窗口 `271×680` / 最小 `271×126` / 最大 `640×860` / `shadow(false)` / innerSize 口径 —— 全部一致；§2.6 stealth `IDLE_HIDE_DELAY_MS=10_000` 一致；§2.7 6 套皮肤 + 双滑块解耦一致；§2.9 托盘 2 项 + 左键恢复一致；§7.1 单一 `PROXY(select)` + `PROXY__METRICS(url-test)` **确凿成立**（`M:enhance/mod.rs:762-812`）；§8.9 的 13 语言 × 10 json 模块 = **848 key**，missing=0/extra=0，运行时仅 1 个 `translation` namespace；`@ts-expect-error` 未使用；三处同款右键菜单齐全。

**无法判定 6 项**：B-16（GlowBorder 外发光，全 src 无该标识）、B-20（节点行高固定 20px，仅找到图标尺寸）、B-31（唤醒是否触发 `reload_config(true)`，需跨调用链）、B-35（URL「先 Query 后 Userinfo」顺序）、B-36（导入后 1.5s 轮询本体，仅见 `staleTime:1500`）、B-48（附录 B 三类 i/o timeout 的 IP/时间戳判据属运行时日志，源码无可比对对象）。

## 3. 我方审计误报（已撤销，非冲突）

- **B-D35「卸载器删共用服务」= 误报**。`WR:installer/clashwin.nsi:343` 调用 `un.RemoveServiceIfOurs`（`:433-459`），先读 `HKLM\...\Services\<name>\ImagePath`，仅当路径前缀 == `$INSTDIR` 才 stop/uninstall，否则 `DetailPrint 保留该共享服务` + Return。原审计只看了 `:454` 的 `ExecWait` 漏看守卫。已从第一批清单撤销。
- 该守卫还与 §1.8 未提交补充里「卸载仅在 ImagePath 指向本安装目录时移除」**完全一致**。

## 4. 审计 A 结果（§1 / §3 / §5 / §6，含 §1.5 修订）

**两账去重（A、B 互相核对确认）**：§1.5「英文保留原则」3 条被双计——`C10≡B-45`（冲突）、`C11≡B-47`（冲突）、`N15b≡B-46`（一致）。全局须减 3 条（2 冲突 + 1 一致）。

| 项 | A（§1/§3/§5/§6） | B（§2/§4/§7/§8.9/附录B） | 去重 | 全局 |
| --- | --- | --- | --- | --- |
| 合计 | 82 | 49 | −3 | **128** |
| 冲突（断言） | 12 | 6 | −2 | **16** |
| 一致 | 62 | 36 | −1 | **97** |
| 无法判定 | 8 | 7 | −0 | **15** |

验算：16+97+15 = 128 ✓；82 = 12+62+8 ✓；49 = 6+36+7 ✓。

**主理人复核后的最终口径**：16 条冲突断言归并为 **15 个专题**（A 11 + B 6 − 重复 2；C1 由 L121+L329 两条断言合并，故 16 断言 ↔ 15 专题）。其中 **C6 因我方复核推翻审计结论，由「冲突」改判「一致」**（见 §4.3）→ 最终 **冲突 15 条断言 / 14 个专题、一致 98 条、无法判定 15 条**，合计仍 128。用户可见的裁决表按 **14 个专题**呈现。

### 4.1 实证冲突专题（C1~C11）

| 编号 | agreements | 裁定值 | v3.2.2 源码实际 | 性质 |
| --- | --- | --- | --- | --- |
| C1 | `:121`、`:329` | `GOMEMLIMIT = 96MiB` | `M:core/manager/state.rs:39` 默认 **128MiB** | 数值过时 |
| C2 | `:330` | `GOGC = 50` | `state.rs:40` 默认 **100** | 数值过时 |
| C3 | `:324` | 默认注入 `geodata-loader: memconservative` | 全项目无该字面量；`M:docs/ARCHITECTURE.md:212` 明言「应用代码中未发现设置」 | 声明的机制不存在 |
| C4 | `:316` | 「废弃所有硬编码公共密钥，采用全局强随机散列固定值」 | `M:config/clash.rs:73,83,175` 硬编码固定 `"adapted-by-qiu-yuxiao"` | 描述与实现相反 |
| C5 | `:311` | 「**指数退避**重试引擎」+ `WaitNamedPipeW` | `M:crates/tauri-plugin-mihomo/src/ipc.rs:118-144` 固定 **125ms** 等间隔、24 次；无 `WaitNamedPipeW` 字面 | 描述与实现不符 |
| C6 | `:475` | 流量采样 1000ms | `M:hooks/use-traffic-monitor.ts:62`、`traffic.worker.ts:10` = 3000 | **误判，已撤销**（§4.3） |
| C7 | `:510-511` | ZIP 逐条目查 `..`/绝对路径、拒绝并记安全告警 | `M:core/core_updater.rs:467-491` 固定路径 `cores/mini-mihomo.exe` 直写，不按条目名落盘 | 实现方式不符（客观无穿越面） |
| C8 | `:498` | 「彻底剔除」危险 Shell 权限 | `M:capabilities/migrated.json:73,79` 残留 `shell:allow-open` / `shell:default`（无 allow-execute/spawn/stdin） | 残留性质 |
| C9 | `:93/95/96` | 彻底剔除非 Windows 平台声明 | `M:capabilities/desktop.json:3` `platforms:["macOS","windows","linux"]`；CI 矩阵确为单一 windows-latest | 元数据残留 |
| C10 | `:113` | 行业词汇在非英文包「一律保持英文、不得硬性翻译」 | 仅 `Mixed Port` 13/13 保留；`Allow LAN` 12 语言被译、`TUN Mode`/`System Proxy`/`Direct`/`Rule`/`Global` 普遍被译 | 与 B 的 C-4 同一事，A 复核确认 |
| C11 | `:115` | 3 字母缩写三组 `MAN/SYS/TUN` + `DIR/RUL/PRX` + `SYS/LIGHT/DARK` | 仅 `MAN/SYS/TUN`（10 语言 `settings.json:541-543`）与 240px 存在；后两组全 src 无 | 与 B 的 C-5 同一事，部分缺失 |

**A 的关键一致项**：§1.1 端口 `10801`/`9098`/`33335`/`33336` ↔ `M:constants.rs:4,7,12,14`；§5.1 15s/3s、`monitor.rs:237,240`；§5.3 60s 冷却 `:1528-1542`、连续 5 次告警 `:1490-1511`；§5.5 双频 60s/5s `:1266-1269`；§3.7 TUN IPv6 全套（`enhance/tun.rs:106,109-112`、`constants.rs:59,67`）、`dns.fallback` 对偶补齐 `enhance/mod.rs:637-697`；§6.4 Boa 冻结原型链 `enhance/script.rs:36-46`；§6.5 `overflow-checks=true`、sourcemap false。

### 4.2 §1.8 专项结论（未提交补充，直接约束 Clash Win）

- **N20 核验**：§1.8 引用的所有源码 `file:line`（`_layout.tsx:1086-1088`、`use-service-installer.ts:28,43`、`use-system-state.ts:65`、`cmd/service.rs:18/28/32/38`、`core/service.rs:76/193/211/333`、`lifecycle.rs:151/183`）**全部存在且行号吻合** → §1.8 不是凭空编造，与源码一致。
- **但主理人复核发现关键限定**：`M:lib.rs:140-142` 的 `generate_handler!` **只注册了 `install_service` / `uninstall_service` / `is_service_available` 三个命令**；`cmd/service.rs:28,32` 定义的 `reinstall_service` / `repair_service` **从未注册**，`src/` 前端零调用点。即这两个命令在 Mini 里是**前端不可达的死命令**。
- **Win 现状**：`W:cmd/cmd_service.cpp:50-101` `install_service` 已实现 §1.8 要求的完整链路（缺失 → 用户可见提示 → UAC 安装 → 重新查询 → 启动 → 30s 就绪等待）；`:103-119` 三个禁用命令返回 `refuse_service_mutation()`（共存保护）。
- **结论**：§1.8 的核心要求「干净机自举 / 禁止假设服务存在 / 禁止静默失败」**Win 已满足**；「修复」能力在 Mini 自身也不可达。原差异清单 B-D34 的裁决（禁用不影响用户可见行为）成立，**无需改码**。

### 4.3 我方复核推翻的两条审计结论（不作冲突计入）

1. **C6（流量采样 1000 vs 3000ms）= 审计 A 误判**：Mini 存在**两层不同采样**——`M:components/layout/traffic-graph.tsx:16` `sampleIntervalMs = 1000`（图表采样）、`M:hooks/use-traffic-monitor.ts:62` + `traffic.worker.ts:10` `snapshotIntervalMs = 3000`（数据快照）。agreements `:475` 写的是「折线图渲染节流」，对应前者。Win `W:ui/scenes/traffic_pane.h:26` `kTrafficSampleMs = 1000`，注释正指向 `traffic-graph.tsx:16` → **与源码一致**。
2. **B-D35（卸载器删共用服务）= 原审计误报**，见 §3。

### 4.4 无法判定（A 8 条 + B 7 条）

运行时内存基准 35~45MB / 轻量 10MB、包体积 45→33.7MB、`WaitNamedPipeW` 是否由 tokio 间接调用、F4 界面子集约束、Tauri 事件广播细节、连接列表静默门控、网页唤起 scheme 白名单、GlowBorder 外发光、节点行高 20px、唤醒是否触发 `reload_config(true)`、URL 解析顺序、导入后 1.5s 轮询本体、附录 B 的运行时日志判据。均为**源码无字面量或需跨调用链/运行时验证**，不编造结论。

## 5. ★ 收敛结论（按用户 15:20 通则）

用户通则：「**和 clash mini 的现状对齐。因为现状是在经过实际操作的时候遇到了困难，为回避困难采取的手段**」——即源码现状是实践定型，文字裁定的旧值不得反推。

**主理人对 14 个冲突专题逐一核实 Win 侧现值，结论：全部已站在源码一侧，零代码改动。**

| 冲突 | Win 侧现值（实测） | 收敛 |
| --- | --- | --- |
| C1 / C2 | `W:include/clashwin/core/constants.h:9-11` = `128MiB` / `100` / `2` | 跟源码 ✓（96MiB/50 是过度压制后被放开的旧值，正印证用户「实操碰壁」判断） |
| C3 | Win 全仓无 `geodata-loader` / `memconservative` | 跟源码 ✓ |
| C4 | `W:constants.h` secret = `adapted-by-clash-win` | 同为硬编码固定值，值错开属**共存豁免** ✓ |
| C5 | `W:src/mihomo/pipe_http.h:33-35` = 24 次 × 125ms 固定间隔 + `pipe_http.cpp:285` `WaitNamedPipeW` | 跟源码 ✓ |
| C7 | `W:src/core/core_updater.cpp:383-438` 固定路径直写 / `tar.exe` | 跟源码语义 ✓ |
| C8 / C9 | Win 为 C++ / Win32，无 Tauri capabilities 文件 | 不适用，仅登记 |
| C10 | Win 130 个 locale 与 Mini **逐字节一致** | 跟源码 ✓ |
| C11 | `W:src/ui/scenes/quick_bar.cpp:33` `MAN/SYS/TUN`，无 `DIR/RUL/PRX` | 跟源码 ✓ |
| C-1 | `W:src/system/tray.cpp:143-158` `update_icon()` / `modify_icon()`；`W:src/app/app.cpp:251-264` 启动+订阅 `kRefreshVerge` 实时刷新 | **已由用户 15:33 裁决：跟 Mini 动态，零改动** ✓ |
| C-2 | Win 默认 271；`:1204` 的 330 是帮助菜单高度，非窗口宽 | 跟源码 ✓ |
| C-3 | `W:ui/scenes/settings_drawer.cpp:580,608` 滑块标签硬编码英文 | 与源码例外**一致**；但 Win 其余文案全硬编码中文属 F-D01 缺口（见下） |
| C-6 | Win 无 WebView 视口概念 | 语义等价 ✓ |

**C-3 对 F-D01 的边界约束（重要）**：§8.9 的绝对化表述与源码存在例外（滑块标签硬编码英文、`'未知时间'` 未走 t()）。按用户通则跟源码 → **F-D01 i18n 接线必须排除 Mini 自己硬编码的那几类**（双滑块标签 `Depth/Vibrancy/...`、图表 tooltip 时间戳兜底），否则全量接线反而偏离 Mini。其余 128 个中文文案按 `tr()` 接线不变。

**§2.9 已由用户 15:33 裁决闭合**：该条「严禁动态更新」自称也是为避开高权限下 `E_FAIL` 系统错误，即双方都自称「实操避坑」而方向相反，故此前单独上报。用户裁决原话：「**C-1，托盘要 mini 那样。也是可以变的。**」→ **以 Mini 源码现状为准，托盘保持动态，§2.9 的静态化裁定不再适用**。

**Win 侧链路复核（已完整接通，零代码改动）**：

| 环节 | Win 实现 | Mini 对应 |
| --- | --- | --- |
| 三套图标资产 | `W:system/tray.cpp:104-110` `tray-icon.ico` / `-sys.ico` / `-tun.ico` | `tray/mod.rs:152-158` |
| 状态判定与切换 | `tray.cpp:143-153` `update_icon()` → `update_icon_state()` → `backend_->modify_icon()` | `tray/mod.rs:180` `set_icon()` |
| 启动时设置 | `W:app/app.cpp:251-253` 按 verge 的 tun/sysproxy 调用 | `feat/config.rs:240` `SYSTRAY_ICON` |
| 运行期实时刷新 | `W:app/app.cpp:255-264` 订阅 `kRefreshVerge`，配置变更即重算 | `lightweight.rs` 7 处 |
| 菜单勾选态动态文本 | `tray.cpp:49-52` `✔ 轻量模式 / Lite mode` + `MF_CHECKED`，`tray.cpp:156-158` 状态缓存 | `tray/mod.rs:284-294` `item.set_text()` |

## 5.1 前端证据短路径 → 完整路径映射（审计 B 补正，判定不变）

> 目的：满足「任何结论都能按 `file:line` 定位」。`MF:` = `ClashVerge/src/`。

| 简写 | 完整路径（`ClashVerge/` 下） |
| --- | --- |
| `constants.ts` / `main.tsx` | `src/constants.ts` / `src/main.tsx` |
| `window-provider.tsx` / `window-context.ts` | `src/providers/window/` |
| `_layout.tsx` | `src/pages/_layout.tsx` |
| `theme-settings-card.tsx`、`first-run-guide-dialog.tsx`、`add-url-dialog.tsx`、`profile-import-card.tsx`、`active-node-card.tsx`、`help-menu-button.tsx`、`basic-settings-card.tsx`、`routing-preference-card.tsx`、`takeover-mode-card.tsx`、`connections-panel.tsx`、`mini-traffic-panel.tsx` | `src/pages/_layout/components/` |
| `use-skin-controls.ts` / `use-custom-theme.ts` | `src/pages/_layout/hooks/` |
| `style-helpers.tsx` | `src/pages/_layout/utils/` |
| `enhanced-canvas-traffic-graph.tsx` | `src/components/home/` |
| `connection-table.tsx` | `src/components/connection/` |
| `proxy-render.tsx` / `proxy-item.tsx` / `proxy-groups.tsx` / `use-render-list.ts` | `src/components/proxy/` |
| `use-profiles.ts` / `use-window.ts` | `src/hooks/` |
| `app-data-provider.tsx` | `src/providers/` |
| `query-client.ts` / `i18n.ts` / `delay.ts` / `cmds.ts` | `src/services/` |
| `profile-items.ts` / `mini-window.ts` / `window-resizing.ts` / `node.ts` | `src/utils/` |
| `i18n-keys.ts` / `i18n-resources.ts` | `src/types/generated/`；`verge.ts` / `mui-theme.d.ts` → `src/types/` |
| `index.scss` / `layout.scss` | `src/assets/styles/` |
| `shared.json` / `settings.json` / `proxies.json` / `layout.json` / `home.json` | `src/locales/<lang>/`（13 语种同构） |

Rust 侧路径两路报告均已给全（`src-tauri/src/...`、`crates/tauri-plugin-mihomo/src/...`）。

## 6. 处置纪律

- 14 个冲突按用户通则收敛为「跟源码」，实测 Win 全部一致 → **零代码改动**，仅登记于此；未自行改动任何一方的语义。
- **§2.9（C-1）已由用户 15:33 明确裁决**（托盘跟 Mini 动态），见 §5 末，无遗留待决项。
- 与冲突项无关的第一批小修（F-D01 i18n 接线，范围按 §5 C-3 边界；B-D06 verge 模板兜底；B-D26 日志 Service 分支；F-D05 帮助菜单 loading）继续推进。
- `ClashVerge` 全程只读；`sidecar/` 5 个 exe 不改动；不 push / 不建 Release / 不推 tag。

## 5. 处置纪律

- 上述 C-1～C-6 在用户裁决前**一律不写码**；其中 C-1/C-3/C-4/C-5 若严守 agreements 会与「1:1 对齐 Mini 源码」的成功标准直接相悖，必须由用户定夺优先级。
- 与冲突项无关的第一批小修（F-D01 i18n 接线范围待 C-3/C-4 裁决后定、B-D06 verge 模板兜底、B-D26 日志 Service 分支、F-D05 帮助菜单 loading）继续推进。
- `ClashVerge` 全程只读；`sidecar/` 5 个 exe 不改动；不 push / 不建 Release / 不推 tag。
