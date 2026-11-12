# Changelog

All notable changes to **Clash Win** are documented here.

Version format follows the project's **own** line starting at `0.1.0` (constitution §5.3): Clash Win versions increment independently and are **not** bound to Clash Mini's `3.x` numbering (`v3.2.2` was the initial port reference anchor, with subsequent releases continuously synced through `v3.2.5`).

Format reference: `Changelog.md` in the Clash Mini repository (newest first, `## vX.Y.Z` headings with grouped change bullets). Content here is Clash Win's own history, not a copy of Mini's entries.

---

## v0.2.10 (2026-11-12)

### 原生无障碍声音读屏（Win32 UI Automation）全量补齐
- **纯自绘 Direct2D 控件树原生桥接 Windows UI Automation (`WM_GETOBJECT`)**：在零 WebView2 / 零浏览器内核开销的前提下，完整实现 `IRawElementProviderSimple` / `IRawElementProviderFragment` / `IRawElementProviderFragmentRoot` COM 无障碍提供程序树，支持 **Windows 讲述人（Narrator）**、**NVDA**、**JAWS** 及 Accessibility Insights 直接发现、逐项聚焦、框选定位并语音朗读全界面自绘控件。
- **六大 UIA 标准控制模式与复合控件虚拟子项全覆盖**：全面实现 `IInvokeProvider`（按钮/菜单项点击）、`IToggleProvider`（开关/置顶/皮肤格状态读写）、`IValueProvider`（过滤框/订阅框/端口框与弹窗输入框读写）、`IRangeValueProvider`（各皮肤双因子滑块精确调节）、`ISelectionItemProvider`（代理节点切换、流量接管/分流分段单选、连接标签页）及 `IExpandCollapseProvider`（13 国语言选择器与帮助菜单展开状态），并为自绘复合区域（快捷栏活跃节点卡与延迟胶囊、订阅条目动作钮、连接面板表头、首启两步式向导及全部模态弹窗）补齐虚拟无障碍子项。
- **13 种内置语言实时读屏热切换与智能降噪**：接入惰性 `i18n` 版本校验（`ensure_i18n_fresh`），切换 13 种界面语言（`简体中文`、`繁體中文`、`English`、`日本語`、`한국어`、`Русский`、`Deutsch`、`Español`、`Bahasa Indonesia`、`Türkçe`、`فارسی`、`العربية`、`Татарча`）后无需等待重绘即可即时向读屏器输出对应语种 `aria-label`（`layout.a11y.*` 等）文案；自动聚合节点行（名称 + 协议 + 延迟 + 当前节点标注 + `PositionInSet/SizeOfSet`）与底部四宫格流量摘要，屏蔽碎片化装饰图元干扰。
- **实时通知语音播报、模态焦点隔离与全键盘无障碍导航**：动态绑定 Windows `UiaRaiseNotificationEvent` 实现 `SnackbarW` 顶部通知条免抢焦点实时语音播报；在打开清洁安装首启引导、编辑订阅、调试日志等模态弹窗或展开设置抽屉时自动屏蔽底层被遮挡控件，提供 `Tab` / `Shift+Tab` 模态环绕焦点遍历与 `Space` / `Enter` 激活，并在控件动态销毁时自动断开 COM 引用杜绝悬空访问。

## v0.2.9 (2026-11-12)

### 同步跟进 Clash Mini v3.2.5（BUG-316）：双层防误杀与零断流自愈守护
- **第一层事前免疫——常驻零渲染防清理哨兵窗口对**：针对 Windows 下隐藏至系统托盘后因无可见顶层窗口被 360 加速球等第三方内存清理工具以 `TerminateProcess(hProcess, 99)` 无差别强杀导致托盘图标消失、后台防断流守护停摆的问题，在主线程创建全生命周期常驻的 1×1 屏幕外全透明（`alpha=0`）哨兵窗口对——既满足系统可见性契约彻底豁免无窗口清理，又完全不在任务栏与 `Alt+Tab` 露面，渲染与 CPU 开销为零。
- **第二层事后兜底——超轻量级伴生看门狗（`--guardian`）与退出码取证**：启动时拉起纯内核事件阻塞态的伴生看门狗进程（`0% CPU`、约 `1 MB` 工作集）；主进程正常退出、系统关机注销或对端互斥接管时毫秒级同步退出，而在后台隐身/轻量模式下遭遇外部强杀或致命异常时，同步向 `clashwin.log` 写入含退出码特征诊断（如 `99/0x63` 360 强杀特征码、`0xC0000005` 等）、存活时长与自愈代数的取证记录，并在通过关机、卸载/安装器、对端客户端及单实例端口四重安全校验后静默自愈拉起主进程（含快速崩溃退避上限保护）。
- **系统服务内核零断流无感挂载（`--guardian-respawn`）**：伴生看门狗静默拉起主进程时，直接以无闪烁零渲染模式进入托盘轻量态；若 `clash_verge_service` 持有的 `win-mihomo` 内核仍在健康运行，直接无感挂载现有内核并延续当前出口节点健康守护，全程不重建 TUN 网卡、不打断正在进行的下载或通话。
- **双端互斥礼貌协作与监测日志闭环**：互斥接管终止对端客户端前先置位其主动退出事件避免误报崩溃日志，看门狗检测到对端客户端运行时自动让位；同时在 `CoreMonitor` 补齐偶发节点抖动复检恢复后的闭环日志记录。

## v0.2.8 (2026-11-12)

### 通知反馈彻底修复与全场景补齐
- **根治通知栏同线程自死锁导致操作无弹窗的问题**：修复 `NoticeService` 在主线程（UI 线程）推送通知时持非递归互斥锁同步回调 `SnackbarW::refresh()` 重入取快照引发 `resource_deadlock_would_occur` 异常并被静默吞没的致命缺陷；改为锁外派发通知回调并支持多订阅者独立句柄，确保主线程与后台线程触发的所有操作提示都能 100% 即时弹出。
- **全操作链通知覆盖与 13 语种对齐**：补齐订阅导入/切换/删除、端口保存、亮暗主题/皮肤/语言切换、单节点右键测速、`F1` 全局自动优选、排序模式切换、单条连接断开与清空历史记录等全场景的「进行中 + 结果」通知，统一补全全部 13 套语言包文案，并将原先 1~1.5 秒一闪而过的提示统一延长至清晰的 2~2.5 秒。

### 同步跟进 Clash Mini v3.2.4（BUG-315）
- **物理网络连通性双频快速探测重构（打破 TUN 断流死锁）**：将后台守护的物理网络探测升级为「在线 60s / 离线 5s」双频节流，优先对公网高可用 IP（`223.5.5.5:53`、`119.29.29.29:53`、`1.1.1.1:53`）发起毫秒级 TCP 握手，在局域网封禁 53 端口时自动回退测速地址 DNS 解析兜底——彻底消除虚拟网卡常亮导致的假在线误判，以及 TUN 模式下节点断流引发 DNS 卡死导致的自愈死锁。
- **断流自愈感知与二次切点全面提速**：1:1 同步缩短三大计时阈值——节点健康数据新鲜度阈值从 `60s` 缩短至 `20s`，节点切换宽限期从 `60s` 缩短至 `20s`，自愈失败冷却保护从 `60s` 缩短至 `25s`，突发断流自愈响应收敛至 15~25 秒。
- **隐身轻量模式主循环防意外退出加固**：在 `WM_DESTROY` 与主消息循环补充显式退出状态守卫（`Lifecycle::is_exiting()`），防止非预期窗口销毁或外部消息误终止常驻托盘与后台防断流守护线程。

### 其它稳定性与交互细节修复
- **Direct2D 设备丢失自愈修复**：修复 `D2dRenderTarget::recreate()` 在销毁旧渲染目标时误清空 `hwnd_` 导致远程桌面切换或显卡驱动重置后窗口无法恢复绘制的问题。
- **代理分流卡片与全新安装日志目录修复**：启动与刷新设置时自动同步代理分流分段控件状态；已处于自定义路由档位时再次点击「添加网址」支持随时重开添加弹窗；修复全新安装首次启动时 `%APPDATA%` 父目录尚未创建导致日志文件初始化失败的问题。

## v0.2.7 (2026-11-11)

### 用户可见修复与交互完善
- **像素级布局与浮层防裁剪精修**：修正基础设置 4 个小开关标签与开关实体的水平底边对齐；悬浮提示框（Tooltip）改为独立顶层透明分层窗口承载，彻底消除靠近主窗口边缘时被截断一半的问题；补齐各图标按钮缺失的悬停提示与鼠标移出清理；右键菜单支持视口边界钳制与滚轮滚动；窄窗（271px）下各模态对话框自适应贴合不溢出。
- **操作与结果通知全面补齐**：修复服务安装/重装通知重复前缀、订阅导入/更新与配置切换期间误弹 `IPC timeout` 错误条、标题栏阴影穿透污染右上角通知条像素等问题；并为代理分流切换（全局代理 / GFWList）、基础设置 4 项开关（开机自启 / 静默启动 / Allow LAN / IPv6）、手动/轮换切换节点、`⚡` 批量测速择优、窗口置顶及断开全部连接补齐清晰明确的状态与结果提示。
- **节点过滤条件与窗口几何跨重启记忆**：启动时自动读回上次保存的过滤词与排序方式并回填界面（程序化回填不误触发自动测速，且保留用户输入原始大小写）；安装/升级脚本保留 `window_state.json`，并修正高 DPI 副屏/缩放屏脱屏判定口径。
- **设置开关即时响应与真实初始态**：设置命令全面改为后台异步执行，点击即刻位移不卡 UI 线程；Allow LAN / IPv6 启动时读取真实配置状态。

### 工程与发版体系重构
- **发版全链路重构**：统一 `constants.h` / `clashwin.rc` / `clashwin.nsi` 版本同步与跨文件单测门禁，统一本地与 CI 的隔离回归测试运行器 `scripts/run_tests.ps1`，消除发布前清空 `updater/app-update.json` 导致的分支分叉与窗口期断档，修复 `pre-push` 钩子在 Windows PowerShell 5.1 下的兼容性并收敛常青文档 `docs/KNOWN_LIMITATIONS.md`。

## v0.2.6 (2026-11-11)

### 用户可见修复
- **UAC 提权前先自报家门**：安装 TUN 系统服务触发 UAC 前，先弹出来源明确的「Clash Win 需要管理员权限…」确认框，解决此前弹窗只显示「未知发布者」、用户无法判断请求来源的问题（与 Clash Mini 对齐）。
- **内核缺失自动恢复**：启动时若检测不到 `win-mihomo.exe`，自动从 GitHub（MetaCubeX/mihomo）下载并校验内核到 `cores/` 后重试，等价于 Clash Mini「内核始终存在」的保证；不再因内核缺失导致节点表空白。
- **sidecar 内核路径修正**：`dirs.cpp` 的 `sidecar_core_path()` 原先错误指向 exe 根目录，已修正为 `<exe>\sidecar\win-mihomo.exe`。

### 工程 / 发版流程
- **根治发布漏内核**：新增三道锁——`build_installer.ps1` 硬性要求内核存在、`release.yml` 在归档缺失时从上游补齐并按基线校验哈希、`release.yml` 断言 setup 体积 > 35MB；预检脚本新增 GATE 6 校验本地 sidecar 含内核。v0.2.5 的 `setup.exe` 因外部归档漏内核且流程无拦截，曾打出不含内核的安装包。
- **构建环境治理**：新增 `scripts/build.ps1` 标准编译入口，构建前剥离 WorkBuddy 透明代理注入的大小写重复 `*_PROXY` 变量，根治 MSBuild「字典关键字冲突」编译崩溃。

## v0.2.5 (2026-11-11)

### UI detail parity (1:1 with Clash Mini)

- **Task Manager now shows "Clash Win"**: the main executable gained a `VERSIONINFO` resource (FileDescription / ProductName = "Clash Win"), 1:1 with Clash Mini's own "Clash Mini" branding, instead of falling back to the raw `clashwin.exe` image name.
- **Basic-settings 2×2 switch grid spacing aligned**: equal `1fr 1fr` columns, 24px column gap, 13px labels — matches Clash Mini's `basic-settings-card.tsx`. Previously the grid used a 6px gap with unequal columns, jamming the two columns together and pushing the right-column labels (Allow LAN / Allow IPv6) far from their toggles.
- **Narrow-window breakpoint consolidated**: the 285px "stop rendering the connections panel" rule now references the single `win::kMiniWidthThreshold` constant instead of three duplicated literals. No behavior change.

### Runtime correctness

- **Single-instance hardened**: a second launch no longer starts a second copy under load. The lock-port bind failure is now treated exactly like Clash Mini's `check_singleton` — wake the existing window and exit — instead of a "maybe an instance is alive" probe that could time out and wrongly continue.
- **Lite mode now truly idles**: entering lite mode stops the 1-second stealth timer and the 5-second TUN-guard probe thread (the latter previously spun up a worker thread every 5s); silent boot defers creating the render layer until after entering lite mode, matching Clash Mini's "don't build a window you're about to hide" optimization. This closes the gap where Clash Win looked heavier than Clash Mini under Task Manager in lite mode.

---

## v0.2.4 (2026-11-10)

### Coexistence fix — TUN broke when installed over a running Clash Mini

- **Kill rule now actually kills the other side's elevated core**: previously the coexister opened `PROCESS_TERMINATE` with no counterpart and silently skipped any process it could not open. When the other client's core ran elevated (e.g. Clash Mini in TUN mode) the `TerminateProcess` was denied and the other side survived — leaving two cores running and the TUN hijacked. The kill path now identifies the other side by process name up front and (on the always-elevated TUN entry path) kills the other core before (re)starting our own.
- **Orphan Wintun/Mihomo adapter cleanup added**: after killing the other side, any leftover `Wintun`/`Mihomo` virtual adapter (and its routes) is now removed. This was the root cause of "browsers dead after switching to TUN" on a machine where Clash Mini was already in TUN mode — the orphaned adapter collided with / was reused as a stale adapter when Clash Win opened TUN.

### Internal

- Version literal in the release-readiness gate (`test_t35_release_readiness.cpp`) bumped to 0.2.4.

---

## v0.2.3 (2026-11-10)

### Kernel & coexistence

- **Kernel upgraded to mihomo v1.19.33**: the bundled core is now the **official MetaCubeX/mihomo v1.19.33 gvisor release build** (sourced directly from the upstream GitHub Release, not copied from Mini), renamed `mini-mihomo.exe` → `win-mihomo.exe`. The rename is a **user-ratified special case** documented in the constitution (`移植基本准则.md`) as part of the exclusive-coexistence naming chain (verge-mihomo → mini-mihomo → win-mihomo).
- **Exclusive coexistence policy landed (§5.2.2 "borrow & return")**: launching or installing one client terminates the other's client/core processes (strong-kill `TerminateProcess`, ownership judged by exe directory path, so a same-name core inside the other's own directory is no longer mis-killed by the installer) and starts the shared `clash_verge_service` from its own install path (service-registry takeover). **Nothing on the other side's disk is ever deleted.**

### Parity fixes (1:1 with Clash Mini v3.2.3)

- **Filter auto-select debounce 800 → 700 ms (G1)**: matches Mini's current debounce interval.
- **Focus-lost trigger removed (G2)**: the Win-added "trigger auto-select when the search box loses focus" behavior is removed — Mini only triggers on debounce completion or Enter.
- **IME composing guard (G3)**: pressing Enter while an IME composition is active (confirming pinyin candidates) no longer triggers auto-select, matching Mini's `!isComposing && keyCode !== 229` guard.
- **Same-word dedup (G4)**: re-submitting the identical filter word no longer re-triggers auto-select.
- **Log dialog layout (G6–G8)**: 44 px header, title 15 px / weight 600, level dropdown widened to 95 px, divider moved to match Mini's current metrics.
- **Log dialog tool buttons as real icons (G9)**: pause / sort-order / clear are now 18 px MUI vector glyphs (`pause_circle`-equivalent, `swap_vert` with vertical flip for descending) instead of text glyphs.
- **Log wrapping (G10)**: log payload wraps (`anywhere`) instead of ellipsis-truncating; row height grows dynamically, no horizontal scrollbar in narrow windows.

### Internal

- Regression assertions updated for the kernel rename (3 test files still expected `mini-mihomo.exe`; code itself was correct).
- Ledger corrections: the "auto-update install/restart not wired" record was stale (the chain was already fully wired: download → verify → install → exit to installer); `geodata-loader` resolved — **neither side sets it, both inherit mihomo's `memconservative` default, 1:1 with no change needed**.

## v0.2.2 (2026-11-09)

### Audit-gap parity fixes (1:1 with Clash Mini v3.2.2)

- **Node list 3-column grid (❌1)**: a wide window (>285px content) now lays out nodes as a 3-column card wall, same as Mini; the narrow window stays single-column (centered).
- **Alive probe guard (❌2)**: dead nodes (`alive===false`) render as Timeout(0) instead of a stale green delay — closes the 2026-10-24 incident recurrence path.
- **Self-heal refresh on no-candidate (❌3)**: auto-select now broadcasts `refresh_proxies` even when no candidate survives, so the UI never sticks on stale delay/alive.
- **On-demand probe gating (❌4)**: `probe_metrics_with_fallback` skips redundant probes when the active node is healthy & fresh (<60s) or freshly confirmed dead (<10s), reproducing Mini's fix for probe self-excitation false-deaths.
- **Self-heal alert = 3-fail reset-to-zero (△8)**: removed the standalone 5-fail popup; at 3 consecutive failures the counter resets and only an API-unreachable → core-restart → restart-fail ladder raises the alert (matches Mini `monitor.rs`).
- **Settings switch rollback on patch failure (❌5)**: a failed `patch_verge`/`patch_clash` snaps the toggle back to the authoritative value (Mini `mutateInfo` behavior).
- **Autostart `StartupApproved` write-back (❌6)**: enabling autostart also writes `StartupApproved\Run`, so the config wins over Task Manager's disable.
- **Autostart reads real registry state (❌7)**: the "launch on boot" switch reflects the actual Run-key state, not the verge config value.
- **Paint error boundary (❌8)**: `Widget::paint` wraps `on_paint` in try/catch and draws a placeholder rect instead of crashing the whole tree on a single widget exception.
- **First-run "later" opens drawer (❌9)**: dismissing the first-run guide with "later" auto-opens the settings drawer to point at the subscription card.
- **Auto-update manifest closure (❌10)**: `release.yml` now generates `updater/app-update.json` with a real `platforms.windows-x86_64` (url + sha256 + signature) on publish.

### Internal

- Added missing `#include <shellapi.h>` in `src/system/coexist.cpp` (build fix).
- Version bumped to 0.2.2 (independent of Clash Mini 3.2.2).

## v0.2.1 (2026-11-08)

### Exclusive coexistence + installer polish

- **Exclusive coexistence policy**: Clash Win and Clash Mini now **exclude each other** instead of sharing one machine silently. Launching one client terminates the other's client/core processes and **takes over the shared `clash_verge_service`**; mutual exclusion is also enforced **at install time**. Consequently all network ports / controller secret / identifiers return to a **1:1 alignment with Clash Mini's official scheme** (`10801` mixed / `10802` SOCKS / `10803` HTTP / `7899` guard fallback / `9098` controller / `33335` single-instance), replacing the staggered-port coexistence layout of v0.1.0.
- **Installer**: the installer window stays **always on top**; the IME candidate window follows the caret; the node filter **auto-selects** on completion.
- **Tests**: new release-readiness regression suite (`t35`) — title-bar color formula 1:1, tooltip 100 ms, `::selection` colors, update-manifest ownership (no `Clash.Mini` / no `3.2.2`, version == `kAppVersion`) and a registration-set assertion (every `UT_TEST` file must be registered in `CMakeLists.txt`).

### Notes

- v0.2.2 supersedes v0.2.1 as the current version line (`include/clashwin/config/constants.h:20 kAppVersion`).

---

## v0.1.0 (2026-11-07)

### First release — Windows-native 1:1 port

- **Core**: a ground-up C++20 / Win32 / Direct2D implementation of the Clash Mini v3.2.2 client. No Tauri, no WebView2, no JavaScript runtime — a single native executable plus the upstream Mihomo sidecar.
- **UI**: frameless Win32 window with `WM_NCHITTEST` hit-testing, `WM_SIZING` geometry clamping and Per-Monitor v2 DPI; Direct2D/DirectWrite rendering of the six skins (`retro-3d`, `original`, `modern-flat`, `frosted-glass`, `cyberpunk`, `monochrome`), with per-skin factor sliders wired to visible effects.
- **Workflows**: subscription import (19 protocols), automatic node benchmarking and fastest-node switching, rule/GeoIP updates, mini traffic-monitor layout, right-click routing rules, streaming-unlock detection, first-run onboarding, autostart, tray and quit recovery.
- **Coexistence**: a dedicated port set (`10811` mixed / `10812` SOCKS / `10813` HTTP / `10814` guard / `9108` controller / `33337` single-instance), its own secret (`adapted-by-clash-win`), config directory `io.github.clash-win.clash-win`, and a shared `clash_verge_service` Windows service — designed to run alongside Clash Mini on one machine. Do not enable TUN mode or system proxy on both clients at once.
- **Localization**: 13 UI languages under `locales/`; the NSIS installer starts in Simplified Chinese, English or Russian.
- **Distribution**: two artifacts per release — `ClashWin_0.1.0_x64-setup.exe` (NSIS, per-machine, requires UAC) and `ClashWin_0.1.0_x64_portable.zip` (extract-and-run, `.config/PORTABLE`), with `SHA256SUMS.txt` for offline verification.
- **Verification**: full regression passed (`EXIT=0`, `0 failed`) — see the delivery records in `docs/06-验收状态台账.md` and `docs/evidence/`. The case count is intentionally not pinned here: the suite grows with each regression addition.

See the [v0.1.0 GitHub Release](https://github.com/qiu-yuxiao/clash-win/releases/tag/v0.1.0) for the historical v0.1.0 release notes and [Known limitations](docs/KNOWN_LIMITATIONS.md) for the current evergreen disclosure list.

### Notes

- The only public difference from Clash Mini is the display name `Clash Mini` → `Clash Win`.
- **The v0.1.0 release assets are unsigned** — no Authenticode signature is applied. The signing step (`scripts/sign.ps1` / `installer/build_installer.ps1`) needs an elevated session, unavailable in the non-interactive release environment, so the signature status is recorded but does not block the release; Windows shows "Unknown publisher". This matches Clash Mini v3.2.2 installers, which likewise carry no Authenticode signature. Application in-app auto-update is an **empty channel** in v0.1.0. The five sidecar executables are not committed. These are documented, not hidden — see Known Limitations (a)–(f).
