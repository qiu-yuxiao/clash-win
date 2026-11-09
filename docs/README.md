# Clash Win 移植项目文档索引

一句话定位：把 **Clash Mini 3.2.2**（React/TS/Tauri/Rust）按 1:1 语义移植为 **C++20 + Win32 + Direct2D** 的原生 Windows 客户端，显示名唯一例外为 `Clash Win`。

## 篇目

| 文件 | 导读 |
| --- | --- |
| [01-目的与方针.md](01-目的与方针.md) | 移植目标、与 Mini 的 1:1 契约（端口/服务名/互斥项）、版本起点、三条决策方针 |
| [02-方法与流程.md](02-方法与流程.md) | 读源码到写 C++ 的固定顺序、`ui_src_gate` 出处门禁、验收证据链、小步重编译纪律 |
| [03-踩坑与避坑清单.md](03-踩坑与避坑清单.md) | 14 条已实际发生的错误：DPI 二次缩放、use-after-move 启动崩溃、抽屉吃标题栏、端口硬编码等 |
| [04-工程技巧.md](04-工程技巧.md) | 隔离测试、DPI 探针、PrintWindow+OCR、并排对照、SEH 崩溃钩子、单实例探测、WMI 启动 |

历史设计资料保留在 `docs/phase1/`（源码普查、PRD）与 `docs/phase2/`（C++ 架构、任务拆解、用户裁决）。

## 当前状态

**独立可用性（P0）— 此前遗漏、2026-11-05 补记**
- 要求：在**从未安装过 Clash Mini 的干净机**上全功能运行（含 TUN、系统代理）；服务自举是 Mini 既有行为，属 1:1 移植完整性。
- 依据与链路：`clash_mini_agreements.md` §1.8（附 Mini `file:line`）；`检测（未安装/STOPPED/未就绪）→ UAC 安装 → 启动 → IPC 就绪 → SYSTEM 内核`。
- 状态：代码侧已补"已注册但 ImagePath 失效 → 重新安装"、"启动失败/就绪超时 → 可见提示"，安装器在安装时已装服务（`clashwin.nsi:405-431`）。**干净机无服务 → UAC 安装 → 三项 URL 通**的实测需一次 UAC，登记在"明日一次点击清单"。

**已验收**
- 互斥：v0.2.1 起 Mini 与本程序**不可同时运行**（启动任一方会终止另一方的 client/core 进程并接管 `clash_verge_service`，安装期同样互斥）；本程序端口已全量 1:1 对齐 Mini：`10801/10802/10803`、guard 兜底 `7899`、控制器 `9098`、单实例 `33335`（`include/clashwin/config/constants.h:22-31`）；控制面走命名管道 `\\.\pipe\mini-mihomo-win`（`src/util/dirs.cpp:226-228`）；`/proxy.pac` 404 属预期（未开 PAC）。
- TUN：`verge.yaml enable_tun_mode=false`，默认路由已回 WLAN/直连，`10801` 出网 204。
- P0-1～P0-6 均已改完并通过隔离模式全量测试（用例数按动态基线、不写死数字，结论 0 failed；以冻结重跑日志为准，详见 `docs/evidence/`）。
- 抽屉改为挂载在标题栏下方内容区（与 Mini `src/pages/_layout.tsx:1254-1424` 同构），标题栏常驻可点；关闭钮、主题卡（标题/Depth/Vibrancy）、日志入口 `LogViewerModal` 已实机验证。卡片/面板渐变已按源码 `button-styles.ts:538-539`（4 停靠）与 `index.scss:126/147`（3 停靠）重写（`src/ui/scenes/settings_drawer.cpp:133-173`），135° 单渐变，无竖向条纹；不再以单点采样判色。
- quickbar 胶囊：名称可收缩省略、延迟固定宽完整显示、gap 12px、字号 12/11 DIP；实测 CJK 字高 16～18 物理像素（≈12 DIP，与 Mini 同高 17 物理像素一致）。
- 节点行点击已修复（`NodeRowWidget::on_mouse` Down 未返回 true 导致 capture 被祖先截走，`src/ui/scenes/node_list.cpp:154-160`）；实机点行可切换，切节点后流量总量保留（47.8M→47.8M 不清零）。

**已闭环**
- 启动崩溃 `0xC0000005`：**use-after-move 已修复并闭环**（`main_window.cpp:241-252` move 前取裸指针）。全仓扫描补出 1 处真隐患（`core_manager.cpp:62-64` 环境块 move 后取 `size()`）；`Widget::add_child/insert_child` 已加空指针断言（`src/ui/widget.cpp:37/44`）。60.5 秒六类操作两轮复验无崩溃、无 `crash_*.log`、WS 连接失败有单次告警日志（`src/mihomo/ws_manager.cpp:370-377`）。

**2026-11-05 差异台账清零（本轮）**
- #3 连接面板文案：表头 `域名/地址` → `链接目标`，路由列改为中文 `直连/封锁/代理`
  （`src/ui/scenes/connections_panel.cpp`，依据 `locales/zh/connections.json:35/37-39`）。实机 OCR 命中 x≈421「链接目标」、x≈878「路由」、行内「直连」。
- #2 quickbar wifi 图标字号 13 → 12 DIP（`src/ui/scenes/quick_bar.cpp`，依据 `active-node-card.tsx:348-351`）。
  实机实测图标盒 18 物理像素 = 12 DIP，字形 17 像素。
- #1 dark 选中竖条按 MUI `primary.light`：`lighten(main, 0.2)`（`src/ui/scenes/node_list.cpp`）。
  **同时修正 palettes**：Mini 运行时 primary 由 CSS `AccentColor` 解析（`use-custom-theme.ts`
  `resolvedPrimary = setting.primary_color || getSystemAccentColor() || dt.primary_color`），
  本机实测 `#0075FF`，故 `src/ui/theme/palettes.cpp` 的 light/dark `primary_color` 由 `#007AFF`/`#0A84FF` 改为 `#0075FF`。
  实测我方竖条 `rgb(51,145,255)` vs Mini `rgb(51,144,255)`（ΔG=1，浮点取整），宽度 4 物理像素、高 38 像素，两侧一致。
- #5 `VK_APPS`：Mini **未实现**（全仓 `*.tsx` 只有 `onContextMenu`，无 `ContextMenu` 键判定；
  键盘处理仅 `first-run-guide-dialog.tsx:423-441`（Enter）与 `basic-settings-card.tsx:233-237`（Enter），
  均为 Enter）。故本程序不为该键单独加逻辑。

**2026-11-05 安装器与验收（本轮）**
- 0.1.0 包已按本轮源码重打：`dist\ClashWin_0.1.0_x64-setup.exe`
  （体积 / SHA256 / 签名状态的具体值不在此写死，指向 `build\bin\Release\.delivery\stamp.json` 与 `docs/evidence/` 对应验收证据；当时为 `NotSigned`）；
  签名版 `dist\ClashWin_0.1.0_x64-setup-signed.exe`（本机 `Valid / CN=ClashWin` + RFC3161 时间戳；体积 / 哈希以交付记录为准）。
  **2026-11-06 按磁盘实测校正**：此处原把签名包的字节数与 `Valid` 写在了不带 `-signed` 的路径上，与本文件「待办」段及 `README.md` 矛盾，见 `evidence/2026-11-06-regression.md` §4。
- `/KEEP` 连续两次静默安装实测：`exit=0`，安装后 `Program Files\ClashWin\clashwin.exe` 时间戳 = `installer\stage` 的 03:41:42；
  配置目录 `%APPDATA%\io.github.clash-win.clash-win` 的 10 个文件 SHA256 在两次安装前后**完全一致**
  （基线 `hash_before.txt` / 一次后 `hash_after.txt` / 二次后 `hash_after2.txt`，`Compare-Object` 输出为空）。
  **这三份文件从未入库**；2026-11-06 起由 `scripts\acceptance_uac.ps1` 的 #3 在实测时自动生成到 `docs\evidence\`。
- UAC 发布者：安装包 Authenticode `Subject=CN=ClashWin`，UAC 提示的发布者即取自该 Subject；
  secure desktop 无法程序化截屏，此项以签名链为准。

**安装器交互：Clash Mini 真实行为与本程序映射**

Mini 侧没有任何自定义 NSIS 配置 —— `src-tauri/tauri.conf.json` 的 `bundle`（`:4-20`）里
**没有 `windows.nsis` 段**，因此全部走 Tauri 默认。逐条：

| 项 | Mini 实际值（依据） | 本程序 |
| --- | --- | --- |
| 安装作用域 | `currentUser`（Tauri 默认，`node_modules/@tauri-apps/cli/config.schema.json:3054-3056`） | `SetShellVarContext all`（机器级，需 UAC）—— 与 Mini 不同，见「明日一次点击清单」 |
| 语言选择器 | `false`（schema `:3083-3085` 默认） | 开启 `MUI_LANGDLL_DISPLAY`（`clashwin.nsi:167`） |
| 旧设定（升级） | **总是保留** `%APPDATA%`；Mini 无「全新安装」入口 | 新增「安装选项」向导页（`clashwin.nsi:110`），默认**全新安装**，勾选「保留原设定」则等同 `/KEEP` |
| `deleteAppDataOnUninstall` | 未配置 = `false` | 卸载确认页复选框，默认不勾（`clashwin.nsi:123/242`） |
| `installerHooks` | 无 | 无 |
| 更新产物 | `createUpdaterArtifacts:false`（`:19`） | 无 updater |
| 打包参数 | `tauri-action` + `--target x86_64-pc-windows-msvc`，无 NSIS 覆盖（`release.yml:145-159`） | `installer/build_installer.ps1` + makensis |

结论：Mini 的「旧设定」处理是**永远保留**且不可选；按用户口径，本程序保留「保留原设定」可见勾选项
（文案 `clashwin.nsi:131-140`，与 Mini 卸载页「同时删除配置数据」同一套用语），默认全新安装。
`/KEEP` 命令行参数仍保留用于自动化（`:188-192`），与勾选框等价。

**窗口尺寸口径**

- 以 Mini 源码默认值为准：`src-tauri/src/utils/resolve/window.rs:18-19`
  `DEFAULT_WIDTH = 271.0 / DEFAULT_HEIGHT = 680.0` → 150% 下 406×1020 物理像素（本程序 `src/ui/breakpoints.h:21-22` 一致）。
- Mini 现役窗口实测 407×1022（逻辑 271.33×681.33），属 DPI 取整，**不作为对齐目标**。
- `window_state.json` 恢复路径新增归一：`src/config/window_state.cpp:246-257` 把
  `(680, 681.5]` / `(271, 272.5]` 的 1px 级残留归一到默认 271×680，
  防止历史 681 被一次次带回来。现有两份 `window_state.json` 均为 `{"width":640,"height":820}`
  （测试时的宽窗状态），**无 681 残留**，无需额外覆盖。

## 四栏总台账（2026-11-05 夜间收口）

**已一致**（现值已达源码值）
- 帮助菜单文案结构、13 bold / 48 / 17 / `padding 8px 16px` / 135° 4 停靠（`settings_drawer.cpp:957-1021`）
- 主题卡三行 745/785/820（`settings_drawer.cpp:556-564`）
- 开关 28×14 / thumb 14 / 位移 14，行内 23.8×11.9（`switch_w.cpp:65-109`、`settings_drawer.cpp:447`）
- 卡片渐变 4 停靠 135°（`settings_drawer.cpp:148`）；面板 3 停靠不透明（`:160-173`）
- 选中卡 高 28 / 圆角 6 / `m:0`+容器 8（`quick_bar.cpp:241,42`）
- 抽屉无遮罩：Mini 抽屉是条件渲染 `theme-panel`，非 Dialog（`_layout.tsx:1403-1424`）；
  本程序 `mask_` 恒隐藏（`settings_drawer.cpp:596-600,736`）——**一致，不是口径偏差**
- 连接详情无可见遮罩（Mini 为 Popover，`Popover.js:337` `invisible`）：
  `connection_detail_dialog.cpp:109-111` 不画遮罩，点外部关闭 `:214-216`
- 连接详情按触发行锚定（Popover `anchorOrigin` bottom/left + `transformOrigin` top/left、
  `marginThreshold` 16）：首选贴在该行左下，下方不足上翻、右侧不足左翻，卡片 332×324
  （`connection_detail_dialog.cpp:88-137`；证据图 `out/conn_detail_03_popover.png`）

**已修差异**（本轮改完）
- 帮助菜单纯标题 → 动态后缀 `(v0.1.0)` / `(Ver.1.19.27 Mihomo)`（`settings_drawer.cpp:990-1021`）
- 主题卡标题行盒 18 → 20.4（半行距 1.2）（`settings_drawer.cpp:556-564`）
- 选中卡 29/9/8 → 28/6/0+12（`quick_bar.cpp:241,42`）
- 抽屉半透明 → 不透明（`settings_drawer.cpp:160-173`）；渐变竖纹 → 单次 135°（`:148`）
- 日志/客户端更新/内核更新弹窗遮罩 0.45 → **0.5**（MUI Backdrop 默认，`Backdrop.js:48`；
  `utility_modals.cpp:351,588,860`；解锁检测模态壳同步 `unlock_dialog.cpp:157`，ClashWin 自加豁免）
- 客户端/内核更新在下载/检查中点遮罩与 Esc 不关闭（对齐 Mini `onClose` 守卫，
  `utility_modals.cpp:484-487,495-501,717-723,731-740`）
- 安装器补 **Russian**（`clashwin.nsi:128-130`），三语与 Mini 一致；`makensis` 编译通过
- 更新通道（2026-11-07 已按 §5.4 第三条整体改向 Clash Win 自身）：endpoint `qiu-yuxiao/clash-mini/dev/updater/app-update.json` → `qiu-yuxiao/clash-win/main/updater/app-update.json`；`updater/app-update.json` 改为自家 `version 0.1.0` / `platforms: {}` 空通道；首启引导两外链 `clash-mini` → `clash-win`。原「GitHub 地址 → `local://…`」为 11-05 旧记录，已作废，见总纲 `移植基本准则.md` §5.4 第三条。

**既定口径偏差（历史时间线记录，不构成豁免）**
> 2026-11-07 口径校正：本栏与下方「刻意偏差」栏**均不构成豁免**（总纲 `移植基本准则.md` §5.6）。遮罩 0.5、安装时 `.bak-N` 重命名、`app-update.json` 旧记录等须逐条按 1:1 重新对齐 Mini 源码；现行唯一允许的偏差以总纲 §5（尤其 §5.4 当前三条）为准。以下条目保留作时间线，不作涂抹、不作为放行依据。
- 窗口 407×1020 vs Mini 407×1022：271×680 DIP @150% 取整（`main_window.cpp:192-223`，源码 `window.rs:18-19`）
- 开关行内以布局 23.8×11.9 等价 `scale(.85)`，非 CSS transform；实测 ±1 px AA
- 字体度量不逐字宽（DirectWrite vs WebView2 字体栈）

**刻意偏差（历史时间线记录，不构成豁免；现行清单见总纲 §5.4）**
> 2026-11-07 口径校正：本栏不构成豁免（总纲 §5.6）。现行刻意偏差共三条、统一登记于总纲 `移植基本准则.md` §5.4（安装器语言→首启语言、对外发布与发布动作授权、更新通道指向 Clash Win 自身），本栏 `.bak-N` 条目须按 §5.6 逐条重新对齐 Mini 源码，不再默认「保留」。
- 安装默认「全新安装」会整目录 `Rename` 为 `%APPDATA%\io.github.clash-win.clash-win.bak-N`
  （非删除；Mini 安装从不移动配置）。生成规则：`clashwin.nsi:389-403` 从 `bak-1` 起取第一个空号。
  恢复：退出程序 → `%APPDATA%` 下把 `.bak-N` 改名回去（保留当前目录留底）。
  **2026-11-06 校正**：原文转引的 `ACCEPTANCE-20261105.md` 10.5 六步操作从未入库、内容不可还原；
  此处只保留上述两步要点，完整步骤须重新整理，见 [06-验收状态台账.md](06-验收状态台账.md) §3。

**2026-11-06 13:44 更新：UAC 清单 #1～#4 已完整实测通过**
- `scripts\acceptance_uac.ps1` 一次管理员授权跑完 #2 打包 → #1 验签 → #3 `/KEEP` ×2 → #4 卸载，`RESULT=PASS`（EXIT=0），报告见 `evidence/acceptance-uac-20261106-134426.md`，三份 `hash_*.txt` 已入库。
- 仍需人工：**21 条僵尸 PID → 重启清理**（#0）、**UAC 发布者目视**（#5）、干净机 P0、4 页并排图、启动自检复跑。
- 工具环境无交互桌面时无法弹出 UAC，可双击 `scripts\run_uac_acceptance.cmd` 一键完成（点一次「是」）。

## UAC 验收清单（#1～#4 已于 2026-11-06 实测通过）

| # | 动作 | 命令 | 预期 |
| --- | --- | --- | --- |
| 0 | 重启清理僵尸 PID | 正常重启一次 | `Get-Process clashwin*` 无 `threads=0` 残留 |
| 1 | 签名（读 `LocalMachine\My` 私钥） | `powershell -File scripts\sign.ps1 -Paths dist\ClashWin_0.1.0_x64-setup.exe` | 一次 UAC；`Get-AuthenticodeSignature` 返回 `Valid / CN=ClashWin` |
| 2 | 完整打包（含卸载器签名钩子） | `powershell -File installer\build_installer.ps1` | 一次 UAC（脚本自提升，`build_installer.ps1:27-37`）；产出签名包 |
| 3 | `/KEEP` 重装实测 | `dist\ClashWin_0.1.0_x64-setup.exe /S /KEEP` ×2 + 哈希比对 | 一次 UAC；两次后配置目录 SHA256 不变 |
| 4 | 卸载实测 | `"C:\Program Files\ClashWin\uninstall.exe" /S` | 一次 UAC；程序文件移除、配置目录保留 |
| 5 | UAC 发布者目视确认 | 双击安装包，看提示框顶部 | 显示「ClashWin」 |

**2026-11-06 实测结果（#1～#4）**：#2 产出签名安装包；#1 本机 `Valid / CN=ClashWin` + RFC3161 时间戳（**签名状态只记录、不作发布门禁**，见 `06-验收状态台账.md` §2 与 §4 第 6 条）；#3 两次 `/S /KEEP` 后配置目录三份快照两两 `diff` 为空；#4 `uninstall.exe /S` 退出 0，`C:\Program Files\ClashWin` 已移除、配置目录保留。体积 / 哈希不写死具体值，以构建产物与 `build\bin\Release\.delivery\stamp.json` 交付记录为准。原始报告见 `evidence/acceptance-uac-20261106-134426.md`。#0 / #5 仍需人工。

**待办**
- 逐页并排图覆盖：已产出 主页/抽屉首屏/抽屉下半/连接面板/日志/编辑订阅(remote)/帮助菜单
  （`pair_07_help.png`，自绘 `ContextMenu` 已实拍，含动态版本后缀）；
  首次引导、添加网址、编辑订阅(local)、解锁整页——仍待实机产出。
  **2026-11-06 校正**：`pair_07_help.png` 等 `pair_*` 与 `ACCEPTANCE-20261105.md` 均从未入库，仓库内无逐组结论文件；现状见 [06-验收状态台账.md](06-验收状态台账.md) B/C 节。
- 全量测试：**用例数按动态基线，以冻结重跑后的全量回归日志为准，详见 `docs/evidence/`，文档不写死任何数字**（531 / 547 / 557 / 655 均为历史时点值，口径统一为行首 `UT_TEST(` 宏调用数）；结论口径 `0 failed / EXIT=0`。冻结重跑后须把 `tests_full.log` 副本落到 `docs/evidence/` 并写明 HEAD + exe SHA256 + 用例数 + 0 failed；原始日志路径与 `*.log` 忽略规则见 `evidence/2026-11-06-regression.md` §3。
- 启动自检：`scripts\run_app_test.ps1 -AppDir build\bin\Release -Runs 3` → **PASS ×3**，
  Post **clean**、**NET-RESTORED**；21 条 `threads=0` 僵尸 PID 不计存活。
- 安装包（历史时间线）：11-05 有未签名 / 签名两版，11-06 重打包后为一版带本机签名的 `dist\ClashWin_0.1.0_x64-setup.exe`；体积 / SHA256 / 签名状态的具体值不写死，一律以构建产物与 `build\bin\Release\.delivery\stamp.json` 交付记录为准，原件备份信息见 `evidence/2026-11-06-regression.md` §4。已通过 `/KEEP` ×2 与卸载实测。**以上 11-05 / 11-06 的 `Valid / CN=ClashWin` 均为当时一次性 UAC 提权下的实测值，保留作时间线，不代表 v0.1.0 发布资产状态（见下条与 `06-验收状态台账.md` §4 第 6 条）。**
- **对外发布口径（2026-11-07 最终裁决）**：用户已全权授权对外发布，发布动作自主执行，不再逐动作确认；旧「不对外发布：不建 Release / 不推 tag / 不上传资产 / 包只留本地 `dist\`」口径已被当场指令覆盖。依据与原口径存档见总纲 `移植基本准则.md` §5.4 第二条。**v0.1.0 发布未签名资产**：实测当前会话非管理员、`Set-AuthenticodeSignature` 返回 `UnknownError / NotSigned`，`sign.ps1` 与 `build_installer.ps1` 需 UAC 提升、非交互会话不可用；签名状态只记录、不作发布门禁，与 Mini v3.2.2 安装包未做 Authenticode 签名一致，并在 Release Notes / Known Limitations 如实披露「未签名，Windows SmartScreen / UAC 可能提示未知发布者」。详见 `06-验收状态台账.md` §4 第 6 条。
- 窄窗抽屉高度差已归零：订阅卡 117 → 180 为主因（+94.5 px），主题卡标题行盒 18 → 20.4（半行距 1.2），
  三行 OCR **745/785/820** 与 Mini 零差（`settings_drawer.cpp:556-564`）。

## 关键路径速查

- 常量单一事实源：`include/clashwin/config/constants.h`
- UI 门禁：`scripts/ui_src_gate.ps1`；全量测试：`scripts/run_tests_v4.ps1`
- Mini 参照源码：`C:\Users\sun_y\Documents\AntiGravity_Projects\ClashVerge`

## 2026-11-06 校正与验收入口

> 本节为地基整理后的权威索引。**上文 11-05 记录与本节冲突之处，以本节及 `06-验收状态台账.md` 为准**；11-05 原文保留作时间线，不作涂抹。

- **状态矩阵**：[06-验收状态台账.md](06-验收状态台账.md) —— 逐项标注【本轮实测】/【文档记载】/【待验】/【已知缺口】。
- **本轮实测证据**：`evidence/2026-11-06-regression.md` —— sidecar 5/5、UI 门禁 PASS、全量回归 `0 failed`（用例数按动态基线，以 `docs/evidence/` 冻结重跑日志为准）、安装包验收（体积 / 哈希 / 签名具体值以构建产物与交付记录为准）。
- **原始验收报告**：`ACCEPTANCE-20261105.md` **从未入库**，其「第九～十一节」「10.5 六步操作」无法还原；上文所有指向它的引用一律以状态矩阵为准。`pair_*.png`、`cmp_*.png`、`out/*.png`、`tests_full.log` 同样从未入库，不伪造补图；**`hash_*.txt` 已于 2026-11-06 实测生成并入库** `evidence/`。
- **外部参照**：`clash_mini_agreements.md` §1.8 位于 `ClashVerge` 参考仓库，不在本仓库内；该目录按铁律只读参考，不得改动、移动或覆写。
- **一键验收入口**：`scripts\acceptance_uac.ps1`（把 UAC 清单 #1～#4 编排为一次授权、自动留证；#0 重启与 #5 目视标 MANUAL；提权失败固定 `exit 2`，不写假报告）、`scripts\run_uac_acceptance.cmd`（双击 → 一次 UAC → 自动跑完并显示退出码）、`scripts\acceptance_clean_machine.ps1`（干净机 P0 链路留证，默认只读、绝不自动开 TUN/系统代理）。`-DryRun` 只预演、不改动系统、不写报告。
- **交付链现状（2026-11-07 口径校正）**：哈希 / 体积 / 时点值一律不写死，以 `build\bin\Release\clashwin.exe` 构建产物、`build\bin\Release\.delivery\stamp.json` 与 `scripts\delivery_check.ps1` 的交付记录为准，详见台账 §4 第 1/2 条。**当前 exe 与工作区源码不同源、交付基线尚未冻结**：`delivery_check.ps1` 已加工作区门禁，有未提交改动时 `-Record` 与非 `-Record` 一律 FAIL。按 2026-11-07 最终裁决的冻结顺序执行：**写完 → 单次冻结提交 → 从干净 HEAD 重建 → 全量重跑 → 重挂证据 → 发布**（「停止写入」与用户「一直做下去」直接抵触，不采纳）；重挂时重新登记 stamp，方能建立可追溯的发布对象。原「2026-11-06 13:44 已收敛」及 `60ECEDAF…` 等具体值仅为历史时点，不作为当前发布基线。
- **回归链**：`ui_src_gate.ps1` → `cmake --build build --config Release` → `run_tests_v4.ps1` → `run_app_test.ps1 -AppDir build\bin\Release -Runs 3` → Post `clean` + `NET-RESTORED`（`02-方法与流程.md:43`）。`run_app_test.ps1` 默认 `AppDir` 原指向已清理的 `build_verify\`，2026-11-06 已改为 `build\bin\Release`。
- **方针更新（2026-11-07）**：不新增页面、不改内核、不做兼容层三条边界不变；用户已全权授权对外发布，旧「不对外发布 / 逐动作确认」口径由总纲 `移植基本准则.md` §5.4 第二条覆盖，不再适用。发布相关的已知限制（安装包未签名、backdrop-blur 未迁移、hover 0.15s 补间未迁移）在 Release Notes / Known Limitations 如实披露。
